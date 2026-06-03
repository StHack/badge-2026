#include "ota.h"
#include "../config/version.h"
#include "../hal/display.h"
#include "../eyes/eye_geometry.h"
#include "../eyes/eye_types.h"
#include <WiFi.h>
#include <WiFiClient.h>
#include <HTTPUpdate.h>
#include <Arduino.h>

static bool sConnected = false;

// ── OTA progress display (eye-aware) ─────────────────────────────────────────
// Left eye  → status text, straight, centred.
// Right eye → progress bar whose width matches the eye opening at mid-height.

void drawOtaProgress(const char* status, int pct) {
    const uint32_t nowMs = millis();
    EyeUiGeom gL, gR;

    // ── Left eye: status label ────────────────────────────────────────────────
    eyeUiBeginFrame(gLeftDisplay, false, nowMs, &gL);
    if (gL.opening > 3) {
        eyeUiDrawTextCenteredNudged(gLeftDisplay, gL.cy + 9, 1, status, +3);
    }
    gLeftDisplay.display();

    // ── Right eye: percentage (top) + progress bar (bottom, wider) ───────────
    eyeUiBeginFrame(gRightDisplay, true, nowMs, &gR);
    if (gR.opening > 3) {
        // Percentage label at the top
        char pctStr[6];
        snprintf(pctStr, sizeof(pctStr), "%d%%", pct);
        eyeUiDrawTextCenteredNudged(gRightDisplay, gR.cy - 6, 1, pctStr, +13);

        // Scan bottom row (wider part of the eye)
        const int16_t scanY = gR.cy + 10;
        const int16_t margin = 4;
        int16_t xMin = 127, xMax = 0;
        for (int16_t x = 0; x < 128; x++) {
            if (eyeUiPointInsideEyeOpening(&gR, x, scanY, margin)) {
                if (x < xMin) xMin = x;
                if (x > xMax) xMax = x;
            }
        }
        if (xMax > xMin + 4) {
            const int16_t barW = xMax - xMin;
            const int16_t barH = 6;
            const int16_t barY = scanY - barH / 2;
            gRightDisplay.drawRect(xMin, barY, barW, barH, SSD1306_WHITE);
            const int16_t fill = (int16_t)((int32_t)pct * (barW - 2) / 100);
            if (fill > 0)
                gRightDisplay.fillRect(xMin + 1, barY + 1, fill, barH - 2, SSD1306_WHITE);
        }
    }
    gRightDisplay.display();
}

bool otaConnect(const char* ssid, const char* psk, const char* url,
                uint32_t timeoutMs) {
    if (sConnected) return false;

    Serial.printf("[ota] connecting to %s\n", ssid);
    drawOtaProgress("Connect", 0);

    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid, psk);

    uint32_t start = millis();
    while (WiFi.status() != WL_CONNECTED) {
        if (millis() - start > timeoutMs) {
            Serial.println("[ota] WiFi timeout");
            drawOtaProgress("Timeout", 0);
            delay(1500);
            WiFi.disconnect(true);
            WiFi.mode(WIFI_OFF);
            return false;
        }
        delay(200);
    }
    Serial.printf("[ota] connected, IP=%s\n", WiFi.localIP().toString().c_str());
    sConnected = true;

    drawOtaProgress("Download", 0);

    httpUpdate.onProgress([](int cur, int total) {
        int pct = (total > 0) ? (cur * 100 / total) : 0;
        Serial.printf("[ota] %d / %d B (%d%%)\n", cur, total, pct);
        drawOtaProgress("Download", pct);
    });

    Serial.printf("[ota] pulling %s\n", url);
    WiFiClient client;
    t_httpUpdate_return ret = httpUpdate.update(client, url);

    // Reaching here means the update failed (success triggers ESP.restart()).
    sConnected = false;
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);

    switch (ret) {
        case HTTP_UPDATE_FAILED:
            Serial.printf("[ota] failed (%d): %s\n",
                          httpUpdate.getLastError(),
                          httpUpdate.getLastErrorString().c_str());
            drawOtaProgress("Failed", 0);
            delay(2000);
            break;
        case HTTP_UPDATE_NO_UPDATES:
            Serial.println("[ota] server: no update available");
            drawOtaProgress("Up2date", 100);
            delay(2000);
            break;
        default:
            break;
    }
    return false;
}

void otaDisconnect() {
    if (!sConnected) return;
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
    sConnected = false;
}

bool otaIsConnected()    { return sConnected; }
bool otaTransferActive() { return false; }   // pull is synchronous, never "in progress"
void otaTick()           {}                  // no-op
