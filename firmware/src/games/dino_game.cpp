// dino_game.cpp — Chrome Dino runner for badge_v2.
//
// RIGHT eye : scrolling runner — dino fixed left, cacti scroll right-to-left.
// LEFT eye  : score (cacti cleared / target) while running; "CLICK" while waiting.
//
// Controls: any button = jump (single jump, must be on ground).
//
// Physics: gravity 0.35 px/frame², jump vy -4.0 px/frame.
// Win: clear kWinScore cacti. Lose: collision with a cactus.

#include "dino_game.h"
#include "game_result.h"
#include "minigame_ui.h"
#include "inactivity.h"
#include "../hal/buttons.h"
#include "../hal/leds.h"
#include "../hal/display.h"
#include "../config/hardware.h"
#include "../eyes/eye_geometry.h"
#include "../sound/rtttl.h"
#include <Arduino.h>
#include <esp_random.h>
#include <cmath>
#include <algorithm>

namespace {

// ── Layout ────────────────────────────────────────────────────────────────────
constexpr int16_t  kDinoX        = 31;    // horizontal center of dino (fixed)
constexpr int16_t  kGroundY      = 48;    // ground (feet) y
constexpr int16_t  kDinoH        = 10;    // dino total height
constexpr int16_t  kDinoBodyW    = 8;     // body width (for hitbox)

// ── Physics ───────────────────────────────────────────────────────────────────
constexpr float    kGravity      = 0.35f;
constexpr float    kJumpVy       = -4.0f;

// ── Scroll ────────────────────────────────────────────────────────────────────
constexpr float    kScrollBase      = 1.8f;
constexpr float    kScrollPerScore  = 0.12f;  // px/frame per cactus cleared

// ── Obstacles ─────────────────────────────────────────────────────────────────
constexpr uint8_t  kCactusW      = 5;
constexpr uint8_t  kCactusArmW   = 3;
constexpr int16_t  kCactusSpawnX = 130;
constexpr int16_t  kCactusSpacing = 56;  // min gap between consecutive cacti
constexpr uint8_t  kMaxCacti     = 3;

// ── Clouds ────────────────────────────────────────────────────────────────────
constexpr float    kCloudSpeed    = 0.45f;  // fixed, no acceleration
constexpr int16_t  kCloudSpawnX   = 130;
constexpr int16_t  kCloudSpacing  = 55;    // min gap between clouds
constexpr uint8_t  kMaxClouds     = 3;
constexpr int16_t  kCloudYMin     = 10;
constexpr int16_t  kCloudYMax     = 20;

// ── Win / anim ────────────────────────────────────────────────────────────────
constexpr uint8_t  kWinScoreDef  = 20;
constexpr uint32_t kSuccessAnimMs = 1500;
constexpr uint32_t kDeathAnimMs   =  800;
constexpr uint32_t kTourFlashMs  = 500;

// ── State ─────────────────────────────────────────────────────────────────────
struct Cactus {
    float   x;
    uint8_t h;      // trunk height (8-16 px)
    bool    scored;
};

enum class Phase : uint8_t { Waiting, Playing, SuccessAnim, FailAnim };

static bool    sActive          = false;
static Phase   sPhase           = Phase::Waiting;
static uint32_t sPhaseEndMs     = 0;
static uint8_t  sScore          = 0;

static float   sDinoFeetY       = kGroundY;
static float   sDinoVy          = 0.0f;
static bool    sOnGround        = true;

static Cactus  sCacti[kMaxCacti];
static uint8_t sCactiCount      = 0;

struct Cloud { float x; int16_t y; };
static Cloud   sClouds[kMaxClouds];
static uint8_t sCloudsCount     = 0;

static bool    sPrevBtn[3]      = {};
static uint8_t  sWinScore       = kWinScoreDef;
static bool    sTargetReached   = false;
static bool    sWaitingFirstTap = true;

static InactivityTimer sInactivity;
static bool    sInactivityReady = false;

// ── Helpers ───────────────────────────────────────────────────────────────────

static void clearAllLeds() {
    for (uint8_t i = 0; i < kLedTotal; ++i) gLedStrip.setPixelColor(i, 0);
}

static float scrollSpeed() {
    return kScrollBase + sScore * kScrollPerScore;
}

static void spawnCloud(float x) {
    if (sCloudsCount >= kMaxClouds) return;
    int16_t y = kCloudYMin + (int16_t)(esp_random() % (uint32_t)(kCloudYMax - kCloudYMin + 1));
    sClouds[sCloudsCount++] = { x, y };
}

static void spawnCactus() {
    if (sCactiCount >= kMaxCacti) return;
    uint8_t h = (uint8_t)(8 + (uint8_t)(esp_random() % 9u));  // 8–16 px
    sCacti[sCactiCount++] = { (float)kCactusSpawnX, h, false };
}

// ── Drawing ───────────────────────────────────────────────────────────────────

// Simple pixel-art dino: head (right-shifted), body, tail, animated legs.
// Game field draws on the LEFT eye.
static void drawDino(uint32_t nowMs) {
    const int16_t feet = (int16_t)lroundf(sDinoFeetY);

    // Head (offset 1 px right of body centre)
    gLeftDisplay.fillRect(kDinoX + 1, feet - kDinoH,     6, 4, SSD1306_WHITE);
    // Body
    gLeftDisplay.fillRect(kDinoX - 3, feet - kDinoH + 4, kDinoBodyW, 4, SSD1306_WHITE);
    // Tail
    gLeftDisplay.fillRect(kDinoX - 5, feet - kDinoH + 5, 3, 2, SSD1306_WHITE);

    // Legs — alternate when running on ground
    if (sOnGround && ((nowMs / 100) & 1u)) {
        gLeftDisplay.fillRect(kDinoX - 2, feet - 2, 2, 2, SSD1306_WHITE);
        gLeftDisplay.fillRect(kDinoX + 2, feet - 1, 2, 1, SSD1306_WHITE);
    } else {
        gLeftDisplay.fillRect(kDinoX - 2, feet - 1, 2, 1, SSD1306_WHITE);
        gLeftDisplay.fillRect(kDinoX + 2, feet - 2, 2, 2, SSD1306_WHITE);
    }
}

static void drawClouds() {
    for (uint8_t i = 0; i < sCloudsCount; ++i) {
        const int16_t cx = (int16_t)lroundf(sClouds[i].x);
        const int16_t cy = sClouds[i].y;
        if (cx + 16 < 0 || cx > 128) continue;
        gLeftDisplay.fillRect(cx,     cy,     14, 3, SSD1306_WHITE);  // base
        gLeftDisplay.fillRect(cx + 2, cy - 3,  8, 4, SSD1306_WHITE);  // bump
    }
}

static void drawCacti() {
    for (uint8_t i = 0; i < sCactiCount; ++i) {
        const int16_t cx = (int16_t)lroundf(sCacti[i].x);
        const int16_t h  = sCacti[i].h;
        if (cx + kCactusW + kCactusArmW < 0 || cx > 128) continue;
        gLeftDisplay.fillRect(cx, kGroundY - h, kCactusW, h, SSD1306_WHITE);
        if (h >= 10) {
            gLeftDisplay.fillRect(cx - kCactusArmW, kGroundY - h + 3, kCactusArmW, 3, SSD1306_WHITE);
            gLeftDisplay.fillRect(cx + kCactusW,    kGroundY - h + 3, kCactusArmW, 3, SSD1306_WHITE);
        }
    }
}

static void drawGameEye(uint32_t nowMs) {
    gLeftDisplay.clearDisplay();
    gLeftDisplay.drawFastHLine(0, kGroundY, 128, SSD1306_WHITE);
    drawClouds();
    drawCacti();
    drawDino(nowMs);
    gLeftDisplay.display();
}

// Score / CLICK on the RIGHT eye.
static void drawScoreEye(uint32_t nowMs) {
    EyeUiGeom g;
    if (!eyeUiBeginFrame(gRightDisplay, true, nowMs, &g)) return;
    if (sWaitingFirstTap) {
        eyeUiDrawTextCenteredNudged(gRightDisplay, 43, 1, "CLICK", 0);
    } else {
        char buf[8];
        if (sWinScore > 0)
            snprintf(buf, sizeof(buf), "%u/%u", (unsigned)sScore, (unsigned)sWinScore);
        else
            snprintf(buf, sizeof(buf), "%u", (unsigned)sScore);
        eyeUiDrawRotatedKeyword(gRightDisplay, &g, buf);
    }
    gRightDisplay.display();
}

static void drawBothEyes(uint32_t nowMs) {
    drawGameEye(nowMs);
    drawScoreEye(nowMs);
}

// ── Collision ─────────────────────────────────────────────────────────────────

static bool hitCactus(uint8_t i) {
    const int16_t cx   = (int16_t)lroundf(sCacti[i].x);
    const int16_t feet = (int16_t)lroundf(sDinoFeetY);

    // Dino hitbox (inset 1 px for leniency)
    const int16_t dL   = kDinoX - 2;
    const int16_t dR   = kDinoX + 4;
    const int16_t dTop = feet - kDinoH + 2;
    const int16_t dBot = feet;

    // Cactus box (inset 1 px)
    const int16_t cL   = cx + 1;
    const int16_t cR   = cx + (int16_t)kCactusW - 1;
    const int16_t cTop = kGroundY - (int16_t)sCacti[i].h;
    const int16_t cBot = kGroundY;

    if (dR <= cL || dL >= cR) return false;
    if (dBot <= cTop || dTop >= cBot) return false;
    return true;
}

}  // namespace

// ── Public API ────────────────────────────────────────────────────────────────

void dinoGameStart(uint8_t winScore) {
    rtttlStop();
    clearAllLeds();
    gLedStrip.show();

    sWinScore        = winScore;  // 0 = infinite
    sTargetReached   = false;
    sDinoFeetY       = kGroundY;
    sDinoVy          = 0.0f;
    sOnGround        = true;
    sCactiCount      = 0;
    sCloudsCount     = 0;
    sScore           = 0;
    sPhase           = Phase::Waiting;
    sWaitingFirstTap = true;
    sInactivityReady = false;
    sActive          = true;
    gGameResult      = GameResult::None;

    sPrevBtn[0] = gButtonPressedLatched[0];
    sPrevBtn[1] = gButtonPressedLatched[1];
    sPrevBtn[2] = gButtonPressedLatched[2];

    const uint32_t t = millis();
    spawnCactus();
    spawnCloud(80.0f);   // one cloud already on screen at start
    drawBothEyes(t);
}

void dinoGameStop() {
    if (!sActive) return;
    sActive = false;
    rtttlStop();
    clearAllLeds();
    gLedStrip.show();
}

bool dinoGameIsActive() { return sActive; }

void dinoGameTick(uint32_t nowMs) {
    if (!sActive) return;

    // ── Result animations ─────────────────────────────────────────────────────
    if (sPhase == Phase::SuccessAnim) {
        minigameUiDrawWinBothEyes(nowMs);
        minigameUiPaintTourFlashIfActive(nowMs);
        if (nowMs >= sPhaseEndMs) { gGameResult = GameResult::Won; dinoGameStop(); }
        return;
    }
    if (sPhase == Phase::FailAnim) {
        drawBothEyes(nowMs);
        minigameUiPaintTourFlashIfActive(nowMs);
        if (nowMs >= sPhaseEndMs) {
            // Reset for another attempt — back to the CLICK waiting screen
            sDinoFeetY       = kGroundY;
            sDinoVy          = 0.0f;
            sOnGround        = true;
            sCactiCount      = 0;
            sCloudsCount     = 0;
            sScore           = 0;
            sTargetReached   = false;
            sWaitingFirstTap = true;
            sPhase           = Phase::Waiting;
            sInactivityReady = false;  // re-anchor inactivity on next play start
            spawnCactus();
            spawnCloud(80.0f);
        }
        return;
    }

    // ── Button edge detection ─────────────────────────────────────────────────
    bool jumpEdge = false;
    for (uint8_t i = 0; i < 3; ++i) {
        if (gButtonPressedLatched[i] && !sPrevBtn[i]) jumpEdge = true;
        sPrevBtn[i] = gButtonPressedLatched[i];
    }

    // ── Waiting: first tap launches the run ───────────────────────────────────
    if (sWaitingFirstTap) {
        if (!jumpEdge) {
            drawBothEyes(nowMs);
            minigameUiPaintTourFlashIfActive(nowMs);
            return;
        }
        sWaitingFirstTap = false;
        sPhase = Phase::Playing;
    }

    // ── Inactivity (anchored on first playing tick) ───────────────────────────
    if (!sInactivityReady) { sInactivityReady = true; sInactivity.reset(nowMs); }
    if (jumpEdge) sInactivity.reset(nowMs);
    InactivityState inact = sInactivity.tick(nowMs);
    if (inact == InactivityState::Timeout) { dinoGameStop(); return; }

    // ── Jump ─────────────────────────────────────────────────────────────────
    if (jumpEdge && sOnGround) {
        sDinoVy   = kJumpVy;
        sOnGround = false;
    }

    // ── Physics ───────────────────────────────────────────────────────────────
    sDinoVy    += kGravity;
    sDinoFeetY += sDinoVy;
    if (sDinoFeetY >= (float)kGroundY) {
        sDinoFeetY = kGroundY;
        sDinoVy    = 0.0f;
        sOnGround  = true;
    }

    // ── Scroll cacti, score, collision ────────────────────────────────────────
    const float spd = scrollSpeed();
    bool collision  = false;

    for (uint8_t i = 0; i < sCactiCount; ++i) {
        sCacti[i].x -= spd;

        if (!sCacti[i].scored &&
                sCacti[i].x + (float)kCactusW < (float)(kDinoX - 5)) {
            sCacti[i].scored = true;
            ++sScore;
            if (sWinScore > 0 && sScore >= sWinScore && !sTargetReached) {
                sTargetReached = true;
            }
            minigameUiArmTourFlashGreen(nowMs, kTourFlashMs);
        }

        if (hitCactus(i)) collision = true;
    }

    if (collision) {
        if (sTargetReached) {
            rtttlStart("win:d=16,o=5,b=200:16g,16e,16g,16c6");
            minigameUiArmTourFlashGreen(nowMs, kTourFlashMs);
            sPhase      = Phase::SuccessAnim;
            sPhaseEndMs = nowMs + kSuccessAnimMs;
        } else {
            rtttlStart("go:d=8,o=3,b=200:8c,8c");
            minigameUiArmTourFlashRed(nowMs, kTourFlashMs);
            sPhase      = Phase::FailAnim;
            sPhaseEndMs = nowMs + kDeathAnimMs;
        }
        drawBothEyes(nowMs);
        minigameUiPaintTourFlashIfActive(nowMs);
        return;
    }

    // Compact off-screen cacti
    {
        uint8_t w = 0;
        for (uint8_t i = 0; i < sCactiCount; ++i) {
            if (sCacti[i].x + (float)(kCactusW + kCactusArmW) > -4.0f) {
                if (w != i) sCacti[w] = sCacti[i];
                ++w;
            }
        }
        sCactiCount = w;
    }

    // Spawn a new cactus when there's enough room on screen
    float maxX = -200.0f;
    for (uint8_t i = 0; i < sCactiCount; ++i)
        if (sCacti[i].x > maxX) maxX = sCacti[i].x;
    if (sCactiCount == 0 || maxX < (float)(128 - kCactusSpacing))
        spawnCactus();

    // ── Scroll clouds (parallax) ──────────────────────────────────────────────
    for (uint8_t i = 0; i < sCloudsCount; ++i)
        sClouds[i].x -= kCloudSpeed;

    {
        uint8_t w = 0;
        for (uint8_t i = 0; i < sCloudsCount; ++i) {
            if (sClouds[i].x + 16.0f > -2.0f) {
                if (w != i) sClouds[w] = sClouds[i];
                ++w;
            }
        }
        sCloudsCount = w;
    }

    float maxCloudX = -200.0f;
    for (uint8_t i = 0; i < sCloudsCount; ++i)
        if (sClouds[i].x > maxCloudX) maxCloudX = sClouds[i].x;
    if (sCloudsCount == 0 || maxCloudX < (float)(128 - kCloudSpacing))
        spawnCloud((float)kCloudSpawnX);

    // ── Render ────────────────────────────────────────────────────────────────
    drawBothEyes(nowMs);
    if (inact == InactivityState::Warning)
        minigameUiDrawInactivityWarning(nowMs, sInactivity.secondsLeft(nowMs));
    minigameUiPaintTourFlashIfActive(nowMs);
}
