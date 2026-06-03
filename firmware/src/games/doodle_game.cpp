// doodle_game.cpp — Doodle Jump minigame for badge_v2.
//
// Right eye: game field (platforms + character).
// Left eye:  progress percentage toward the win scroll target.
//
// Physics runs at kPhysTickMs (40 ms) independent of the render frame rate.
// Platforms are stored in world-space Y coordinates:
//   screenY = worldY + sScrollY
// sScrollY increases as the camera scrolls upward (player climbs).
// Win when sScrollY >= sWinScroll.  Lose when character falls below kDeathScreenY.

#include "doodle_game.h"
#include "game_result.h"
#include "minigame_ui.h"
#include "../hal/buttons.h"
#include "../hal/leds.h"
#include "../hal/display.h"
#include "../hal/buzzer.h"
#include "../config/hardware.h"
#include "../eyes/eye_geometry.h"
#include "../sound/rtttl.h"
#include <Arduino.h>
#include <esp_random.h>
#include <algorithm>

namespace {

// ── Tuning ────────────────────────────────────────────────────────────────────
constexpr uint16_t kWinScrollDef    = 350;   // total upward scroll to win (px)
constexpr uint32_t kPhysTickMs      =  40;   // physics step interval (ms)
constexpr float    kGravity         = 0.35f; // px / tick²
constexpr float    kJumpVy          = -5.5f; // initial jump velocity (negative = up)
constexpr float    kMaxFallVy       =  5.0f; // terminal fall velocity
constexpr int8_t   kMoveSpeed       =   4;   // horizontal px / physics tick
constexpr int16_t  kCamLine         =  22;   // screen Y where camera starts scrolling
constexpr int16_t  kDeathScreenY    =  70;   // screen Y: player dies if charY exceeds this
constexpr uint8_t  kPlatMinW        =  14;   // minimum platform width (px)
constexpr uint8_t  kPlatMaxW        =  24;   // maximum platform width (px)
constexpr uint8_t  kPlatSpMin       =  10;   // min vertical gap between platforms (px)
constexpr uint8_t  kPlatSpMax       =  15;   // max vertical gap between platforms (px)
constexpr uint8_t  kMaxPlats        =  20;   // platform pool size
constexpr uint8_t  kCharW           =   3;   // character width (px)
constexpr uint8_t  kCharH           =   3;   // character height (px)
constexpr int16_t  kScreenW         = 128;
constexpr int16_t  kScreenH         =  64;
constexpr uint32_t kAnimMs          = 1500;  // win/lose animation duration
constexpr uint32_t kCountdownStepMs =  700;
constexpr uint32_t kGoCueMs         =  700;
constexpr int16_t  kTextY           =  38;   // Y for centred text in right eye

// First platform is placed at worldY=53 (character feet land here on start).
constexpr int16_t  kFirstPlatWorldY = 53;
constexpr float    kCharSpawnY      = 50.0f; // character top (feet at 53)

// ── Platform ──────────────────────────────────────────────────────────────────
struct Plat {
    int16_t worldY;  // world Y — screenY = worldY + sScrollY
    uint8_t x;       // left edge screen X
    uint8_t w;       // width; 0 = slot free
};

// ── State ─────────────────────────────────────────────────────────────────────
enum class Phase : uint8_t { Countdown, GoCue, Playing, SuccessAnim, FailAnim, Done };

static uint16_t  sWinScroll;
static bool      sActive       = false;
static Phase     sPhase        = Phase::Countdown;
static uint8_t   sCdCount      = 3;
static uint32_t  sNextMs       = 0;
static uint32_t  sPhaseEndMs   = 0;
static uint32_t  sLastPhysMs   = 0;

static float     sCx, sCy;     // character top-left in screen coordinates
static float     sCvy;         // vertical velocity (px/tick; negative = up)
static uint16_t  sScrollY;     // total pixels scrolled upward (progress)

static Plat      sPlats[kMaxPlats];
static int16_t   sTopWorldY;   // worldY of the highest generated platform

static EyeUiGeom sDoodleGeom;

// ── Helpers ───────────────────────────────────────────────────────────────────

static uint8_t randRange(uint8_t lo, uint8_t hi) {
    return (uint8_t)(lo + (uint8_t)(esp_random() % (uint32_t)(hi - lo + 1u)));
}

static void clearAllLeds() {
    for (uint8_t i = 0; i < kLedTotal; ++i) gLedStrip.setPixelColor(i, 0);
}

static void syncLeds(uint32_t nowMs) {
    clearAllLeds();
    minigameUiPaintTourFlashIfActive(nowMs);
    gLedStrip.show();
}

// ── Platform pool ─────────────────────────────────────────────────────────────

static void spawnPlatAt(int16_t worldY) {
    for (uint8_t i = 0; i < kMaxPlats; ++i) {
        if (sPlats[i].w != 0) continue;
        uint8_t w = randRange(kPlatMinW, kPlatMaxW);
        uint8_t x = (uint8_t)(esp_random() % (uint32_t)(kScreenW - w));
        sPlats[i] = { worldY, x, w };
        return;
    }
}

// Remove platforms that have scrolled below the screen bottom.
static void purgePlatforms() {
    for (uint8_t i = 0; i < kMaxPlats; ++i) {
        if (sPlats[i].w == 0) continue;
        if (sPlats[i].worldY + (int16_t)sScrollY > kScreenH + 4) {
            sPlats[i].w = 0;
        }
    }
}

// Generate platforms until coverage extends kCamLine pixels above camera top.
// Camera top = world Y -sScrollY; target = world Y -(sScrollY + kCamLine).
static void generatePlatforms() {
    int16_t target = -(int16_t)sScrollY - kCamLine;
    while (sTopWorldY > target) {
        int16_t next = sTopWorldY - (int16_t)randRange(kPlatSpMin, kPlatSpMax);
        spawnPlatAt(next);
        sTopWorldY = next;
    }
}

static void initPlatforms() {
    for (uint8_t i = 0; i < kMaxPlats; ++i) sPlats[i] = { 0, 0, 0 };

    // First platform: wide and centered so the character always lands after the
    // first jump regardless of horizontal movement.
    constexpr uint8_t kSafePlatW = 40;
    sPlats[0] = { kFirstPlatWorldY,
                  (uint8_t)(kScreenW / 2 - kSafePlatW / 2),
                  kSafePlatW };
    sTopWorldY = kFirstPlatWorldY;

    // Pre-fill the screen and a look-ahead buffer above it with random platforms.
    int16_t y = kFirstPlatWorldY;
    for (uint8_t i = 0; i < 14; ++i) {
        y -= (int16_t)randRange(kPlatSpMin, kPlatSpMax);
        spawnPlatAt(y);
        sTopWorldY = y;
    }
}

// ── Collision ─────────────────────────────────────────────────────────────────

// Auto-jump when falling character's feet cross a platform top surface.
static void checkLanding(float prevCy) {
    if (sCvy <= 0.0f) return;  // only check while falling

    float prevFeet = prevCy + (float)kCharH;
    float curFeet  = sCy   + (float)kCharH;

    int16_t charL = (int16_t)sCx;
    int16_t charR = (int16_t)(sCx + (float)(kCharW - 1));

    for (uint8_t i = 0; i < kMaxPlats; ++i) {
        if (sPlats[i].w == 0) continue;
        float platY = (float)(sPlats[i].worldY + (int16_t)sScrollY);

        // Did feet cross the platform surface this tick?
        if (prevFeet <= platY && curFeet >= platY) {
            int16_t platL = (int16_t)sPlats[i].x;
            int16_t platR = (int16_t)(sPlats[i].x + sPlats[i].w - 1);
            if (charR >= platL && charL <= platR) {
                sCy  = platY - (float)kCharH;
                sCvy = kJumpVy;
                return;
            }
        }
    }
}

// ── Physics step ─────────────────────────────────────────────────────────────

static void physicsTick() {
    // Horizontal movement — continuous while button held, wraps edge to edge
    if (gButtonState[kBtnLeft]) {
        sCx -= (float)kMoveSpeed;
        if (sCx < 0.0f) sCx = (float)(kScreenW - kCharW);
    }
    if (gButtonState[kBtnRight]) {
        sCx += (float)kMoveSpeed;
        if (sCx > (float)(kScreenW - kCharW)) sCx = 0.0f;
    }

    // Gravity
    sCvy += kGravity;
    if (sCvy > kMaxFallVy) sCvy = kMaxFallVy;

    float prevCy = sCy;
    sCy += sCvy;

    checkLanding(prevCy);

    // Camera scroll: lock character above kCamLine
    if (sCy < (float)kCamLine) {
        int16_t delta = (int16_t)((float)kCamLine - sCy);
        if (delta > 0) {
            sScrollY += (uint16_t)delta;
            sCy = (float)kCamLine;
        }
    }

    purgePlatforms();
    generatePlatforms();
}

// ── Drawing ───────────────────────────────────────────────────────────────────

static void drawLeftScore(uint32_t nowMs) {
    EyeUiGeom g;
    if (!eyeUiBeginFrame(gLeftDisplay, false, nowMs, &g)) return;
    char buf[6];
    if (sWinScroll > 0) {
        uint8_t pct = (uint8_t)std::min((uint32_t)99u,
                        (uint32_t)sScrollY * 100u / (uint32_t)sWinScroll);
        snprintf(buf, sizeof(buf), "%u%%", (unsigned)pct);
    } else {
        snprintf(buf, sizeof(buf), "%u", (unsigned)sScrollY);
    }
    eyeUiDrawRotatedKeyword(gLeftDisplay, &g, buf);
    gLeftDisplay.display();
}

static void drawRightCountdown(uint32_t nowMs) {
    if (!eyeUiBeginFrame(gRightDisplay, true, nowMs, &sDoodleGeom)) return;
    char digit[2] = { (char)('0' + sCdCount), '\0' };
    eyeUiDrawTextCenteredNudged(gRightDisplay, kTextY, 2, digit, 0);
    gRightDisplay.display();
}

static void drawRightGo(uint32_t nowMs) {
    if (!eyeUiBeginFrame(gRightDisplay, true, nowMs, &sDoodleGeom)) return;
    eyeUiDrawTextCenteredNudged(gRightDisplay, kTextY, 2, "GO", 0);
    gRightDisplay.display();
}

static void drawGameField(uint32_t nowMs) {
    if (!eyeUiBeginFrame(gRightDisplay, true, nowMs, &sDoodleGeom)) return;

    // Platforms: 2 px thick horizontal lines
    for (uint8_t i = 0; i < kMaxPlats; ++i) {
        if (sPlats[i].w == 0) continue;
        int16_t sy = sPlats[i].worldY + (int16_t)sScrollY;
        if (sy < 0 || sy >= kScreenH) continue;
        gRightDisplay.drawFastHLine(sPlats[i].x, sy, sPlats[i].w, SSD1306_WHITE);
        if (sy + 1 < kScreenH)
            gRightDisplay.drawFastHLine(sPlats[i].x, sy + 1, sPlats[i].w, SSD1306_WHITE);
    }

    // Character: 3×3 filled square
    gRightDisplay.fillRect((int16_t)sCx, (int16_t)sCy, kCharW, kCharH, SSD1306_WHITE);

    gRightDisplay.display();
}

static void drawPhaseUi(uint32_t nowMs) {
    switch (sPhase) {
        case Phase::Countdown:
            drawRightCountdown(nowMs);
            drawLeftScore(nowMs);
            break;
        case Phase::GoCue:
            drawRightGo(nowMs);
            drawLeftScore(nowMs);
            break;
        case Phase::Playing:
            drawGameField(nowMs);
            drawLeftScore(nowMs);
            break;
        case Phase::SuccessAnim:
            minigameUiDrawWinBothEyes(nowMs);
            break;
        case Phase::FailAnim:
            minigameUiDrawLoseBothEyes(nowMs);
            break;
        case Phase::Done:
            break;
    }
}

}  // namespace

// ── Public API ────────────────────────────────────────────────────────────────

void doodleGameStart(uint16_t winScroll) {
    sWinScroll  = winScroll;  // 0 = infinite
    rtttlStop();
    clearAllLeds();
    gLedStrip.show();

    sActive     = true;
    sPhase      = Phase::Countdown;
    sCdCount    = 3;
    sScrollY    = 0;
    sCx         = (float)(kScreenW / 2 - kCharW / 2);
    sCy         = kCharSpawnY;
    sCvy        = 0.0f;

    initPlatforms();

    const uint32_t t = millis();
    sNextMs     = t + kCountdownStepMs;
    sLastPhysMs = t;
    drawPhaseUi(t);
    syncLeds(t);
}

void doodleGameStop() {
    if (!sActive) return;
    sActive = false;
    sPhase  = Phase::Countdown;
    rtttlStop();
    clearAllLeds();
    gLedStrip.show();
}

bool doodleGameIsActive() { return sActive; }

void doodleGameTick(uint32_t nowMs) {
    if (!sActive) return;

    if (sPhase == Phase::SuccessAnim) {
        drawPhaseUi(nowMs);
        syncLeds(nowMs);
        if (nowMs >= sPhaseEndMs) {
            gGameResult = GameResult::Won;
            doodleGameStop();
        }
        return;
    }

    if (sPhase == Phase::FailAnim) {
        drawPhaseUi(nowMs);
        syncLeds(nowMs);
        if (nowMs >= sPhaseEndMs) {
            gGameResult = GameResult::Lost;
            doodleGameStop();
        }
        return;
    }

    if (sPhase == Phase::GoCue) {
        drawPhaseUi(nowMs);
        syncLeds(nowMs);
        if (nowMs >= sNextMs) {
            sPhase      = Phase::Playing;
            sLastPhysMs = nowMs;
            sCvy        = kJumpVy;  // first jump on game start
        }
        return;
    }

    if (sPhase == Phase::Countdown) {
        drawPhaseUi(nowMs);
        syncLeds(nowMs);
        if (nowMs >= sNextMs) {
            if (sCdCount <= 1) {
                sPhase  = Phase::GoCue;
                sNextMs = nowMs + kGoCueMs;
            } else {
                --sCdCount;
                sNextMs = nowMs + kCountdownStepMs;
            }
        }
        return;
    }

    // ── Playing ───────────────────────────────────────────────────────────────
    while (nowMs - sLastPhysMs >= kPhysTickMs) {
        sLastPhysMs += kPhysTickMs;
        physicsTick();

        // Win
        if (sWinScroll > 0 && sScrollY >= sWinScroll) {
            rtttlStart("win:d=16,o=5,b=200:16g,16e,16g,16c6");
            minigameUiArmTourFlashGreen(nowMs, 700);
            sPhase      = Phase::SuccessAnim;
            sPhaseEndMs = nowMs + kAnimMs;
            drawPhaseUi(nowMs);
            syncLeds(nowMs);
            return;
        }

        // Death
        if (sCy > (float)kDeathScreenY) {
            rtttlStart("go:d=8,o=3,b=200:8c,8c");
            minigameUiArmTourFlashRed(nowMs, 700);
            sPhase      = Phase::FailAnim;
            sPhaseEndMs = nowMs + kAnimMs;
            drawPhaseUi(nowMs);
            syncLeds(nowMs);
            return;
        }
    }

    drawPhaseUi(nowMs);
    syncLeds(nowMs);
}
