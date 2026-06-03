#include "ble_scanner.h"
#include "beacon_parser.h"
#include "../core/identity.h"
#include <BLEDevice.h>
#include <BLEScan.h>
#include <BLEAdvertisedDevice.h>
#include <Arduino.h>

static BLEScan*       sScan    = nullptr;
static BeaconCallback sCb      = nullptr;
static bool           sRunning = false;

// ── Deduplication ─────────────────────────────────────────────────────────────
// Ignore a beacon if it is identical (cmd + badgeId + payload) to the last
// one recorded. Send ST:CLEAR to reset and allow the same command again.
// Access is serialised with a portMUX because onResult runs on the BLE task.
static ParsedBeacon    sLastBeacon = {};
static bool            sHasLast   = false;
static portMUX_TYPE    sDedupMux  = portMUX_INITIALIZER_UNLOCKED;

// Returns true and records the beacon if it is NOT a duplicate.
// The check+record is atomic to prevent two concurrent callbacks both
// seeing sHasLast=false and both firing.
// Exception: BeaconCmd::Tama always fires (no deduplication) so that
// two consecutive TAMA signals can both trigger an interaction.
static bool checkAndRecord(const ParsedBeacon& b) {
    if (b.cmd == BeaconCmd::Tama) return true;
    bool fire = false;
    portENTER_CRITICAL_ISR(&sDedupMux);
    if (!sHasLast || memcmp(&b, &sLastBeacon, sizeof(ParsedBeacon)) != 0) {
        sLastBeacon = b;
        sHasLast    = true;
        fire        = true;
    }
    portEXIT_CRITICAL_ISR(&sDedupMux);
    return fire;
}

static void dedupClear() {
    portENTER_CRITICAL_ISR(&sDedupMux);
    sHasLast = false;
    portEXIT_CRITICAL_ISR(&sDedupMux);
}

class BadgeAdvertisedDeviceCallbacks : public BLEAdvertisedDeviceCallbacks {
public:
    void onResult(BLEAdvertisedDevice device) override {
        if (!sCb) return;

        const int rssi = device.getRSSI();

        auto dispatch = [rssi](const ParsedBeacon& b) {
            if (!beaconIsForMe(b)) return;
            // Proximity gate: ignore if sender is too far away
            if (b.proximityRequired && rssi < (int)kProximityRssiThreshold) return;
            if (b.cmd == BeaconCmd::Clear) { dedupClear(); return; }
            if (checkAndRecord(b)) sCb(b);
        };

        // Check manufacturer data.
        // getManufacturerData() includes the 2-byte company ID prefix — skip it.
        if (device.haveManufacturerData()) {
            std::string mfr = device.getManufacturerData();
            const uint8_t* raw = reinterpret_cast<const uint8_t*>(mfr.c_str());
            size_t rawLen = mfr.size();
            if (rawLen > 2) { raw += 2; rawLen -= 2; }
            ParsedBeacon b = {};
            if (beaconParse(raw, rawLen, &b))
                dispatch(b);
        }

        // Also check device name
        if (device.haveName()) {
            std::string name = device.getName();
            ParsedBeacon b = {};
            if (beaconParse(reinterpret_cast<const uint8_t*>(name.c_str()), name.size(), &b))
                dispatch(b);
        }
    }
};

static BadgeAdvertisedDeviceCallbacks sCallbacks;

bool bleScanStart(BeaconCallback cb) {
    if (sRunning) return true;

    sCb = cb;

    // Toujours initialiser avec le vrai nom du badge : si le GATT démarre
    // ensuite alors que BLE est déjà actif, le nom sera déjà correct.
    if (!BLEDevice::getInitialized()) {
        BLEDevice::init(badgeIdName());
    }

    sScan = BLEDevice::getScan();
    sScan->setAdvertisedDeviceCallbacks(&sCallbacks, false);
    sScan->setActiveScan(false); // passive scan only
    sScan->setInterval(100);
    sScan->setWindow(90);

    sRunning = sScan->start(0, nullptr, false); // 0 = continuous until stopped
    return sRunning;
}

void bleScanStop() {
    if (!sRunning) return;
    if (sScan) {
        sScan->stop();
        sScan->clearResults();
    }
    sRunning = false;
    BLEDevice::deinit(false);  // full teardown — safe before light sleep
}

void bleScanStopSoft() {
    if (!sRunning) return;
    if (sScan) {
        sScan->stop();
        sScan->clearResults();
    }
    sRunning = false;
    // BLE stack stays initialized — no deinit, no blocking delay
}

bool bleScanIsRunning() { return sRunning; }
