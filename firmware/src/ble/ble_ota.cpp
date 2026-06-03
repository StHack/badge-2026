// ble_ota.cpp — BLE-driven OTA firmware update for badge_v2.
//
// Thread model:
//   bleOtaBegin / bleOtaOnDataChunk / bleOtaRequestCommit / bleOtaRequestAbort
//   are called from the BLE stack's FreeRTOS task.
//   bleOtaInProgress / bleOtaPercent / bleOtaTick run on the Arduino loop task.
//   Coordination via volatile state enum; esp_ota_write is internally thread-safe.

#include "ble_ota.h"
#include "../wifi_ota/ota.h"
#include <esp_ota_ops.h>
#include <Arduino.h>

namespace {

enum class BleOtaState : uint8_t {
    Idle,
    Active,          // receiving chunks
    CommitPending,   // END received — main loop must finalize
    AbortPending,    // ABORT received — main loop must clean up
};

static volatile BleOtaState   sState         = BleOtaState::Idle;
static esp_ota_handle_t       sHandle        = 0;
static const esp_partition_t* sTarget        = nullptr;
static volatile uint32_t      sBytesReceived = 0;
static volatile uint32_t      sTotalBytes    = 1;   // avoid div-by-zero

} // namespace

// ── Public API — called from BLE callback task ────────────────────────────────

void bleOtaBegin(uint32_t totalBytes) {
    if (sState != BleOtaState::Idle) {
        Serial.println("[ble_ota] BEGIN ignored — session already open");
        return;
    }

    sTarget = esp_ota_get_next_update_partition(nullptr);
    if (!sTarget) {
        Serial.println("[ble_ota] no OTA partition available");
        return;
    }

    esp_err_t err = esp_ota_begin(sTarget, OTA_SIZE_UNKNOWN, &sHandle);
    if (err != ESP_OK) {
        Serial.printf("[ble_ota] esp_ota_begin failed: %d\n", err);
        return;
    }

    sTotalBytes    = (totalBytes > 0) ? totalBytes : 1;
    sBytesReceived = 0;
    sState         = BleOtaState::Active;
    Serial.printf("[ble_ota] started — target=%s size=%u\n",
                  sTarget->label, totalBytes);
}

void bleOtaOnDataChunk(const uint8_t* data, size_t len) {
    if (sState != BleOtaState::Active) return;

    esp_err_t err = esp_ota_write(sHandle, data, len);
    if (err != ESP_OK) {
        Serial.printf("[ble_ota] esp_ota_write failed: %d — aborting\n", err);
        esp_ota_abort(sHandle);
        sState = BleOtaState::Idle;
        return;
    }
    sBytesReceived += (uint32_t)len;
}

void bleOtaRequestCommit() {
    if (sState == BleOtaState::Active) {
        sState = BleOtaState::CommitPending;
        Serial.printf("[ble_ota] commit requested (%u/%u bytes)\n",
                      (unsigned)sBytesReceived, (unsigned)sTotalBytes);
    }
}

void bleOtaRequestAbort() {
    if (sState == BleOtaState::Active || sState == BleOtaState::CommitPending) {
        sState = BleOtaState::AbortPending;
        Serial.println("[ble_ota] abort requested");
    }
}

// ── Public API — called from main loop ───────────────────────────────────────

bool bleOtaInProgress() {
    return sState == BleOtaState::Active || sState == BleOtaState::CommitPending;
}

uint32_t bleOtaBytesReceived() { return sBytesReceived; }

uint8_t bleOtaPercent() {
    if (sTotalBytes == 0) return 0;
    uint32_t pct = (uint64_t)sBytesReceived * 100 / sTotalBytes;
    return (uint8_t)(pct > 100 ? 100 : pct);
}

void bleOtaTick(uint32_t nowMs) {
    (void)nowMs;

    switch (sState) {
        case BleOtaState::Idle:
            return;

        case BleOtaState::Active:
            drawOtaProgress("BLE OTA", bleOtaPercent());
            return;

        case BleOtaState::CommitPending: {
            drawOtaProgress("BLE OTA", 100);

            esp_err_t err = esp_ota_end(sHandle);
            if (err != ESP_OK) {
                Serial.printf("[ble_ota] esp_ota_end failed: %d\n", err);
                drawOtaProgress("Failed", 0);
                delay(2000);
                sState = BleOtaState::Idle;
                return;
            }
            err = esp_ota_set_boot_partition(sTarget);
            if (err != ESP_OK) {
                Serial.printf("[ble_ota] set_boot_partition failed: %d\n", err);
                drawOtaProgress("Failed", 0);
                delay(2000);
                sState = BleOtaState::Idle;
                return;
            }
            Serial.println("[ble_ota] success — rebooting");
            drawOtaProgress("Reboot", 100);
            delay(800);
            esp_restart();
            return;  // unreachable
        }

        case BleOtaState::AbortPending:
            esp_ota_abort(sHandle);
            sState = BleOtaState::Idle;
            Serial.println("[ble_ota] aborted");
            drawOtaProgress("Abort", 0);
            delay(1200);
            return;
    }
}
