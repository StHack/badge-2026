#include "badge_menu.h"
#include "../eyes/eye_geometry.h"
#include <Preferences.h>
#include "../eyes/eye_animation.h"
#include "../eyes/eye_catalog.h"
#include "../leds/led_anim.h"
#include "../leds/led_anim_zones.h"
#include "../ble/gatt_server.h"
#include "../hal/buttons.h"
#include "../hal/buzzer.h"
#include "../hal/display.h"
#include "../hal/leds.h"
#include "../config/hardware.h"
#include "../config/timing.h"
#include "../config/version.h"
#include "../core/identity.h"
#include "../tamagotchi/tamagotchi.h"
#include "../games/simon_game.h"
#include "../games/stop_game.h"
#include "../games/snake_game.h"
#include "../games/mastermind_game.h"
#include "../games/tetris_game.h"
#include "../games/flappy_game.h"
#include "../games/breakout_game.h"
#include "../games/pacman_game.h"
#include "../games/doodle_game.h"
#include "../games/maze_game.h"
#include "../games/dino_game.h"
#include "../games/space_game.h"
#include "../mode_sleep/badge_fsm.h"
#include "../sound/rtttl.h"
#include "../sound/cry.h"
#include <Arduino.h>
#include <string.h>
#include <stdio.h>

// ── Menu levels ───────────────────────────────────────────────────────────────

enum class MenuLevel : uint8_t { Root, Info, Games, Skin, Settings, Bluetooth };

// ── Root (5 items) ────────────────────────────────────────────────────────────
static constexpr uint8_t kRootCount = 5;
static const char* const kRootKeyword[kRootCount] = { "INFO", "GAMES", "SKIN", "SETTINGS", "BONUS" };
static const char* const kRootTitle[kRootCount]   = { "Info", "Games", "Skin", "Settings", "Bonus" };
static const char* const kRootData[kRootCount]    = { "Badge info", "12 games", "Style", "Configure", "Secret" };

// ── Info submenu (Name / ID / Version) ───────────────────────────────────────
static constexpr uint8_t kInfoCount = 3;
// Data is dynamic, generated in drawRight()

// ── Games submenu ─────────────────────────────────────────────────────────────
static constexpr uint8_t kGamesCount = 12;
static const char* const kGamesKeyword[kGamesCount] = {
    "SIMON", "STOP", "SNAKE", "MASTER", "TETRIS", "FLAPPY", "BREAKOUT", "PACMAN", "DOODLE", "MAZE", "DINO", "SPACE"
};
static const char* const kGamesTitle[kGamesCount] = {
    "Simon", "Stop", "Snake", "Mastermind", "Tetris", "Flappy", "Breakout", "Pac-Man", "Doodle", "Maze", "Dino", "Space Inv."
};

// ── Skin submenu (Eyes / Mouth / Rings / Blink) ───────────────────────────────
static constexpr uint8_t kSkinCount = 4;
static constexpr uint8_t kSkinStyle = 0;
static constexpr uint8_t kSkinMouth = 1;
static constexpr uint8_t kSkinTour  = 2;
static constexpr uint8_t kSkinBlink = 3;
static const char* const kSkinTitle[kSkinCount] = { "Eyes", "Mouth", "Rings", "Blink" };

// ── Bluetooth submenu (Server / Advertizing) ──────────────────────────────────
static constexpr uint8_t kBtCount  = 2;
static constexpr uint8_t kBtServer = 0;
static constexpr uint8_t kBtAdv    = 1;
static const char* const kBtTitle[kBtCount] = { "Server", "Remote ctrl" };

// ── Settings submenu (LEDs / Sound / Bluetooth) ───────────────────────────────
static constexpr uint8_t kSettCount = 3;
static constexpr uint8_t kSettLeds  = 0;
static constexpr uint8_t kSettVol   = 1;
static constexpr uint8_t kSettBle   = 2;
static const char* const kSettKeyword[kSettCount] = { "LEDS", "SON", "BLE" };
static const char* const kSettTitle[kSettCount]   = { "LEDs", "Son", "Bluetooth" };

// ── State ─────────────────────────────────────────────────────────────────────

static bool       sOpen        = false;
static MenuLevel  sLevel       = MenuLevel::Root;
static uint8_t    sSelected    = 0;
static bool       sEditMode    = false;   // value editing in Settings
static uint32_t   sHoldC       = 0;       // menu entry hold (outside menu)
static uint32_t   sMenuHoldC   = 0;       // C hold inside menu (long = back)
static bool       sMenuLongC   = false;   // long-press already fired, suppress release action
static uint32_t   sScrollSince = 0;
static bool       sPrevState[3] = {};

// Settings values (persisted in NVS)
static uint8_t    sBrightness  = 4;   // 0-10  (default 4/10)
bool              gBeaconScanDisabled = false;  // FullMode periodic scan on/off
bool              gGattPersisted      = false;  // GATT server auto-start on boot


// Pending game launched from BLE
static volatile uint8_t sPendingGame = 0xFF;

// Return-to-menu after game ends
static uint8_t sLastGameIdx  = 0xFF;  // index of last game launched from menu
static bool    sPrevAnyGame  = false; // was a game active last tick


bool badgeMenuIsOpen() { return sOpen; }

void badgeMenuSyncButtons() {
    sPrevState[0] = gButtonState[kBtnLeft];
    sPrevState[1] = gButtonState[kBtnCenter];
    sPrevState[2] = gButtonState[kBtnRight];
    sHoldC        = 0;
    sMenuHoldC    = 0;
}

void badgeMenuRequestGame(uint8_t id) {
    if (id < kGamesCount) sPendingGame = id;
}

// ── Helpers ───────────────────────────────────────────────────────────────────

static uint8_t itemCount() {
    switch (sLevel) {
        case MenuLevel::Root:      return kRootCount;
        case MenuLevel::Info:      return kInfoCount;
        case MenuLevel::Games:     return kGamesCount;
        case MenuLevel::Skin:      return kSkinCount;
        case MenuLevel::Settings:  return kSettCount;
        case MenuLevel::Bluetooth: return kBtCount;
    }
    return 0;
}

static void selectItem(uint8_t idx, uint32_t nowMs) {
    sSelected    = idx;
    sScrollSince = nowMs;
}

static void enterLevel(MenuLevel level, uint32_t nowMs) {
    sLevel    = level;
    sEditMode = false;
    selectItem(0, nowMs);
}

static void menuOpen(uint32_t nowMs) {
    sOpen       = true;
    sHoldC      = 0;
    sMenuHoldC  = 0;
    sMenuLongC  = true;   // center is still held from entry hold; suppress until released
    enterLevel(MenuLevel::Root, nowMs);
    sPrevState[0] = gButtonState[kBtnLeft];
    sPrevState[1] = gButtonState[kBtnCenter];
    sPrevState[2] = gButtonState[kBtnRight];
}

static void menuClose() { sOpen = false; }

// ── Apply settings ────────────────────────────────────────────────────────────

static void applyBrightness() { ledsSetBrightness(sBrightness); }

static void settingsLoad() {
    Preferences prefs;
    if (prefs.begin(kNvsSettings, true)) {
        gBuzzerMuted        = prefs.getBool("mute",     false);
        sBrightness         = prefs.getUChar("bright",  4);
        gBeaconScanDisabled = prefs.getBool("beacscan", false);
        gGattPersisted      = prefs.getBool("gatt",     false);
        eyeAnimSetBlinkEnabled(prefs.getBool("blink",   false));
        prefs.end();
    }
    applyBrightness();
}
static void settingsSaveMute()   { Preferences p; if (p.begin(kNvsSettings,false)) { p.putBool("mute",     gBuzzerMuted);                    p.end(); } }
static void settingsSaveBright() { Preferences p; if (p.begin(kNvsSettings,false)) { p.putUChar("bright",  sBrightness);                     p.end(); } }
static void settingsSaveScan()   { Preferences p; if (p.begin(kNvsSettings,false)) { p.putBool("beacscan", gBeaconScanDisabled);              p.end(); } }
static void settingsSaveGatt()   { Preferences p; if (p.begin(kNvsSettings,false)) { p.putBool("gatt",     gGattPersisted);                   p.end(); } }
static void settingsSaveBlink()  { Preferences p; if (p.begin(kNvsSettings,false)) { p.putBool("blink",    eyeAnimGetBlinkEnabled());         p.end(); } }

void badgeMenuSettingsLoad() { settingsLoad(); }

// ── Game launch ───────────────────────────────────────────────────────────────

static bool anyMenuGameActive() {
    return simonGameIsActive() || stopGameIsActive() || snakeGameIsActive()
        || mastermindGameIsActive() || tetrisGameIsActive() || flappyGameIsActive()
        || breakoutGameIsActive() || pacmanGameIsActive() || doodleGameIsActive()
        || mazeGameIsActive() || dinoGameIsActive() || spaceGameIsActive();
}

static void stopAllGames() {
    simonGameStop(); stopGameStop(); snakeGameStop(); mastermindGameStop();
    tetrisGameStop(); flappyGameStop(); breakoutGameStop(); pacmanGameStop();
    doodleGameStop(); mazeGameStop(); dinoGameStop(); spaceGameStop();
}

static bool gameBlocksMenuEntry() {
    return tamaNeedActive() || anyMenuGameActive();
}

static void launchGame(uint8_t gameIdx) {
    stopAllGames();
    buttonsClearLatched();
    menuClose();
    sLastGameIdx = gameIdx;
    sPrevAnyGame = true;
    switch (gameIdx) {
        case 0: simonGameStart(0);     break;  // infinite
        case 1: stopGameStart(0);      break;  // 0 = quit on lose
        case 2: snakeGameStart(0);     break;  // infinite
        case 3: mastermindGameStart(); break;
        case 4: tetrisGameStart(0);    break;  // infinite
        case 5: flappyGameStart(0);    break;  // infinite
        case 6: breakoutGameStart();   break;
        case 7: pacmanGameStart();     break;
        case 8: doodleGameStart(0);    break;  // infinite
        case 9:  mazeGameStart(0, 0); break;  // size selector
        case 10: dinoGameStart(0);  break;  // infinite
        case 11: spaceGameStart(); break;
        default: break;
    }
}

// ── Scrolling data helper ─────────────────────────────────────────────────────
// Returns a window into 'str' of at most maxCh characters, scrolled by time.
// Caller must provide a buffer of at least maxCh+1 bytes.
static const char* scrolledSlice(const char* str, uint8_t maxCh,
                                  uint32_t nowMs, char* buf) {
    const size_t len = strlen(str);
    if (len <= maxCh) {
        strncpy(buf, str, maxCh);
        buf[maxCh] = '\0';
        return buf;
    }
    // Ping-pong scroll: 3-step pause at each end, then advance one char / 350 ms.
    const uint32_t range  = (uint32_t)(len - maxCh);
    const uint32_t period = range + 6;  // +3 pause start, +3 pause end
    uint32_t elapsed = (nowMs - sScrollSince) / 350;
    uint32_t pos     = elapsed % (period * 2);
    if (pos >= period) pos = period * 2 - 1 - pos;   // reverse
    uint32_t offset  = pos > 3 ? pos - 3 : 0;
    if (offset > range) offset = range;
    strncpy(buf, str + offset, maxCh);
    buf[maxCh] = '\0';
    return buf;
}

// ── Drawing ───────────────────────────────────────────────────────────────────

// Left eye always shows the current section name, not the selected item.
// Max 5 chars (fits the rotated keyword rendering).
static const char* sectionKeyword() {
    switch (sLevel) {
        case MenuLevel::Root:      return "MENU";
        case MenuLevel::Info:      return "INFO";
        case MenuLevel::Games:     return "GAMES";
        case MenuLevel::Skin:      return "SKIN";
        case MenuLevel::Settings:  return "SET";
        case MenuLevel::Bluetooth: return "BLE";
    }
    return "MENU";
}

static void drawLeft(uint32_t nowMs) {
    EyeUiGeom g;
    if (!eyeUiBeginFrame(gLeftDisplay, false, nowMs, &g)) return;
    eyeUiDrawRotatedKeyword(gLeftDisplay, &g, sectionKeyword());
    gLeftDisplay.display();
}

static void drawRight(uint32_t nowMs) {
    EyeUiGeom g;
    if (!eyeUiBeginFrame(gRightDisplay, true, nowMs, &g)) return;

    constexpr uint8_t kMaxCh = 13;
    char scrollBuf[kMaxCh + 1];
    char line[32] = {};

    // 2-line layout (label top, value bottom) for Info, Skin, Bluetooth, and LEDs setting.
    const bool isTwoLineSetting = (sLevel == MenuLevel::Bluetooth)
                               || (sLevel == MenuLevel::Skin)
                               || (sLevel == MenuLevel::Settings && sSelected == kSettLeds);

    if (sLevel == MenuLevel::Info) {
        static const char* const kInfoLabel[kInfoCount] = { "Name", "ID", "Ver" };
        char value[32] = {};
        if      (sSelected == 0) snprintf(value, sizeof(value), "%s", badgeIdName());
        else if (sSelected == 1) snprintf(value, sizeof(value), "%s", badgeIdHexString());
        else if (sSelected == 2) snprintf(value, sizeof(value), "%s", firmwareVersionString());

        eyeUiDrawTextCenteredNudged(gRightDisplay, g.cy - 3, 1, kInfoLabel[sSelected], +17);
        const char* slice = scrolledSlice(value, kMaxCh, nowMs, scrollBuf);
        eyeUiDrawTextCenteredNudged(gRightDisplay, g.cy + 9, 1, slice, +5);
    } else if (isTwoLineSetting) {
        char value[32] = {};
        const char* label = nullptr;
        if (sLevel == MenuLevel::Bluetooth) {
            label = kBtTitle[sSelected];
            switch (sSelected) {
                case kBtServer:
                    snprintf(value, sizeof(value), "%s", gattIsRunning() ? "ON" : "OFF");
                    break;
                case kBtAdv:
                    snprintf(value, sizeof(value), "%s", gBeaconScanDisabled ? "OFF" : "ON");
                    break;
            }
        } else if (sLevel == MenuLevel::Skin) {
            label = kSkinTitle[sSelected];
            switch (sSelected) {
                case kSkinStyle:
                    snprintf(value, sizeof(value), sEditMode ? "<%s>" : "%s",
                             eyeCatalogStyleNameCurrent());
                    break;
                case kSkinMouth:
                    snprintf(value, sizeof(value), sEditMode ? "<%s>" : "%s",
                             ledMouthAnimName(ledZonesGetMouth()));
                    break;
                case kSkinTour:
                    snprintf(value, sizeof(value), sEditMode ? "<%s>" : "%s",
                             ledTourAnimName(ledZonesGetTour()));
                    break;
                case kSkinBlink:
                    snprintf(value, sizeof(value), "%s",
                             eyeAnimGetBlinkEnabled() ? "ON" : "OFF");
                    break;
            }
        } else {  // Settings kSettLeds
            label = kSettTitle[kSettLeds];
            snprintf(value, sizeof(value), sEditMode ? "< %d/10 >" : "%d/10", sBrightness);
        }
        const char* slice = scrolledSlice(value, kMaxCh, nowMs, scrollBuf);
        if (sLevel == MenuLevel::Bluetooth) {
            eyeUiDrawTextCenteredNudged(gRightDisplay, g.cy - 3, 1, slice,  +17);
            eyeUiDrawTextCenteredNudged(gRightDisplay, g.cy + 9, 1, label,  +5);
        } else {
            eyeUiDrawTextCenteredNudged(gRightDisplay, g.cy - 3, 1, label,  +17);
            eyeUiDrawTextCenteredNudged(gRightDisplay, g.cy + 9, 1, slice,  +5);
        }
    } else {
        switch (sLevel) {
            case MenuLevel::Root:
                strncpy(line, kRootTitle[sSelected], sizeof(line) - 1);
                break;
            case MenuLevel::Info: break;
            case MenuLevel::Games:
                strncpy(line, kGamesTitle[sSelected], sizeof(line) - 1);
                break;
            case MenuLevel::Settings:
                switch (sSelected) {
                    case kSettVol:
                        snprintf(line, sizeof(line), "Son %s", gBuzzerMuted ? "OFF" : "ON");
                        break;
                    case kSettBle:
                        strncpy(line, "Bluetooth", sizeof(line) - 1);
                        break;
                    default: break;
                }
                break;
            case MenuLevel::Skin:
            case MenuLevel::Bluetooth: break;
        }
        const char* slice = scrolledSlice(line, kMaxCh, nowMs, scrollBuf);
        eyeUiDrawTextCenteredNudged(gRightDisplay, g.cy + 9, 1, slice, +5);
    }

    gRightDisplay.display();
}

// ── Tick ──────────────────────────────────────────────────────────────────────


void badgeMenuTick(uint32_t nowMs) {

    // ── Return to Games menu when a menu-launched game ends ───────────────────
    if (sLastGameIdx != 0xFF && !sOpen) {
        const bool anyNow = anyMenuGameActive();
        if (sPrevAnyGame && !anyNow) {
            // Game just finished — reopen menu directly on Games submenu
            sOpen         = true;
            sHoldC        = 0;
            sMenuHoldC    = 0;
            sMenuLongC    = false;
            sLevel        = MenuLevel::Games;
            sEditMode     = false;
            selectItem(sLastGameIdx, nowMs);
            sPrevState[0] = gButtonState[kBtnLeft];
            sPrevState[1] = gButtonState[kBtnCenter];
            sPrevState[2] = gButtonState[kBtnRight];
            sLastGameIdx  = 0xFF;
            sPrevAnyGame  = false;
            drawLeft(nowMs);
            drawRight(nowMs);
            return;
        }
        sPrevAnyGame = anyNow;
    }

    // ── BLE game request: auto-launch outside menu context ────────────────────
    if (sPendingGame != 0xFF) {
        uint8_t id = sPendingGame;
        sPendingGame = 0xFF;
        menuClose();
        launchGame(id);
        return;
    }

    // ── Entry detection (menu closed) ─────────────────────────────────────────
    if (!sOpen) {
        if (!gButtonState[kBtnLeft] && !gButtonState[kBtnRight]) {
            if (!gameBlocksMenuEntry()) {
                // Awake mode (no game, no nightmare): 250ms hold opens menu
                if (gButtonPressedLatched[kBtnCenter] && sHoldC == 0) sHoldC = nowMs;
                if (!gButtonState[kBtnCenter]) sHoldC = 0;
                if (sHoldC > 0 && nowMs - sHoldC >= 250) { menuOpen(nowMs); sHoldC = 0; }
            } else if (anyMenuGameActive() && !tetrisGameIsActive()) {
                // In-game: long hold exits to menu.
                // Tetris excluded: CENTER hold = soft drop, conflicts with the 800ms threshold.
                if (gButtonPressedLatched[kBtnCenter] && sHoldC == 0) sHoldC = nowMs;
                if (!gButtonState[kBtnCenter]) sHoldC = 0;
                if (sHoldC > 0 && nowMs - sHoldC >= 800) {
                    stopAllGames();
                    menuOpen(nowMs);
                    sHoldC = 0;
                }
            } else {
                sHoldC = 0;  // nightmare window: bloque tout
            }
        } else {
            sHoldC = 0;
        }
        return;
    }

    // ── Inside menu ───────────────────────────────────────────────────────────

    const bool edgeL    = gButtonState[kBtnLeft]    && !sPrevState[0];
    const bool edgeR    = gButtonState[kBtnRight]   && !sPrevState[2];
    const bool pressC   = gButtonState[kBtnCenter]  && !sPrevState[1];
    const bool releaseC = !gButtonState[kBtnCenter] &&  sPrevState[1];
    sPrevState[0] = gButtonState[kBtnLeft];
    sPrevState[1] = gButtonState[kBtnCenter];
    sPrevState[2] = gButtonState[kBtnRight];

    // Long-press C: back one level (or exit if at root).
    // Short-press C (on release): action.
    if (gButtonState[kBtnCenter]) {
        if (pressC) { sMenuHoldC = nowMs; sMenuLongC = false; }
        if (!sMenuLongC && nowMs - sMenuHoldC >= 800) {
            sMenuLongC = true;
            sEditMode  = false;
            if (sLevel == MenuLevel::Root)      { menuClose(); return; }
            else if (sLevel == MenuLevel::Bluetooth) { enterLevel(MenuLevel::Settings, nowMs); }
            else                                { enterLevel(MenuLevel::Root, nowMs); }
        }
    } else {
        sMenuHoldC = 0;
    }
    const bool actionC = releaseC && !sMenuLongC;
    if (releaseC) sMenuLongC = false;

    // ── Edit mode (Skin: Eyes / Mouth / Rings  |  Settings: LEDs) ───────────
    if (sEditMode) {
        const uint8_t styleCount = (uint8_t)EyeStyleId::Count;
        const uint8_t tourCount  = (uint8_t)LedTourAnim::Count;
        const uint8_t mouthCount = (uint8_t)LedMouthAnim::Count;
        if (sLevel == MenuLevel::Skin) {
            if (edgeL) {
                if (sSelected == kSkinStyle) {
                    uint8_t idx = eyeCatalogStyleIndex();
                    eyeCatalogSetStyleByIndex(idx > 0 ? idx - 1 : styleCount - 1);
                }
                if (sSelected == kSkinTour) {
                    uint8_t idx = (uint8_t)ledZonesGetTour();
                    ledZonesSetTour(static_cast<LedTourAnim>(idx > 0 ? idx - 1 : tourCount - 1));
                }
                if (sSelected == kSkinMouth) {
                    uint8_t idx = (uint8_t)ledZonesGetMouth();
                    ledZonesSetMouth(static_cast<LedMouthAnim>(idx > 0 ? idx - 1 : mouthCount - 1));
                }
            }
            if (edgeR) {
                if (sSelected == kSkinStyle) {
                    uint8_t idx = eyeCatalogStyleIndex();
                    eyeCatalogSetStyleByIndex((idx + 1) % styleCount);
                }
                if (sSelected == kSkinTour) {
                    uint8_t idx = (uint8_t)ledZonesGetTour();
                    ledZonesSetTour(static_cast<LedTourAnim>((idx + 1) % tourCount));
                }
                if (sSelected == kSkinMouth) {
                    uint8_t idx = (uint8_t)ledZonesGetMouth();
                    ledZonesSetMouth(static_cast<LedMouthAnim>((idx + 1) % mouthCount));
                }
            }
            if (actionC) {
                if (sSelected == kSkinStyle) eyeCatalogStyleSave();
                if (sSelected == kSkinTour || sSelected == kSkinMouth) ledZonesSave();
                sEditMode = false;
            }
        } else {  // Settings: only LEDs has edit mode
            if (edgeL && sBrightness > 0)  { sBrightness--; applyBrightness(); }
            if (edgeR && sBrightness < 10) { sBrightness++; applyBrightness(); }
            if (actionC) { settingsSaveBright(); sEditMode = false; }
        }
        drawLeft(nowMs);
        drawRight(nowMs);
        return;
    }

    // ── Normal navigation ─────────────────────────────────────────────────────
    const uint8_t cnt = itemCount();
    if (edgeL) selectItem(sSelected > 0 ? sSelected - 1 : cnt - 1, nowMs);
    if (edgeR) selectItem(sSelected + 1 < cnt ? sSelected + 1 : 0, nowMs);

    // ── Action on short-press C ───────────────────────────────────────────────
    if (actionC) {
        switch (sLevel) {
            case MenuLevel::Root:
                switch (sSelected) {
                    case 0: enterLevel(MenuLevel::Info,     nowMs); break;
                    case 1: enterLevel(MenuLevel::Games,    nowMs); break;
                    case 2: enterLevel(MenuLevel::Skin,     nowMs); break;
                    case 3: enterLevel(MenuLevel::Settings, nowMs); break;
                    case 4:
                        rtttlStart("rickroll:d=4,o=5,b=200:8g,8a,8c6,8a,e6,8p,e6,8p,d6.,p,8p,8g,8a,8c6,8a,d6,8p,d6,8p,c6,8b,a.,8g,8a,8c6,8a,2c6,d6,b,a,g.,8p,g,2d6,2c6.,p,8g,8a,8c6,8a,e6,8p,e6,8p,d6.,p,8p,8g,8a,8c6,8a,2g6,b,c6.,8b,a,8g,8a,8c6,8a,2c6,d6,b,a,g.,8p,g,2d6,2c6");
                        break;
                }
                break;

            case MenuLevel::Info:
                if (sSelected == 0) cryStart(badgeIdCrySeed());  // Name → cry
                break;

            case MenuLevel::Games:
                launchGame(sSelected);
                return;

            case MenuLevel::Skin:
                if (sSelected == kSkinBlink) {
                    eyeAnimSetBlinkEnabled(!eyeAnimGetBlinkEnabled());
                    settingsSaveBlink();
                } else {
                    sEditMode = true;
                }
                break;

            case MenuLevel::Bluetooth:
                switch (sSelected) {
                    case kBtServer:
                        if (gattIsRunning()) {
                            badgeFsmSignalGattOff();
                            gGattPersisted = false;
                        } else {
                            badgeFsmSignalGattOn();
                            gGattPersisted = true;
                        }
                        settingsSaveGatt();
                        break;
                    case kBtAdv:
                        gBeaconScanDisabled = !gBeaconScanDisabled;
                        settingsSaveScan();
                        break;
                }
                break;

            case MenuLevel::Settings:
                switch (sSelected) {
                    case kSettVol:
                        gBuzzerMuted = !gBuzzerMuted;
                        settingsSaveMute();
                        break;
                    case kSettLeds:
                        sEditMode = true;
                        break;
                    case kSettBle:
                        enterLevel(MenuLevel::Bluetooth, nowMs);
                        break;
                }
                break;
        }
    }

    drawLeft(nowMs);
    drawRight(nowMs);
}
