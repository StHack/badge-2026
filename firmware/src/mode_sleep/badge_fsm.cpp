#include "badge_fsm.h"
#include "../sleep/power.h"
#include "../ble/ble_scanner.h"
#include "../ble/gatt_server.h"
#include "../wifi_ota/ota.h"
#include "../tamagotchi/tamagotchi.h"
#include "../eyes/eye_animation.h"
#include "../eyes/eye_calibration.h"
#include "../leds/led_anim.h"
#include "../sound/rtttl.h"
#include "../sound/cry.h"
#include "../core/identity.h"
#include "../core/storage.h"
#include "../mode_full/badge_menu.h"
#include "../hal/buttons.h"
#include "../hal/buzzer.h"
#include "../hal/display.h"
#include "../hal/leds.h"
#include "../config/hardware.h"
#include "../config/timing.h"
#include "../config/version.h"
#include <Arduino.h>
#include <Preferences.h>
#include <esp_system.h>
#include <string.h>

static constexpr const char* kFsmNs  = "fsm";
static constexpr const char* kModeKey = "mode";

static void saveMode() {
    Preferences p = storageOpen(kFsmNs);
    p.putUChar(kModeKey, (uint8_t)gBadgeMode);
    p.end();
}

static BadgeMode loadMode() {
    Preferences p = storageOpen(kFsmNs, true);
    uint8_t v = p.getUChar(kModeKey, (uint8_t)BadgeMode::SleepMode);
    p.end();
    return (v == (uint8_t)BadgeMode::FullMode) ? BadgeMode::FullMode : BadgeMode::SleepMode;
}

// ── Module state ──────────────────────────────────────────────────────────────
BadgeMode  gBadgeMode    = BadgeMode::SleepMode;
WakeStage  gWakeStage    = WakeStage::Sleeping;
uint32_t   gStageSinceMs = 0;
bool       gBgScanActive = false;

// ── Pending signals (written from BLE callback, read from main loop) ──────────
static volatile bool sPendingWake    = false;
static volatile bool sPendingSleep   = false;
static volatile bool sPendingBgOn    = false;
static volatile bool sPendingBgOff   = false;
static volatile bool sPendingGattOn  = false;
static volatile bool sPendingGattOff = false;
static volatile bool sPendingCry     = false;
static volatile bool sPendingMsg     = false;
static volatile bool sPendingOta     = false;
static volatile bool sPendingTama    = false;
static char sOtaPayload[128] = {};
static char sMsgText[128]    = {};

// ── Last sleep wake cause ─────────────────────────────────────────────────────
// Tracks whether the most recent wake was a timer (RTC) or a button press.
// Only timer wakes increment the tamagotchi counter.
static bool sLastWakeWasTimer = true;

// ── Flag-on-wake ──────────────────────────────────────────────────────────────
// When max level is reached, button wakes show the CTF flag scroll instead of
// the normal peek animation.
static bool sCtfFlagOnWake = false;

// ── Message display overlay ───────────────────────────────────────────────────
static bool       sMsgDisplayActive = false;
static uint32_t   sMsgDisplayEndMs  = 0;
static WakeStage  sMsgSavedStage    = WakeStage::Sleeping;
static LedAnimMode sMsgSavedLed     = LedAnimMode::Off;

static constexpr uint32_t kMsgDisplayMs = 15000;

// ── Boot target scan mode ─────────────────────────────────────────────────────
// When true (during badgeFsmTargetedBootScan), beaconCallback only processes
// beacons flagged ~B and ignores everything else.
// In normal mode, ~B beacons are silently dropped.
static volatile bool sBootScanMode = false;

// ── BLE scan state ────────────────────────────────────────────────────────────
static bool     sScanRunning = false;
static uint32_t sScanStartMs = 0;
static uint32_t sScanNextMs  = 0;   // earliest time to start the next FullMode scan window

static constexpr uint32_t kFullModeScanIntervalMs = kSleepIntervalSec * 1000u; // same cadence as sleep-mode wake interval
static constexpr uint32_t kBgScanIntervalMs       = 200;   // ~200ms gap between BG scan windows (quasi-continuous)

// ── BLE beacon callback (BLE stack thread) ────────────────────────────────────
static void beaconCallback(const ParsedBeacon& b) {
    Serial.printf("[fsm] beacon cmd=%d boot=%d prox=%d id='%s' bootScanMode=%d\n",
                  (int)b.cmd, b.bootRequired, b.proximityRequired, b.badgeId, sBootScanMode);
    // ~B beacons are exclusive to targeted boot scan — drop them in normal mode.
    if (b.bootRequired && !sBootScanMode) { Serial.println("[fsm] drop: ~B outside boot scan"); return; }
    // In targeted boot scan mode, only ~B beacons are relevant.
    if (sBootScanMode && !b.bootRequired) { Serial.println("[fsm] drop: non-~B during boot scan"); return; }

    switch (b.cmd) {
        case BeaconCmd::Wake:    sPendingWake    = true; break;
        case BeaconCmd::Sleep:   sPendingSleep   = true; break;
        case BeaconCmd::BgOn:    sPendingBgOn    = true; break;
        case BeaconCmd::BgOff:   sPendingBgOff   = true; break;
        case BeaconCmd::Cry:  sPendingCry  = true; break;
        case BeaconCmd::Tama: sPendingTama = true; break;
        case BeaconCmd::Message:
            sPendingMsg = true;
            strncpy(sMsgText, b.payload, sizeof(sMsgText) - 1);
            break;
        case BeaconCmd::Ota:
            sPendingOta = true;
            strncpy(sOtaPayload, b.payload, sizeof(sOtaPayload) - 1);
            break;
        default: break;
    }
}

// ── Helpers ───────────────────────────────────────────────────────────────────
static void setStage(WakeStage stage, uint32_t nowMs) {
    gWakeStage    = stage;
    gStageSinceMs = nowMs;
}

// ── OTA payload: "<target_version>" ──────────────────────────────────────────
// WiFi credentials and OTA password are hardcoded in config/version.h.
static void handleOtaPayload(const char* payload) {
    const char* targetVer = payload;
    if (!targetVer || targetVer[0] == '\0') {
        Serial.println("[fsm] OTA payload empty");
        return;
    }
    Serial.printf("[fsm] OTA request: target=%s current=%s\n",
                  targetVer, BADGE_VERSION_STRING);
    if (strcmp(targetVer, BADGE_VERSION_STRING) > 0) {
        otaConnect(BADGE_OTA_SSID, BADGE_OTA_PSK, BADGE_OTA_URL);
    } else {
        Serial.println("[fsm] OTA skipped (already up-to-date)");
    }
}

// ── Signal setters (callable from any context) ───────────────────────────────
void badgeFsmSignalWake()                 { sPendingWake    = true; }
void badgeFsmSignalSleep()                { sPendingSleep   = true; }
void badgeFsmSignalBgOn()                 { sPendingBgOn    = true; }
void badgeFsmSignalBgOff()                { sPendingBgOff   = true; }
void badgeFsmSignalGattOn()               { sPendingGattOn  = true; }
void badgeFsmSignalGattOff()              { sPendingGattOff = true; }
void badgeFsmSignalCry()                  { sPendingCry     = true; }
void badgeFsmSignalMessage(const char* t) { strncpy(sMsgText, t, 127); sPendingMsg = true; }
void badgeFsmSignalOta(const char* p)     { strncpy(sOtaPayload, p, 127); sPendingOta = true; }

// ── Scan wait helper ──────────────────────────────────────────────────────────
// Waits up to maxMs, but exits early as soon as any pending signal is set.
static void scanPollMs(uint32_t maxMs) {
    uint32_t start = millis();
    while (millis() - start < maxMs) {
        if (sPendingWake || sPendingSleep || sPendingBgOn || sPendingBgOff
         || sPendingGattOn || sPendingGattOff || sPendingCry
         || sPendingMsg || sPendingOta || sPendingTama) break;
        delay(1);
    }
}

// ── Targeted boot scan (CENTER held at boot) ──────────────────────────────────
// Scans exclusively for ~B-flagged beacons for up to kBootTargetScanMs.
// Shows a visual indicator on both displays while waiting.
// Any matching beacon executes its command immediately via pending flags.
void badgeFsmTargetedBootScan() {
    Serial.printf("[fsm] targeted boot scan (~B) %ums\n", kBootTargetScanMs);

    // Completely silent and invisible: no display, no LED, no sound.
    // From the user's perspective the boot looks normal.
    // Only if a matching ~B beacon is received will something happen.
    sBootScanMode = true;
    bleScanStart(beaconCallback);
    scanPollMs(kBootTargetScanMs);
    bleScanStop();
    sBootScanMode = false;

    bool found = sPendingWake || sPendingSleep || sPendingCry || sPendingTama
              || sPendingMsg  || sPendingOta   || sPendingBgOn || sPendingGattOn;
    Serial.printf("[fsm] targeted boot scan done — %s\n",
                  found ? "command received" : "no ~B beacon found");
}

// ── Init ──────────────────────────────────────────────────────────────────────
void badgeFsmInit() {
    gBadgeMode    = loadMode();
    gWakeStage    = (gBadgeMode == BadgeMode::FullMode) ? WakeStage::Awake : WakeStage::Sleeping;
    gStageSinceMs = millis();
    gBgScanActive = false;
    if (gBadgeMode == BadgeMode::FullMode) {
        eyeAnimSetMode(EyeAnimMode::ForceAwake);
        ledAnimSetMode(LedAnimMode::MouthPink);
    } else {
        eyeAnimSetMode(EyeAnimMode::ForceClosed);
        ledAnimSetMode(LedAnimMode::Off);
    }
}

// ── Message display restore ───────────────────────────────────────────────────
static void msgDisplayEnd(uint32_t nowMs) {
    sMsgDisplayActive = false;
    setStage(sMsgSavedStage, nowMs);
    if (sMsgSavedStage == WakeStage::Sleeping) {
        eyeAnimSetMode(EyeAnimMode::ForceClosed);
        ledAnimSetMode(LedAnimMode::Off);
        // displays will be turned off by main.cpp sleeping path
    } else if (gBadgeMode == BadgeMode::FullMode) {
        eyeAnimSetMode(EyeAnimMode::ForceAwake);
        ledAnimSetMode(sMsgSavedLed);
    } else {
        eyeAnimSetMode(EyeAnimMode::ForceAwake);
        ledAnimSetMode(LedAnimMode::Off);
    }
}

// ── Main per-frame tick ───────────────────────────────────────────────────────
void badgeFsmTick(uint32_t nowMs) {
    // ── Message display overlay ───────────────────────────────────────────
    if (sMsgDisplayActive) {
        bool done = nowMs >= sMsgDisplayEndMs
                 || gButtonPressedLatched[kBtnLeft]
                 || gButtonPressedLatched[kBtnCenter]
                 || gButtonPressedLatched[kBtnRight];
        if (done) msgDisplayEnd(nowMs);
        // still process scan window close and pending signals below,
        // but skip stage-machine transitions
    }

    // ── Close scan window if timed out ────────────────────────────────────
    if (sScanRunning && nowMs - sScanStartMs >= kBeaconScanMs) {
        if (gBadgeMode == BadgeMode::FullMode || gBgScanActive) {
            // FullMode / BG: soft stop (keep BLE stack alive), schedule next window
            bleScanStopSoft();
            sScanNextMs = nowMs + (gBgScanActive ? kBgScanIntervalMs : kFullModeScanIntervalMs);
            sScanRunning = false;
        }
        // SleepMode: scan keeps running past kBeaconScanMs so the animation
        // can play its full duration. badgeFsmHandleSleepEntry() owns the stop.
    }

    // ── Consume pending signals ────────────────────────────────────────────
    if (sPendingWake) {
        sPendingWake = false;
        if (gBadgeMode == BadgeMode::SleepMode) {
            gBadgeMode = BadgeMode::FullMode;
            saveMode();
            setStage(WakeStage::WakingUp, nowMs);
            bleScanStop();
            sScanRunning = false;
            eyeAnimSetMode(EyeAnimMode::ForceAwake);
            ledAnimSetMode(LedAnimMode::WakeAccent);
            rtttlStart(badgeIdMusicRtttl());
            Serial.println("[fsm] WAKE → FullMode");
        }
    }

    if (sPendingSleep) {
        sPendingSleep = false;
        gBadgeMode = BadgeMode::SleepMode;
        saveMode();
        setStage(WakeStage::Sleeping, nowMs);
        if (sScanRunning) { bleScanStopSoft(); sScanRunning = false; }
        gattStop();
        otaDisconnect();
        eyeAnimSetMode(EyeAnimMode::ForceClosed);
        ledAnimSetMode(LedAnimMode::Off);
        Serial.println("[fsm] SLEEP → SleepMode");
    }

    if (sPendingBgOn) {
        sPendingBgOn  = false;
        gBgScanActive = true;
        sScanNextMs   = nowMs;  // start scanning immediately
        Serial.println("[fsm] BG_ON");
    }
    if (sPendingBgOff) {
        sPendingBgOff = false;
        gBgScanActive = false;
        if (sScanRunning && gBadgeMode == BadgeMode::SleepMode) {
            bleScanStopSoft();
            sScanRunning = false;
        }
        Serial.println("[fsm] BG_OFF");
    }
    if (sPendingGattOn) {
        sPendingGattOn = false;
        if (sScanRunning) { bleScanStopSoft(); sScanRunning = false; }
        gattStart();
        Serial.println("[fsm] GATT ON");
    }
    if (sPendingGattOff) {
        sPendingGattOff = false;
        if (sScanRunning) { bleScanStopSoft(); sScanRunning = false; }
        gattStop();
        Serial.println("[fsm] GATT OFF");
    }
    if (sPendingCry) { sPendingCry = false; cryStart(badgeIdCrySeed()); }
    if (sPendingMsg) {
        sPendingMsg = false;
        sMsgSavedStage = gWakeStage;
        sMsgSavedLed   = ledAnimGetMode();
        sMsgDisplayActive = true;
        sMsgDisplayEndMs  = nowMs + kMsgDisplayMs;
        displaysOn();
        cryStart(badgeIdCrySeed());
        eyeAnimSetMode(EyeAnimMode::ForceScrollText, sMsgText);
        ledAnimSetMode(LedAnimMode::Off);
        setStage(WakeStage::Awake, nowMs);
        Serial.printf("[fsm] MSG: %s\n", sMsgText);
    }
    if (sPendingOta) {
        sPendingOta = false;
        displaysOn();
        setStage(WakeStage::Awake, nowMs);
        handleOtaPayload(sOtaPayload);
    }
    if (sPendingTama) {
        sPendingTama = false;
        tamaTriggerNeed();
        // Force Awake from any state so ForceSick eye mode is not overridden
        // by a subsequent stage transition (e.g. Peeking → ForceClosing).
        if (gWakeStage != WakeStage::Awake && gWakeStage != WakeStage::WakingUp) {
            setStage(WakeStage::Awake, nowMs);
        }
        Serial.println("[fsm] TAMA → force need");
    }
    // ── Wake stage transitions ─────────────────────────────────────────────
    if (sMsgDisplayActive) return; // hold all stage transitions during message

    uint32_t elapsed = nowMs - gStageSinceMs;

    switch (gWakeStage) {
        case WakeStage::Sleeping:
            // Handled by badgeFsmHandleSleepEntry
            break;

        case WakeStage::Peeking:
            if (sCtfFlagOnWake) {
                // Show flag scroll for the full flag scroll duration, then close.
                if (elapsed >= (uint32_t)kCelebFlagScrollMs) {
                    sCtfFlagOnWake = false;
                    if (tamaBusy()) {
                        setStage(WakeStage::Awake, nowMs);
                    } else {
                        eyeAnimSetMode(EyeAnimMode::ForceClosing);
                        ledAnimSetMode(LedAnimMode::Off);
                        setStage(WakeStage::Closing, nowMs);
                    }
                }
            } else {
                // Normal peek: eye opens and holds, then close.
                if (elapsed >= (uint32_t)(kPeekClosedMs + kPeekOpeningMs + kPeekHoldMs)) {
                    if (tamaBusy()) {
                        setStage(WakeStage::Awake, nowMs);
                    } else {
                        eyeAnimSetMode(EyeAnimMode::ForceClosing);
                        ledAnimSetMode(LedAnimMode::Off);
                        setStage(WakeStage::Closing, nowMs);
                    }
                }
            }
            break;

        case WakeStage::WakingUp:
            if (elapsed >= (uint32_t)(kWakeIntroCloseMs + kWakeIntroOpenMs + kWakeIntroHoldMs)) {
                setStage(WakeStage::Awake, nowMs);
                if (gBadgeMode == BadgeMode::FullMode) {
                    ledAnimSetMode(LedAnimMode::MouthPink);
                }
            }
            break;

        case WakeStage::Awake:
            // In FullMode: stay awake indefinitely until SLEEP signal.
            // In SleepMode: transition to Dozing after idle period,
            // but NOT if a tamagotchi need is active.
            if (gBadgeMode == BadgeMode::SleepMode
                    && !tamaBusy()
                    && elapsed >= 10000) {
                // If eyes are already closed (e.g. after a celebration that ran
                // its own closing animation), go directly to Sleeping to avoid
                // briefly reopening the eyes via ForceSleepy.
                if (eyeAnimGetMode() == EyeAnimMode::ForceClosed) {
                    setStage(WakeStage::Sleeping, nowMs);
                } else {
                    setStage(WakeStage::Dozing, nowMs);
                    eyeAnimSetMode(EyeAnimMode::ForceSleepy);
                }
            }
            break;

        case WakeStage::Dozing:
            if (tamaBusy()) {
                // Celebration started while dozing — reset to Awake.
                setStage(WakeStage::Awake, nowMs);
            } else if (elapsed >= 3000) {
                eyeAnimSetMode(EyeAnimMode::ForceClosed);
                ledAnimSetMode(LedAnimMode::Off);
                setStage(WakeStage::Sleeping, nowMs);
            }
            break;

        case WakeStage::Closing:
            if (elapsed >= (uint32_t)kPeekClosingMs) {
                if (tamaBusy()) {
                    // Celebration started during close animation — snap back to Awake.
                    setStage(WakeStage::Awake, nowMs);
                } else {
                    eyeAnimSetMode(EyeAnimMode::ForceClosed);
                    setStage(WakeStage::Sleeping, nowMs);
                }
            }
            break;
    }

    // ── Tama busy in SleepMode: guarantee Awake so display is never suppressed ─
    // Covers any stage (Sleeping, Closing, Dozing) that badgeFsmTick may have set
    // before tamaTick had a chance to set tamaBusy() — or that a pending BLE signal
    // pushed us into while a celebration was already running.
    if (gBadgeMode == BadgeMode::SleepMode && tamaBusy()
            && gWakeStage != WakeStage::Awake && gWakeStage != WakeStage::WakingUp) {
        displaysOn();
        setStage(WakeStage::Awake, nowMs);
    }

    // ── FullMode: ensure mouth-pink LEDs when settled ─────────────────────
    if (gBadgeMode == BadgeMode::FullMode && gWakeStage == WakeStage::Awake) {
        LedAnimMode m = ledAnimGetMode();
        if (m != LedAnimMode::MouthPink && m != LedAnimMode::TamaSuccess
                && m != LedAnimMode::TamaCry) {
            ledAnimSetMode(LedAnimMode::MouthPink);
        }
    }

    // ── Periodic scan window: FullMode (~1 min) or BG active (quasi-continuous) ─
    // gBeaconScanDisabled suppresses FullMode scan only; BG_ON overrides it.
    if ((gBgScanActive || (gBadgeMode == BadgeMode::FullMode && !gBeaconScanDisabled))
            && !sScanRunning && nowMs >= sScanNextMs) {
        bleScanStart(beaconCallback);
        sScanRunning = true;
        sScanStartMs = nowMs;
    }

}

// ── Sleep entry ───────────────────────────────────────────────────────────────
// Returns true if we just slept and woke. Caller should return from loop().
bool badgeFsmHandleSleepEntry(bool doScan) {
    if (gWakeStage != WakeStage::Sleeping) return false;
    if (sMsgDisplayActive) return false;
    if (gBgScanActive || gattIsRunning() || otaIsConnected()) return false;
    if (tamaBusy()) return false;
    if (cryIsPlaying()) return false;  // don't scan while cry is playing — scan blocks cryTick

    // Clear outputs before sleep
    displaysClear();
    displaysOff();
    ledsOff();
    rtttlStop();

    // Wait for the scan window that started at wake time to complete, then stop
    // cleanly before light sleep (active BLE + light sleep = bad state).
    // If the tick stopped it early (safety stop), restart with a full window.
    if (doScan && !gattIsRunning()) {
        if (!sScanRunning) {
            bleScanStart(beaconCallback);
            sScanRunning = true;
            sScanStartMs = millis();
        }
        uint32_t elapsed   = millis() - sScanStartMs;
        uint32_t remaining = elapsed < kBeaconScanMs ? kBeaconScanMs - elapsed : 0;
        if (remaining > 0) scanPollMs(remaining);
        bleScanStop();
        sScanRunning = false;
    }

    // Notify tamagotchi — only timer wakes count toward the interaction counter.
    // Button wakes are ignored so that players pressing buttons don't accelerate needs.
    bool needTriggered = sLastWakeWasTimer ? tamaOnWake() : false;
    if (needTriggered) {
        setStage(WakeStage::Awake, millis());
        Serial.println("[fsm] tamagotchi need — staying awake");
        return false;
    }

    // Don't sleep if a signal arrived during scan
    if (sPendingWake || sPendingBgOn  || sPendingGattOn  || sPendingOta
     || sPendingCry  || sPendingMsg   || sPendingTama) {
        return false;
    }
    // Don't sleep while a cry is playing — let it finish through the main loop
    if (cryIsPlaying()) return false;

    Serial.printf("[fsm] sleeping %us\n", kSleepIntervalSec);
    powerEnterLightSleep(kSleepIntervalSec);
    // ── woke up ──

    // LEDC loses its GPIO matrix routing across light sleep — re-attach pin only.
    buzzerReinit();

    WakeCause cause = powerGetWakeCause();
    Serial.printf("[fsm] wake: %s\n",
        cause == WakeCause::Timer ? "timer" :
        cause == WakeCause::GPIO  ? "button" : "cold");

    // Scan starts immediately on every wake — sleep entry will wait for the
    // remaining window and stop it cleanly before the next light sleep.
    if (!gattIsRunning()) {
        bleScanStart(beaconCallback);
        sScanRunning = true;
        sScanStartMs = millis();
    }

    if (cause == WakeCause::GPIO) {
        sLastWakeWasTimer = false;
        setStage(WakeStage::Peeking, millis());
        if (tamaMaxLevelReached()) {
            sCtfFlagOnWake = true;
            eyeAnimSetMode(EyeAnimMode::ForceScrollText, tamaCtfFlag());
        } else {
            eyeAnimSetMode(EyeAnimMode::ForceAwake);
        }
    } else {
        sLastWakeWasTimer = true;
        // Timer wake: stay Sleeping, next call to badgeFsmHandleSleepEntry handles sleep
    }

    return true;
}

// ── Public state queries ──────────────────────────────────────────────────────
bool badgeFsmMsgActive() { return sMsgDisplayActive; }

// ── Pose / expression delegation ─────────────────────────────────────────────
EyePose       badgeFsmComputePose(uint32_t nowMs)       { return eyeAnimComputePose(nowMs); }
EyeExpression badgeFsmComputeExpression(uint32_t nowMs) { return eyeAnimComputeExpression(nowMs); }
const char*   badgeFsmScrollText(uint32_t /*nowMs*/)    { return eyeAnimScrollText(); }
