// flappy_game.cpp — Flappy Bird minigame for badge_v2.
#include "flappy_game.h"
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
#include <string.h>
#include <algorithm>
#include <cmath>
#include <esp_random.h>

namespace {

constexpr uint8_t  kWinScore          = 10;  // built-in default
constexpr uint32_t kTourFlashMs       = 500;
constexpr uint32_t kAnimMs            = 1500;
constexpr int16_t  kInsideMargin      = -2;
constexpr int16_t  kSpawnInsidePad    = 2;
constexpr uint8_t  kGraceTicksAfterStart = 48;

constexpr int16_t  kBirdX        = 59;
constexpr int16_t  kBirdHalf     = 3;
constexpr int16_t  kBirdDrawHalo = 1;
constexpr float    kBirdStartY   = 42.0f;
constexpr int16_t  kPipeW             = 8;
constexpr float    kPipeSpeedBase     = 1.65f;
constexpr float    kPipeSpeedPerPoint = 0.18f;  // +0.18 px/frame per point scored
constexpr int16_t  kPipeGapHalfBase   = 10;
constexpr float    kPipeGapShrink     = 0.15f;  // gap half shrinks 0.15 px per point
constexpr int16_t  kPipeGapHalfMin    = 7;      // never narrower than this
constexpr int16_t  kPipeSpawnX   = 132;
constexpr int16_t  kPipeSpacing  = 54;
constexpr float    kGravity      = 0.30f;
constexpr float    kFlapVel      = -2.05f;
constexpr float    kVelClampDown = 4.0f;
constexpr float    kVelClampUp   = -3.2f;

struct Pipe {
    float   x;
    int16_t gapCy;
    int16_t gapHalf;  // gap half-height at spawn time
    bool    scored;
};

enum class Phase : uint8_t { Waiting, Playing, SuccessAnim, FailAnim, Done };

bool     sActive          = false;
Phase    sPhase           = Phase::Waiting;
uint32_t sPhaseEndMs      = 0;

float    sBirdY           = kBirdStartY;
float    sBirdVy          = 0.0f;
uint8_t  sScore           = 0;
uint8_t  sWinScore        = kWinScore;  // runtime win target (set by flappyGameStart)
Pipe     sPipes[4];
uint8_t  sPipeCount       = 0;
bool     sWaitingFirstTap = true;
uint8_t  sInvulnTicks     = 0;

bool     sTargetReached   = false;
bool     sPrevBtn[3]      = {};

float   pipeSpeed()   { return kPipeSpeedBase + sScore * kPipeSpeedPerPoint; }
int16_t pipeGapHalf() {
    int16_t g = (int16_t)(kPipeGapHalfBase - sScore * kPipeGapShrink);
    return g < kPipeGapHalfMin ? kPipeGapHalfMin : g;
}

void clearAllLeds() {
    for (uint8_t i = 0; i < kLedTotal; ++i) gLedStrip.setPixelColor(i, 0);
}

void syncLeds(uint32_t nowMs) {
    clearAllLeds();
    minigameUiPaintTourFlashIfActive(nowMs);
}

// ── Drawing ───────────────────────────────────────────────────────────────────

void drawLeftHud(uint32_t nowMs) {
    EyeUiGeom geom;
    if (!eyeUiBeginFrame(gLeftDisplay, false, nowMs, &geom)) return;
    if (!sWaitingFirstTap) {
        char buf[8];
        if (sWinScore > 0)
            snprintf(buf, sizeof(buf), "%u/%u", static_cast<unsigned>(sScore), static_cast<unsigned>(sWinScore));
        else
            snprintf(buf, sizeof(buf), "%u", static_cast<unsigned>(sScore));
        eyeUiDrawRotatedKeyword(gLeftDisplay, &geom, buf);
    } else {
        eyeUiDrawTextCenteredNudged(gLeftDisplay, 43, 1, "CLICK", 0);
    }
    gLeftDisplay.display();
}

void drawPipesOnly() {
    for (uint8_t i = 0; i < sPipeCount; ++i) {
        const int16_t px = static_cast<int16_t>(lroundf(sPipes[i].x));
        const int16_t cy = sPipes[i].gapCy;
        const int16_t gt = static_cast<int16_t>(cy - sPipes[i].gapHalf);
        const int16_t gb = static_cast<int16_t>(cy + sPipes[i].gapHalf);
        if (px + kPipeW > 0 && px < 128) {
            if (gt > 0)
                gRightDisplay.fillRect(px, 0, kPipeW, gt, SSD1306_WHITE);
            if (gb < 63)
                gRightDisplay.fillRect(px, static_cast<int16_t>(gb + 1), kPipeW,
                                       static_cast<int16_t>(64 - (gb + 1)), SSD1306_WHITE);
        }
    }
}

void drawBird() {
    const int16_t bx = static_cast<int16_t>(kBirdX - kBirdHalf);
    const int16_t by = static_cast<int16_t>(lroundf(sBirdY) - kBirdHalf);
    const int16_t sz = static_cast<int16_t>(kBirdHalf * 2);
    const int16_t h  = kBirdDrawHalo;
    gRightDisplay.fillRect(static_cast<int16_t>(bx - h), static_cast<int16_t>(by - h),
                           static_cast<int16_t>(sz + 2 * h), static_cast<int16_t>(sz + 2 * h),
                           SSD1306_BLACK);
    gRightDisplay.fillRect(bx, by, sz, sz, SSD1306_WHITE);
}

void drawPlaying(uint32_t nowMs) {
    EyeUiGeom gr;
    if (eyeUiBeginFrame(gRightDisplay, true, nowMs, &gr)) {
        drawPipesOnly();
        drawBird();
        gRightDisplay.display();
    }
    drawLeftHud(nowMs);
}

void drawPhaseUi(uint32_t nowMs) {
    switch (sPhase) {
        case Phase::Waiting:
        case Phase::Playing:
            drawPlaying(nowMs);
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

// ── Physics helpers ───────────────────────────────────────────────────────────

bool birdInsideEye(uint32_t nowMs) {
    EyeUiGeom g;
    eyeUiComputeGeomForMenu(true, nowMs, &g);
    if (g.opening <= 3) return true;
    return eyeUiPointInsideEyeOpening(&g, kBirdX,
                                      static_cast<int16_t>(lroundf(sBirdY)),
                                      kInsideMargin);
}

bool hitPipe(uint8_t idx) {
    const Pipe& p        = sPipes[idx];
    const int16_t pLeft  = static_cast<int16_t>(lroundf(p.x));
    const int16_t pRight = static_cast<int16_t>(pLeft + kPipeW);
    const int16_t bLeft  = static_cast<int16_t>(kBirdX - kBirdHalf);
    const int16_t bRight = static_cast<int16_t>(kBirdX + kBirdHalf);
    if (bRight <= pLeft || bLeft >= pRight) return false;
    const int16_t gapTop = static_cast<int16_t>(p.gapCy - p.gapHalf);
    const int16_t gapBot = static_cast<int16_t>(p.gapCy + p.gapHalf);
    const int16_t bTop   = static_cast<int16_t>(lroundf(sBirdY) - kBirdHalf);
    const int16_t bBot   = static_cast<int16_t>(lroundf(sBirdY) + kBirdHalf);
    return !(bTop >= gapTop && bBot <= gapBot);
}

void spawnPipe(uint32_t nowMs) {
    EyeUiGeom g;
    eyeUiComputeGeomForMenu(true, nowMs, &g);
    int16_t cy = 32;
    for (uint8_t attempt = 0; attempt < 24; ++attempt) {
        const int16_t cand = static_cast<int16_t>(22 + (int)(esp_random() % 22U));
        const int16_t gap = pipeGapHalf();
        if (eyeUiPointInsideEyeOpening(&g, kBirdX, cand,
                                        kSpawnInsidePad + gap + 2) &&
            eyeUiPointInsideEyeOpening(&g, (int16_t)(kPipeSpawnX - 10), cand,
                                        kSpawnInsidePad + gap)) {
            cy = cand;
            break;
        }
    }
    if (sPipeCount < 4) {
        sPipes[sPipeCount] = {(float)kPipeSpawnX, cy, pipeGapHalf(), false};
        ++sPipeCount;
    }
}

void enterPlaying(uint32_t nowMs) {
    sPhase           = Phase::Waiting;
    sBirdY           = kBirdStartY;
    sBirdVy          = 0.0f;
    sScore           = 0;
    sPipeCount       = 0;
    sWaitingFirstTap = true;
    sInvulnTicks     = 0;
    sPrevBtn[0]      = gButtonPressedLatched[0];
    sPrevBtn[1]      = gButtonPressedLatched[1];
    sPrevBtn[2]      = gButtonPressedLatched[2];
}

}  // namespace

// ── Public API ────────────────────────────────────────────────────────────────

void flappyGameStart(uint8_t winScore) {
    sWinScore      = winScore;  // 0 = infinite
    sTargetReached = false;
    rtttlStop();
    clearAllLeds();
    gLedStrip.show();

    sActive = true;

    const uint32_t t = millis();
    enterPlaying(t);
    spawnPipe(t);
    drawPhaseUi(t);
    syncLeds(t);
}

void flappyGameStop() {
    if (!sActive) return;
    sActive = false;
    sPhase  = Phase::Waiting;
    rtttlStop();
    clearAllLeds();
    gLedStrip.show();
}

bool flappyGameIsActive() { return sActive; }

void flappyGameTick(uint32_t nowMs) {
    if (!sActive) return;

    if (sPhase == Phase::SuccessAnim) {
        drawPhaseUi(nowMs);
        syncLeds(nowMs);
        if (nowMs >= sPhaseEndMs) {
            gGameResult = GameResult::Won;
            flappyGameStop();
        }
        return;
    }

    if (sPhase == Phase::FailAnim) {
        drawPhaseUi(nowMs);
        syncLeds(nowMs);
        if (nowMs >= sPhaseEndMs) {
            gGameResult = sTargetReached ? GameResult::Won : GameResult::Lost;
            flappyGameStop();
        }
        return;
    }

    // Waiting or Playing.
    bool flapEdge = false;
    for (uint8_t i = 0; i < 3; ++i) {
        if (gButtonPressedLatched[i] && !sPrevBtn[i]) flapEdge = true;
    }

    if (sWaitingFirstTap) {
        if (!flapEdge) {
            for (uint8_t i = 0; i < 3; ++i) sPrevBtn[i] = gButtonPressedLatched[i];
            drawPhaseUi(nowMs);
            syncLeds(nowMs);
            return;
        }
        sWaitingFirstTap = false;
        sPhase           = Phase::Playing;
        sBirdVy          = kFlapVel;
        sInvulnTicks     = kGraceTicksAfterStart;
    } else {
        if (flapEdge) sBirdVy = kFlapVel;
    }
    for (uint8_t i = 0; i < 3; ++i) sPrevBtn[i] = gButtonPressedLatched[i];

    // Physics.
    sBirdVy += kGravity;
    sBirdVy  = std::max(kVelClampUp, std::min(kVelClampDown, sBirdVy));
    sBirdY  += sBirdVy;

    bool collisionsOn = true;
    if (sInvulnTicks > 0U) { collisionsOn = false; --sInvulnTicks; }

    // Scroll pipes and score.
    const float curSpeed = pipeSpeed();
    for (uint8_t i = 0; i < sPipeCount; ++i) {
        sPipes[i].x -= curSpeed;
        if (!sPipes[i].scored &&
            sPipes[i].x + (float)kPipeW < (float)(kBirdX - kBirdHalf)) {
            sPipes[i].scored = true;
            ++sScore;
            if (sWinScore > 0 && sScore >= sWinScore && !sTargetReached) {
                sTargetReached = true;
                rtttlStart("win:d=16,o=5,b=200:16g,16e,16g,16c6");
                minigameUiArmTourFlashGreen(nowMs, kTourFlashMs);
            } else {
                rtttlStart("s2:d=16,o=5,b=400:16g");
                minigameUiArmTourFlashGreen(nowMs, kTourFlashMs);
            }
        }
        if (collisionsOn && hitPipe(i)) {
            rtttlStart("go:d=8,o=3,b=200:8c,8c");
            minigameUiArmTourFlashRed(nowMs, kTourFlashMs);
            sPhase      = Phase::FailAnim;
            sPhaseEndMs = nowMs + kAnimMs;
            drawPhaseUi(nowMs);
            syncLeds(nowMs);
            return;
        }
    }

    if (collisionsOn && !birdInsideEye(nowMs)) {
        rtttlStart("go:d=8,o=3,b=200:8c,8c");
        minigameUiArmTourFlashRed(nowMs, kTourFlashMs);
        sPhase      = Phase::FailAnim;
        sPhaseEndMs = nowMs + kAnimMs;
        drawPhaseUi(nowMs);
        syncLeds(nowMs);
        return;
    }

    // Compact off-screen pipes.
    uint8_t w = 0;
    for (uint8_t i = 0; i < sPipeCount; ++i) {
        if (sPipes[i].x + (float)kPipeW > -2.0f) {
            if (w != i) sPipes[w] = sPipes[i];
            ++w;
        }
    }
    sPipeCount = w;

    float maxPipeX = -200.0f;
    for (uint8_t i = 0; i < sPipeCount; ++i)
        maxPipeX = std::max(maxPipeX, sPipes[i].x);
    if (sPipeCount == 0 || maxPipeX < (float)(128 - kPipeSpacing))
        spawnPipe(nowMs);

    drawPhaseUi(nowMs);
    syncLeds(nowMs);
}
