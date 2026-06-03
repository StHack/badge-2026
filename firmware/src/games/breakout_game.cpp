// breakout_game.cpp — Breakout minigame for badge_v2.
#include "breakout_game.h"
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

constexpr uint32_t kCountdownStepMs           = 700;
constexpr uint32_t kGoCueMs                   = 700;
constexpr uint32_t kTourFlashMs               = 500;
constexpr uint32_t kAnimMs                    = 1500;
constexpr uint32_t kBallLostRespawnMs         = 1000;
constexpr int16_t  kTextY                     = 38;
constexpr int16_t  kMargin                    = 1;
constexpr int16_t  kBrickPlaceMargin          = 3;
constexpr float    kBrickLocalXInsetFrac      = 0.09f;
constexpr uint8_t  kBreakoutLowerEyelidSegs   = 9;

constexpr int16_t  kBrickW     = 4;
constexpr int16_t  kBrickH     = 2;
constexpr int16_t  kBrickStepX = 5;
constexpr int16_t  kBrickStepY = 3;
constexpr uint8_t  kMaxBricks  = 240;

constexpr float    kBallR              = 0.95f;
constexpr float    kPaddleHalfLenLocal = 11.0f;
constexpr float    kPaddleYInset       = 1.25f;
constexpr float    kPaddleLocalSpeed   = 3.4f;
constexpr float    kBallStartVy        = 2.6f;
constexpr float    kBallMaxVx          = 3.4f;
constexpr uint8_t  kStartLives         = 5;

struct Brick { int16_t x, y; bool alive; };

enum class Phase : uint8_t { Countdown, GoCue, Playing, BallLost, SuccessAnim, FailAnim, Done };

uint8_t  sMaxBricks      = 0;    // 0 = unlimited
bool     sActive         = false;
Phase    sPhase          = Phase::Countdown;
uint8_t  sCdCount        = 3;
uint32_t sNextMs         = 0;
uint32_t sPhaseEndMs     = 0;
uint32_t sBallLostEndMs  = 0;

Brick    sBricks[kMaxBricks];
uint8_t  sBrickCount     = 0;

float    sBallX          = 64.0f;
float    sBallY          = 40.0f;
float    sBallVx         = 1.1f;
float    sBallVy         = kBallStartVy;

float    sPaddleLocalX   = 0.0f;
uint8_t  sLives          = kStartLives;

bool     sPrevBtn[3]     = {};

static EyeUiGeom sGeom;

// ── Helpers ───────────────────────────────────────────────────────────────────

void clearAllLeds() {
    for (uint8_t i = 0; i < kLedTotal; ++i) gLedStrip.setPixelColor(i, 0);
}

void syncLeds(uint32_t nowMs) {
    clearAllLeds();
    minigameUiPaintTourFlashIfActive(nowMs);
}

uint16_t bricksAliveCount() {
    uint16_t n = 0;
    for (uint8_t i = 0; i < sBrickCount; ++i) if (sBricks[i].alive) ++n;
    return n;
}

bool brickFitsInEye(const EyeUiGeom& g, int16_t bx, int16_t by,
                    int16_t cornerM, float lxInsetFrac) {
    if (!eyeUiPointInsideEyeOpening(&g, bx, by, cornerM)) return false;
    if (!eyeUiPointInsideEyeOpening(&g, (int16_t)(bx + kBrickW - 1), by, cornerM)) return false;
    if (!eyeUiPointInsideEyeOpening(&g, bx, (int16_t)(by + kBrickH - 1), cornerM)) return false;
    if (!eyeUiPointInsideEyeOpening(&g, (int16_t)(bx + kBrickW - 1), (int16_t)(by + kBrickH - 1), cornerM))
        return false;
    if (lxInsetFrac <= 0.0f) return true;
    float lx, ly, lxMin, lxMax;
    eyeGeomScreenToLocal(&g, (int16_t)(bx + kBrickW / 2), (int16_t)(by + kBrickH / 2), &lx, &ly);
    eyeGeomGetLocalXExtents(&lxMin, &lxMax);
    const float inset = (lxMax - lxMin) * lxInsetFrac;
    return lx >= lxMin + inset && lx <= lxMax - inset;
}

bool brickRectOverlapsAny(int16_t bx, int16_t by) {
    for (uint8_t i = 0; i < sBrickCount; ++i) {
        if (!sBricks[i].alive) continue;
        if (bx < sBricks[i].x + kBrickW && bx + kBrickW > sBricks[i].x &&
            by < sBricks[i].y + kBrickH && by + kBrickH > sBricks[i].y)
            return true;
    }
    return false;
}

void buildBricks(uint32_t nowMs) {
    eyeUiComputeGeomForMenu(true, nowMs, &sGeom);
    float lxMin, lxMax;
    eyeGeomGetLocalXExtents(&lxMin, &lxMax);
    int16_t x0, y0, x1, y1;
    eyeUiUpperPaddleSegmentScreen(&sGeom, (lxMin + lxMax) * 0.5f,
                                  kPaddleHalfLenLocal, kPaddleYInset, &x0, &y0, &x1, &y1);
    const int16_t paddleTipLow = (y0 > y1) ? y0 : y1;
    const int16_t yBase = std::max<int16_t>((int16_t)(paddleTipLow + 6), 12);
    const int16_t yEnd  = 46;

    const uint8_t limit = (sMaxBricks > 0 && sMaxBricks < kMaxBricks) ? sMaxBricks : kMaxBricks;

    // Staircase on the right half: for every 2 brick-columns past x=60,
    // the top of the column rises by one brick row.
    // brickFitsInEye clips anything outside the actual eye opening.
    sBrickCount = 0;
    for (int16_t bx = 0; bx <= 124 && sBrickCount < limit; bx += kBrickStepX) {
        int16_t stairUp = 0;
        if (bx > 60) stairUp = ((bx - 60) / (2 * kBrickStepX)) * kBrickStepY;
        if (stairUp > 5 * kBrickStepY) stairUp = 5 * kBrickStepY;
        const int16_t yStart = std::max<int16_t>(yBase - stairUp, 10);
        for (int16_t by = yStart; by <= yEnd && sBrickCount < limit; by += kBrickStepY)
            if (brickFitsInEye(sGeom, bx, by, kBrickPlaceMargin, kBrickLocalXInsetFrac))
                sBricks[sBrickCount++] = {bx, by, true};
    }

    // Fallback: flat grid with relaxed constraints
    if (sBrickCount == 0) {
        for (int16_t by = yBase; by <= yEnd && sBrickCount < limit; by += kBrickStepY)
            for (int16_t bx = 0; bx <= 124 && sBrickCount < limit; bx += kBrickStepX)
                if (brickFitsInEye(sGeom, bx, by, 2, 0.05f))
                    sBricks[sBrickCount++] = {bx, by, true};
    }
}

void clampPaddleLocalX() {
    float lxMin, lxMax;
    eyeGeomGetLocalXExtents(&lxMin, &lxMax);
    constexpr float kEdge = 4.0f;
    const float lo = lxMin + kPaddleHalfLenLocal + kEdge;
    const float hi = lxMax - kPaddleHalfLenLocal - kEdge;
    if (sPaddleLocalX < lo) sPaddleLocalX = lo;
    if (sPaddleLocalX > hi) sPaddleLocalX = hi;
}

bool circleHitsBrick(float cx, float cy, float r, const Brick& b) {
    if (!b.alive) return false;
    const float nx = (cx < b.x) ? (float)b.x :
                     (cx > (float)(b.x + kBrickW - 1) ? (float)(b.x + kBrickW - 1) : cx);
    const float ny = (cy < b.y) ? (float)b.y :
                     (cy > (float)(b.y + kBrickH - 1) ? (float)(b.y + kBrickH - 1) : cy);
    const float dx = cx - nx, dy = cy - ny;
    return (dx * dx + dy * dy) < r * r;
}

void bounceOffBrick(const Brick& b) {
    const float bcx = (float)b.x + (float)kBrickW * 0.5f;
    const float bcy = (float)b.y + (float)kBrickH * 0.5f;
    if (fabsf(sBallX - bcx) * (float)kBrickH > fabsf(sBallY - bcy) * (float)kBrickW)
        sBallVx = -sBallVx;
    else
        sBallVy = -sBallVy;
}

bool segmentBallCollision(float bx, float by, float br,
                          int16_t sx0, int16_t sy0, int16_t sx1, int16_t sy1,
                          float* outNx, float* outNy) {
    const float px0 = sx0, py0 = sy0, px1 = sx1, py1 = sy1;
    float dx = px1 - px0, dy = py1 - py0;
    const float len2 = dx * dx + dy * dy;
    if (len2 < 1e-5f) return false;
    float t = ((bx - px0) * dx + (by - py0) * dy) / len2;
    t = fmaxf(0.0f, fminf(1.0f, t));
    const float d = sqrtf((bx - (px0 + t*dx))*(bx - (px0 + t*dx)) +
                          (by - (py0 + t*dy))*(by - (py0 + t*dy)));
    if (d > br) return false;
    float segNx = -dy, segNy = dx;
    const float snl = sqrtf(segNx*segNx + segNy*segNy);
    if (snl < 1e-4f) return false;
    segNx /= snl; segNy /= snl;
    if (segNy < 0.0f) { segNx = -segNx; segNy = -segNy; }
    *outNx = segNx; *outNy = segNy;
    return true;
}

bool tryPaddleBounce(const EyeUiGeom& g) {
    int16_t x0, y0, x1, y1;
    eyeUiUpperPaddleSegmentScreen(&g, sPaddleLocalX, kPaddleHalfLenLocal, kPaddleYInset,
                                  &x0, &y0, &x1, &y1);
    float pnX, pnY;
    if (!segmentBallCollision(sBallX, sBallY, kBallR + 0.85f, x0, y0, x1, y1, &pnX, &pnY))
        return false;
    const float vn = sBallVx * pnX + sBallVy * pnY;
    if (vn >= 0.0f) return false;

    // Save incoming speed — direction corrections must not change the magnitude.
    const float speed = sqrtf(sBallVx * sBallVx + sBallVy * sBallVy);

    sBallVx -= 2.0f * vn * pnX;
    sBallVy -= 2.0f * vn * pnY;
    sBallX  += pnX * 3.0f;
    sBallY  += pnY * 3.0f;
    // Add angle based on hit position along paddle.
    const float px0f = (float)x0, py0f = (float)y0, px1f = (float)x1, py1f = (float)y1;
    float sdx = px1f - px0f, sdy = py1f - py0f;
    const float sl2 = sdx*sdx + sdy*sdy;
    float tHit = 0.5f;
    if (sl2 > 1e-4f) {
        tHit = ((sBallX - px0f)*sdx + (sBallY - py0f)*sdy) / sl2;
        tHit = fmaxf(0.0f, fminf(1.0f, tHit));
    }
    const float hit  = (tHit - 0.5f) * 2.0f;
    const float tlen = sqrtf(sl2);
    if (tlen > 1e-4f) { sdx /= tlen; sdy /= tlen; }
    sBallVx += sdy * hit * 1.25f;
    sBallVy -= sdx * hit * 0.4f;
    sBallVx = fmaxf(-kBallMaxVx, fminf(kBallMaxVx, sBallVx));

    // Renormalize: restore incoming speed so angle correction only changes direction.
    const float newSpeed = sqrtf(sBallVx * sBallVx + sBallVy * sBallVy);
    if (newSpeed > 1e-4f) {
        const float k = speed / newSpeed;
        sBallVx *= k;
        sBallVy *= k;
    }
    return true;
}

bool ballPenetratesLower(const EyeUiGeom& g, float m) {
    float lx, ly;
    eyeGeomScreenToLocal(&g, (int16_t)lroundf(sBallX), (int16_t)lroundf(sBallY), &lx, &ly);
    const int16_t yLoC = eyeUiLowerEyelidFacetedYAtLocalX(&g, lx, kBreakoutLowerEyelidSegs);
    float lxB, lyB;
    eyeGeomScreenToLocal(&g, (int16_t)lroundf(sBallX), (int16_t)lroundf(sBallY + kBallR), &lxB, &lyB);
    const int16_t yLoB = eyeUiLowerEyelidFacetedYAtLocalX(&g, lxB, kBreakoutLowerEyelidSegs);
    return ly > (float)yLoC - m || lyB > (float)yLoB - m;
}

bool reflectOffFacetedLower(const EyeUiGeom& g) {
    float lx, ly;
    eyeGeomScreenToLocal(&g, (int16_t)lroundf(sBallX), (int16_t)lroundf(sBallY), &lx, &ly);
    int16_t sx0, sy0, sx1, sy1;
    eyeUiLowerLidFacetedSegmentScreen(&g, lx, kBreakoutLowerEyelidSegs, &sx0, &sy0, &sx1, &sy1);
    float tx = (float)(sx1 - sx0), ty = (float)(sy1 - sy0);
    const float tlen = sqrtf(tx*tx + ty*ty);
    if (tlen < 1e-3f) return false;
    tx /= tlen; ty /= tlen;
    float nx = -ty, ny = tx;
    // Ensure normal points toward eye centre.
    const float mx = 0.5f * ((float)sx0 + (float)sx1);
    const float my = 0.5f * ((float)sy0 + (float)sy1);
    if (nx*(g.cx - mx) + ny*(g.cy - my) < 0.0f) { nx = -nx; ny = -ny; }
    const float vn = sBallVx*nx + sBallVy*ny;
    if (vn >= 0.0f) return false;
    sBallVx -= 2.0f * vn * nx;
    sBallVy -= 2.0f * vn * ny;
    sBallX  += nx * 2.5f;
    sBallY  += ny * 2.5f;
    return true;
}

void advanceBallY(const EyeUiGeom& g, float m) {
    const float totalDy = sBallVy;
    const float ady = fabsf(totalDy);
    const int n = std::max(1, (int)ceilf(ady / 1.15f));
    const float step = totalDy / (float)n;
    for (int s = 0; s < n; ++s) {
        const float oy = sBallY;
        sBallY += step;
        if (ballPenetratesLower(g, m)) {
            sBallY = oy;
            if (!reflectOffFacetedLower(g)) sBallVy = -sBallVy;
            break;
        }
    }
}

void clampBallAboveLower(const EyeUiGeom& g, float m) {
    for (int i = 0; i < 14; ++i) {
        if (!ballPenetratesLower(g, m)) return;
        sBallY -= 0.7f;
    }
    if (ballPenetratesLower(g, m) && sBallVy > 0.0f) sBallVy = -sBallVy;
}

bool ballLostOffTop(const EyeUiGeom& g, float m) {
    if (sBallVy >= -0.08f) return false;
    const int16_t cx = (int16_t)lroundf(sBallX);
    const int16_t cy = (int16_t)lroundf(sBallY);
    if (eyeUiPointInsideEyeOpening(&g, cx, cy, 0)) return false;
    float lx, ly, lxMin, lxMax;
    eyeGeomScreenToLocal(&g, cx, cy, &lx, &ly);
    eyeGeomGetLocalXExtents(&lxMin, &lxMax);
    const float lxSample = fminf(fmaxf(lx, lxMin + m), lxMax - m);
    const int16_t yUp = eyeUiInterpolateCurveAtLocalX(lxSample, g.upper);
    constexpr float kSlack = 3.5f;
    return ly < (float)yUp + m - kSlack;
}

void resetBallOnPaddle(const EyeUiGeom& g) {
    int16_t x0, y0, x1, y1;
    eyeUiUpperPaddleSegmentScreen(&g, sPaddleLocalX, kPaddleHalfLenLocal, kPaddleYInset,
                                  &x0, &y0, &x1, &y1);
    const float mx = 0.5f * ((float)x0 + (float)x1);
    const float my = 0.5f * ((float)y0 + (float)y1);
    float dx = (float)(x1 - x0), dy = (float)(y1 - y0);
    float nx = -dy, ny = dx;
    const float nl = sqrtf(nx*nx + ny*ny);
    if (nl > 1e-4f) { nx /= nl; ny /= nl; }
    if (ny < 0.0f) { nx = -nx; ny = -ny; }
    sBallX = mx + nx * (kBallR + 2.5f);
    sBallY = my + ny * (kBallR + 2.5f);
    const float sign = ((esp_random() & 1U) != 0U) ? 1.0f : -1.0f;
    sBallVx = fmaxf(-kBallMaxVx, fminf(kBallMaxVx,
        sign * (0.85f + (float)(esp_random() % 80U) * 0.01f)));
    sBallVy = kBallStartVy;
}

// ── Drawing ───────────────────────────────────────────────────────────────────

void drawLeftHud(uint32_t nowMs) {
    EyeUiGeom geom;
    if (!eyeUiBeginFrame(gLeftDisplay, false, nowMs, &geom)) return;
    char buf[8];
    snprintf(buf, sizeof(buf), "%u", static_cast<unsigned>(bricksAliveCount()));
    eyeUiDrawRotatedKeyword(gLeftDisplay, &geom, buf);
    gLeftDisplay.display();
}

void drawPlayingField() {
    for (uint8_t i = 0; i < sBrickCount; ++i) {
        if (!sBricks[i].alive) continue;
        gRightDisplay.fillRect(sBricks[i].x, sBricks[i].y, kBrickW, kBrickH, SSD1306_WHITE);
    }
    eyeUiDrawUpperPaddle(gRightDisplay, &sGeom, sPaddleLocalX, kPaddleHalfLenLocal, kPaddleYInset);
    const int16_t bx   = (int16_t)lroundf(sBallX - kBallR);
    const int16_t by   = (int16_t)lroundf(sBallY - kBallR);
    const int16_t diam = (int16_t)lroundf(kBallR * 2.0f);
    gRightDisplay.fillRect(bx, by, diam, diam, SSD1306_WHITE);
}

void drawPlaying(uint32_t nowMs) {
    if (!eyeUiBeginFrameNoUpper(gRightDisplay, true, nowMs, &sGeom, kBreakoutLowerEyelidSegs)) {
        drawLeftHud(nowMs);
        return;
    }
    drawPlayingField();
    gRightDisplay.display();
    drawLeftHud(nowMs);
}

void drawBallLost(uint32_t nowMs) {
    // Show lives remaining on right eye.
    if (!eyeUiBeginFrame(gRightDisplay, true, nowMs, &sGeom)) {
        drawLeftHud(nowMs);
        return;
    }
    char buf[4];
    snprintf(buf, sizeof(buf), "%u", static_cast<unsigned>(sLives));
    eyeUiDrawTextCenteredNudged(gRightDisplay, kTextY, 2, buf, 0);
    gRightDisplay.display();
    drawLeftHud(nowMs);
}

void drawPhaseUi(uint32_t nowMs) {
    switch (sPhase) {
        case Phase::Countdown: {
            drawLeftHud(nowMs);
            if (!eyeUiBeginFrame(gRightDisplay, true, nowMs, &sGeom)) break;
            char digit[2] = {(char)('0' + sCdCount), '\0'};
            eyeUiDrawTextCenteredNudged(gRightDisplay, kTextY, 2, digit, 0);
            gRightDisplay.display();
            break;
        }
        case Phase::GoCue: {
            drawLeftHud(nowMs);
            if (!eyeUiBeginFrame(gRightDisplay, true, nowMs, &sGeom)) break;
            eyeUiDrawTextCenteredNudged(gRightDisplay, kTextY, 2, "GO", 0);
            gRightDisplay.display();
            break;
        }
        case Phase::Playing:     drawPlaying(nowMs);                  break;
        case Phase::BallLost:    drawBallLost(nowMs);                 break;
        case Phase::SuccessAnim: minigameUiDrawWinBothEyes(nowMs);    break;
        case Phase::FailAnim:    minigameUiDrawLoseBothEyes(nowMs);   break;
        case Phase::Done:                                              break;
    }
}

void enterPlaying(uint32_t nowMs) {
    sPhase  = Phase::Playing;
    sLives  = kStartLives;
    buildBricks(nowMs);
    if (sBrickCount == 0) {
        rtttlStart("win:d=16,o=5,b=200:16g,16e,16g,16c6");
        minigameUiArmTourFlashGreen(nowMs, kTourFlashMs);
        sPhase      = Phase::SuccessAnim;
        sPhaseEndMs = nowMs + kAnimMs;
        return;
    }
    float lxMin, lxMax;
    eyeGeomGetLocalXExtents(&lxMin, &lxMax);
    sPaddleLocalX = (lxMin + lxMax) * 0.5f;
    clampPaddleLocalX();
    eyeUiComputeGeomForMenu(true, nowMs, &sGeom);
    resetBallOnPaddle(sGeom);
    sPrevBtn[0] = gButtonPressedLatched[0];
    sPrevBtn[1] = gButtonPressedLatched[1];
    sPrevBtn[2] = gButtonPressedLatched[2];
}

}  // namespace

// ── Public API ────────────────────────────────────────────────────────────────

void breakoutGameStart(uint8_t maxBricks) {
    sMaxBricks = maxBricks;
    rtttlStop();
    clearAllLeds();
    gLedStrip.show();

    sActive   = true;
    sPhase    = Phase::Countdown;
    sCdCount  = 3;
    sPrevBtn[0] = gButtonPressedLatched[0];
    sPrevBtn[1] = gButtonPressedLatched[1];
    sPrevBtn[2] = gButtonPressedLatched[2];

    const uint32_t t = millis();
    sNextMs = t + kCountdownStepMs;
    drawPhaseUi(t);
    syncLeds(t);
}

void breakoutGameStop() {
    if (!sActive) return;
    sActive = false;
    sPhase  = Phase::Countdown;
    rtttlStop();
    clearAllLeds();
    gLedStrip.show();
}

bool breakoutGameIsActive() { return sActive; }

void breakoutGameTick(uint32_t nowMs) {
    if (!sActive) return;

    if (sPhase == Phase::SuccessAnim) {
        drawPhaseUi(nowMs);
        syncLeds(nowMs);
        if (nowMs >= sPhaseEndMs) { gGameResult = GameResult::Won; breakoutGameStop(); }
        return;
    }
    if (sPhase == Phase::FailAnim) {
        drawPhaseUi(nowMs);
        syncLeds(nowMs);
        if (nowMs >= sPhaseEndMs) { gGameResult = GameResult::Lost; breakoutGameStop(); }
        return;
    }
    if (sPhase == Phase::BallLost) {
        drawPhaseUi(nowMs);
        syncLeds(nowMs);
        if (nowMs >= sBallLostEndMs) {
            sPhase = Phase::Playing;
            eyeUiComputeGeomForMenu(true, nowMs, &sGeom);
            resetBallOnPaddle(sGeom);
        }
        return;
    }
    if (sPhase == Phase::GoCue) {
        drawPhaseUi(nowMs);
        syncLeds(nowMs);
        if (nowMs < sNextMs) return;
        enterPlaying(nowMs);
        drawPhaseUi(nowMs);
        syncLeds(nowMs);
        return;
    }
    if (sPhase == Phase::Countdown) {
        drawPhaseUi(nowMs);
        syncLeds(nowMs);
        if (nowMs < sNextMs) return;
        if (sCdCount <= 1) {
            sPhase  = Phase::GoCue;
            sNextMs = nowMs + kGoCueMs;
        } else {
            --sCdCount;
            sNextMs = nowMs + kCountdownStepMs;
        }
        drawPhaseUi(nowMs);
        return;
    }

    // Playing
    eyeUiComputeGeomForMenu(true, nowMs, &sGeom);

    const bool curL = gButtonState[0];   // held for continuous paddle movement
    const bool curR = gButtonState[2];
    if (curL) sPaddleLocalX -= kPaddleLocalSpeed;
    if (curR) sPaddleLocalX += kPaddleLocalSpeed;
    sPrevBtn[0] = curL; sPrevBtn[1] = gButtonPressedLatched[1]; sPrevBtn[2] = curR;
    clampPaddleLocalX();

    float lx, ly, lxMin, lxMax;
    eyeGeomGetLocalXExtents(&lxMin, &lxMax);
    const float m = (float)(kMargin + 1);

    const float ox = sBallX;
    sBallX += sBallVx;
    eyeGeomScreenToLocal(&sGeom, (int16_t)lroundf(sBallX), (int16_t)lroundf(sBallY), &lx, &ly);
    if (lx < lxMin + m || lx > lxMax - m) { sBallX = ox; sBallVx = -sBallVx; }

    advanceBallY(sGeom, m);

    const bool bounced = tryPaddleBounce(sGeom);
    clampBallAboveLower(sGeom, m);
    bool ballMissedTop = false;
    if (!bounced && ballLostOffTop(sGeom, m)) ballMissedTop = true;

    // Brick collisions.
    for (int iter = 0; iter < 6; ++iter) {
        uint8_t hitIx = 255;
        for (uint8_t i = 0; i < sBrickCount; ++i) {
            if (circleHitsBrick(sBallX, sBallY, kBallR, sBricks[i])) { hitIx = i; break; }
        }
        if (hitIx == 255) break;
        sBricks[hitIx].alive = false;
        bounceOffBrick(sBricks[hitIx]);
        minigameUiArmTourFlashGreen(nowMs, kTourFlashMs / 2);
        if (bricksAliveCount() == 0) {
            rtttlStart("win:d=16,o=5,b=200:16g,16e,16g,16c6");
            minigameUiArmTourFlashGreen(nowMs, kTourFlashMs);
            sPhase      = Phase::SuccessAnim;
            sPhaseEndMs = nowMs + kAnimMs;
            break;
        }
        clampBallAboveLower(sGeom, m);
    }
    clampBallAboveLower(sGeom, m);

    if (sPhase == Phase::SuccessAnim) { drawPhaseUi(nowMs); syncLeds(nowMs); return; }

    if (ballMissedTop) {
        if (sLives <= 1) {
            rtttlStart("go:d=8,o=3,b=200:8c,8c");
            minigameUiArmTourFlashRed(nowMs, kTourFlashMs);
            sPhase      = Phase::FailAnim;
            sPhaseEndMs = nowMs + kAnimMs;
        } else {
            --sLives;
            rtttlStart("go:d=8,o=3,b=200:8c");
            minigameUiArmTourFlashRed(nowMs, kTourFlashMs / 2);
            sPhase        = Phase::BallLost;
            sBallLostEndMs = nowMs + kBallLostRespawnMs;
        }
        drawPhaseUi(nowMs);
        syncLeds(nowMs);
        return;
    }

    drawPhaseUi(nowMs);
    syncLeds(nowMs);
}
