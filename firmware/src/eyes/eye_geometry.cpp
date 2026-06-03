// eye_geometry.cpp — Core eye geometry: curve building, coordinate transforms,
// drawing helpers, and EyeUi frame helpers.
// Ported from badge_production/src/badge_core.cpp (the monolith PoC).

#include "eye_geometry.h"
#include "eye_calibration.h"
#include "eye_animation.h"
#include "../config/hardware.h"
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Arduino.h>
#include <math.h>

// ── Globals ───────────────────────────────────────────────────────────────────

float gEyeSpanNegW = static_cast<float>(kEyeHalfWidth);
float gEyeSpanPosW = static_cast<float>(kEyeHalfWidth);
float gUpperSideDropLocal = kUpperSideDropLocal;

// Can be tweaked externally for live lid-animation nudges; default zero.
int16_t gGraphicsUpperLidExtraOffset = 0;
int16_t gGraphicsLowerLidExtraOffset = 0;

// ── Constants ─────────────────────────────────────────────────────────────────

constexpr int16_t kMenuLeftKeywordNudgeScreenX = -3;
constexpr int16_t kMenuLeftKeywordNudgeScreenY = -4;

constexpr float kUpperOpen[kCurveNodes]   = { 0.8f, -0.8f, -4.2f, -8.6f, -11.7f,
                                              -10.9f, -7.5f, -3.6f,  -1.1f };
constexpr float kLowerOpen[kCurveNodes]   = { 1.4f,  2.2f,  4.6f,  6.7f,   7.8f,
                                               7.5f,  6.1f,  4.3f,   2.5f };
constexpr float kUpperClosed[kCurveNodes] = { 0.4f,  0.1f, -0.4f, -0.8f,  -0.8f,
                                              -0.5f, -0.1f,  0.4f,   0.9f };
constexpr float kLowerClosed[kCurveNodes] = { 1.8f,  1.4f,  0.9f,  0.6f,   0.7f,
                                               1.0f,  1.4f,  1.9f,   2.4f };

// ════════════════════════════════════════════════════════════════════════════
// §1  Static math helpers
// ════════════════════════════════════════════════════════════════════════════

static float lerp(float a, float b, float t) {
    return a + (b - a) * t;
}

static float clampUnit(float v) {
    if (v < 0.0f) return 0.0f;
    if (v > 1.0f) return 1.0f;
    return v;
}

static float smoothStep(float e0, float e1, float x) {
    const float t = clampUnit((x - e0) / (e1 - e0));
    return t * t * (3.0f - 2.0f * t);
}

static float easeInOut(float t) {
    return 0.5f - 0.5f * cosf(t * PI);
}


// ════════════════════════════════════════════════════════════════════════════
// §2  Primitive drawing helper
// ════════════════════════════════════════════════════════════════════════════

static void drawThickLine(Adafruit_SSD1306& display,
                          int16_t x0, int16_t y0, int16_t x1, int16_t y1,
                          uint8_t thickness, uint16_t color) {
    const int8_t half = static_cast<int8_t>(thickness / 2);
    for (int8_t offset = -half; offset <= half; ++offset) {
        display.drawLine(x0, static_cast<int16_t>(y0 + offset),
                         x1, static_cast<int16_t>(y1 + offset), color);
    }
}

// ════════════════════════════════════════════════════════════════════════════
// §3  Span extents  (set directly from corner calibration in §8)
// ════════════════════════════════════════════════════════════════════════════

// ════════════════════════════════════════════════════════════════════════════
// §4  Public: eyeLocalXFromIndex
//     (needed by almost everything below, so defined early)
// ════════════════════════════════════════════════════════════════════════════

float eyeLocalXFromIndex(uint8_t i) {
    if (kEyeSpan <= 1) return 0.0f;
    const float span = gEyeSpanNegW + gEyeSpanPosW;
    return -gEyeSpanNegW + static_cast<float>(i) * span / static_cast<float>(kEyeSpan - 1);
}

// ════════════════════════════════════════════════════════════════════════════
// §5  Coordinate transforms (static internal versions)
// ════════════════════════════════════════════════════════════════════════════

static void rotateLocalPoint(float localX, float localY, float cosA, float sinA,
                              int16_t centerX, int16_t centerY,
                              int16_t* screenX, int16_t* screenY) {
    const float rotX = localX * cosA - localY * sinA;
    const float rotY = localX * sinA + localY * cosA;
    *screenX = static_cast<int16_t>(lroundf(static_cast<float>(centerX) + rotX));
    *screenY = static_cast<int16_t>(lroundf(static_cast<float>(centerY) + rotY));
}

// Convert left-eye screen coords to left-eye local frame.
static void leftScreenToLocal(int16_t screenX, int16_t screenY,
                               float* outLx, float* outLy) {
    const float eyeAngle = kEyeTiltDegrees * DEG_TO_RAD;
    const float cosA     = cosf(eyeAngle);
    const float sinA     = sinf(eyeAngle);
    const float cx       = static_cast<float>(kDisplayWidth  / 2);
    const float cy       = static_cast<float>(kDisplayHeight / 2);
    const float dx       = static_cast<float>(screenX) - cx;
    const float dy       = static_cast<float>(screenY) - cy;
    *outLx =  dx * cosA + dy * sinA;
    *outLy = -dx * sinA + dy * cosA;
}

// ════════════════════════════════════════════════════════════════════════════
// §6  Curve interpolation (static)
// ════════════════════════════════════════════════════════════════════════════

static int16_t interpolateEyeCurveAtLocalX(float lxTarget, const int16_t* curveY) {
    const float lxMin = eyeLocalXFromIndex(0);
    const float lxMax = eyeLocalXFromIndex(static_cast<uint8_t>(kEyeSpan - 1));
    if (lxTarget <= lxMin) return curveY[0];
    if (lxTarget >= lxMax) return curveY[kEyeSpan - 1];
    for (uint8_t i = 0; i < kEyeSpan - 1; ++i) {
        const float x0 = eyeLocalXFromIndex(i);
        const float x1 = eyeLocalXFromIndex(static_cast<uint8_t>(i + 1));
        if (lxTarget >= x0 && lxTarget <= x1) {
            const float span = x1 - x0;
            const float t    = span > 1e-5f ? (lxTarget - x0) / span : 0.0f;
            return static_cast<int16_t>(
                lroundf((1.0f - t) * static_cast<float>(curveY[i]) +
                                 t * static_cast<float>(curveY[static_cast<uint8_t>(i + 1)])));
        }
    }
    return curveY[kEyeSpan / 2];
}

// ════════════════════════════════════════════════════════════════════════════
// §7  Span extents query (static)
// ════════════════════════════════════════════════════════════════════════════

static void getLocalXExtents(float* lxMin, float* lxMax) {
    *lxMin = eyeLocalXFromIndex(0);
    *lxMax = eyeLocalXFromIndex(static_cast<uint8_t>(kEyeSpan - 1));
}

// ════════════════════════════════════════════════════════════════════════════
// §8  Landmark-based curve building
// ════════════════════════════════════════════════════════════════════════════

static void fillLandmarkCanonicalOpen(int16_t* upper, int16_t* lower) {
    // tl = left corner (shared endpoint for both lids)
    // br = right corner (shared endpoint for both lids)
    // bm = lower Bezier arc midpoint (at t=0.5)
    float lxL, lyL, lxR, lyR, lxBm, lyBm;
    leftScreenToLocal(gEyeCal.tlX, gEyeCal.tlY, &lxL, &lyL);
    leftScreenToLocal(gEyeCal.brX, gEyeCal.brY, &lxR, &lyR);
    leftScreenToLocal(gEyeCal.bmX, gEyeCal.bmY, &lxBm, &lyBm);

    // Span set directly from corner positions
    gEyeSpanNegW = fmaxf(4.0f, -lxL);
    gEyeSpanPosW = fmaxf(4.0f,  lxR);
    const float spanSum = gEyeSpanNegW + gEyeSpanPosW;

    // ── Upper lid: straight line from left corner to right corner ──────────
    for (uint8_t i = 0; i < kEyeSpan; ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(kEyeSpan - 1);
        upper[i] = static_cast<int16_t>(lroundf(lyL + t * (lyR - lyL)));
    }

    // ── Lower lid: quadratic Bezier through BM at t = 0.5 ─────────────────
    // P0 = left corner, P2 = right corner, P1 derived from constraint at t=0.5.
    float fLow[kEyeSpan];
    bool  got[kEyeSpan];
    for (uint8_t i = 0; i < kEyeSpan; ++i) {
        got[i]  = false;
        fLow[i] = lyR;
    }

    const float p0x = lxL,  p0y = lyL;
    const float p2x = lxR,  p2y = lyR;
    const float p1x = 2.0f * lxBm - 0.5f * p0x - 0.5f * p2x;
    const float p1y = 2.0f * lyBm - 0.5f * p0y - 0.5f * p2y;

    constexpr int kBezSteps = 400;
    for (int s = 0; s <= kBezSteps; ++s) {
        const float t   = static_cast<float>(s) * (1.0f / static_cast<float>(kBezSteps));
        const float omt = 1.0f - t;
        const float bx  = omt * omt * p0x + 2.0f * omt * t * p1x + t * t * p2x;
        const float by  = omt * omt * p0y + 2.0f * omt * t * p1y + t * t * p2y;
        const float ix  = (bx + gEyeSpanNegW) / spanSum * static_cast<float>(kEyeSpan - 1);
        const int   idx = static_cast<int>(lroundf(ix));
        if (idx >= 0 && idx < static_cast<int>(kEyeSpan)) {
            const unsigned u = static_cast<unsigned>(idx);
            if (!got[u] || by > fLow[u]) { fLow[u] = by; got[u] = true; }
        }
    }

    // Fill gaps by linear interpolation between known neighbours.
    for (uint8_t i = 0; i < kEyeSpan; ++i) {
        if (got[i]) continue;
        int li = static_cast<int>(i), l = li - 1, r = li + 1;
        while (l >= 0 && !got[static_cast<unsigned>(l)]) --l;
        while (r < static_cast<int>(kEyeSpan) && !got[static_cast<unsigned>(r)]) ++r;
        float fy = lyR;
        if (l >= 0 && r < static_cast<int>(kEyeSpan) &&
            got[static_cast<unsigned>(l)] && got[static_cast<unsigned>(r)]) {
            const float w = static_cast<float>(li - l) / static_cast<float>(r - l);
            fy = (1.0f - w) * fLow[static_cast<unsigned>(l)] +
                         w  * fLow[static_cast<unsigned>(r)];
        } else if (l >= 0 && got[static_cast<unsigned>(l)]) {
            fy = fLow[static_cast<unsigned>(l)];
        } else if (r < static_cast<int>(kEyeSpan) && got[static_cast<unsigned>(r)]) {
            fy = fLow[static_cast<unsigned>(r)];
        }
        fLow[static_cast<unsigned>(li)] = fy;
    }

    // Clamp the boundary regions to the nearest sampled value.
    int firstG = -1, lastG = -1;
    for (uint8_t i = 0; i < kEyeSpan; ++i) {
        if (got[i]) { lastG = static_cast<int>(i); if (firstG < 0) firstG = static_cast<int>(i); }
    }
    if (firstG >= 0) {
        const float fl = fLow[static_cast<unsigned>(firstG)];
        for (int i = 0; i < firstG; ++i) fLow[static_cast<unsigned>(i)] = fl;
        const float fr = fLow[static_cast<unsigned>(lastG)];
        for (unsigned i = static_cast<unsigned>(lastG + 1); i < kEyeSpan; ++i) fLow[i] = fr;
    }

    for (uint8_t i = 0; i < kEyeSpan; ++i) {
        lower[i] = static_cast<int16_t>(lroundf(fLow[i]));
        if (lower[i] < upper[i] + 1) lower[i] = static_cast<int16_t>(upper[i] + 1);
    }
}

static void fillLandmarkOpenOutline(int16_t* upper, int16_t* lower, bool isRightEye) {
    int16_t uc[kEyeSpan], lc[kEyeSpan];
    fillLandmarkCanonicalOpen(uc, lc);
    // After the call above, gEyeSpanNegW > gEyeSpanPosW (left eye: wider on the negative side
    // due to the +26° tilt). The right eye uses -26° tilt, so the sides swap.

    if (!isRightEye) {
        for (uint8_t i = 0; i < kEyeSpan; ++i) { upper[i] = uc[i]; lower[i] = lc[i]; }
        return;
    }

    // Right eye: swap span extents so index 0 maps to the inner corner and
    // index kEyeSpan-1 maps to the outer corner, matching the -26° screen geometry.
    // Under swapped extents the mirror transform Y(lx) = Y_canon(-lx) reduces
    // exactly to a simple array reversal: right[i] = canonical[kEyeSpan-1-i].
    const float tmp = gEyeSpanNegW;
    gEyeSpanNegW    = gEyeSpanPosW;
    gEyeSpanPosW    = tmp;

    for (uint8_t i = 0; i < kEyeSpan; ++i) {
        const uint8_t j = static_cast<uint8_t>(kEyeSpan - 1u - i);
        upper[i] = uc[j];
        lower[i] = lc[j];
    }
    // gEyeSpanNegW/PosW are intentionally left at right-eye values.
    // eyeGeomCompute always pairs compute + immediate draw for one eye at a time,
    // so the globals remain consistent throughout the frame.
}

static void squeezeClosedEyelidsFromOpen(const int16_t* uOpen, const int16_t* lOpen,
                                          int16_t* uClosed, int16_t* lClosed) {
    for (uint8_t i = 0; i < kEyeSpan; ++i) {
        const float uo  = static_cast<float>(uOpen[i]);
        const float lo  = static_cast<float>(lOpen[i]);
        const float mid = 0.5f * (uo + lo);
        float half = 0.5f * (lo - uo);
        if (half < 1.0f) half = 1.0f;
        uClosed[i] = static_cast<int16_t>(lroundf(mid - half * kClosedSlitFrac));
        lClosed[i] = static_cast<int16_t>(lroundf(mid + half * kClosedSlitFrac));
        if (lClosed[i] < uClosed[i] + 1)
            lClosed[i] = static_cast<int16_t>(uClosed[i] + 1);
    }
}

static void buildEyeCurves(const EyePose& pose, bool isRightEye,
                            int16_t* upper, int16_t* lower, int16_t* brow) {
    int16_t uOpen[kEyeSpan], lOpen[kEyeSpan];
    fillLandmarkOpenOutline(uOpen, lOpen, isRightEye);

    int16_t uc[kEyeSpan], lc[kEyeSpan];
    squeezeClosedEyelidsFromOpen(uOpen, lOpen, uc, lc);

    for (uint8_t i = 0; i < kEyeSpan; ++i) {
        const float tcol        = static_cast<float>(i) / static_cast<float>(kEyeSpan - 1);
        const float innerT      = isRightEye ? 1.0f - tcol : tcol;
        const float centerT     = 1.0f - fabsf(tcol * 2.0f - 1.0f);
        const float innerWeight = smoothStep(0.25f, 1.0f, innerT);

        const float openBlend =
            pose.openness <= 0.0f ? 0.0f
                                  : min(1.0f, pose.openness / kOpennessAtFullLandmark);
        const float exprW = 1.0f - openBlend;

        float up = lerp(static_cast<float>(uc[i]), static_cast<float>(uOpen[i]), openBlend);
        float lo = lerp(static_cast<float>(lc[i]), static_cast<float>(lOpen[i]), openBlend);

        up -= pose.browLift   * (1.2f + centerT * 2.4f) * exprW;
        up += pose.browTilt   * (-0.9f + innerT * 4.2f) * exprW;
        up += pose.innerPinch * innerWeight * (1.4f + (1.0f - pose.openness) * 4.5f) * exprW;
        up += (1.0f - pose.openness) * 0.8f * (1.0f - centerT) * exprW;

        lo -= pose.lowerLift  * (0.5f + centerT * 2.0f) * exprW;
        lo -= pose.innerPinch * innerWeight * 0.7f * exprW;
        lo += 0.4f * (1.0f - centerT) * exprW;

        int16_t uyi = static_cast<int16_t>(lroundf(up));
        int16_t lyi = static_cast<int16_t>(lroundf(lo));

        uyi = static_cast<int16_t>(uyi + gGraphicsUpperLidExtraOffset);
        lyi = static_cast<int16_t>(lyi + gGraphicsLowerLidExtraOffset);

        if (lyi < uyi + 1) lyi = static_cast<int16_t>(uyi + 1);

        upper[i] = uyi;
        lower[i] = lyi;

        int16_t browY = static_cast<int16_t>(lroundf(
            static_cast<float>(uyi) - (4.8f + centerT * 2.8f)));
        brow[i] = browY;
        if (brow[i] > upper[i] - 1) brow[i] = static_cast<int16_t>(upper[i] - 1);
    }
}

// ════════════════════════════════════════════════════════════════════════════
// §9  Internal faceted lower eyelid helpers (depend on §6 and §7)
// ════════════════════════════════════════════════════════════════════════════

static void eyeUiDrawLowerEyelidFaceted(Adafruit_SSD1306& display,
                                         const EyeUiGeom* g,
                                         uint8_t segmentCount) {
    if (segmentCount < 2) return;
    float lxMin, lxMax;
    getLocalXExtents(&lxMin, &lxMax);
    int16_t prevX = 0, prevY = 0;
    const int nPt = static_cast<int>(segmentCount) + 1;
    for (int i = 0; i < nPt; ++i) {
        const float u   = static_cast<float>(i) / static_cast<float>(segmentCount);
        const float t   = easeInOut(u);
        const float lxm = lxMin + t * (lxMax - lxMin);
        const int16_t ly = interpolateEyeCurveAtLocalX(lxm, g->lower);
        int16_t sx, sy;
        rotateLocalPoint(lxm, static_cast<float>(ly), g->cosA, g->sinA, g->cx, g->cy, &sx, &sy);
        if (i > 0) drawThickLine(display, prevX, prevY, sx, sy, 1, SSD1306_WHITE);
        prevX = sx; prevY = sy;
    }
}

static void drawOutlineNoUpper(Adafruit_SSD1306& display,
                                const EyeUiGeom* g, uint8_t lowerEyelidFacets);

// ════════════════════════════════════════════════════════════════════════════
// §10  Public API — span and coordinate
// ════════════════════════════════════════════════════════════════════════════

void eyeGeomGetLocalXExtents(float* lxMin, float* lxMax) {
    *lxMin = eyeLocalXFromIndex(0);
    *lxMax = eyeLocalXFromIndex(static_cast<uint8_t>(kEyeSpan - 1));
}

void eyeGeomLocalToScreen(float lx, float ly, float cosA, float sinA,
                           int16_t cx, int16_t cy,
                           int16_t* sx, int16_t* sy) {
    rotateLocalPoint(lx, ly, cosA, sinA, cx, cy, sx, sy);
}

void eyeGeomScreenToLocal(const EyeUiGeom* g, int16_t sx, int16_t sy,
                           float* lx, float* ly) {
    const float dx = static_cast<float>(sx - g->cx);
    const float dy = static_cast<float>(sy - g->cy);
    *lx =  dx * g->cosA + dy * g->sinA;
    *ly = -dx * g->sinA + dy * g->cosA;
}

// ════════════════════════════════════════════════════════════════════════════
// §11  Public API — curve building and geometry compute
// ════════════════════════════════════════════════════════════════════════════

void eyeGeomBuildCurves(const EyePose& pose, bool isRightEye,
                         int16_t* upper, int16_t* lower, int16_t* brow) {
    buildEyeCurves(pose, isRightEye, upper, lower, brow);
}

void eyeGeomCompute(const EyePose& pose, bool isRightEye, EyeUiGeom* g) {
    g->cx  = kDisplayWidth  / 2;
    g->cy  = kDisplayHeight / 2;
    const float angRad = (isRightEye ? -kEyeTiltDegrees : kEyeTiltDegrees) * DEG_TO_RAD;
    g->cosA = cosf(angRad);
    g->sinA = sinf(angRad);
    buildEyeCurves(pose, isRightEye, g->upper, g->lower, g->brow);
    const int16_t midIdx = kEyeSpan / 2;
    g->opening = static_cast<int16_t>(g->lower[midIdx] - g->upper[midIdx]);
}

// ════════════════════════════════════════════════════════════════════════════
// §12  Public API — curve interpolation
// ════════════════════════════════════════════════════════════════════════════

int16_t eyeGeomInterpolateAtLocalX(float lxTarget, const int16_t* curveY) {
    return interpolateEyeCurveAtLocalX(lxTarget, curveY);
}

int16_t eyeUiInterpolateCurveAtLocalX(float lxTarget, const int16_t* curveY) {
    return interpolateEyeCurveAtLocalX(lxTarget, curveY);
}

// ════════════════════════════════════════════════════════════════════════════
// §13  Public API — drawing helpers
// ════════════════════════════════════════════════════════════════════════════

void eyeGeomDrawCurve(Adafruit_SSD1306& d, const EyeUiGeom* g,
                       const int16_t* curve, uint8_t thickness, uint16_t color) {
    for (uint8_t i = 1; i < kEyeSpan; ++i) {
        const float lx0 = eyeLocalXFromIndex(static_cast<uint8_t>(i - 1));
        const float lx1 = eyeLocalXFromIndex(i);
        const float ly0 = static_cast<float>(curve[i - 1]);
        const float ly1 = static_cast<float>(curve[i]);
        int16_t x0, y0, x1, y1;
        rotateLocalPoint(lx0, ly0, g->cosA, g->sinA, g->cx, g->cy, &x0, &y0);
        rotateLocalPoint(lx1, ly1, g->cosA, g->sinA, g->cx, g->cy, &x1, &y1);
        drawThickLine(d, x0, y0, x1, y1, thickness, color);
    }
}

void eyeGeomFillSclera(Adafruit_SSD1306& d, const EyeUiGeom* g, uint16_t color) {
    for (uint8_t i = 0; i < kEyeSpan; ++i) {
        const float lx = eyeLocalXFromIndex(i);
        for (int16_t y = g->upper[i]; y <= g->lower[i]; ++y) {
            int16_t sx, sy;
            rotateLocalPoint(lx, static_cast<float>(y), g->cosA, g->sinA,
                             g->cx, g->cy, &sx, &sy);
            d.drawPixel(sx, sy, color);
        }
    }
}

// ════════════════════════════════════════════════════════════════════════════
// §14  Public API — EyeUi helpers
// ════════════════════════════════════════════════════════════════════════════

bool eyeUiPointInsideEyeOpening(const EyeUiGeom* g, int16_t sx, int16_t sy, int16_t margin) {
    if (g->opening <= 3) return false;
    float lx, ly;
    eyeGeomScreenToLocal(g, sx, sy, &lx, &ly);
    const float m     = static_cast<float>(margin);
    const float lxMin = eyeLocalXFromIndex(0);
    const float lxMax = eyeLocalXFromIndex(static_cast<uint8_t>(kEyeSpan - 1));
    if (lx < lxMin + m || lx > lxMax - m) return false;
    const int16_t yUp = interpolateEyeCurveAtLocalX(lx, g->upper);
    const int16_t yLo = interpolateEyeCurveAtLocalX(lx, g->lower);
    if (static_cast<float>(yLo) - m <= static_cast<float>(yUp) + m) return false;
    return ly >= static_cast<float>(yUp) + m && ly <= static_cast<float>(yLo) - m;
}

int16_t eyeUiLowerEyelidFacetedYAtLocalX(const EyeUiGeom* g, float lx,
                                          uint8_t segmentCount) {
    if (segmentCount < 2) return interpolateEyeCurveAtLocalX(lx, g->lower);
    float lxMin, lxMax;
    getLocalXExtents(&lxMin, &lxMax);
    if (lx <= lxMin) return interpolateEyeCurveAtLocalX(lxMin, g->lower);
    if (lx >= lxMax) return interpolateEyeCurveAtLocalX(lxMax, g->lower);

    const int nSeg = static_cast<int>(segmentCount);
    for (int i = 0; i < nSeg; ++i) {
        const float u0  = static_cast<float>(i)     / static_cast<float>(nSeg);
        const float u1  = static_cast<float>(i + 1) / static_cast<float>(nSeg);
        const float t0  = easeInOut(u0);
        const float t1  = easeInOut(u1);
        const float lx0 = lxMin + t0 * (lxMax - lxMin);
        const float lx1 = lxMin + t1 * (lxMax - lxMin);
        if (lx >= lx0 && lx <= lx1) {
            const int16_t y0 = interpolateEyeCurveAtLocalX(lx0, g->lower);
            const int16_t y1 = interpolateEyeCurveAtLocalX(lx1, g->lower);
            const float   span = lx1 - lx0;
            const float   u    = span > 1e-5f ? (lx - lx0) / span : 0.0f;
            return static_cast<int16_t>(
                lroundf(static_cast<float>(y0) + u * static_cast<float>(y1 - y0)));
        }
    }
    return interpolateEyeCurveAtLocalX(lx, g->lower);
}

void eyeUiLowerLidFacetedSegmentScreen(const EyeUiGeom* g, float lx,
                                        uint8_t segmentCount,
                                        int16_t* sx0, int16_t* sy0,
                                        int16_t* sx1, int16_t* sy1) {
    float lx0, ly0, lx1, ly1;
    float lxMin, lxMax;
    getLocalXExtents(&lxMin, &lxMax);

    if (segmentCount < 2) {
        lx0 = lxMin; lx1 = lxMax;
        ly0 = static_cast<float>(interpolateEyeCurveAtLocalX(lxMin, g->lower));
        ly1 = static_cast<float>(interpolateEyeCurveAtLocalX(lxMax, g->lower));
    } else {
        const int nSeg = static_cast<int>(segmentCount);
        bool have = false;

        if (lx <= lxMin) {
            const float u1 = 1.0f / static_cast<float>(nSeg);
            lx0  = lxMin + easeInOut(0.0f) * (lxMax - lxMin);
            lx1  = lxMin + easeInOut(u1)   * (lxMax - lxMin);
            have = true;
        } else if (lx >= lxMax) {
            const float u0 = static_cast<float>(nSeg - 1) / static_cast<float>(nSeg);
            lx0  = lxMin + easeInOut(u0)   * (lxMax - lxMin);
            lx1  = lxMin + easeInOut(1.0f) * (lxMax - lxMin);
            have = true;
        } else {
            for (int i = 0; i < nSeg; ++i) {
                const float u0     = static_cast<float>(i)     / static_cast<float>(nSeg);
                const float u1     = static_cast<float>(i + 1) / static_cast<float>(nSeg);
                const float segLx0 = lxMin + easeInOut(u0) * (lxMax - lxMin);
                const float segLx1 = lxMin + easeInOut(u1) * (lxMax - lxMin);
                if (lx >= segLx0 && lx <= segLx1) {
                    lx0 = segLx0; lx1 = segLx1; have = true; break;
                }
            }
        }
        if (!have) { lx0 = lxMin; lx1 = lxMax; }
        ly0 = static_cast<float>(interpolateEyeCurveAtLocalX(lx0, g->lower));
        ly1 = static_cast<float>(interpolateEyeCurveAtLocalX(lx1, g->lower));
    }

    rotateLocalPoint(lx0, ly0, g->cosA, g->sinA, g->cx, g->cy, sx0, sy0);
    rotateLocalPoint(lx1, ly1, g->cosA, g->sinA, g->cx, g->cy, sx1, sy1);
}

void eyeUiUpperPaddleSegmentScreen(const EyeUiGeom* g,
                                    float centerLx, float halfLenLocal, float yInset,
                                    int16_t* x0, int16_t* y0,
                                    int16_t* x1, int16_t* y1) {
    const float   lx0 = centerLx - halfLenLocal;
    const float   lx1 = centerLx + halfLenLocal;
    const int16_t uy0 = interpolateEyeCurveAtLocalX(lx0, g->upper);
    const int16_t uy1 = interpolateEyeCurveAtLocalX(lx1, g->upper);
    rotateLocalPoint(lx0, static_cast<float>(uy0) + yInset, g->cosA, g->sinA, g->cx, g->cy, x0, y0);
    rotateLocalPoint(lx1, static_cast<float>(uy1) + yInset, g->cosA, g->sinA, g->cx, g->cy, x1, y1);
}

void eyeUiDrawUpperPaddle(Adafruit_SSD1306& d, const EyeUiGeom* g,
                           float centerLx, float halfLenLocal, float yInset) {
    int16_t x0, y0, x1, y1;
    eyeUiUpperPaddleSegmentScreen(g, centerLx, halfLenLocal, yInset, &x0, &y0, &x1, &y1);
    drawThickLine(d, x0, y0, x1, y1, 3, SSD1306_WHITE);
}

// ════════════════════════════════════════════════════════════════════════════
// §15  EyeUi frame setup
// ════════════════════════════════════════════════════════════════════════════

void eyeUiComputeGeomForMenu(bool isRightEye, uint32_t nowMs, EyeUiGeom* g) {
    const EyePose pose = makeMenuEyePose(nowMs);
    eyeGeomCompute(pose, isRightEye, g);
}

void eyeUiDrawOutline(Adafruit_SSD1306& display, bool isRightEye, const EyeUiGeom* g) {
    (void)isRightEye;
    if (g->opening <= 3) return;
    eyeGeomDrawCurve(display, g, g->upper, 1, SSD1306_WHITE);
    if (g->opening > 4) {
        eyeGeomDrawCurve(display, g, g->lower, 1, SSD1306_WHITE);
    }
}

// Defined here after eyeGeomDrawCurve is visible.
static void drawOutlineNoUpper(Adafruit_SSD1306& display,
                                const EyeUiGeom* g, uint8_t lowerEyelidFacets) {
    if (g->opening <= 3) return;
    if (g->opening > 4) {
        if (lowerEyelidFacets >= 2) {
            eyeUiDrawLowerEyelidFaceted(display, g, lowerEyelidFacets);
        } else {
            eyeGeomDrawCurve(display, g, g->lower, 1, SSD1306_WHITE);
        }
    }
}

bool eyeUiBeginFrame(Adafruit_SSD1306& display, bool isRightEye,
                      uint32_t nowMs, EyeUiGeom* g) {
    eyeUiComputeGeomForMenu(isRightEye, nowMs, g);
    display.clearDisplay();
    if (g->opening <= 3) {
        display.setTextSize(1);
        display.setTextColor(SSD1306_WHITE);
        display.setCursor(24, 28);
        display.print("...");
        display.display();
        return false;
    }
    eyeUiDrawOutline(display, isRightEye, g);
    return true;
}

bool eyeUiBeginFrameNoUpper(Adafruit_SSD1306& display, bool isRightEye,
                              uint32_t nowMs, EyeUiGeom* g,
                              uint8_t lowerEyelidFacets) {
    eyeUiComputeGeomForMenu(isRightEye, nowMs, g);
    display.clearDisplay();
    if (g->opening <= 3) {
        display.setTextSize(1);
        display.setTextColor(SSD1306_WHITE);
        display.setCursor(24, 28);
        display.print("...");
        display.display();
        return false;
    }
    drawOutlineNoUpper(display, g, lowerEyelidFacets);
    return true;
}

// ════════════════════════════════════════════════════════════════════════════
// §16  In-eye text helpers
// ════════════════════════════════════════════════════════════════════════════

void eyeUiDrawTextCenteredNudged(Adafruit_SSD1306& d, int16_t y, uint8_t textSize,
                                  const char* txt, int16_t nudgeX) {
    if (txt == nullptr || txt[0] == '\0') return;
    const size_t  len = strlen(txt);
    const int16_t cw  = (textSize >= 2) ? 12 : 6;
    const int16_t w   = static_cast<int16_t>(len * cw);
    int32_t x = (static_cast<int32_t>(kDisplayWidth) - static_cast<int32_t>(w)) / 2 +
                static_cast<int32_t>(nudgeX);
    if (x < 0) x = 0;
    if (x + static_cast<int32_t>(w) > kDisplayWidth)
        x = static_cast<int32_t>(kDisplayWidth) - static_cast<int32_t>(w);
    d.setTextSize(textSize);
    d.setTextColor(SSD1306_WHITE);
    d.setCursor(static_cast<int16_t>(x), y);
    d.print(txt);
}

void eyeUiDrawRotatedKeyword(Adafruit_SSD1306& d, const EyeUiGeom* g, const char* word) {
    if (g->opening <= 3 || word == nullptr || word[0] == '\0') return;

    const float ang    = atan2f(g->sinA, g->cosA);
    const int   mid    = kEyeSpan / 2;
    const float localX = eyeLocalXFromIndex(static_cast<uint8_t>(mid));
    const float localY = (static_cast<float>(g->upper[mid]) +
                          static_cast<float>(g->lower[mid])) * 0.5f;
    int16_t ax, ay;
    rotateLocalPoint(localX, localY, g->cosA, g->sinA, g->cx, g->cy, &ax, &ay);
    ax = static_cast<int16_t>(static_cast<int32_t>(ax) +
                               static_cast<int32_t>(kMenuLeftKeywordNudgeScreenX));
    ay = static_cast<int16_t>(static_cast<int32_t>(ay) +
                               static_cast<int32_t>(kMenuLeftKeywordNudgeScreenY));

    const size_t  len  = strlen(word);
    const int16_t cw   = static_cast<int16_t>(len * 12);
    constexpr int16_t kCh = 16;

    GFXcanvas1 canvas(static_cast<uint16_t>(cw), static_cast<uint16_t>(kCh));
    canvas.fillScreen(0);
    canvas.setTextSize(2);
    canvas.setTextColor(1);
    canvas.setCursor(0, 0);
    canvas.print(word);

    const float pivotCx = static_cast<float>(cw)  * 0.5f;
    const float pivotCy = static_cast<float>(kCh) * 0.5f;
    const float c = cosf(ang);
    const float s = sinf(ang);

    for (int16_t py = 0; py < kCh; ++py) {
        for (int16_t px = 0; px < cw; ++px) {
            if (!canvas.getPixel(px, py)) continue;
            const float dx = static_cast<float>(px) - pivotCx;
            const float dy = static_cast<float>(py) - pivotCy;
            const int sx = static_cast<int>(lroundf(static_cast<float>(ax) + dx * c - dy * s));
            const int sy = static_cast<int>(lroundf(static_cast<float>(ay) + dx * s + dy * c));
            if (sx >= 0 && sx < kDisplayWidth && sy >= 0 && sy < kDisplayHeight) {
                d.drawPixel(static_cast<int16_t>(sx), static_cast<int16_t>(sy), SSD1306_WHITE);
            }
        }
    }
}

// ════════════════════════════════════════════════════════════════════════════
// §17  eyeGeomPutScreenOffset
// ════════════════════════════════════════════════════════════════════════════

void eyeGeomPutScreenOffset(Adafruit_SSD1306& d, const EyeUiGeom* g,
                             int16_t irisX, int16_t irisY,
                             int16_t dx, int16_t dy, uint16_t color) {
    const int16_t scrX = static_cast<int16_t>(irisX + dx);
    const int16_t scrY = static_cast<int16_t>(irisY + dy);
    if (!eyeUiPointInsideEyeOpening(g, scrX, scrY, 0)) return;
    d.drawPixel(scrX, scrY, color);
}
