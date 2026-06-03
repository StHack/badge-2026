// snake_game.cpp — Snake minigame for badge_v2.
#include "snake_game.h"
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
#include <esp_random.h>

namespace {

constexpr uint8_t  kWinFood          = 10;  // built-in default
constexpr uint32_t kCountdownStepMs  = 700;
constexpr uint32_t kGoCueMs          = 700;
constexpr uint32_t kTickMs           = 200;
constexpr uint32_t kAnimMs           = 1500;
constexpr uint32_t kTourFlashMs      = 500;
constexpr int16_t  kTextY            = 38;
constexpr int16_t  kInsideMargin     = 2;
constexpr uint8_t  kGridStep         = 3;
constexpr uint8_t  kGridW            = 43;
constexpr uint8_t  kGridH            = 21;
constexpr uint8_t  kMidGridGx        = 21;
constexpr uint8_t  kMidGridGy        = 10;
constexpr uint8_t  kSnakeHeadGx      = static_cast<uint8_t>(kMidGridGx + 1U);
constexpr uint8_t  kFoodEdgeMargin   = 3;
constexpr uint8_t  kFoodCenterRadGx  = 6;
constexpr uint8_t  kFoodCenterRadGy  = 5;
constexpr uint8_t  kMaxLen           = 80;

// Headings: 0=up, 1=right, 2=down, 3=left
constexpr int8_t kDx[4] = {0, 1, 0, -1};
constexpr int8_t kDy[4] = {-1, 0, 1, 0};

enum class Phase : uint8_t { Countdown, GoCue, Playing, SuccessAnim, FailAnim, Done };

uint8_t  sWinFood      = kWinFood;   // runtime win target (set by snakeGameStart)
bool     sTargetReached = false;
bool     sActive       = false;
Phase    sPhase        = Phase::Countdown;
uint8_t  sCdCount      = 3;
uint32_t sNextMs       = 0;
uint32_t sPhaseEndMs   = 0;
uint32_t sLastMoveMs   = 0;

uint8_t  sDir          = 1;
uint8_t  sSnakeGx[kMaxLen];
uint8_t  sSnakeGy[kMaxLen];
uint8_t  sSnakeLen     = 0;
uint8_t  sFoodGx       = 0;
uint8_t  sFoodGy       = 0;
uint8_t  sFoodEaten    = 0;

bool     sPrevBtn[3]   = {};

constexpr uint8_t kTurnPendingNone = 4;
uint8_t  sPendingTurnDir = kTurnPendingNone;

// Static to avoid stack overflow (~400 bytes).
static EyeUiGeom sSnakeGeom;

// ── Helpers ───────────────────────────────────────────────────────────────────

inline int16_t cellCenterX(uint8_t gx) {
    return static_cast<int16_t>(static_cast<int>(gx) * kGridStep + kGridStep / 2);
}
inline int16_t cellCenterY(uint8_t gy) {
    return static_cast<int16_t>(static_cast<int>(gy) * kGridStep + kGridStep / 2);
}

bool cellPlayable(const EyeUiGeom& g, uint8_t gx, uint8_t gy) {
    return eyeUiPointInsideEyeOpening(&g, cellCenterX(gx), cellCenterY(gy), kInsideMargin);
}

bool placeSnakeHorizontalRight(const EyeUiGeom& g, uint8_t gx, uint8_t gy) {
    if (gx < 2U) return false;
    if (!cellPlayable(g, gx, gy) ||
        !cellPlayable(g, static_cast<uint8_t>(gx - 1U), gy) ||
        !cellPlayable(g, static_cast<uint8_t>(gx - 2U), gy)) {
        return false;
    }
    sSnakeLen     = 3;
    sSnakeGx[0]   = gx;           sSnakeGy[0] = gy;
    sSnakeGx[1]   = gx - 1U;      sSnakeGy[1] = gy;
    sSnakeGx[2]   = gx - 2U;      sSnakeGy[2] = gy;
    sDir = 1;
    return true;
}

bool pickFoodInBox(const EyeUiGeom& g, uint8_t x0, uint8_t x1, uint8_t y0, uint8_t y1,
                   uint16_t maxAttempts) {
    if (x0 > x1 || y0 > y1) return false;
    const uint32_t spanX = static_cast<uint32_t>(x1 - x0 + 1U);
    const uint32_t spanY = static_cast<uint32_t>(y1 - y0 + 1U);
    for (uint16_t a = 0; a < maxAttempts; ++a) {
        const uint8_t gx = static_cast<uint8_t>(x0 + (esp_random() % spanX));
        const uint8_t gy = static_cast<uint8_t>(y0 + (esp_random() % spanY));
        if (!cellPlayable(g, gx, gy)) continue;
        bool onSnake = false;
        for (uint8_t i = 0; i < sSnakeLen; ++i) {
            if (sSnakeGx[i] == gx && sSnakeGy[i] == gy) { onSnake = true; break; }
        }
        if (!onSnake) { sFoodGx = gx; sFoodGy = gy; return true; }
    }
    return false;
}

bool pickRandomFood(const EyeUiGeom& g) {
    const int mid  = kMidGridGx;
    const int midy = kMidGridGy;
    int ix0 = std::max(mid - (int)kFoodCenterRadGx, (int)kFoodEdgeMargin);
    int ix1 = std::min(mid + (int)kFoodCenterRadGx, (int)kGridW - 1 - (int)kFoodEdgeMargin);
    int iy0 = std::max(midy - (int)kFoodCenterRadGy, (int)kFoodEdgeMargin);
    int iy1 = std::min(midy + (int)kFoodCenterRadGy, (int)kGridH - 1 - (int)kFoodEdgeMargin);
    if (pickFoodInBox(g, (uint8_t)ix0, (uint8_t)ix1, (uint8_t)iy0, (uint8_t)iy1, 220))
        return true;
    return pickFoodInBox(g,
        kFoodEdgeMargin, (uint8_t)(kGridW - 1U - kFoodEdgeMargin),
        kFoodEdgeMargin, (uint8_t)(kGridH - 1U - kFoodEdgeMargin), 260);
}

bool collides(uint8_t ngx, uint8_t ngy, bool willGrow) {
    const uint8_t lim = willGrow ? sSnakeLen : static_cast<uint8_t>(sSnakeLen - 1U);
    for (uint8_t i = 0; i < lim; ++i) {
        if (sSnakeGx[i] == ngx && sSnakeGy[i] == ngy) return true;
    }
    return false;
}

void clearAllLeds() {
    for (uint8_t i = 0; i < kLedTotal; ++i) gLedStrip.setPixelColor(i, 0);
}

void syncLeds(uint32_t nowMs) {
    clearAllLeds();
    minigameUiPaintTourFlashIfActive(nowMs);
}

// ── Drawing ───────────────────────────────────────────────────────────────────

void drawLeftScore(uint32_t nowMs) {
    if (!eyeUiBeginFrame(gLeftDisplay, false, nowMs, &sSnakeGeom)) return;
    char buf[8];
    if (sWinFood > 0)
        snprintf(buf, sizeof(buf), "%u/%u", static_cast<unsigned>(sFoodEaten), static_cast<unsigned>(sWinFood));
    else
        snprintf(buf, sizeof(buf), "%u", static_cast<unsigned>(sFoodEaten));
    eyeUiDrawRotatedKeyword(gLeftDisplay, &sSnakeGeom, buf);
    gLeftDisplay.display();
}

void drawRightCountdown(uint32_t nowMs) {
    drawLeftScore(nowMs);
    if (!eyeUiBeginFrame(gRightDisplay, true, nowMs, &sSnakeGeom)) return;
    char digit[2] = {static_cast<char>('0' + sCdCount), '\0'};
    eyeUiDrawTextCenteredNudged(gRightDisplay, kTextY, 2, digit, 0);
    gRightDisplay.display();
}

void drawRightGo(uint32_t nowMs) {
    drawLeftScore(nowMs);
    if (!eyeUiBeginFrame(gRightDisplay, true, nowMs, &sSnakeGeom)) return;
    eyeUiDrawTextCenteredNudged(gRightDisplay, kTextY, 2, "GO", 0);
    gRightDisplay.display();
}

void drawSnakeField() {
    for (uint8_t i = 0; i < sSnakeLen; ++i) {
        const int16_t x = static_cast<int16_t>(static_cast<int>(sSnakeGx[i]) * kGridStep);
        const int16_t y = static_cast<int16_t>(static_cast<int>(sSnakeGy[i]) * kGridStep);
        if (i == 0) {
            gRightDisplay.fillRect(x, y, kGridStep, kGridStep, SSD1306_WHITE);
        } else {
            gRightDisplay.fillRect(x + 1, y + 1, kGridStep - 2, kGridStep - 2, SSD1306_WHITE);
        }
    }
    gRightDisplay.fillRect(
        static_cast<int16_t>(static_cast<int>(sFoodGx) * kGridStep + 1),
        static_cast<int16_t>(static_cast<int>(sFoodGy) * kGridStep + 1),
        kGridStep - 2, kGridStep - 2, SSD1306_WHITE);
}

void drawPlaying(uint32_t nowMs) {
    if (eyeUiBeginFrame(gRightDisplay, true, nowMs, &sSnakeGeom)) {
        drawSnakeField();
        gRightDisplay.display();
    }
    drawLeftScore(nowMs);
}

void drawPhaseUi(uint32_t nowMs) {
    switch (sPhase) {
        case Phase::Countdown:    drawRightCountdown(nowMs);             break;
        case Phase::GoCue:        drawRightGo(nowMs);                   break;
        case Phase::Playing:      drawPlaying(nowMs);                   break;
        case Phase::SuccessAnim:  minigameUiDrawWinBothEyes(nowMs);     break;
        case Phase::FailAnim:     minigameUiDrawLoseBothEyes(nowMs);    break;
        case Phase::Done:                                               break;
    }
}

// ── Init ──────────────────────────────────────────────────────────────────────

bool tryInitSnakeAndFood(uint32_t nowMs) {
    eyeUiComputeGeomForMenu(true, nowMs, &sSnakeGeom);
    if (sSnakeGeom.opening <= 3) return false;

    bool placed = false;
    for (int8_t ring = 0; ring <= 8 && !placed; ++ring) {
        for (int8_t dgy = -ring; dgy <= ring && !placed; ++dgy) {
            for (int8_t dgx = -ring; dgx <= ring && !placed; ++dgx) {
                if (std::max(std::abs((int)dgx), std::abs((int)dgy)) != ring) continue;
                const int hx = (int)kSnakeHeadGx + dgx;
                const int gy = (int)kMidGridGy + dgy;
                if (hx < 2 || hx > kGridW - 1 || gy < 1 || gy > kGridH - 2) continue;
                if (placeSnakeHorizontalRight(sSnakeGeom, (uint8_t)hx, (uint8_t)gy))
                    placed = true;
            }
        }
    }
    if (!placed) {
        for (uint8_t gy = 5; gy <= 18 && !placed; ++gy)
            for (uint8_t gx = 6; gx <= 34 && !placed; ++gx)
                if (placeSnakeHorizontalRight(sSnakeGeom, gx, gy)) placed = true;
    }
    if (!placed) return false;
    return pickRandomFood(sSnakeGeom);
}

void placeFoodOnly(uint32_t nowMs) {
    eyeUiComputeGeomForMenu(true, nowMs, &sSnakeGeom);
    pickRandomFood(sSnakeGeom);
}

}  // namespace

// ── Public API ────────────────────────────────────────────────────────────────

void snakeGameStart(uint8_t winFood) {
    sWinFood       = winFood;  // 0 = infinite
    sTargetReached = false;
    rtttlStop();
    clearAllLeds();
    gLedStrip.show();

    sActive          = true;
    sPhase           = Phase::Countdown;
    sCdCount         = 3;
    sFoodEaten       = 0;
    sSnakeLen        = 0;
    sDir             = 1;
    sPendingTurnDir  = kTurnPendingNone;
    sPrevBtn[0]      = gButtonPressedLatched[0];
    sPrevBtn[1]      = gButtonPressedLatched[1];
    sPrevBtn[2]      = gButtonPressedLatched[2];

    const uint32_t t = millis();
    sNextMs          = t + kCountdownStepMs;
    sLastMoveMs      = t;
    drawPhaseUi(t);
    syncLeds(t);
}

void snakeGameStop() {
    if (!sActive) return;
    sActive = false;
    sPhase  = Phase::Countdown;
    rtttlStop();
    clearAllLeds();
    gLedStrip.show();
}

bool snakeGameIsActive() { return sActive; }

void snakeGameTick(uint32_t nowMs) {
    if (!sActive) return;

    if (sPhase == Phase::SuccessAnim) {
        drawPhaseUi(nowMs);
        syncLeds(nowMs);
        if (nowMs >= sPhaseEndMs) {
            gGameResult = GameResult::Won;
            snakeGameStop();
        }
        return;
    }

    if (sPhase == Phase::FailAnim) {
        drawPhaseUi(nowMs);
        syncLeds(nowMs);
        if (nowMs >= sPhaseEndMs) {
            gGameResult = sTargetReached ? GameResult::Won : GameResult::Lost;
            snakeGameStop();
        }
        return;
    }

    if (sPhase == Phase::GoCue) {
        drawPhaseUi(nowMs);
        syncLeds(nowMs);
        if (nowMs < sNextMs) return;
        if (!tryInitSnakeAndFood(nowMs)) {
            sPhase       = Phase::FailAnim;
            sPhaseEndMs  = nowMs + kAnimMs;
            drawPhaseUi(nowMs);
            return;
        }
        sPhase          = Phase::Playing;
        sLastMoveMs     = nowMs;
        sPendingTurnDir = kTurnPendingNone;
        sPrevBtn[0]     = gButtonPressedLatched[0];
        sPrevBtn[1]     = gButtonPressedLatched[1];
        sPrevBtn[2]     = gButtonPressedLatched[2];
        drawPhaseUi(nowMs);
        return;
    }

    if (sPhase == Phase::Countdown) {
        drawPhaseUi(nowMs);
        syncLeds(nowMs);
        if (nowMs < sNextMs) return;
        if (sCdCount <= 1) {
            sPhase   = Phase::GoCue;
            sNextMs  = nowMs + kGoCueMs;
        } else {
            --sCdCount;
            sNextMs  = nowMs + kCountdownStepMs;
        }
        drawPhaseUi(nowMs);
        return;
    }

    // Playing — buffer turn input.
    const bool curL = gButtonPressedLatched[0];
    const bool curR = gButtonPressedLatched[2];
    const bool edgeL = curL && !sPrevBtn[0];
    const bool edgeR = curR && !sPrevBtn[2];
    if (edgeL) {
        sPendingTurnDir = static_cast<uint8_t>((static_cast<unsigned>(sDir) + 3U) % 4U);
    } else if (edgeR) {
        sPendingTurnDir = static_cast<uint8_t>((static_cast<unsigned>(sDir) + 1U) % 4U);
    }
    sPrevBtn[0] = curL;
    sPrevBtn[1] = gButtonPressedLatched[1];
    sPrevBtn[2] = curR;

    if (nowMs - sLastMoveMs < kTickMs) {
        drawPhaseUi(nowMs);
        syncLeds(nowMs);
        return;
    }
    sLastMoveMs = nowMs;

    // Apply buffered turn (disallow 180°).
    if (sPendingTurnDir < 4U) {
        const uint8_t rev = static_cast<uint8_t>((static_cast<unsigned>(sDir) + 2U) % 4U);
        if (!(sSnakeLen > 1U && sPendingTurnDir == rev)) sDir = sPendingTurnDir;
        sPendingTurnDir = kTurnPendingNone;
    }

    eyeUiComputeGeomForMenu(true, nowMs, &sSnakeGeom);

    const int nh = static_cast<int>(sSnakeGx[0]) + static_cast<int>(kDx[sDir % 4]);
    const int nv = static_cast<int>(sSnakeGy[0]) + static_cast<int>(kDy[sDir % 4]);

    auto doLose = [&]() {
        rtttlStart("go:d=8,o=3,b=200:8c,8c");
        minigameUiArmTourFlashRed(nowMs, kTourFlashMs);
        sPhase      = Phase::FailAnim;
        sPhaseEndMs = nowMs + kAnimMs;
    };

    if (nh < 0 || nh > kGridW - 1 || nv < 0 || nv > kGridH - 1) {
        doLose();
    } else {
        const uint8_t ngx = static_cast<uint8_t>(nh);
        const uint8_t ngy = static_cast<uint8_t>(nv);

        if (!cellPlayable(sSnakeGeom, ngx, ngy)) {
            doLose();
        } else {
            const bool eat = (ngx == sFoodGx && ngy == sFoodGy);
            if (collides(ngx, ngy, eat)) {
                doLose();
            } else if (eat) {
                if (sSnakeLen >= kMaxLen) {
                    doLose();
                } else {
                    // Grow: shift body then prepend head.
                    for (uint8_t i = sSnakeLen; i > 0; --i) {
                        sSnakeGx[i] = sSnakeGx[i - 1];
                        sSnakeGy[i] = sSnakeGy[i - 1];
                    }
                    sSnakeGx[0] = ngx; sSnakeGy[0] = ngy;
                    ++sSnakeLen;
                    ++sFoodEaten;
                    if (sWinFood > 0 && sFoodEaten >= sWinFood && !sTargetReached) {
                        sTargetReached = true;
                        rtttlStart("win:d=16,o=5,b=200:16g,16e,16g,16c6");
                        minigameUiArmTourFlashGreen(nowMs, kTourFlashMs);
                    } else {
                        minigameUiArmTourFlashGreen(nowMs, kTourFlashMs);
                    }
                    placeFoodOnly(nowMs);
                }
            } else {
                // Move: shift body.
                for (uint8_t i = sSnakeLen - 1U; i > 0; --i) {
                    sSnakeGx[i] = sSnakeGx[i - 1];
                    sSnakeGy[i] = sSnakeGy[i - 1];
                }
                sSnakeGx[0] = ngx; sSnakeGy[0] = ngy;
            }
        }
    }

    drawPhaseUi(nowMs);
    syncLeds(nowMs);
}
