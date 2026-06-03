// stop_game.cpp — Stop (cursor stop) minigame for badge_v2.
// 5 levels: cursor gets faster, target zone gets narrower each level.
// Bar is drawn inside the eye opening (adapts to eye shape).
// Mouth LEDs show level progress. Countdown before each level.
#include "stop_game.h"
#include "game_result.h"
#include "minigame_ui.h"
#include "inactivity.h"
#include "../hal/buttons.h"
#include "../hal/leds.h"
#include "../hal/display.h"
#include "../config/timing.h"
#include "../config/hardware.h"
#include "../eyes/eye_geometry.h"
#include <Arduino.h>

namespace {

constexpr uint8_t  kNumLevels       = 5;   // built-in default
constexpr int16_t  kBarHalfH        = 5;    // bar half-height in pixels
constexpr int16_t  kBarMargin       = 3;    // eye-opening margin when scanning bar width
constexpr int16_t  kBarScanOffsetY  = 10;   // scan this many px below g.cy (wider eye area)
constexpr uint32_t kWinDisplayMs    = 2000;
constexpr uint32_t kLoseDisplayMs   = 1500;
constexpr uint32_t kBetweenMs       = 800;
constexpr uint32_t kCountdownStepMs = 600;
constexpr uint32_t kTimeoutMs       = 15000;
constexpr uint32_t kTourFlashMs     = 700;

// Per-level parameters
struct LevelDef {
    float targetFrac;    // target zone width as fraction of bar width
    float speedFracPerS; // cursor speed in bar-widths per second
};
constexpr LevelDef kLevels[kNumLevels] = {
    { 0.22f, 1.40f },  // level 1
    { 0.16f, 1.90f },  // level 2
    { 0.16f, 2.50f },  // level 3
    { 0.08f, 3.10f },  // level 4
    { 0.05f, 3.80f },  // level 5
};

enum class StopPhase : uint8_t {
    Countdown,
    Running,
    LevelComplete,
    BetweenLevels,
    LoseFeedback,
    Won,
};

uint8_t        sNumLevels     = kNumLevels;  // runtime level count (set by stopGameStart)
bool           sQuitOnLose   = false;       // true = menu mode (lose → exit)
InactivityTimer sInactivity;
bool      sActive        = false;
StopPhase sPhase         = StopPhase::Running;
uint8_t   sCurrentLevel  = 0;   // 0-based index into kLevels[]
float     sCursorNorm    = 0.f; // cursor position in [0..1] across bar
float     sDirection     = 1.f;
uint32_t  sPhaseStartMs  = 0;
uint32_t  sTimeoutEndMs  = 0;
uint8_t   sCdCount       = 3;
uint32_t  sCdNextMs      = 0;

// ── Helpers ───────────────────────────────────────────────────────────────────

void syncMouthLeds() {
    for (uint8_t i = 0; i < kLedMouth; ++i) {
        uint32_t c = (i < sCurrentLevel) ? gLedStrip.Color(0, 90, 200) : 0;
        gLedStrip.setPixelColor(kLedMouthStart + i, c);
    }
    gLedStrip.show();
}

// ── Drawing ───────────────────────────────────────────────────────────────────

// Draw the stop bar inside one eye, adapted to its opening shape.
void drawBar(Adafruit_SSD1306& disp, bool isRight, uint32_t nowMs) {
    EyeUiGeom g;
    if (!eyeUiBeginFrame(disp, isRight, nowMs, &g)) return;
    if (g.opening < 10) { disp.display(); return; }

    // Scan at a row below centre — wider part of the eye opening
    const int16_t scanY = g.cy + kBarScanOffsetY;
    int16_t xMin = 127, xMax = 0;
    for (int16_t x = 2; x < 126; ++x) {
        if (eyeUiPointInsideEyeOpening(&g, x, scanY, kBarMargin)) {
            if (x < xMin) xMin = x;
            if (x > xMax) xMax = x;
        }
    }
    if (xMax <= xMin + 6) { disp.display(); return; }

    const int16_t barW = xMax - xMin;
    const int16_t barY = scanY - kBarHalfH;
    const int16_t barH = kBarHalfH * 2;

    // Outer bar border
    disp.drawRect(xMin, barY, barW, barH, SSD1306_WHITE);

    // Target zone: centred, hatched (diagonal lines) — cursor stays white everywhere
    const float   tFrac   = kLevels[sCurrentLevel].targetFrac;
    const int16_t tW      = (int16_t)(barW * tFrac);
    const int16_t tX      = xMin + (barW - tW) / 2;
    const int16_t innerH  = barH - 2;
    const int16_t innerY0 = barY + 1;
    const int16_t innerY1 = barY + barH - 2;

    // Diagonal hatch inside target zone, step 4px
    for (int16_t d = -innerH; d < tW + innerH; d += 4) {
        int16_t ax = tX + d,          ay = innerY0;
        int16_t bx = tX + d + innerH, by = innerY1;
        // Clip to target rect horizontally
        if (ax < tX)      { ay += (tX - ax);       ax = tX; }
        if (bx > tX+tW-1) { by -= (bx-(tX+tW-1)); bx = tX+tW-1; }
        if (ax > bx || ay > by) continue;
        disp.drawLine(ax, ay, bx, by, SSD1306_WHITE);
    }

    // Cursor: 1px wide, always white
    const int16_t cx = xMin + (int16_t)(sCursorNorm * (float)(barW - 1));
    disp.drawFastVLine(cx, barY, barH, SSD1306_WHITE);

    disp.display();
}

void drawLeftScore(uint32_t nowMs) {
    EyeUiGeom g;
    if (!eyeUiBeginFrame(gLeftDisplay, false, nowMs, &g)) return;
    char buf[8];
    snprintf(buf, sizeof(buf), "%u/%u", sCurrentLevel, sNumLevels);
    eyeUiDrawRotatedKeyword(gLeftDisplay, &g, buf);
    gLeftDisplay.display();
}

void drawBothBars(uint32_t nowMs) {
    drawLeftScore(nowMs);
    drawBar(gRightDisplay, true, nowMs);
}

void drawCountdown(uint32_t nowMs) {
    char digit[2] = { (char)('0' + sCdCount), '\0' };
    EyeUiGeom g;
    drawLeftScore(nowMs);
    if (eyeUiBeginFrame(gRightDisplay, true, nowMs, &g)) {
        eyeUiDrawTextCenteredNudged(gRightDisplay, g.cy + 8, 2, digit);
        gRightDisplay.display();
    }
}

void drawEyeOutlines(uint32_t nowMs) {
    EyeUiGeom g;
    drawLeftScore(nowMs);
    if (eyeUiBeginFrame(gRightDisplay, true,  nowMs, &g)) gRightDisplay.display();
}

// ── Phase transitions ─────────────────────────────────────────────────────────

void enterCountdown(uint32_t nowMs) {
    sPhase    = StopPhase::Countdown;
    sCdCount  = 3;
    sCdNextMs = nowMs + kCountdownStepMs;
}

void enterRunning(uint32_t nowMs) {
    sPhase        = StopPhase::Running;
    sCursorNorm   = 0.f;
    sDirection    = 1.f;
    sTimeoutEndMs = nowMs + kTimeoutMs;
}

} // namespace

// ── Public API ────────────────────────────────────────────────────────────────

void stopGameStart(uint8_t numLevels) {
    sQuitOnLose = (numLevels == 0);
    sNumLevels  = (numLevels > 0 && numLevels <= kNumLevels) ? numLevels : kNumLevels;
    for (uint8_t i = 0; i < kLedTotal; ++i) gLedStrip.setPixelColor(i, 0);
    gLedStrip.show();
    sActive       = true;
    sCurrentLevel = 0;
    gGameResult   = GameResult::None;
    const uint32_t t = millis();
    sInactivity.reset(t);
    syncMouthLeds();
    enterCountdown(t);
    drawCountdown(t);
    Serial.printf("[stop] started — %u levels\n", sNumLevels);
}

bool stopGameIsActive() { return sActive; }

void stopGameStop() {
    if (!sActive) return;
    sActive = false;
    for (uint8_t i = 0; i < kLedTotal; ++i) gLedStrip.setPixelColor(i, 0);
    gLedStrip.show();
}

void stopGameTick(uint32_t nowMs) {
    if (!sActive) return;

    // ── Won ───────────────────────────────────────────────────────────────────
    if (sPhase == StopPhase::Won) {
        minigameUiDrawWinBothEyes(nowMs);
        if (nowMs - sPhaseStartMs >= kWinDisplayMs) {
            gGameResult = GameResult::Won;
            stopGameStop();
        }
        return;
    }

    // ── LoseFeedback ──────────────────────────────────────────────────────────
    if (sPhase == StopPhase::LoseFeedback) {
        minigameUiDrawLoseBothEyes(nowMs);
        if (nowMs - sPhaseStartMs >= kLoseDisplayMs) {
            if (sQuitOnLose) {
                gGameResult = GameResult::Lost;
                stopGameStop();
            } else {
                // Tama mode: restart from level 1 until the player wins.
                sCurrentLevel = 0;
                sInactivity.reset(nowMs);
                syncMouthLeds();
                enterCountdown(nowMs);
            }
        }
        return;
    }

    // ── LevelComplete ─────────────────────────────────────────────────────────
    if (sPhase == StopPhase::LevelComplete) {
        minigameUiDrawWinBothEyes(nowMs);
        if (nowMs - sPhaseStartMs >= kWinDisplayMs) {
            sPhase        = StopPhase::BetweenLevels;
            sPhaseStartMs = nowMs;
        }
        return;
    }

    // ── BetweenLevels ─────────────────────────────────────────────────────────
    if (sPhase == StopPhase::BetweenLevels) {
        drawEyeOutlines(nowMs);
        if (nowMs - sPhaseStartMs >= kBetweenMs) {
            enterCountdown(nowMs);
        }
        return;
    }

    // ── Countdown ─────────────────────────────────────────────────────────────
    if (sPhase == StopPhase::Countdown) {
        drawCountdown(nowMs);
        if (nowMs >= sCdNextMs) {
            if (sCdCount <= 1) {
                enterRunning(nowMs);
                drawBothBars(nowMs);
            } else {
                --sCdCount;
                sCdNextMs = nowMs + kCountdownStepMs;
            }
        }
        return;
    }

    // ── Running ───────────────────────────────────────────────────────────────

    // Timeout → lose
    if (nowMs >= sTimeoutEndMs) {
        sPhase        = StopPhase::LoseFeedback;
        sPhaseStartMs = nowMs;
        minigameUiArmTourFlashRed(nowMs, kTourFlashMs);
        minigameUiDrawLoseBothEyes(nowMs);
        return;
    }

    // Advance cursor
    const float speed = kLevels[sCurrentLevel].speedFracPerS
                        * (float)kFrameMs * 0.001f;
    sCursorNorm += speed * sDirection;
    if (sCursorNorm >= 1.f) { sCursorNorm = 1.f; sDirection = -1.f; }
    if (sCursorNorm <= 0.f) { sCursorNorm = 0.f; sDirection =  1.f; }

    drawBothBars(nowMs);

    {
        InactivityState inact = sInactivity.tick(nowMs);
        if (inact == InactivityState::Timeout) { stopGameStop(); return; }
        if (inact == InactivityState::Warning)
            minigameUiDrawInactivityWarning(nowMs, sInactivity.secondsLeft(nowMs));
    }

    // Center button: check hit
    if (gButtonPressedLatched[kBtnCenter]) {
        sInactivity.reset(nowMs);
        const float tFrac   = kLevels[sCurrentLevel].targetFrac;
        const float tCenter = 0.5f;
        // Latency margin: quarter-frame of cursor travel on each side
        const float margin  = kLevels[sCurrentLevel].speedFracPerS * (kFrameMs * 0.25f) * 0.001f;
        const bool  hit     = (sCursorNorm >= tCenter - tFrac * 0.5f - margin &&
                                sCursorNorm <= tCenter + tFrac * 0.5f + margin);

        if (hit) {
            ++sCurrentLevel;
            syncMouthLeds();

            if (sCurrentLevel >= sNumLevels) {
                // All levels cleared → win
                sPhase        = StopPhase::Won;
                sPhaseStartMs = nowMs;
                minigameUiArmTourFlashGreen(nowMs, kTourFlashMs);
                minigameUiDrawWinBothEyes(nowMs);
            } else {
                sPhase        = StopPhase::LevelComplete;
                sPhaseStartMs = nowMs;
                minigameUiArmTourFlashGreen(nowMs, kTourFlashMs);
                minigameUiDrawWinBothEyes(nowMs);
            }
        } else {
            sPhase        = StopPhase::LoseFeedback;
            sPhaseStartMs = nowMs;
            minigameUiArmTourFlashRed(nowMs, kTourFlashMs);
            minigameUiDrawLoseBothEyes(nowMs);
        }
    }
}
