/* Badge Sthack 2026 — Firmware v1.0 */

#include <Arduino.h>
#include <esp_ota_ops.h>

#include "config/hardware.h"
#include "config/timing.h"
#include "config/version.h"

#include "hal/buttons.h"
#include "hal/buzzer.h"
#include "hal/display.h"
#include "hal/leds.h"

#include "core/identity.h"

#include "sleep/power.h"
#include "sound/rtttl.h"
#include "sound/cry.h"

#include "eyes/eye_calibration.h"
#include "eyes/eye_catalog.h"
#include "eyes/eye_renderer.h"
#include "eyes/eye_animation.h"

#include "leds/led_anim.h"
#include "leds/led_anim_zones.h"

#include "ble/gatt_server.h"
#include "ble/ble_ota.h"
#include "wifi_ota/ota.h"

#include "games/simon_game.h"
#include "games/stop_game.h"
#include "games/snake_game.h"
#include "games/mastermind_game.h"
#include "games/tetris_game.h"
#include "games/flappy_game.h"
#include "games/breakout_game.h"
#include "games/pacman_game.h"
#include "games/doodle_game.h"
#include "games/maze_game.h"
#include "games/dino_game.h"
#include "games/space_game.h"
#include "games/minigame_ui.h"

#include "tamagotchi/tamagotchi.h"

#include "mode_sleep/badge_fsm.h"
#include "mode_full/badge_menu.h"

// ── Frame pacing ──────────────────────────────────────────────────────────────
// Poll buttons at 1 ms resolution so short presses are never missed.
static void waitNextFrame() {
    // Clear latched presses from the current frame before accumulating new ones.
    // This ensures gButtonPressedLatched[i] reflects presses that happen *between*
    // frames (during this delay window) rather than the press that opened the frame.
    buttonsClearLatched();
    uint32_t start = millis();
    while (millis() - start < kFrameMs) {
        pollButtons();
        delay(1);
    }
}

// ── Rollback check ────────────────────────────────────────────────────────────
// If all 3 buttons are held at power-on, switch to the inactive OTA partition
// and reboot. This lets the user recover from a bad firmware update.
static void checkRollback() {
    // Raw GPIO read — buttonsInit() has not run yet.
    pinMode(kPinBtnLeft,   INPUT_PULLUP);
    pinMode(kPinBtnCenter, INPUT_PULLUP);
    pinMode(kPinBtnRight,  INPUT_PULLUP);
    delay(50);  // let pins settle

    if (digitalRead(kPinBtnLeft)   != LOW ||
        digitalRead(kPinBtnCenter) != LOW ||
        digitalRead(kPinBtnRight)  != LOW) {
        return;
    }

    Serial.println("[rollback] all buttons held — checking alternate partition");

    const esp_partition_t* running = esp_ota_get_running_partition();
    const esp_partition_t* target  = esp_ota_get_next_update_partition(NULL);

    if (!target) {
        Serial.println("[rollback] no alternate partition");
        return;
    }

    esp_app_desc_t desc;
    if (esp_ota_get_partition_description(target, &desc) != ESP_OK) {
        Serial.println("[rollback] alternate partition has no valid app");
        return;
    }

    Serial.printf("[rollback] %s -> %s  fw=%s\n",
                  running->label, target->label, desc.version);

    // Show feedback on both displays before rebooting.
    displayInit(gLeftDisplay,  gLeftBus,  kPinLeftSda,  kPinLeftScl,  "left");
    displayInit(gRightDisplay, gRightBus, kPinRightSda, kPinRightScl, "right");
    for (Adafruit_SSD1306* d : {&gLeftDisplay, &gRightDisplay}) {
        d->clearDisplay();
        d->setTextSize(1);
        d->setTextColor(SSD1306_WHITE);
        d->setCursor(20, 18);
        d->print("Rollback...");
        d->setCursor(4, 34);
        d->print(target->label);
        d->print("  ");
        d->print(desc.version);
        d->display();
    }
    delay(1500);

    esp_ota_set_boot_partition(target);
    esp_restart();
}

// ── Arduino setup ─────────────────────────────────────────────────────────────
void setup() {
    Serial.begin(115200);
    delay(100);

    // Read buttons early — raw GPIO, before any init that could disrupt state.
    // Must come first so the user only needs to hold buttons during power-on,
    // not through the entire init sequence.
    pinMode(kPinBtnLeft,   INPUT_PULLUP);
    pinMode(kPinBtnCenter, INPUT_PULLUP);
    pinMode(kPinBtnRight,  INPUT_PULLUP);
    delay(10);
    const bool centerHeldAtBoot = (digitalRead(kPinBtnCenter) == LOW);
    const bool secretWakeAtBoot = (digitalRead(kPinBtnLeft)   == LOW)
                                && (digitalRead(kPinBtnRight)  == LOW)
                                && (digitalRead(kPinBtnCenter) != LOW);

    // Rollback: must run before any other init.
    checkRollback();

    // Badge identity (must come first — other modules use it)
    badgeIdInit();
    Serial.printf("[boot] fw=%s id=%s name=%s\n",
                  firmwareVersionString(), badgeIdHexString(), badgeIdName());

    // Hardware
    buttonsInit();
    buzzerInit();
    ledsInit();
    displayInit(gLeftDisplay,  gLeftBus,  kPinLeftSda,  kPinLeftScl,  "left");
    displayInit(gRightDisplay, gRightBus, kPinRightSda, kPinRightScl, "right");

    // Persistent state
    eyeCalLoad();
    eyeCatalogStyleLoad();
    tamaInit();
    ledZonesLoad();
    badgeMenuSettingsLoad();

    // Badge FSM
    badgeFsmInit();

    // Restore GATT server if it was active before last reboot (FullMode only).
    if (gGattPersisted && gBadgeMode == BadgeMode::FullMode) {
        Serial.println("[boot] restoring GATT server (persisted)");
        badgeFsmSignalGattOn();
    }

    if (secretWakeAtBoot) {
        Serial.println("[boot] secret wake — awaiting code (sliding window)");
        static constexpr uint8_t kSecretLen = 6;
        static constexpr uint8_t kSecret[kSecretLen] = {0, 0, 2, 1, 2, 0};
        uint8_t buf[kSecretLen] = {};
        uint8_t count = 0;
        bool    found = false;
        while (!found) {
            pollButtons();
            for (uint8_t i = 0; i < 3; i++) {
                if (gButtonPressedLatched[i]) {
                    buttonsClearLatched();
                    // Slide buffer left, append new press
                    for (uint8_t j = 0; j < kSecretLen - 1; j++) buf[j] = buf[j + 1];
                    buf[kSecretLen - 1] = i;
                    if (++count >= kSecretLen) {
                        found = true;
                        for (uint8_t j = 0; j < kSecretLen; j++) {
                            if (buf[j] != kSecret[j]) { found = false; break; }
                        }
                    }
                    break;
                }
            }
            delay(1);
        }
        Serial.println("[boot] secret code OK — FullMode");
        badgeFsmSignalWake();
    }

    // Boot acknowledgement beep
    buzzerBeep(kBootBeepHz, kBootBeepMs);
    Serial.printf("[boot] sleep_interval=%us scan_window=%ums\n",
                  kSleepIntervalSec, kBeaconScanMs);

    // Targeted boot scan: if CENTER is held at boot, scan exclusively for ~B beacons.
    // This allows an organiser to push a targeted command to a specific badge
    // without it being triggered by a nearby broadcast.
    Serial.printf("[boot] centerHeldAtBoot=%d\n", centerHeldAtBoot);
    if (centerHeldAtBoot) {
        Serial.println("[boot] CENTER held — targeted boot scan mode");
        badgeFsmTargetedBootScan();
    }

    // First sleep — scan only if CENTER was held (targeted boot scan already ran above).
    // On a plain cold boot, go directly to sleep without scanning.
    badgeFsmHandleSleepEntry(centerHeldAtBoot);
}

// ── Arduino loop ──────────────────────────────────────────────────────────────
void loop() {
    pollButtons();
    const uint32_t now = millis();

    // ── GATT tick first: handles auth UI and button events for PIN confirm ─
    gattTick(now);
    static bool sPrevAuthPending = false;
    if (gattAuthPending()) {
        sPrevAuthPending = true;
        waitNextFrame();
        return;
    }
    if (sPrevAuthPending) {
        // Auth just ended — absorb any button presses so menu/games don't react
        sPrevAuthPending = false;
        badgeMenuSyncButtons();
        buttonsClearLatched();
        return;
    }

    // ── ① OTA binary transfer: tight loop, nothing else runs ──────────────
    if (bleOtaInProgress()) {
        bleOtaTick(now);
        delay(8);
        return;
    }
    if (otaTransferActive()) {
        otaTick();
        pollButtons();
        delay(3);
        return;
    }

    // ── ② Badge menu (FullMode only) ──────────────────────────────────────
    if (gBadgeMode == BadgeMode::FullMode) {
        badgeMenuTick(now);
        if (badgeMenuIsOpen()) {
            badgeFsmTick(now);
            if (badgeFsmMsgActive()) {
                displaysOn();
                eyesDrawBoth(badgeFsmComputePose(now), badgeFsmComputeExpression(now), badgeFsmScrollText(now));
            }
            ledAnimTick(now);
            rtttlTick(now);
            cryTick(now);
            waitNextFrame();
            return;
        }
    }

    // ── ④ Minigames ────────────────────────────────────────────────────────
#define GAME_TICK(active, tick) \
    if (active()) { \
        badgeFsmTick(now); \
        if (badgeFsmMsgActive()) { \
            displaysOn(); \
            eyesDrawBoth(badgeFsmComputePose(now), badgeFsmComputeExpression(now), badgeFsmScrollText(now)); \
        } else { \
            tick(now); minigameUiPaintTourFlashIfActive(now); \
        } \
        rtttlTick(now); cryTick(now); waitNextFrame(); return; \
    }

    GAME_TICK(simonGameIsActive,     simonGameTick)
    GAME_TICK(stopGameIsActive,      stopGameTick)
    GAME_TICK(snakeGameIsActive,     snakeGameTick)
    GAME_TICK(mastermindGameIsActive, mastermindGameTick)
    GAME_TICK(tetrisGameIsActive,    tetrisGameTick)
    GAME_TICK(flappyGameIsActive,    flappyGameTick)
    GAME_TICK(breakoutGameIsActive,  breakoutGameTick)
    GAME_TICK(pacmanGameIsActive,    pacmanGameTick)
    GAME_TICK(doodleGameIsActive,    doodleGameTick)
    GAME_TICK(mazeGameIsActive,        mazeGameTick)
    GAME_TICK(dinoGameIsActive,        dinoGameTick)
    GAME_TICK(spaceGameIsActive,       spaceGameTick)
#undef GAME_TICK

    // ── ⑤ Badge FSM tick (BLE signals, stage transitions) ─────────────────
    badgeFsmTick(now);

    // ── ⑥ Tamagotchi: drive interaction, forward buttons ──────────────────
    bool tamaActive = tamaTick(now);
    if (tamaActive && !simonGameIsActive() && !stopGameIsActive()) {
        // Forward button edges to tamagotchi pre-game tasks
        uint8_t mask = (gButtonPressedLatched[0] ? 0x01 : 0)
                     | (gButtonPressedLatched[1] ? 0x02 : 0)
                     | (gButtonPressedLatched[2] ? 0x04 : 0);
        if (mask) tamaOnButton(mask);
    }

    // ── ⑦ Sleep entry (blocks until wakeup if conditions met) ─────────────
    if (badgeFsmHandleSleepEntry()) return;

    // If tamaOnButton just launched a game this frame, skip the idle render.
    // GAME_TICK will own the display from the next frame onward.
    // Without this guard, eyesDrawBoth (sick mode) would overwrite the game's
    // first drawAllUi call, causing a one-frame sick-eye flash at game launch.
    if (simonGameIsActive() || stopGameIsActive() || snakeGameIsActive()
            || mastermindGameIsActive() || tetrisGameIsActive()
            || flappyGameIsActive() || breakoutGameIsActive() || pacmanGameIsActive()
            || doodleGameIsActive() || mazeGameIsActive()
            || dinoGameIsActive() || spaceGameIsActive()) {
        waitNextFrame();
        return;
    }

    // ── ⑧ Idle frame ───────────────────────────────────────────────────────
    // While the FSM is in Sleeping stage (BG ON / GATT / OTA preventing deep
    // sleep), keep screens blank — identical to what deep sleep leaves behind.
    // Exception: never blank during a tamagotchi celebration (tamaBusy) even if
    // badgeFsmTick hasn't had a chance to restore Awake yet this frame.
    if (gBadgeMode == BadgeMode::SleepMode && gWakeStage == WakeStage::Sleeping
            && !tamaBusy()) {
        displaysClear();
        displaysOff();
        ledAnimTick(now);
        rtttlTick(now);
        cryTick(now);
        otaTick();
        waitNextFrame();
        return;
    }

    displaysOn();

    EyePose       pose  = badgeFsmComputePose(now);
    EyeExpression expr  = badgeFsmComputeExpression(now);
    const char*   text  = badgeFsmScrollText(now);

    eyesDrawBoth(pose, expr, text);
    ledAnimTick(now);
    rtttlTick(now);
    cryTick(now);

    otaTick();

    waitNextFrame();
}
