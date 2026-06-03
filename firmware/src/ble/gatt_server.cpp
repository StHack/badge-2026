#include "gatt_server.h"
#include "ble_types.h"
#include "ble_ota.h"
#include "../core/identity.h"
#include "../config/version.h"
#include "../eyes/eye_animation.h"
#include "../eyes/eye_catalog.h"
#include "../eyes/eye_calibration.h"
#include "../eyes/eye_geometry.h"
#include "../leds/led_anim.h"
#include "../leds/led_anim_zones.h"
#include "../sound/rtttl.h"
#include "../sound/cry.h"
#include "../hal/display.h"
#include "../hal/buttons.h"
#include "../mode_full/badge_menu.h"
#include "../tamagotchi/tamagotchi.h"
#include "../mode_sleep/badge_fsm.h"
#include "../hal/buzzer.h"
#include "../config/hardware.h"

#include <BLEDevice.h>
#include <Preferences.h>
#include <esp_system.h>
#include <esp_gap_ble_api.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>
#include <Arduino.h>
#include <esp_random.h>

static BLEServer*         sServer   = nullptr;
static BLEService*        sService  = nullptr;
static BLECharacteristic* sCmdChar     = nullptr;
static BLECharacteristic* sInfoChar   = nullptr;
static BLECharacteristic* sMusicChar  = nullptr;
static BLECharacteristic* sPinChar    = nullptr;
static BLECharacteristic* sOtaDataChar = nullptr;
static bool sRunning            = false;
static bool sClientConnected    = false;
static bool sAdvertisingRunning = false;

// ── PIN auth state ────────────────────────────────────────────────────────────
enum class AuthPhase : uint8_t { Idle, Asking, Accepted, Rejected };

static volatile bool sPendingAuth   = false;
static bool          sClientAuthorized = false;
static AuthPhase     sAuthPhase     = AuthPhase::Idle;
static uint32_t      sAuthStartMs   = 0;
static char          sAuthPin[5]    = "";   // 4-digit ASCII + NUL

constexpr uint32_t   kAuthTimeoutMs = 20000;

static void generatePin() {
    uint32_t n = esp_random() % 10000;
    snprintf(sAuthPin, sizeof(sAuthPin), "%04u", n);
    if (sPinChar) sPinChar->setValue(sAuthPin);
    Serial.printf("[gatt] auth PIN: %s\n", sAuthPin);
}

static void drawAuthUi(uint32_t nowMs) {
    uint32_t secsLeft = (kAuthTimeoutMs - (nowMs - sAuthStartMs) + 999) / 1000;

    // Left eye: "OK?" hint + countdown — direct draw, bypasses eye blink system
    gLeftDisplay.clearDisplay();
    gLeftDisplay.setTextColor(SSD1306_WHITE);
    gLeftDisplay.setTextSize(1);
    gLeftDisplay.setCursor(55, 35);
    gLeftDisplay.print("OK?");
    char secBuf[5];
    snprintf(secBuf, sizeof(secBuf), "%us", secsLeft);
    int16_t sw = (int16_t)(strlen(secBuf) * 6);
    gLeftDisplay.setCursor((128 - sw) / 2, 51);
    gLeftDisplay.print(secBuf);
    gLeftDisplay.display();

    // Right eye: 4-digit PIN at size 2 (compare with web page)
    gRightDisplay.clearDisplay();
    gRightDisplay.setTextColor(SSD1306_WHITE);
    gRightDisplay.setTextSize(2);
    gRightDisplay.setCursor(40, 39);
    gRightDisplay.print(sAuthPin);
    gRightDisplay.display();
}

// ── Command format: "<CMD>:<value>" ──────────────────────────────────────────
// EYE:<mode_number>     set eye animation mode
// LED:<mode_number>     set LED animation mode
// MUSIC:<rtttl>         play RTTTL string
// INFO                  return firmware info on info char

class ServerCallbacks : public BLEServerCallbacks {
    void onConnect(BLEServer* s) override {
        sClientConnected    = true;
        sClientAuthorized   = false;
        sAdvertisingRunning = false;
        sPendingAuth        = true;   // triggers auth UI in gattTick
        generatePin();
        Serial.println("[gatt] client connected — awaiting PIN confirm");
    }
    void onDisconnect(BLEServer* s) override {
        sClientConnected  = false;
        sClientAuthorized = false;
        sAuthPhase        = AuthPhase::Idle;
        sAdvertisingRunning = false;
        Serial.println("[gatt] client disconnected");
    }
};

class CmdCallbacks : public BLECharacteristicCallbacks {
    void onWrite(BLECharacteristic* c) override {
        if (!sClientAuthorized) {
            Serial.println("[gatt] write rejected — not authorized");
            return;
        }
        std::string val = c->getValue();
        if (val.empty()) return;

        const char* cmd = val.c_str();

        if (strncmp(cmd, "EYE:", 4) == 0) {
            int mode = atoi(cmd + 4);
            eyeAnimSetMode(static_cast<EyeAnimMode>(mode));
        } else if (strncmp(cmd, "LED:", 4) == 0) {
            int mode = atoi(cmd + 4);
            ledAnimSetMode(static_cast<LedAnimMode>(mode));
        } else if (strncmp(cmd, "MUSIC:", 6) == 0) {
            rtttlStart(cmd + 6);
        } else if (strncmp(cmd, "TEXT:", 5) == 0) {
            eyeAnimSetMode(EyeAnimMode::ForceScrollText, cmd + 5);
        } else if (strncmp(cmd, "STYLE:", 6) == 0) {
            int idx = atoi(cmd + 6);
            eyeCatalogSetStyleByIndex((uint8_t)idx);
            eyeCatalogStyleSave();
        } else if (strncmp(cmd, "GAME:", 5) == 0) {
            int id = atoi(cmd + 5);
            badgeMenuRequestGame((uint8_t)id);
        } else if (strncmp(cmd, "DROP:", 5) == 0) {
            // DROP:n — set upper eyelid side drop (local px, default 26)
            float v = atof(cmd + 5);
            if (v >= 0.0f && v <= 60.0f) gUpperSideDropLocal = v;
        } else if (strncmp(cmd, "CAL:", 4) == 0) {
            // CAL:tlX,tlY,brX,brY,bmX,bmY — update eye calibration (not saved)
            int a, b, c, d, e, f;
            if (sscanf(cmd + 4, "%d,%d,%d,%d,%d,%d", &a, &b, &c, &d, &e, &f) == 6) {
                gEyeCal.tlX = (int16_t)a; gEyeCal.tlY = (int16_t)b;
                gEyeCal.brX = (int16_t)c; gEyeCal.brY = (int16_t)d;
                gEyeCal.bmX = (int16_t)e; gEyeCal.bmY = (int16_t)f;
            }
        } else if (strcmp(cmd, "CALSAVE") == 0) {
            eyeCalSave();
        } else if (strcmp(cmd, "CALGET") == 0) {
            // Write current cal values into the info char so the page can read them
            if (sInfoChar) {
                char buf[64];
                snprintf(buf, sizeof(buf), "cal:%d,%d,%d,%d,%d,%d,%.1f",
                         (int)gEyeCal.tlX, (int)gEyeCal.tlY,
                         (int)gEyeCal.brX, (int)gEyeCal.brY,
                         (int)gEyeCal.bmX, (int)gEyeCal.bmY,
                         gUpperSideDropLocal);
                sInfoChar->setValue(buf);
            }
        } else if (strncmp(cmd, "OTA_BLE_BEGIN:", 14) == 0) {
            uint32_t sz = (uint32_t)strtoul(cmd + 14, nullptr, 10);
            bleOtaBegin(sz);
        } else if (strcmp(cmd, "OTA_BLE_END") == 0) {
            bleOtaRequestCommit();
        } else if (strcmp(cmd, "OTA_BLE_ABORT") == 0) {
            bleOtaRequestAbort();
        } else if (strncmp(cmd, "LEDT:", 5) == 0) {
            uint8_t idx = (uint8_t)atoi(cmd + 5);
            ledZonesSetTour(static_cast<LedTourAnim>(idx));
        } else if (strncmp(cmd, "LEDM:", 5) == 0) {
            uint8_t idx = (uint8_t)atoi(cmd + 5);
            ledZonesSetMouth(static_cast<LedMouthAnim>(idx));
        } else if (strcmp(cmd, "LEDSAVE") == 0) {
            ledZonesSave();
        } else if (strncmp(cmd, "LIST:", 5) == 0) {
            const char* what = cmd + 5;
            if (sInfoChar) {
                char buf[300]; int pos = 0;
                if (strcmp(what, "styles") == 0) {
                    pos = snprintf(buf, sizeof(buf), "styles:");
                    for (uint8_t i = 0; i < (uint8_t)EyeStyleId::Count; ++i) {
                        if (i > 0 && pos < (int)sizeof(buf) - 1) buf[pos++] = ',';
                        pos += snprintf(buf + pos, sizeof(buf) - pos, "%s",
                                        eyeCatalogStyleName((EyeStyleId)i));
                    }
                    buf[pos] = '\0';
                    sInfoChar->setValue(buf);
                } else if (strcmp(what, "tour") == 0) {
                    pos = snprintf(buf, sizeof(buf), "tour:");
                    for (uint8_t i = 0; i < (uint8_t)LedTourAnim::Count; ++i) {
                        if (i > 0 && pos < (int)sizeof(buf) - 1) buf[pos++] = ',';
                        pos += snprintf(buf + pos, sizeof(buf) - pos, "%s",
                                        ledTourAnimName((LedTourAnim)i));
                    }
                    buf[pos] = '\0';
                    sInfoChar->setValue(buf);
                } else if (strcmp(what, "mouth") == 0) {
                    pos = snprintf(buf, sizeof(buf), "mouth:");
                    for (uint8_t i = 0; i < (uint8_t)LedMouthAnim::Count; ++i) {
                        if (i > 0 && pos < (int)sizeof(buf) - 1) buf[pos++] = ',';
                        pos += snprintf(buf + pos, sizeof(buf) - pos, "%s",
                                        ledMouthAnimName((LedMouthAnim)i));
                    }
                    buf[pos] = '\0';
                    sInfoChar->setValue(buf);
                } else if (strcmp(what, "games") == 0) {
                    sInfoChar->setValue("games:Simon,Stop,Snake,Mastermind,Tetris,Flappy,Breakout,Pacman");
                }
            }
        } else if (strcmp(cmd, "TAMA_TRIGGER") == 0) {
            tamaTriggerNeed();
        } else if (strcmp(cmd, "TAMA_GET") == 0) {
            if (sInfoChar) {
                char buf[80];
                snprintf(buf, sizeof(buf), "tama:lvl=%u,wakes=%u,needAt=%u,need=%d,flag=%d",
                         (unsigned)gTama.level,
                         (unsigned)gTama.wakeCount,
                         (unsigned)gTama.needAt,
                         (int)tamaNeedActive(),
                         (int)tamaFlagUnlocked());
                sInfoChar->setValue(buf);
            }
        } else if (strncmp(cmd, "TAMA_SETLEVEL:", 14) == 0) {
            uint8_t lv = (uint8_t)atoi(cmd + 14);
            tamaSetLevel(lv);
        } else if (strcmp(cmd, "FSM_WAKE") == 0) {
            badgeFsmSignalWake();
        } else if (strcmp(cmd, "FSM_SLEEP") == 0) {
            badgeFsmSignalSleep();
        } else if (strcmp(cmd, "FSM_GET") == 0) {
            if (sInfoChar) {
                char buf[48];
                snprintf(buf, sizeof(buf), "fsm:mode=%d,stage=%d,bg=%d",
                         (int)gBadgeMode, (int)gWakeStage, (int)gBgScanActive);
                sInfoChar->setValue(buf);
            }
        } else if (strcmp(cmd, "NVS_RESET") == 0) {
            Serial.println("[gatt] NVS_RESET → erasing all namespaces then reboot");
            buzzerBeep(2500, 70);
            buzzerBeep(2000, 70);
            buzzerBeep(1500, 70);
            buzzerBeep(1000, 70);
            buzzerBeep(600,  250);
            { Preferences p; p.begin(kNvsTama,     false); p.clear(); p.end(); }
            { Preferences p; p.begin(kNvsSettings, false); p.clear(); p.end(); }
            { Preferences p; p.begin(kNvsEyeCal,   false); p.clear(); p.end(); }
            { Preferences p; p.begin(kNvsEyeStyle, false); p.clear(); p.end(); }
            { Preferences p; p.begin(kNvsLedAnim,  false); p.clear(); p.end(); }
            { Preferences p; p.begin("fsm",        false); p.clear(); p.end(); }
            esp_restart();
        } else if (strncmp(cmd, "CRY", 3) == 0) {
            // CRY              → Pokémon-style glide, badge's own seed
            // CRY:music        → pentatonic melody, badge's own seed
            // CRY:<hex_seed>   → Pokémon-style glide with explicit 24-bit seed (e.g. CRY:2A24E1)
            if (cmd[3] == ':' && strcmp(cmd + 4, "music") == 0) {
                rtttlStart(badgeIdMusicRtttl());
            } else if (cmd[3] == ':' && cmd[4] != '\0') {
                uint32_t seed = (uint32_t)strtoul(cmd + 4, nullptr, 16);
                cryStart(seed);
            } else {
                cryStart(badgeIdCrySeed());
            }
        }
    }
};

class OtaDataCallbacks : public BLECharacteristicCallbacks {
    void onWrite(BLECharacteristic* c) override {
        std::string v = c->getValue();
        // 0-byte write = END sentinel (guaranteed in-order on same characteristic)
        if (v.empty()) {
            bleOtaRequestCommit();
            return;
        }
        if (!bleOtaInProgress()) return;
        bleOtaOnDataChunk(reinterpret_cast<const uint8_t*>(v.data()), v.size());
    }
};

static ServerCallbacks  sSrvCb;
static CmdCallbacks     sCmdCb;
static OtaDataCallbacks sOtaCb;

void gattStart() {
    if (sRunning) return;

    if (!BLEDevice::getInitialized()) {
        BLEDevice::init(badgeIdName());
    } else {
        esp_ble_gap_set_device_name(badgeIdName());
    }
    BLEDevice::setMTU(517);  // allow large WRITE_NR packets (OTA chunks = 244 B)

    sServer = BLEDevice::createServer();
    sServer->setCallbacks(&sSrvCb);

    sService = sServer->createService(kGattServiceUuid);

    // Info characteristic (read)
    sInfoChar = sService->createCharacteristic(
        kGattInfoCharUuid, BLECharacteristic::PROPERTY_READ);
    char info[64];
    snprintf(info, sizeof(info), "fw=%s id=%s name=%s",
             firmwareVersionString(), badgeIdHexString(), badgeIdName());
    sInfoChar->setValue(info);

    // Command characteristic (write)
    sCmdChar = sService->createCharacteristic(
        kGattCmdCharUuid, BLECharacteristic::PROPERTY_WRITE);
    sCmdChar->setCallbacks(&sCmdCb);

    // Music characteristic (write)
    sMusicChar = sService->createCharacteristic(
        kGattMusicCharUuid, BLECharacteristic::PROPERTY_WRITE);
    sMusicChar->setCallbacks(&sCmdCb);

    // OTA data characteristic (write-without-response) — raw firmware chunks
    sOtaDataChar = sService->createCharacteristic(
        kGattOtaDataCharUuid,
        BLECharacteristic::PROPERTY_WRITE_NR);
    sOtaDataChar->setCallbacks(&sOtaCb);

    // PIN characteristic (read + notify) — PIN on connect, then "OK"/"FAIL" on auth result
    sPinChar = sService->createCharacteristic(
        kGattPinCharUuid,
        BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY);
    sPinChar->addDescriptor(new BLE2902());
    sPinChar->setValue("----");   // placeholder before first connection

    sService->start();

    BLEAdvertising* adv = BLEDevice::getAdvertising();
    adv->addServiceUUID(kGattServiceUuid);
    adv->setScanResponse(true);
    adv->setMinPreferred(0x06);
    BLEDevice::startAdvertising();
    sAdvertisingRunning = true;

    sRunning           = true;
    sClientAuthorized  = false;
    sAuthPhase         = AuthPhase::Idle;
    Serial.println("[gatt] server started — advertising");
}

void gattStop() {
    if (!sRunning) return;
    BLEDevice::stopAdvertising();
    if (sService) sService->stop();
    BLEDevice::deinit(false);
    sServer             = nullptr;
    sService            = nullptr;
    sPinChar            = nullptr;
    sOtaDataChar        = nullptr;
    sRunning            = false;
    sClientConnected    = false;
    sClientAuthorized   = false;
    sAuthPhase          = AuthPhase::Idle;
    sAdvertisingRunning = false;
    Serial.println("[gatt] server stopped");
}

bool gattIsRunning()    { return sRunning; }
bool gattAuthPending()  { return sRunning && sAuthPhase == AuthPhase::Asking; }

void gattTick(uint32_t nowMs) {
    if (!sRunning) return;

    // Re-arm advertising after disconnect
    if (!sClientConnected && !sAdvertisingRunning) {
        Serial.println("[gatt] restarting advertising after disconnect");
        BLEDevice::startAdvertising();
        sAdvertisingRunning = true;
    }

    // Consume pending auth request (set from BLE callback thread)
    if (sPendingAuth) {
        sPendingAuth  = false;
        sAuthPhase    = AuthPhase::Asking;
        sAuthStartMs  = nowMs;
        eyeAnimSetMode(EyeAnimMode::ForceAwake);  // freeze eyes open, no blink during auth
        buttonsClearLatched();
    }

    // Update info char with OTA bytes received so the page can poll it
    if (bleOtaInProgress() && sInfoChar) {
        char buf[16];
        snprintf(buf, sizeof(buf), "ota:%u", bleOtaBytesReceived());
        sInfoChar->setValue(buf);
    }

    if (sAuthPhase == AuthPhase::Asking) {
        drawAuthUi(nowMs);

        // Timeout → reject
        if (nowMs - sAuthStartMs >= kAuthTimeoutMs) {
            sAuthPhase        = AuthPhase::Rejected;
            sClientAuthorized = false;
            if (sPinChar) { sPinChar->setValue("FAIL"); sPinChar->notify(); }
            if (sServer && sClientConnected) sServer->disconnect(0);
            Serial.println("[gatt] auth timeout — disconnecting");
            return;
        }

        // CENTER = accept
        if (gButtonPressedLatched[kBtnCenter]) {
            buttonsClearLatched();
            sClientAuthorized = true;
            sAuthPhase        = AuthPhase::Accepted;
            if (sPinChar) { sPinChar->setValue("OK"); sPinChar->notify(); }
            eyeAnimSetMode(EyeAnimMode::ForceAwake);
            Serial.printf("[gatt] auth accepted (PIN %s)\n", sAuthPin);
        }
        // LEFT or RIGHT = reject
        else if (gButtonPressedLatched[kBtnLeft] || gButtonPressedLatched[kBtnRight]) {
            buttonsClearLatched();
            sClientAuthorized = false;
            sAuthPhase        = AuthPhase::Rejected;
            if (sPinChar) { sPinChar->setValue("FAIL"); sPinChar->notify(); }
            if (sServer && sClientConnected) sServer->disconnect(0);
            Serial.println("[gatt] auth rejected by user");
        }
    }

}
