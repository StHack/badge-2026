// eye_styles.cpp — all AlternatePupilDrawFn implementations for badge_v2
// Ported from badge_production eye_style_alternate.cpp and eye_style_extra.cpp.
// All functions clip pixels to the eye opening via putOffset / putPx helpers.
#include "eye_styles.h"
#include "eye_geometry.h"
#include "eye_types.h"
#include "../config/hardware.h"
#include <Adafruit_SSD1306.h>
#include <math.h>
#include <string.h>
#include <Arduino.h>

// ── Constants ─────────────────────────────────────────────────────────────────

static constexpr float kPi = 3.14159265f;

// ── Pixel helpers ─────────────────────────────────────────────────────────────
// putPx: draw screen pixel (sx,sy) only if it falls inside the eye opening.
// The eye opening is defined by upper[]/lower[] indexed along the local X axis.
// We convert the screen position to local coordinates, find the span index,
// and test against the stored eyelid values.

static inline void putPx(Adafruit_SSD1306& d,
                          int16_t sx, int16_t sy,
                          int16_t cx, int16_t cy,
                          float cosA, float sinA,
                          const int16_t* upper, const int16_t* lower,
                          uint16_t color = SSD1306_WHITE) {
    float dx = (float)(sx - cx);
    float dy = (float)(sy - cy);
    // screen → local
    float lx = dx * cosA + dy * sinA;
    float ly = -dx * sinA + dy * cosA;
    // find span index
    float span = gEyeSpanNegW + gEyeSpanPosW;
    if (span < 1e-4f) return;
    float tIdx = (lx + gEyeSpanNegW) / span * (float)(kEyeSpan - 1);
    int idx = (int)lroundf(tIdx);
    if (idx < 0 || idx >= kEyeSpan) return;
    if (ly <= (float)upper[idx] || ly >= (float)lower[idx]) return;
    d.drawPixel(sx, sy, color);
}

// putOffset: draw pixel at (irisX+dx, irisY+dy) clipped to eye opening.
static inline void putOffset(Adafruit_SSD1306& d,
                              int16_t irisX, int16_t irisY,
                              int16_t dx, int16_t dy,
                              int16_t cx, int16_t cy,
                              float cosA, float sinA,
                              const int16_t* upper, const int16_t* lower,
                              uint16_t color = SSD1306_WHITE) {
    putPx(d, (int16_t)(irisX + dx), (int16_t)(irisY + dy),
          cx, cy, cosA, sinA, upper, lower, color);
}

// ── drawRimmedPupil ───────────────────────────────────────────────────────────
// Iterate every pixel in a square [-irisRadius..+irisRadius] around irisX/Y.
// inside(dx,dy) → true means pixel is part of the pupil shape.
// shade(dx,dy)  → true = WHITE, false = BLACK within the shape.
// A 4-neighbour rim test adds a BLACK outline when blackRim is true.

template<typename InsideFn, typename ShadeFn>
static void drawRimmedPupil(
    Adafruit_SSD1306& d,
    int16_t irisX, int16_t irisY,
    int16_t bound,
    int16_t cx, int16_t cy,
    float cosA, float sinA,
    const int16_t* upper, const int16_t* lower,
    InsideFn inside, ShadeFn shade,
    bool blackRim)
{
    for (int16_t py = -bound; py <= bound; ++py) {
        for (int16_t px = -bound; px <= bound; ++px) {
            if (!inside(px, py)) continue;
            uint16_t col = shade(px, py) ? SSD1306_WHITE : SSD1306_BLACK;
            putOffset(d, irisX, irisY, px, py, cx, cy, cosA, sinA, upper, lower, col);
        }
    }
    if (!blackRim) return;
    for (int16_t py = -bound; py <= bound; ++py) {
        for (int16_t px = -bound; px <= bound; ++px) {
            if (!inside(px, py)) continue;
            bool edge = !inside((int16_t)(px + 1), py) ||
                        !inside((int16_t)(px - 1), py) ||
                        !inside(px, (int16_t)(py + 1)) ||
                        !inside(px, (int16_t)(py - 1));
            if (edge) {
                putOffset(d, irisX, irisY, px, py, cx, cy, cosA, sinA, upper, lower, SSD1306_BLACK);
            }
        }
    }
}

// ── putLine helper (Bresenham) ────────────────────────────────────────────────
static void putLine(Adafruit_SSD1306& d,
                    int16_t irisX, int16_t irisY,
                    int16_t cx, int16_t cy,
                    float cosA, float sinA,
                    const int16_t* upper, const int16_t* lower,
                    int16_t x0, int16_t y0, int16_t x1, int16_t y1,
                    uint16_t color) {
    int16_t dx = (int16_t)abs((int)(x1 - x0));
    int16_t dy = (int16_t)(-abs((int)(y1 - y0)));
    int16_t sx = x0 < x1 ? (int16_t)1 : (int16_t)-1;
    int16_t sy = y0 < y1 ? (int16_t)1 : (int16_t)-1;
    int16_t err = (int16_t)(dx + dy);
    for (;;) {
        putOffset(d, irisX, irisY, x0, y0, cx, cy, cosA, sinA, upper, lower, color);
        if (x0 == x1 && y0 == y1) break;
        int16_t e2 = (int16_t)(2 * err);
        if (e2 >= dy) { err = (int16_t)(err + dy); x0 = (int16_t)(x0 + sx); }
        if (e2 <= dx) { err = (int16_t)(err + dx); y0 = (int16_t)(y0 + sy); }
    }
}

// ── distPointSeg helper ───────────────────────────────────────────────────────
static float distPointSeg(float px, float py,
                           float ax, float ay,
                           float bx, float by) {
    float abx = bx - ax, aby = by - ay;
    float apx = px - ax, apy = py - ay;
    float ab2 = abx * abx + aby * aby;
    float t = (ab2 > 1e-6f) ? ((apx * abx + apy * aby) / ab2) : 0.0f;
    if (t < 0.0f) t = 0.0f;
    else if (t > 1.0f) t = 1.0f;
    float qx = ax + t * abx - px;
    float qy = ay + t * aby - py;
    return sqrtf(qx * qx + qy * qy);
}

// ── triContains helper ────────────────────────────────────────────────────────
static bool triContains(int16_t px, int16_t py,
                        int16_t ax, int16_t ay,
                        int16_t bx, int16_t by,
                        int16_t cxp, int16_t cyp) {
    int32_t d1 = (int32_t)(px - bx) * (ay - by) - (int32_t)(ax - bx) * (py - by);
    int32_t d2 = (int32_t)(px - cxp) * (by - cyp) - (int32_t)(bx - cxp) * (py - cyp);
    int32_t d3 = (int32_t)(px - ax) * (cyp - ay) - (int32_t)(cxp - ax) * (py - ay);
    bool neg = (d1 < 0) || (d2 < 0) || (d3 < 0);
    bool pos = (d1 > 0) || (d2 > 0) || (d3 > 0);
    return !(neg && pos);
}

// ── Star geometry helper ──────────────────────────────────────────────────────
struct StarGeom {
    int16_t x0[5], y0[5];
    int16_t x1[5], y1[5];
    int16_t xm[5], ym[5];
};

static void fillStarGeom(StarGeom* g, int16_t outer, int16_t inner, float turn) {
    for (int i = 0; i < 5; ++i) {
        float a0 = turn + (-kPi / 2.0f) + (float)i * 2.0f * kPi / 5.0f;
        float a1 = turn + (-kPi / 2.0f) + (float)(i + 1) * 2.0f * kPi / 5.0f;
        float am = (a0 + a1) * 0.5f;
        g->x0[i] = (int16_t)lroundf(cosf(a0) * (float)outer);
        g->y0[i] = (int16_t)lroundf(sinf(a0) * (float)outer);
        g->x1[i] = (int16_t)lroundf(cosf(a1) * (float)outer);
        g->y1[i] = (int16_t)lroundf(sinf(a1) * (float)outer);
        g->xm[i] = (int16_t)lroundf(cosf(am) * (float)inner);
        g->ym[i] = (int16_t)lroundf(sinf(am) * (float)inner);
    }
}

static bool starPixelInside(int16_t dx, int16_t dy, const StarGeom* g) {
    for (int i = 0; i < 5; ++i) {
        if (triContains(dx, dy, 0, 0, g->x0[i], g->y0[i], g->xm[i], g->ym[i]) ||
            triContains(dx, dy, 0, 0, g->xm[i], g->ym[i], g->x1[i], g->y1[i])) {
            return true;
        }
    }
    return false;
}

// ── heartBeatScale ────────────────────────────────────────────────────────────
static float heartBeatScale(uint32_t tMs) {
    constexpr float kCycleMs = 880.0f;
    float u = fmodf((float)tMs, kCycleMs) / kCycleMs;
    float k = 0.0f;
    if      (u < 0.09f)                         k = u / 0.09f;
    else if (u < 0.16f)                         k = 1.0f - (u - 0.09f) / 0.07f;
    else if (u > 0.21f && u < 0.29f)            k = (u - 0.21f) / 0.08f;
    else if (u >= 0.29f && u < 0.37f)           k = 1.0f - (u - 0.29f) / 0.08f;
    return 1.0f + 0.16f * k;
}

// ─────────────────────────────────────────────────────────────────────────────
// SECTION 1: Original 13 alternate pupils (ported from eye_style_alternate.cpp)
// ─────────────────────────────────────────────────────────────────────────────

// ── Heart ─────────────────────────────────────────────────────────────────────
static void drawHeartPupil(
    Adafruit_SSD1306& display,
    int16_t irisX, int16_t irisY, uint8_t irisRadius,
    int16_t cx, int16_t cy,
    const int16_t* upper, const int16_t* lower,
    float cosA, float sinA,
    const EyePose& pose, uint32_t nowMs, uint8_t texturePhase, bool)
{
    (void)texturePhase; (void)pose;
    float beat = heartBeatScale(nowMs);
    float scale = fmaxf(3.5f, (float)irisRadius * 0.68f) * beat;
    int16_t bound = (int16_t)lroundf(scale * 1.32f) + 2;
    float is = 1.0f / scale;

    auto inside = [=](int16_t px, int16_t py) -> bool {
        float x = (float)px * is;
        float y = -(float)py * is;
        float a = x * x + y * y - 1.0f;
        float h = a * a * a - x * x * y * y * y;
        return h <= 0.0f;
    };
    auto shade = [](int16_t, int16_t) -> bool { return true; };

    drawRimmedPupil(display, irisX, irisY, bound, cx, cy, cosA, sinA,
                   upper, lower, inside, shade, false);
}

// ── Star ──────────────────────────────────────────────────────────────────────
static void drawStarPupil(
    Adafruit_SSD1306& display,
    int16_t irisX, int16_t irisY, uint8_t irisRadius,
    int16_t cx, int16_t cy,
    const int16_t* upper, const int16_t* lower,
    float cosA, float sinA,
    const EyePose& pose, uint32_t nowMs, uint8_t texturePhase, bool)
{
    (void)pose; (void)texturePhase;
    int16_t outer = (int16_t)fmaxf(4.0f, (float)irisRadius * 0.94f);
    int16_t inner = (int16_t)fmaxf(2.0f, (float)outer * 0.40f);
    int16_t bound = (int16_t)(outer + 2);
    if (2 * (int)bound + 1 > 35) return;

    constexpr float kStep = 2.0f * kPi / 32.0f;
    float turn = (float)((nowMs >> 7) & 31u) * kStep;

    StarGeom geom;
    fillStarGeom(&geom, outer, inner, turn);

    // rasterise into a small stack buffer
    static uint8_t smask[35 * 35];
    int idx = 0;
    for (int16_t py = -bound; py <= bound; ++py)
        for (int16_t px = -bound; px <= bound; ++px)
            smask[idx++] = starPixelInside(px, py, &geom) ? 1u : 0u;

    idx = 0;
    for (int16_t py = -bound; py <= bound; ++py)
        for (int16_t px = -bound; px <= bound; ++px)
            if (smask[idx++])
                putOffset(display, irisX, irisY, px, py, cx, cy, cosA, sinA,
                          upper, lower, SSD1306_WHITE);
}

// ── Angry (sheared ellipse) ───────────────────────────────────────────────────
static void drawAngryPupil(
    Adafruit_SSD1306& display,
    int16_t irisX, int16_t irisY, uint8_t irisRadius,
    int16_t cx, int16_t cy,
    const int16_t* upper, const int16_t* lower,
    float cosA, float sinA,
    const EyePose& pose, uint32_t, uint8_t texturePhase, bool isRightEye)
{
    float halfH = fmaxf(3.5f, (float)irisRadius * 0.92f);
    float maxW  = fmaxf(1.6f, (float)irisRadius * 0.26f);
    float shear = isRightEye ? -0.09f : 0.09f;
    int16_t bound = (int16_t)lroundf(halfH) + (int16_t)fmaxf(2.0f, maxW + 2.0f);

    auto inside = [=](int16_t px, int16_t py) -> bool {
        float fx = (float)px + shear * (float)py;
        float t  = (float)py / halfH;
        if (t * t > 1.0f) return false;
        float wAtY = maxW * sqrtf(1.0f - t * t);
        return fabsf(fx) < wAtY * (0.88f + 0.12f * cosf(t * kPi * 0.5f));
    };
    auto shade = [=](int16_t px, int16_t py) -> bool {
        float fx = (float)px + shear * (float)py;
        if (fabsf(fx) < maxW * 0.28f) return true;
        return (((py + px / 2 + (int16_t)texturePhase) & 1) == 0);
    };

    drawRimmedPupil(display, irisX, irisY, bound, cx, cy, cosA, sinA,
                   upper, lower, inside, shade, true);
    // glint: two white pixels top-left
    putOffset(display, irisX, irisY, (int16_t)(-irisRadius / 3 - 2),
              (int16_t)(-irisRadius / 3), cx, cy, cosA, sinA, upper, lower, SSD1306_WHITE);
    putOffset(display, irisX, irisY, (int16_t)(-irisRadius / 3),
              (int16_t)(-irisRadius / 3 - 1), cx, cy, cosA, sinA, upper, lower, SSD1306_WHITE);
    (void)pose;
}

// ── Traumatized (circle + 5 radiating spokes) ────────────────────────────────
static void drawTraumatizedPupil(
    Adafruit_SSD1306& display,
    int16_t irisX, int16_t irisY, uint8_t irisRadius,
    int16_t cx, int16_t cy,
    const int16_t* upper, const int16_t* lower,
    float cosA, float sinA,
    const EyePose& pose, uint32_t nowMs, uint8_t texturePhase, bool)
{
    (void)pose;
    float R = fmaxf(4.0f, (float)irisRadius);
    int16_t bound = (int16_t)lroundf(R) + 3;
    float phase = (float)nowMs * 0.00065f;
    constexpr float squashX = 0.86f;

    auto inside = [=](int16_t px, int16_t py) -> bool {
        float x = (float)px / squashX;
        float y = (float)py;
        float nx = x / R, ny = y / R;
        return nx * nx + ny * ny <= 0.97f * 0.97f;
    };
    auto shade = [=](int16_t px, int16_t py) -> bool {
        float x = (float)px / squashX;
        float y = (float)py;
        float r = sqrtf(x * x + y * y);
        float ang = atan2f(y, x);
        if (r < R * 0.2f) return (((px + py + (int16_t)texturePhase) & 1) == 0);
        for (int k = 0; k < 5; ++k) {
            float ak = phase + (float)k * (2.0f * kPi / 5.0f);
            float d = ang - ak;
            d -= roundf(d / (2.0f * kPi)) * (2.0f * kPi);
            if (fabsf(d) < 0.095f && r > R * 0.18f && r < R * 0.88f) return false;
        }
        return (((px + py * 2) & 2) == 0);
    };

    drawRimmedPupil(display, irisX, irisY, bound, cx, cy, cosA, sinA,
                   upper, lower, inside, shade, true);
}

// ── Gem (rotating diamond) ────────────────────────────────────────────────────
static void drawGemPupil(
    Adafruit_SSD1306& display,
    int16_t irisX, int16_t irisY, uint8_t irisRadius,
    int16_t cx, int16_t cy,
    const int16_t* upper, const int16_t* lower,
    float cosA, float sinA,
    const EyePose& pose, uint32_t nowMs, uint8_t texturePhase, bool)
{
    int16_t R = (int16_t)fmaxf(4.0f, (float)irisRadius * 0.92f);
    int16_t bound = (int16_t)(R + 3);
    float tw = sinf((float)nowMs * 0.0009f) * 0.04f;

    auto inside = [=](int16_t px, int16_t py) -> bool {
        float x = (float)px, y = (float)py;
        float xr = x * cosf(tw) - y * sinf(tw);
        float yr = x * sinf(tw) + y * cosf(tw);
        return fabsf(xr) + fabsf(yr) <= (float)R + 0.25f;
    };
    auto shade = [=](int16_t px, int16_t py) -> bool {
        float x = (float)px, y = (float)py;
        float xr = x * cosf(tw) - y * sinf(tw);
        float yr = x * sinf(tw) + y * cosf(tw);
        float m = fabsf(xr) + fabsf(yr);
        bool facet = (fabsf(fabsf(xr) - fabsf(yr)) < 1.35f) &&
                     m > (float)R * 0.22f && m < (float)R * 0.88f &&
                     ((((int16_t)(m * 2.0f) + texturePhase) & 3) != 0);
        if (facet) return false;
        if (m < (float)R * 0.35f) return true;
        return (((px + py + ((int16_t)(texturePhase & 1u))) & 1) == 0);
    };

    drawRimmedPupil(display, irisX, irisY, bound, cx, cy, cosA, sinA,
                   upper, lower, inside, shade, true);
    // glint
    putOffset(display, irisX, irisY, -2, -2, cx, cy, cosA, sinA, upper, lower, SSD1306_WHITE);
    putOffset(display, irisX, irisY, -3, -3, cx, cy, cosA, sinA, upper, lower, SSD1306_WHITE);
    (void)pose;
}

// ── Crescent (circle with circular bite) ─────────────────────────────────────
static void drawCrescentPupil(
    Adafruit_SSD1306& display,
    int16_t irisX, int16_t irisY, uint8_t irisRadius,
    int16_t cx, int16_t cy,
    const int16_t* upper, const int16_t* lower,
    float cosA, float sinA,
    const EyePose& pose, uint32_t nowMs, uint8_t texturePhase, bool)
{
    float R = fmaxf(4.5f, (float)irisRadius * 0.9f);
    float bite = R * (0.52f + 0.03f * sinf((float)nowMs * 0.0006f));
    int16_t bound = (int16_t)lroundf(R) + 2;
    float ox = bite;

    auto inside = [=](int16_t px, int16_t py) -> bool {
        float x = (float)px, y = (float)py;
        float d0 = sqrtf(x * x + y * y);
        float x2 = x - ox;
        float d1 = sqrtf(x2 * x2 + y * y);
        return d0 <= R + 0.2f && d1 >= R * 0.78f;
    };
    auto shade = [=](int16_t px, int16_t py) -> bool {
        float x = (float)px, y = (float)py;
        if (x * x + y * y < (R * 0.42f) * (R * 0.42f))
            return (((px + py) & 1) == 0);
        float bias = x * 0.08f + y * 0.05f;
        return (fmodf(bias + (float)texturePhase * 0.25f, 1.0f) < 0.55f);
    };

    drawRimmedPupil(display, irisX, irisY, bound, cx, cy, cosA, sinA,
                   upper, lower, inside, shade, true);
    (void)pose;
}

// ── Bloom (6-petal flower) ────────────────────────────────────────────────────
static void drawBloomPupil(
    Adafruit_SSD1306& display,
    int16_t irisX, int16_t irisY, uint8_t irisRadius,
    int16_t cx, int16_t cy,
    const int16_t* upper, const int16_t* lower,
    float cosA, float sinA,
    const EyePose& pose, uint32_t nowMs, uint8_t texturePhase, bool)
{
    float R = fmaxf(4.5f, (float)irisRadius * 0.9f);
    float drift = (float)nowMs * 0.00055f;
    int16_t bound = (int16_t)lroundf(R) + 2;

    auto inside = [=](int16_t px, int16_t py) -> bool {
        float x = (float)px, y = (float)py;
        float r = sqrtf(x * x + y * y);
        if (r < 1.2f) return true;
        float th = atan2f(y, x);
        float petal = 0.58f + 0.42f * cosf(6.0f * th + drift * 3.0f);
        float rip   = 0.04f * sinf(7.0f * th + (float)texturePhase * 0.4f);
        return r < R * (petal + rip);
    };
    auto shade = [=](int16_t px, int16_t py) -> bool {
        float x = (float)px, y = (float)py;
        float r = sqrtf(x * x + y * y);
        float th = atan2f(y, x);
        if (r < R * 0.2f) return true;
        float v = sinf(th * 6.0f + drift * 4.0f) +
                  0.35f * sinf(th * 12.0f - (float)texturePhase * 0.15f);
        return v > -0.15f;
    };

    drawRimmedPupil(display, irisX, irisY, bound, cx, cy, cosA, sinA,
                   upper, lower, inside, shade, true);
    (void)pose;
}

// ── MoodSleepy (wide flat ellipse bobbing down) ───────────────────────────────
static void drawMoodSleepyPupil(
    Adafruit_SSD1306& display,
    int16_t irisX, int16_t irisY, uint8_t irisRadius,
    int16_t cx, int16_t cy,
    const int16_t* upper, const int16_t* lower,
    float cosA, float sinA,
    const EyePose& pose, uint32_t nowMs, uint8_t texturePhase, bool)
{
    float R = fmaxf(4.5f, (float)irisRadius * 0.92f);
    float bob = 0.45f * sinf((float)nowMs * 0.00082f);
    float yc  = R * 0.28f + bob;
    float a   = R * 0.84f;
    float b   = R * 0.36f;
    int16_t bound = (int16_t)lroundf(R) + 4;

    auto inside = [=](int16_t px, int16_t py) -> bool {
        float x = (float)px, ny = (float)py - yc;
        return (x * x) / (a * a) + (ny * ny) / (b * b) <= 1.0f;
    };
    auto shade = [=](int16_t px, int16_t py) -> bool {
        float y = (float)py;
        if (y < -R * 0.05f) return (((px + py + (int16_t)texturePhase) & 3) < 2);
        if (y > R * 0.12f)  return true;
        return (((px + (texturePhase & 7u)) & 1) == 0);
    };

    drawRimmedPupil(display, irisX, irisY, bound, cx, cy, cosA, sinA,
                   upper, lower, inside, shade, true);
    (void)pose;
}

// ── MoodJoy (ellipse bouncing up) ─────────────────────────────────────────────
static void drawMoodJoyPupil(
    Adafruit_SSD1306& display,
    int16_t irisX, int16_t irisY, uint8_t irisRadius,
    int16_t cx, int16_t cy,
    const int16_t* upper, const int16_t* lower,
    float cosA, float sinA,
    const EyePose& pose, uint32_t nowMs, uint8_t texturePhase, bool)
{
    float R = fmaxf(4.5f, (float)irisRadius * 0.9f);
    float bounce = 0.35f * sinf((float)nowMs * 0.0031f);
    float yc = -R * 0.14f + bounce;
    float a  = R * 0.54f;
    float b  = R * 0.58f;
    int16_t bound = (int16_t)lroundf(R) + 3;

    auto inside = [=](int16_t px, int16_t py) -> bool {
        float x = (float)px, ny = (float)py - yc;
        return (x * x) / (a * a) + (ny * ny) / (b * b) <= 1.0f;
    };
    auto shade = [=](int16_t px, int16_t py) -> bool {
        float x = (float)px, ny = (float)py - yc;
        float r = sqrtf(x * x + ny * ny);
        float th = atan2f(ny, x);
        if (r < R * 0.35f) return true;
        if (r > R * 0.48f && (fabsf(th - 1.05f) < 0.38f || fabsf(th + 1.05f) < 0.38f))
            return (((px + py + (int16_t)texturePhase) & 1) == 0);
        return sinf(th * 3.0f + (float)texturePhase * 0.2f) > -0.1f;
    };

    drawRimmedPupil(display, irisX, irisY, bound, cx, cy, cosA, sinA,
                   upper, lower, inside, shade, true);
    (void)pose;
}

// ── MoodSad (teardrop) ────────────────────────────────────────────────────────
static void drawMoodSadPupil(
    Adafruit_SSD1306& display,
    int16_t irisX, int16_t irisY, uint8_t irisRadius,
    int16_t cx, int16_t cy,
    const int16_t* upper, const int16_t* lower,
    float cosA, float sinA,
    const EyePose& pose, uint32_t nowMs, uint8_t texturePhase, bool)
{
    float R   = fmaxf(4.5f, (float)irisRadius * 0.9f);
    float sag = 0.25f * sinf((float)nowMs * 0.00055f);
    int16_t bound = (int16_t)lroundf(R * 1.25f) + 2;
    float cyTop = -R * 0.38f + sag;
    float rr  = R * 0.52f;

    auto inside = [=](int16_t px, int16_t py) -> bool {
        float x = (float)px, y = (float)py;
        if (y <= 0.0f) {
            float dy = y - cyTop;
            return x * x + dy * dy < rr * rr;
        }
        float tip = R * 1.08f;
        float w   = R * 0.48f * (1.0f - y / tip);
        return fabsf(x) < w && y < tip;
    };
    auto shade = [=](int16_t px, int16_t py) -> bool {
        float y = (float)py;
        if (y > R * 0.55f) return (((px + (texturePhase & 3u)) & 1) == 0);
        if (y < -R * 0.15f) return (((px ^ py) & 1) == 0);
        return true;
    };

    drawRimmedPupil(display, irisX, irisY, bound, cx, cy, cosA, sinA,
                   upper, lower, inside, shade, true);
    // drip pixel
    int16_t drip = (int16_t)(R + 4 + ((int32_t)nowMs / 180) % 3);
    putOffset(display, irisX, irisY, 0, drip, cx, cy, cosA, sinA, upper, lower, SSD1306_WHITE);
    putOffset(display, irisX, irisY, 0, (int16_t)(drip + 1), cx, cy, cosA, sinA, upper, lower, SSD1306_WHITE);
    (void)pose;
}

// ── MoodShock (8-spoke spinning) ──────────────────────────────────────────────
static void drawMoodShockPupil(
    Adafruit_SSD1306& display,
    int16_t irisX, int16_t irisY, uint8_t irisRadius,
    int16_t cx, int16_t cy,
    const int16_t* upper, const int16_t* lower,
    float cosA, float sinA,
    const EyePose& pose, uint32_t nowMs, uint8_t texturePhase, bool)
{
    float R = fmaxf(4.5f, (float)irisRadius * 0.94f);
    int16_t bound = (int16_t)lroundf(R) + 2;
    float jitter = 0.3f * sinf((float)nowMs * 0.018f);
    float spin   = (float)nowMs * 0.0011f + jitter;

    auto inside = [=](int16_t px, int16_t py) -> bool {
        float x = (float)px, y = (float)py;
        float r = sqrtf(x * x + y * y);
        if (r < R * 0.14f) return true;
        if (r > R * 0.46f && r < R * 0.58f) return true;
        float th = atan2f(y, x);
        for (int k = 0; k < 8; ++k) {
            float ak = spin + (float)k * kPi / 4.0f;
            float d  = th - ak;
            d -= roundf(d / (2.0f * kPi)) * (2.0f * kPi);
            if (fabsf(d) < 0.16f && r > R * 0.22f && r < R * 0.94f) return true;
        }
        return false;
    };
    auto shade = [=](int16_t px, int16_t py) -> bool {
        float x = (float)px, y = (float)py;
        float r = sqrtf(x * x + y * y);
        if (r < R * 0.11f) return true;
        if (r > R * 0.46f && r < R * 0.58f)
            return (((px + py + (int16_t)texturePhase) & 1) == 0);
        return (((px ^ py) & 2) == 0);
    };

    drawRimmedPupil(display, irisX, irisY, bound, cx, cy, cosA, sinA,
                   upper, lower, inside, shade, true);
    (void)pose;
}

// ── MoodCalm (dot + ring) ─────────────────────────────────────────────────────
static void drawMoodCalmPupil(
    Adafruit_SSD1306& display,
    int16_t irisX, int16_t irisY, uint8_t irisRadius,
    int16_t cx, int16_t cy,
    const int16_t* upper, const int16_t* lower,
    float cosA, float sinA,
    const EyePose& pose, uint32_t nowMs, uint8_t texturePhase, bool)
{
    float R = fmaxf(4.5f, (float)irisRadius * 0.92f);
    float breath = 0.06f * sinf((float)nowMs * 0.00065f);
    float rRing  = R * (0.74f + breath);
    int16_t bound = (int16_t)lroundf(R) + 2;

    auto inside = [=](int16_t px, int16_t py) -> bool {
        float x = (float)px, y = (float)py;
        float r = sqrtf(x * x + y * y);
        if (r < R * 0.28f) return true;
        return fabsf(r - rRing) < 0.55f;
    };
    auto shade = [=](int16_t px, int16_t py) -> bool {
        float x = (float)px, y = (float)py;
        float r = sqrtf(x * x + y * y);
        if (r < R * 0.15f) return true;
        if (fabsf(r - rRing) < 0.55f)
            return (((px + py + (int16_t)texturePhase) & 3) < 2);
        return (((px ^ py) & 1) == 0);
    };

    drawRimmedPupil(display, irisX, irisY, bound, cx, cy, cosA, sinA,
                   upper, lower, inside, shade, true);
    (void)pose;
}

// ── MoodHype (6-spoke fast spinning) ─────────────────────────────────────────
static void drawMoodHypePupil(
    Adafruit_SSD1306& display,
    int16_t irisX, int16_t irisY, uint8_t irisRadius,
    int16_t cx, int16_t cy,
    const int16_t* upper, const int16_t* lower,
    float cosA, float sinA,
    const EyePose& pose, uint32_t nowMs, uint8_t texturePhase, bool)
{
    float R = fmaxf(4.5f, (float)irisRadius * 0.94f);
    int16_t bound = (int16_t)lroundf(R) + 2;
    float spin = (float)nowMs * 0.0035f + 0.2f * sinf((float)nowMs * 0.009f);

    auto inside = [=](int16_t px, int16_t py) -> bool {
        float x = (float)px, y = (float)py;
        float r = sqrtf(x * x + y * y);
        if (r < R * 0.22f) return true;
        float th = atan2f(y, x);
        for (int k = 0; k < 6; ++k) {
            float ak = spin + (float)k * kPi / 3.0f;
            float d  = th - ak;
            d -= roundf(d / (2.0f * kPi)) * (2.0f * kPi);
            if (fabsf(d) < 0.29f && r > R * 0.24f && r < R * 0.92f) return true;
        }
        return false;
    };
    auto shade = [=](int16_t px, int16_t py) -> bool {
        float x = (float)px, y = (float)py;
        float r = sqrtf(x * x + y * y);
        if (r < R * 0.18f) return true;
        return (((uint32_t)(px + py + (int16_t)texturePhase * 2) >> 1) & 1u) == 0u;
    };

    drawRimmedPupil(display, irisX, irisY, bound, cx, cy, cosA, sinA,
                   upper, lower, inside, shade, true);
    (void)pose;
}

// ─────────────────────────────────────────────────────────────────────────────
// SECTION 2: Extra / tech pupils (ported from eye_style_extra.cpp)
// ─────────────────────────────────────────────────────────────────────────────

// ── Hypnosis (Archimedean spiral) ─────────────────────────────────────────────
static void drawHypnosisPupil(
    Adafruit_SSD1306& display,
    int16_t irisX, int16_t irisY, uint8_t irisRadius,
    int16_t cx, int16_t cy,
    const int16_t* upper, const int16_t* lower,
    float cosA, float sinA,
    const EyePose& pose, uint32_t nowMs, uint8_t texturePhase, bool)
{
    (void)pose;
    float R = fmaxf(4.5f, (float)irisRadius * 0.92f);
    int16_t bound = (int16_t)lroundf(R) + 2;
    float spin = (float)nowMs * 0.0028f;
    constexpr float arms  = 3.25f;
    constexpr float pitch = 0.52f;

    auto inside = [=](int16_t px, int16_t py) -> bool {
        float x = (float)px, y = (float)py;
        float r = sqrtf(x * x + y * y);
        if (r > R) return false;
        if (r < R * 0.07f) return true;
        float th    = atan2f(y, x);
        float phase = arms * th - r * pitch + spin;
        return fabsf(sinf(phase)) < 0.36f;
    };
    auto shade = [=](int16_t px, int16_t py) -> bool {
        float x = (float)px, y = (float)py;
        float r = sqrtf(x * x + y * y);
        if (r < R * 0.07f) return true;
        float th    = atan2f(y, x);
        float phase = arms * th - r * pitch + spin;
        float a     = fabsf(sinf(phase));
        if (a < 0.16f) return true;
        if (a < 0.30f) return (((px + py + (int16_t)texturePhase) >> 1) & 1) == 0;
        return (((px ^ py) & 1) == 0);
    };

    drawRimmedPupil(display, irisX, irisY, bound, cx, cy, cosA, sinA,
                   upper, lower, inside, shade, false);
}

// ── Bolt (lightning zigzag) ───────────────────────────────────────────────────
static void drawBoltPupil(
    Adafruit_SSD1306& display,
    int16_t irisX, int16_t irisY, uint8_t irisRadius,
    int16_t cx, int16_t cy,
    const int16_t* upper, const int16_t* lower,
    float cosA, float sinA,
    const EyePose& pose, uint32_t nowMs, uint8_t texturePhase, bool)
{
    (void)pose; (void)nowMs;
    float s = fmaxf(4.5f, (float)irisRadius * 0.9f) / 9.0f;
    // 5-segment zigzag (fixed waypoints scaled by s)
    float ax = 0.0f,       ay = -8.6f * s;
    float bx = 3.9f * s,   by = -3.6f * s;
    float cxp = -2.1f * s, cyp = -1.6f * s;
    float dx = 4.2f * s,   dy =  0.4f * s;
    float ex = -2.0f * s,  ey =  3.8f * s;
    float fx = 2.8f * s,   fy =  8.8f * s;
    int16_t bound = (int16_t)lroundf((float)irisRadius + 2.0f);
    constexpr float thick = 1.55f;

    auto inside = [=](int16_t px, int16_t py) -> bool {
        float x = (float)px, y = (float)py;
        float m = fminf(distPointSeg(x, y, ax, ay, bx, by),
                  fminf(distPointSeg(x, y, bx, by, cxp, cyp),
                  fminf(distPointSeg(x, y, cxp, cyp, dx, dy),
                  fminf(distPointSeg(x, y, dx, dy, ex, ey),
                        distPointSeg(x, y, ex, ey, fx, fy)))));
        return m < thick;
    };
    auto shade = [=](int16_t px, int16_t py) -> bool {
        float x = (float)px, y = (float)py;
        float m = fminf(distPointSeg(x, y, ax, ay, bx, by),
                  fminf(distPointSeg(x, y, bx, by, cxp, cyp),
                  fminf(distPointSeg(x, y, cxp, cyp, dx, dy),
                  fminf(distPointSeg(x, y, dx, dy, ex, ey),
                        distPointSeg(x, y, ex, ey, fx, fy)))));
        if (m < 0.55f) return true;
        return (((px + py + (int16_t)texturePhase) & 1) == 0);
    };

    drawRimmedPupil(display, irisX, irisY, bound, cx, cy, cosA, sinA,
                   upper, lower, inside, shade, true);
}

// ── Pixel (scanning raster) ───────────────────────────────────────────────────
static void drawPixelPupil(
    Adafruit_SSD1306& display,
    int16_t irisX, int16_t irisY, uint8_t irisRadius,
    int16_t cx, int16_t cy,
    const int16_t* upper, const int16_t* lower,
    float cosA, float sinA,
    const EyePose& pose, uint32_t nowMs, uint8_t texturePhase, bool)
{
    (void)pose;
    float R = fmaxf(4.5f, (float)irisRadius * 0.88f);
    int16_t bound = (int16_t)lroundf(R) + 2;
    int32_t scan  = (int32_t)(nowMs / 70);

    auto inside = [=](int16_t px, int16_t py) -> bool {
        return fabsf((float)px) < R * 0.88f && fabsf((float)py) < R * 0.88f;
    };
    auto shade = [=](int16_t px, int16_t py) -> bool {
        int32_t row = (int32_t)py + scan;
        return (((row + (int32_t)px / 2 + (int32_t)texturePhase) & 3) < 2);
    };

    drawRimmedPupil(display, irisX, irisY, bound, cx, cy, cosA, sinA,
                   upper, lower, inside, shade, true);
}

// ── Reptile (vertical slit pupil) ─────────────────────────────────────────────
static void drawReptilePupil(
    Adafruit_SSD1306& display,
    int16_t irisX, int16_t irisY, uint8_t irisRadius,
    int16_t cx, int16_t cy,
    const int16_t* upper, const int16_t* lower,
    float cosA, float sinA,
    const EyePose& pose, uint32_t nowMs, uint8_t texturePhase, bool)
{
    (void)pose; (void)nowMs;
    float R  = fmaxf(4.5f, (float)irisRadius * 0.92f);
    int16_t bound = (int16_t)lroundf(R) + 2;
    float rx = R * 0.22f;
    float ry = R * 0.88f;

    auto inside = [=](int16_t px, int16_t py) -> bool {
        return fabsf((float)px) / rx + fabsf((float)py) / ry <= 1.0f;
    };
    auto shade = [=](int16_t px, int16_t py) -> bool {
        return (((px + py * 2 + (int16_t)texturePhase) & 2) == 0);
    };

    drawRimmedPupil(display, irisX, irisY, bound, cx, cy, cosA, sinA,
                   upper, lower, inside, shade, true);
}

// ── InfinityShape (two hollow rings overlapping → ∞) ─────────────────────────
static void drawInfinityShapePupil(
    Adafruit_SSD1306& display,
    int16_t irisX, int16_t irisY, uint8_t irisRadius,
    int16_t cx, int16_t cy,
    const int16_t* upper, const int16_t* lower,
    float cosA, float sinA,
    const EyePose& pose, uint32_t nowMs, uint8_t texturePhase, bool)
{
    (void)pose; (void)nowMs; (void)texturePhase;
    float R   = fmaxf(4.5f, (float)irisRadius * 0.9f);
    int16_t bound = (int16_t)lroundf(R) + 2;
    // Two donuts side by side; their centers are offset ±d.
    // Outer radius rO, inner hole rI.  Where the donuts overlap they fill
    // in the crossover, producing a clear ∞ shape.
    float d  = R * 0.32f;
    float rO = R * 0.44f;
    float rI = R * 0.20f;

    auto inside = [=](int16_t px, int16_t py) -> bool {
        float x = (float)px, y = (float)py;
        float dL2 = (x + d) * (x + d) + y * y;
        float dR2 = (x - d) * (x - d) + y * y;
        bool ringL = (dL2 < rO * rO) && (dL2 > rI * rI);
        bool ringR = (dR2 < rO * rO) && (dR2 > rI * rI);
        return ringL || ringR;
    };
    auto shade = [=](int16_t px, int16_t py) -> bool {
        (void)px; (void)py; return true;
    };

    drawRimmedPupil(display, irisX, irisY, bound, cx, cy, cosA, sinA,
                   upper, lower, inside, shade, true);
}

// ── Sun (center dot + 8 radiating spokes) ────────────────────────────────────
static void drawSunPupil(
    Adafruit_SSD1306& display,
    int16_t irisX, int16_t irisY, uint8_t irisRadius,
    int16_t cx, int16_t cy,
    const int16_t* upper, const int16_t* lower,
    float cosA, float sinA,
    const EyePose& pose, uint32_t nowMs, uint8_t texturePhase, bool)
{
    (void)pose;
    float R  = fmaxf(4.5f, (float)irisRadius * 0.92f);
    int16_t bound = (int16_t)lroundf(R) + 2;
    float tw = (float)nowMs * 0.0009f;

    auto inside = [=](int16_t px, int16_t py) -> bool {
        float x = (float)px, y = (float)py;
        float r = sqrtf(x * x + y * y);
        if (r < R * 0.3f) return true;
        if (r > R * 0.26f && r < R * 0.94f) {
            float th = atan2f(y, x) + tw;
            float sector = fabsf(fmodf(th + kPi / 8.0f, kPi / 4.0f) - kPi / 8.0f);
            return sector < 0.13f;
        }
        return false;
    };
    auto shade = [=](int16_t px, int16_t py) -> bool {
        float r = hypotf((float)px, (float)py);
        if (r < R * 0.28f) return true;
        return (((px + py + (int16_t)texturePhase) & 1) == 0);
    };

    drawRimmedPupil(display, irisX, irisY, bound, cx, cy, cosA, sinA,
                   upper, lower, inside, shade, true);
}

// ── Anime (large ring) ────────────────────────────────────────────────────────
static void drawAnimePupil(
    Adafruit_SSD1306& display,
    int16_t irisX, int16_t irisY, uint8_t irisRadius,
    int16_t cx, int16_t cy,
    const int16_t* upper, const int16_t* lower,
    float cosA, float sinA,
    const EyePose& pose, uint32_t nowMs, uint8_t texturePhase, bool)
{
    (void)pose; (void)nowMs; (void)texturePhase;
    float R = fmaxf(4.5f, (float)irisRadius * 0.93f);
    int16_t bound = (int16_t)lroundf(R) + 2;

    auto inside = [=](int16_t px, int16_t py) -> bool {
        float r = hypotf((float)px, (float)py);
        return r > R * 0.38f && r < R * 0.94f;
    };
    auto shade = [](int16_t, int16_t) -> bool { return true; };

    drawRimmedPupil(display, irisX, irisY, bound, cx, cy, cosA, sinA,
                   upper, lower, inside, shade, false);
}

// ── Scope (crosshair + corner brackets) ──────────────────────────────────────
static void drawScopePupil(
    Adafruit_SSD1306& display,
    int16_t irisX, int16_t irisY, uint8_t irisRadius,
    int16_t cx, int16_t cy,
    const int16_t* upper, const int16_t* lower,
    float cosA, float sinA,
    const EyePose& pose, uint32_t nowMs, uint8_t texturePhase, bool)
{
    (void)pose; (void)nowMs;
    float R = fmaxf(4.5f, (float)irisRadius * 0.92f);
    int16_t bound = (int16_t)lroundf(R) + 2;

    auto inside = [=](int16_t px, int16_t py) -> bool {
        float x = (float)px, y = (float)py;
        float r = sqrtf(x * x + y * y);
        if (fabsf(r - R * 0.88f) < 0.6f) return true;
        if (fabsf(x) < R * 0.38f && fabsf(y) < R * 0.1f) return true;
        if (fabsf(y) < R * 0.38f && fabsf(x) < R * 0.1f) return true;
        float q = R * 0.62f;
        if (fabsf(x) > q && fabsf(y) > q) {
            float ax = fabsf(x) - q, ay = fabsf(y) - q;
            if ((ax < R * 0.22f && ay < 0.95f) || (ay < R * 0.22f && ax < 0.95f)) return true;
        }
        return false;
    };
    auto shade = [=](int16_t px, int16_t py) -> bool {
        return (((px + py + (int16_t)texturePhase) & 1) == 0);
    };

    drawRimmedPupil(display, irisX, irisY, bound, cx, cy, cosA, sinA,
                   upper, lower, inside, shade, true);
}

// ── Camera (concentric squares) ───────────────────────────────────────────────
static void drawCameraPupil(
    Adafruit_SSD1306& display,
    int16_t irisX, int16_t irisY, uint8_t irisRadius,
    int16_t cx, int16_t cy,
    const int16_t* upper, const int16_t* lower,
    float cosA, float sinA,
    const EyePose& pose, uint32_t nowMs, uint8_t texturePhase, bool)
{
    (void)pose; (void)nowMs;
    float R = fmaxf(4.5f, (float)irisRadius * 0.9f);
    int16_t bound = (int16_t)lroundf(R) + 2;

    auto inside = [=](int16_t px, int16_t py) -> bool {
        float ax = fabsf((float)px), ay = fabsf((float)py);
        float m = fmaxf(ax, ay);
        return (m > R * 0.38f && m < R * 0.48f) || (m > R * 0.68f && m < R * 0.88f);
    };
    auto shade = [=](int16_t px, int16_t py) -> bool {
        return (((px + py + (int16_t)texturePhase) & 1) == 0);
    };

    drawRimmedPupil(display, irisX, irisY, bound, cx, cy, cosA, sinA,
                   upper, lower, inside, shade, true);
}

// ── Clock (filled circle + two rotating hands) ────────────────────────────────
static void drawClockPupil(
    Adafruit_SSD1306& display,
    int16_t irisX, int16_t irisY, uint8_t irisRadius,
    int16_t cx, int16_t cy,
    const int16_t* upper, const int16_t* lower,
    float cosA, float sinA,
    const EyePose& pose, uint32_t nowMs, uint8_t texturePhase, bool)
{
    (void)pose; (void)texturePhase;
    float R = fmaxf(5.0f, (float)irisRadius * 1.45f);
    int16_t bound = (int16_t)lroundf(R) + 2;

    auto inside = [=](int16_t px, int16_t py) -> bool {
        return hypotf((float)px, (float)py) < R * 0.92f;
    };
    auto shade = [](int16_t, int16_t) -> bool { return true; };

    drawRimmedPupil(display, irisX, irisY, bound, cx, cy, cosA, sinA,
                   upper, lower, inside, shade, true);

    float ha = (float)nowMs * 0.0011f - kPi / 2.0f;
    float ma = (float)nowMs * 0.0019f - kPi / 2.0f;
    int16_t hx = (int16_t)lroundf(cosf(ha) * R * 0.52f);
    int16_t hy = (int16_t)lroundf(sinf(ha) * R * 0.52f);
    int16_t mx = (int16_t)lroundf(cosf(ma) * R * 0.72f);
    int16_t my = (int16_t)lroundf(sinf(ma) * R * 0.72f);
    putLine(display, irisX, irisY, cx, cy, cosA, sinA, upper, lower, 0, 0, hx, hy, SSD1306_BLACK);
    putLine(display, irisX, irisY, cx, cy, cosA, sinA, upper, lower, 0, 0, mx, my, SSD1306_BLACK);
}

// ── QrStub (QR corner markers + noise) ────────────────────────────────────────
static void drawQrStubPupil(
    Adafruit_SSD1306& display,
    int16_t irisX, int16_t irisY, uint8_t irisRadius,
    int16_t cx, int16_t cy,
    const int16_t* upper, const int16_t* lower,
    float cosA, float sinA,
    const EyePose& pose, uint32_t nowMs, uint8_t texturePhase, bool)
{
    (void)pose; (void)nowMs;
    float R = fmaxf(4.5f, (float)irisRadius * 0.9f);
    int16_t bound = (int16_t)lroundf(R) + 2;

    auto inside = [=](int16_t px, int16_t py) -> bool {
        return fabsf((float)px) < R * 0.9f && fabsf((float)py) < R * 0.9f;
    };
    auto shade = [=](int16_t px, int16_t py) -> bool {
        int16_t gx = (int16_t)(px + bound);
        int16_t gy = (int16_t)(py + bound);
        bool corner = (gx < 10 && gy < 10) ||
                      (gx > 2 * bound - 12 && gy < 10) ||
                      (gx < 10 && gy > 2 * bound - 12);
        if (corner) return (gx % 3 == 0) ^ (gy % 3 == 0);
        return (((px / 2 + py / 2 + (int16_t)texturePhase) & 3) < 2);
    };

    drawRimmedPupil(display, irisX, irisY, bound, cx, cy, cosA, sinA,
                   upper, lower, inside, shade, true);
}

// ── BinaryRain (scrolling binary columns) ────────────────────────────────────
static void drawBinaryRainPupil(
    Adafruit_SSD1306& display,
    int16_t irisX, int16_t irisY, uint8_t irisRadius,
    int16_t cx, int16_t cy,
    const int16_t* upper, const int16_t* lower,
    float cosA, float sinA,
    const EyePose& pose, uint32_t nowMs, uint8_t texturePhase, bool)
{
    (void)pose;
    float R = fmaxf(4.5f, (float)irisRadius * 0.9f);
    int16_t bound = (int16_t)lroundf(R) + 2;
    int32_t t = (int32_t)(nowMs / 120);

    auto inside = [=](int16_t px, int16_t py) -> bool {
        return hypotf((float)px, (float)py) < R * 0.94f;
    };
    auto shade = [=](int16_t px, int16_t py) -> bool {
        int32_t col = (int32_t)px / 3;
        bool one = ((col + (int32_t)py + t + (int32_t)texturePhase) & 1) != 0;
        if (one) return ((py & 1) == 0);
        return ((px & 1) == 0);
    };

    drawRimmedPupil(display, irisX, irisY, bound, cx, cy, cosA, sinA,
                   upper, lower, inside, shade, true);
}

// ── HexGrid ───────────────────────────────────────────────────────────────────
static void drawHexGridPupil(
    Adafruit_SSD1306& display,
    int16_t irisX, int16_t irisY, uint8_t irisRadius,
    int16_t cx, int16_t cy,
    const int16_t* upper, const int16_t* lower,
    float cosA, float sinA,
    const EyePose& pose, uint32_t nowMs, uint8_t texturePhase, bool)
{
    (void)pose; (void)nowMs;
    float R = fmaxf(4.5f, (float)irisRadius * 0.9f);
    int16_t bound = (int16_t)lroundf(R) + 2;

    auto inside = [=](int16_t px, int16_t py) -> bool {
        return hypotf((float)px, (float)py) < R * 0.94f;
    };
    auto shade = [=](int16_t px, int16_t py) -> bool {
        int32_t hx = (int32_t)px + (int32_t)py / 2;
        bool a = (hx % 5 < 2);
        bool b = ((int32_t)py % 4 < 2);
        return a ^ b ^ ((texturePhase & 1u) != 0);
    };

    drawRimmedPupil(display, irisX, irisY, bound, cx, cy, cosA, sinA,
                   upper, lower, inside, shade, true);
}

// ── Robot (horizontal scanline sweeping up/down) ─────────────────────────────
static void drawRobotPupil(
    Adafruit_SSD1306& display,
    int16_t irisX, int16_t irisY, uint8_t irisRadius,
    int16_t cx, int16_t cy,
    const int16_t* upper, const int16_t* lower,
    float cosA, float sinA,
    const EyePose& pose, uint32_t nowMs, uint8_t texturePhase, bool)
{
    (void)pose;
    float R = fmaxf(4.5f, (float)irisRadius * 0.9f);
    int16_t bound = (int16_t)lroundf(R) + 2;
    int32_t scan  = (int32_t)(nowMs / 55);

    auto inside = [=](int16_t px, int16_t py) -> bool {
        return fabsf((float)py) < 2.2f && fabsf((float)px) < R * 0.9f;
    };
    auto shade = [=](int16_t px, int16_t py) -> bool {
        (void)py;
        int32_t s = (int32_t)px + scan + (int32_t)texturePhase;
        return (s % 6 < 3);
    };

    drawRimmedPupil(display, irisX, irisY, bound, cx, cy, cosA, sinA,
                   upper, lower, inside, shade, true);
}

// ── Sus (circle body + top-hat rectangle) ────────────────────────────────────
static void drawSusPupil(
    Adafruit_SSD1306& display,
    int16_t irisX, int16_t irisY, uint8_t irisRadius,
    int16_t cx, int16_t cy,
    const int16_t* upper, const int16_t* lower,
    float cosA, float sinA,
    const EyePose& pose, uint32_t nowMs, uint8_t texturePhase, bool)
{
    (void)pose; (void)nowMs;
    float R = fmaxf(4.5f, (float)irisRadius * 0.9f);
    int16_t bound = (int16_t)lroundf(R) + 2;

    auto inside = [=](int16_t px, int16_t py) -> bool {
        float x = (float)px, y = (float)py;
        float r = sqrtf(x * x + y * y);
        if (r < R * 0.86f) return true;
        return (y < -R * 0.32f && y > -R * 0.78f && fabsf(x) < R * 0.52f);
    };
    auto shade = [=](int16_t px, int16_t py) -> bool {
        float y = (float)py;
        if (y < -R * 0.34f) return true;
        return (((px + py + (int16_t)texturePhase) & 1) == 0);
    };

    drawRimmedPupil(display, irisX, irisY, bound, cx, cy, cosA, sinA,
                   upper, lower, inside, shade, true);
}

// ── Shy (offset ellipse) ──────────────────────────────────────────────────────
static void drawShyPupil(
    Adafruit_SSD1306& display,
    int16_t irisX, int16_t irisY, uint8_t irisRadius,
    int16_t cx, int16_t cy,
    const int16_t* upper, const int16_t* lower,
    float cosA, float sinA,
    const EyePose& pose, uint32_t nowMs, uint8_t texturePhase, bool)
{
    (void)pose; (void)nowMs;
    float R  = fmaxf(4.5f, (float)irisRadius * 0.9f);
    int16_t bound = (int16_t)lroundf(R) + 2;
    float ox = R * 0.18f;
    float a  = R * 0.48f;
    float b  = R * 0.68f;

    auto inside = [=](int16_t px, int16_t py) -> bool {
        float x = (float)px - ox, y = (float)py;
        return x * x / (a * a) + y * y / (b * b) <= 1.0f;
    };
    auto shade = [=](int16_t px, int16_t py) -> bool {
        float x = (float)px - ox, y = (float)py;
        if (y < -b * 0.35f && fabsf(x) < a * 0.5f) return false;
        return (((px + py + (int16_t)texturePhase) & 1) == 0);
    };

    drawRimmedPupil(display, irisX, irisY, bound, cx, cy, cosA, sinA,
                   upper, lower, inside, shade, true);
}

// ── Laser (pulsing horizontal scanline) ───────────────────────────────────────
static void drawLaserPupil(
    Adafruit_SSD1306& display,
    int16_t irisX, int16_t irisY, uint8_t irisRadius,
    int16_t cx, int16_t cy,
    const int16_t* upper, const int16_t* lower,
    float cosA, float sinA,
    const EyePose& pose, uint32_t nowMs, uint8_t texturePhase, bool)
{
    (void)pose;
    float R = fmaxf(4.5f, (float)irisRadius * 0.92f);
    int16_t bound = (int16_t)lroundf(R) + 2;
    float pulse = 0.5f + 0.5f * sinf((float)nowMs * 0.012f);

    auto inside = [=](int16_t px, int16_t py) -> bool {
        float ax = fabsf((float)py);
        return fabsf((float)px) < R * 0.92f && ax < 3.2f + pulse * 0.4f;
    };
    auto shade = [=](int16_t px, int16_t py) -> bool {
        int16_t ay = (int16_t)abs((int)py);
        if (ay <= 0) return true;
        if (ay <= 2) return (((px + (int16_t)texturePhase) & 1) == 0);
        return false;
    };

    drawRimmedPupil(display, irisX, irisY, bound, cx, cy, cosA, sinA,
                   upper, lower, inside, shade, true);
}

// ── Dream (two overlapping circles) ──────────────────────────────────────────
static void drawDreamPupil(
    Adafruit_SSD1306& display,
    int16_t irisX, int16_t irisY, uint8_t irisRadius,
    int16_t cx, int16_t cy,
    const int16_t* upper, const int16_t* lower,
    float cosA, float sinA,
    const EyePose& pose, uint32_t nowMs, uint8_t texturePhase, bool)
{
    (void)pose; (void)nowMs;
    float R  = fmaxf(4.5f, (float)irisRadius * 0.88f);
    int16_t bound = (int16_t)lroundf(R) + 2;
    float off = R * 0.32f;
    float rr  = R * 0.52f;

    auto inside = [=](int16_t px, int16_t py) -> bool {
        float x = (float)px, y = (float)py;
        float x0 = x + off, x1 = x - off;
        return (x0 * x0 + y * y < rr * rr) || (x1 * x1 + y * y < rr * rr);
    };
    auto shade = [=](int16_t px, int16_t py) -> bool {
        float x = (float)px, y = (float)py;
        float x0 = x + off, x1 = x - off;
        bool b0 = x0 * x0 + y * y < rr * rr;
        bool b1 = x1 * x1 + y * y < rr * rr;
        if (b0 && b1) return (((px + py) & 1) == 0);
        return true;
    };

    drawRimmedPupil(display, irisX, irisY, bound, cx, cy, cosA, sinA,
                   upper, lower, inside, shade, true);
    (void)texturePhase;
}

// ── Frost (polar snowflake) ───────────────────────────────────────────────────
static void drawFrostPupil(
    Adafruit_SSD1306& display,
    int16_t irisX, int16_t irisY, uint8_t irisRadius,
    int16_t cx, int16_t cy,
    const int16_t* upper, const int16_t* lower,
    float cosA, float sinA,
    const EyePose& pose, uint32_t nowMs, uint8_t texturePhase, bool)
{
    (void)pose;
    float R = fmaxf(4.5f, (float)irisRadius * 0.9f);
    int16_t bound = (int16_t)lroundf(R) + 2;
    float tw = (float)nowMs * 0.0006f;

    auto inside = [=](int16_t px, int16_t py) -> bool {
        return hypotf((float)px, (float)py) < R * 0.94f;
    };
    auto shade = [=](int16_t px, int16_t py) -> bool {
        float th = atan2f((float)py, (float)px);
        float r  = hypotf((float)px, (float)py);
        float sp = sinf(th * 9.0f + tw * 4.0f + r * 0.35f);
        if (fabsf(sp) < 0.16f && r > R * 0.18f) return false;
        return (((px + py + (int16_t)texturePhase) & 1) == 0);
    };

    drawRimmedPupil(display, irisX, irisY, bound, cx, cy, cosA, sinA,
                   upper, lower, inside, shade, true);
}

// ── BadgeLogo (vertical bar + 3 horizontal bars) ─────────────────────────────
static void drawBadgeLogoPupil(
    Adafruit_SSD1306& display,
    int16_t irisX, int16_t irisY, uint8_t irisRadius,
    int16_t cx, int16_t cy,
    const int16_t* upper, const int16_t* lower,
    float cosA, float sinA,
    const EyePose& pose, uint32_t nowMs, uint8_t texturePhase, bool)
{
    (void)pose; (void)nowMs;
    float R = fmaxf(4.5f, (float)irisRadius * 0.9f);
    int16_t bound = (int16_t)lroundf(R) + 2;

    auto inside = [=](int16_t px, int16_t py) -> bool {
        float x = (float)px, y = (float)py;
        // vertical bar
        if (fabsf(x) < 1.0f && y >= -R * 0.55f && y <= R * 0.55f) return true;
        // three horizontal bars
        const float ys[3] = { -R * 0.35f, 0.0f, R * 0.35f };
        for (float yy : ys)
            if (fabsf(y - yy) < 0.85f && x >= -R * 0.25f && x <= R * 0.55f) return true;
        return false;
    };
    auto shade = [=](int16_t px, int16_t py) -> bool {
        return (((px + py + (int16_t)texturePhase) & 1) == 0);
    };

    drawRimmedPupil(display, irisX, irisY, bound, cx, cy, cosA, sinA,
                   upper, lower, inside, shade, true);
}

// ── Lock (padlock: large body + thick U-shackle + keyhole) ───────────────────
static void drawLockPupil(
    Adafruit_SSD1306& display,
    int16_t irisX, int16_t irisY, uint8_t irisRadius,
    int16_t cx, int16_t cy,
    const int16_t* upper, const int16_t* lower,
    float cosA, float sinA,
    const EyePose& pose, uint32_t nowMs, uint8_t texturePhase, bool)
{
    (void)pose; (void)nowMs; (void)texturePhase;
    float R       = fmaxf(5.0f, (float)irisRadius * 1.45f);
    int16_t bound = (int16_t)lroundf(R) + 2;

    // Shackle: thick U-arc centred above iris centre
    float shacCY = -R * 0.18f;  // arc centre — low, close to body
    float rO     =  R * 0.48f;  // outer radius (smaller, proportional to body)
    float rI     =  R * 0.24f;  // inner radius  (shackle thickness ≈ 0.24 R)
    float legBot =  R * 0.26f;  // shackle legs extend into body

    // Body: solid rectangle in the lower portion
    float bodyTop = -R * 0.10f;  // overlaps leg bottoms for seamless join
    float bodyBot =  R * 0.58f;
    float bodyHW  =  R * 0.54f;

    // Keyhole: small circle + vertical slot (drawn BLACK inside the body)
    float khCY  =  R * 0.18f;  // circle centre
    float khR   =  R * 0.11f;  // circle radius
    float khHW  =  R * 0.055f; // slot half-width
    float khBot =  R * 0.38f;  // slot bottom

    auto inside = [=](int16_t px, int16_t py) -> bool {
        float x = (float)px, y = (float)py;
        float r2 = x * x + (y - shacCY) * (y - shacCY);
        // Shackle ring (annulus, clipped to upper U + legs)
        if (y <= legBot && r2 >= rI * rI && r2 <= rO * rO) return true;
        // Body
        if (y >= bodyTop && y <= bodyBot && fabsf(x) <= bodyHW) return true;
        return false;
    };
    auto shade = [=](int16_t px, int16_t py) -> bool {
        float x = (float)px, y = (float)py;
        // Keyhole circle
        if (x * x + (y - khCY) * (y - khCY) <= khR * khR) return false;
        // Keyhole slot
        if (y >= khCY && y <= khBot && fabsf(x) <= khHW) return false;
        return true;
    };

    drawRimmedPupil(display, irisX, irisY, bound, cx, cy, cosA, sinA,
                   upper, lower, inside, shade, true);
}

// ── Wifi (dot + 3 large concentric arcs opening upward) ──────────────────────
static void drawWifiPupil(
    Adafruit_SSD1306& display,
    int16_t irisX, int16_t irisY, uint8_t irisRadius,
    int16_t cx, int16_t cy,
    const int16_t* upper, const int16_t* lower,
    float cosA, float sinA,
    const EyePose& pose, uint32_t nowMs, uint8_t texturePhase, bool)
{
    (void)pose; (void)nowMs; (void)texturePhase;
    float R       = fmaxf(5.0f, (float)irisRadius * 1.45f);
    int16_t bound = (int16_t)lroundf(R) + 2;

    float dotCY = R * 0.38f;   // dot centre Y (lower half)
    float dotR  = R * 0.14f;   // dot radius

    // Three thick arc bands [inner_r, outer_r] centred on the dot
    const float bands[3][2] = {
        { R * 0.24f, R * 0.44f },   // inner  (0.20 R thick)
        { R * 0.54f, R * 0.74f },   // middle
        { R * 0.84f, R * 1.04f },   // outer
    };
    // Angular spread: ±70° from straight up
    const float spread = kPi * 0.39f;

    auto inside = [=](int16_t px, int16_t py) -> bool {
        float x = (float)px, y = (float)py;
        // Dot
        if (x * x + (y - dotCY) * (y - dotCY) <= dotR * dotR) return true;
        // Arcs — only above the dot centre
        float dy = y - dotCY;
        if (dy >= 0.0f) return false;
        float r  = sqrtf(x * x + dy * dy);
        float th = atan2f(x, -dy);   // 0 = straight up
        if (fabsf(th) > spread) return false;
        for (const auto& b : bands)
            if (r >= b[0] && r < b[1]) return true;
        return false;
    };
    auto shade = [=](int16_t px, int16_t py) -> bool {
        (void)px; (void)py; return true;
    };

    drawRimmedPupil(display, irisX, irisY, bound, cx, cy, cosA, sinA,
                   upper, lower, inside, shade, true);
}

// ── Live (scrolling "LIVE" text + blink dot) ──────────────────────────────────
static void drawLivePupil(
    Adafruit_SSD1306& display,
    int16_t irisX, int16_t irisY, uint8_t irisRadius,
    int16_t cx, int16_t cy,
    const int16_t* upper, const int16_t* lower,
    float cosA, float sinA,
    const EyePose& pose, uint32_t nowMs, uint8_t texturePhase, bool)
{
    (void)pose;
    float R = fmaxf(5.0f, (float)irisRadius * 0.95f);
    int16_t bound = (int16_t)lroundf(R) + 2;
    bool blink = ((nowMs / 380u) & 1u) == 0u;

    auto inside = [=](int16_t px, int16_t py) -> bool {
        float x = (float)px, y = (float)py;
        if (blink && x < -R * 0.55f && hypotf(x + R * 0.75f, y) < R * 0.16f) return true;
        return (x >= -R * 0.35f && x <= R * 0.75f && fabsf(y) < R * 0.22f);
    };
    auto shade = [=](int16_t px, int16_t py) -> bool {
        int16_t u = (int16_t)(px + (int16_t)(R * 0.55f));
        if (u < 0) return true;
        int pat = (u / 3) % 5;
        int v   = abs((int)py + 3);
        static const uint8_t rows[5][6] = {
            {1, 0, 0, 1, 1, 1},
            {0, 1, 0, 0, 1, 0},
            {1, 1, 1, 1, 0, 1},
            {1, 1, 1, 0, 1, 1},
            {1, 1, 1, 0, 1, 1},
        };
        if (v < 0 || v > 5) return false;
        if (rows[pat][v] == 0u) return false;
        return (((px + py + (int16_t)texturePhase) & 1) == 0);
    };

    drawRimmedPupil(display, irisX, irisY, bound, cx, cy, cosA, sinA,
                   upper, lower, inside, shade, true);
}

// ─────────────────────────────────────────────────────────────────────────────
// Extern function-pointer definitions (declared in eye_styles.h)
// ─────────────────────────────────────────────────────────────────────────────

// ── Cat (vertical slit pupil) ─────────────────────────────────────────────────
// White filled iris + narrow black vertical ellipse pupil + black rim.
static void drawCatPupil(
    Adafruit_SSD1306& display,
    int16_t irisX, int16_t irisY, uint8_t irisRadius,
    int16_t cx, int16_t cy,
    const int16_t* upper, const int16_t* lower,
    float cosA, float sinA,
    const EyePose& pose, uint32_t nowMs, uint8_t texturePhase, bool)
{
    (void)pose; (void)nowMs; (void)texturePhase;
    float R      = fmaxf(4.5f, (float)irisRadius * 0.93f);
    float slitW  = R * 0.22f;   // half-width of the slit
    float slitH  = R * 0.70f;   // half-height of the slit
    int16_t bound = (int16_t)lroundf(R) + 2;

    // Iris disk
    auto inside = [=](int16_t px, int16_t py) -> bool {
        float r2 = (float)px * (float)px + (float)py * (float)py;
        return r2 < R * R;
    };
    // White everywhere except the vertical slit (black pupil)
    auto shade = [=](int16_t px, int16_t py) -> bool {
        float sx = (float)px / slitW;
        float sy = (float)py / slitH;
        return (sx * sx + sy * sy) >= 1.0f;   // false = black inside slit
    };

    drawRimmedPupil(display, irisX, irisY, bound, cx, cy, cosA, sinA,
                   upper, lower, inside, shade, true);
}

AlternatePupilDrawFn eyeStyleHeartFn       = drawHeartPupil;
AlternatePupilDrawFn eyeStyleStarFn        = drawStarPupil;
AlternatePupilDrawFn eyeStyleAngryFn       = drawAngryPupil;
AlternatePupilDrawFn eyeStyleTraumatizedFn = drawTraumatizedPupil;
AlternatePupilDrawFn eyeStyleGemFn         = drawGemPupil;
AlternatePupilDrawFn eyeStyleCrescentFn    = drawCrescentPupil;
AlternatePupilDrawFn eyeStyleBloomFn       = drawBloomPupil;
AlternatePupilDrawFn eyeStyleMoodSleepyFn  = drawMoodSleepyPupil;
AlternatePupilDrawFn eyeStyleMoodJoyFn     = drawMoodJoyPupil;
AlternatePupilDrawFn eyeStyleMoodSadFn     = drawMoodSadPupil;
AlternatePupilDrawFn eyeStyleMoodShockFn   = drawMoodShockPupil;
AlternatePupilDrawFn eyeStyleMoodCalmFn    = drawMoodCalmPupil;
AlternatePupilDrawFn eyeStyleMoodHypeFn    = drawMoodHypePupil;
AlternatePupilDrawFn eyeStyleHypnosisFn    = drawHypnosisPupil;
AlternatePupilDrawFn eyeStyleBoltFn        = drawBoltPupil;
AlternatePupilDrawFn eyeStylePixelFn       = drawPixelPupil;
AlternatePupilDrawFn eyeStyleReptileFn     = drawReptilePupil;
AlternatePupilDrawFn eyeStyleInfinityShapeFn = drawInfinityShapePupil;
AlternatePupilDrawFn eyeStyleSunFn         = drawSunPupil;
AlternatePupilDrawFn eyeStyleAnimeFn       = drawAnimePupil;
AlternatePupilDrawFn eyeStyleScopeFn       = drawScopePupil;
AlternatePupilDrawFn eyeStyleCameraFn      = drawCameraPupil;
AlternatePupilDrawFn eyeStyleClockFn       = drawClockPupil;
AlternatePupilDrawFn eyeStyleQrStubFn      = drawQrStubPupil;
AlternatePupilDrawFn eyeStyleBinaryRainFn  = drawBinaryRainPupil;
AlternatePupilDrawFn eyeStyleHexGridFn     = drawHexGridPupil;
AlternatePupilDrawFn eyeStyleRobotFn       = drawRobotPupil;
AlternatePupilDrawFn eyeStyleSusFn         = drawSusPupil;
AlternatePupilDrawFn eyeStyleShyFn         = drawShyPupil;
AlternatePupilDrawFn eyeStyleLaserFn       = drawLaserPupil;
AlternatePupilDrawFn eyeStyleDreamFn       = drawDreamPupil;
AlternatePupilDrawFn eyeStyleFrostFn       = drawFrostPupil;
AlternatePupilDrawFn eyeStyleBadgeLogoFn   = drawBadgeLogoPupil;
AlternatePupilDrawFn eyeStyleLockFn        = drawLockPupil;
AlternatePupilDrawFn eyeStyleWifiFn        = drawWifiPupil;
AlternatePupilDrawFn eyeStyleCatFn         = drawCatPupil;
AlternatePupilDrawFn eyeStyleLiveFn        = drawLivePupil;

// ── Human (realistic iris: radial fibers, collarette, limbal ring, glint) ────
static void drawHumanPupil(
    Adafruit_SSD1306& display,
    int16_t irisX, int16_t irisY, uint8_t irisRadius,
    int16_t cx, int16_t cy,
    const int16_t* upper, const int16_t* lower,
    float cosA, float sinA,
    const EyePose& pose, uint32_t nowMs, uint8_t texturePhase, bool)
{
    (void)pose; (void)texturePhase;

    float R  = fmaxf(5.0f, (float)irisRadius * 0.97f);
    // Pupil dilates/contracts slowly
    float pulse = (sinf((float)nowMs * 0.00072f) + 1.f) * 0.5f;
    float pR = R * (0.34f + 0.08f * pulse);   // 34%–42% of iris radius
    float cR = R * 0.60f;                      // collarette ring centre
    float lR = R * 0.83f;                      // limbal ring inner edge
    int16_t bound = (int16_t)lroundf(R) + 2;

    // Very slow fiber rotation (one full turn every ~6 min)
    float fRot = (float)nowMs * 0.000035f;

    auto inside = [=](int16_t px, int16_t py) -> bool {
        float r2 = (float)px * (float)px + (float)py * (float)py;
        return r2 <= (R + 0.5f) * (R + 0.5f);
    };

    auto shade = [=](int16_t px, int16_t py) -> bool {
        float fx = (float)px, fy = (float)py;
        float r  = sqrtf(fx * fx + fy * fy);

        // 1. Pupil — black
        if (r < pR) return false;

        // 2. Limbal ring — near-black dither (3 out of 4 pixels black)
        if (r > lR) {
            return ((((px & 1) ^ (py & 1)) + ((py >> 1) & 1)) == 0);
        }

        // 3. Collarette bright ring
        if (fabsf(r - cR) < 1.4f) return true;

        // 4. Radial fiber texture
        float ang  = atan2f(fy, fx);
        // 20 fibers; fFrac = position within one fiber (0=edge, 0.5=centre)
        float fIdx = (ang + kPi + fRot) * (20.0f / (2.0f * kPi));
        float fFrac = fIdx - floorf(fIdx);

        // Crypt boundary lines (dark)
        bool isCrypt = (fFrac < 0.10f || fFrac > 0.90f);

        // Normalized radius: 0 = pupil edge, 1 = limbal ring
        float nr = (r - pR) / (lR - pR);

        if (isCrypt) {
            // Thin dark radial lines — very sparse white
            return ((px & 1) == 0 && (py & 1) == 0);
        }

        if (nr < 0.20f) {
            // Periareolar zone: medium-dark dither (50%)
            return (((px + py) & 1) == 0);
        }

        if (nr < 0.70f) {
            // Main iris body: bright, with subtle radial modulation
            float brightness = 0.60f + 0.30f * sinf(fFrac * kPi);
            // 2×2 ordered dither threshold
            int bayer = ((px & 1) << 1) | (py & 1);  // 0..3
            return brightness > (0.20f + (float)bayer * 0.15f);
        }

        // Outer zone (between collarette and limbal): darkening gradient
        return (((px + py * 2) & 3) < 2);
    };

    drawRimmedPupil(display, irisX, irisY, bound, cx, cy, cosA, sinA,
                   upper, lower, inside, shade, true);

    // Corneal glint — primary (upper-left, 3 px)
    int16_t gx = (int16_t)(-(float)irisRadius * 0.28f);
    int16_t gy = (int16_t)(-(float)irisRadius * 0.30f);
    putOffset(display, irisX, irisY, gx,     gy,     cx, cy, cosA, sinA, upper, lower, SSD1306_WHITE);
    putOffset(display, irisX, irisY, gx + 1, gy,     cx, cy, cosA, sinA, upper, lower, SSD1306_WHITE);
    putOffset(display, irisX, irisY, gx,     gy + 1, cx, cy, cosA, sinA, upper, lower, SSD1306_WHITE);
    // Secondary glint (small, right side)
    putOffset(display, irisX, irisY,
              (int16_t)((float)irisRadius * 0.24f),
              (int16_t)(-(float)irisRadius * 0.16f),
              cx, cy, cosA, sinA, upper, lower, SSD1306_WHITE);
}

AlternatePupilDrawFn eyeStyleHumanFn = drawHumanPupil;

// ── Sick / nausea rings — tamagotchi need distress ────────────────────────────
// Detailed design:
//   • 3-zone iris: black pupil → white inner ring → black band → white outer rim
//   • Ring boundaries deformed by a 3-lobe sinusoid that rotates (dizzy clover effect)
//   • 6 short radial impact lines pulse around the iris edge
//   • Blinking × pain marks flanking the iris in the sclera area
//   • Sweat drop below the iris corner
static void drawSickWavyPupil(
    Adafruit_SSD1306& d,
    int16_t irisX, int16_t irisY, uint8_t irisRadius,
    int16_t cx, int16_t cy,
    const int16_t* upper, const int16_t* lower,
    float cosA, float sinA,
    const EyePose& /*pose*/, uint32_t nowMs, uint8_t /*texturePhase*/, bool /*isRightEye*/)
{
    const float R    = fmaxf(5.0f, (float)irisRadius * 0.92f);
    const float t    = (float)nowMs;
    const float rot  = t * 0.0016f;   // slow 3-lobe rotation (full turn ~4 s)
    const float wAmp = R * 0.24f;     // ring-boundary wobble amplitude

    // Zone radii (effective)
    const float pupR = R * 0.27f;    // black center pupil
    const float innR = R * 0.55f;    // white inner ring ends here
    const float outR = R * 0.77f;    // black band ends here; white outer rim beyond

    auto inside = [&](int16_t px, int16_t py) -> bool {
        return (float)(px * px + py * py) <= R * R;
    };

    // 3-lobe wobble on ring boundaries → spinning clover / nausea pattern
    auto shade = [&](int16_t px, int16_t py) -> bool {
        float r     = sqrtf((float)(px * px + py * py));
        float theta = atan2f((float)py, (float)px);
        float wob   = wAmp * sinf(3.0f * theta + rot);
        float reff  = r - wob;          // lobes point outward
        if (reff < pupR) return false;  // black pupil
        if (reff < innR) return true;   // white inner ring
        if (reff < outR) return false;  // black band
        return true;                    // white outer rim
    };

    const int16_t bound = (int16_t)lroundf(R + 1.0f);
    drawRimmedPupil(d, irisX, irisY, bound, cx, cy, cosA, sinA,
                   upper, lower, inside, shade, true);

    // Glint: 2 px highlight top-left of pupil
    int16_t gpY = -(int16_t)lroundf(pupR + 1.0f);
    putOffset(d, irisX, irisY, -2, gpY, cx, cy, cosA, sinA, upper, lower, SSD1306_WHITE);
    putOffset(d, irisX, irisY, -1, gpY, cx, cy, cosA, sinA, upper, lower, SSD1306_WHITE);

    // ── 6 radial impact lines just outside the iris (clipped to eye opening) ──
    // Pulsate: length oscillates between 2 and 5 px
    const float pulseLen = 2.5f + 2.5f * sinf(t * 0.009f);
    for (int16_t i = 0; i < 6; ++i) {
        float angle  = (float)i * (6.28318f / 6.0f) + rot * 0.25f;
        float startR = R + 1.5f;
        float endR   = R + startR + pulseLen;
        auto x0 = (int16_t)lroundf(startR * cosf(angle));
        auto y0 = (int16_t)lroundf(startR * sinf(angle));
        auto x1 = (int16_t)lroundf(endR   * cosf(angle));
        auto y1 = (int16_t)lroundf(endR   * sinf(angle));
        putLine(d, irisX, irisY, cx, cy, cosA, sinA, upper, lower,
                x0, y0, x1, y1, SSD1306_WHITE);
    }

    // ── Blinking × pain marks flanking the iris (clipped via putLine) ───────────
    if (((nowMs / 420u) & 1u) == 0u) {
        auto mx = (int16_t)lroundf(R + 9.0f);  // horizontal offset from iris center
        auto my = -(int16_t)lroundf(R * 0.5f); // vertical offset from iris center
        // left × (negative x side)
        putLine(d, irisX, irisY, cx, cy, cosA, sinA, upper, lower,
                (int16_t)(-mx - 2), (int16_t)(my - 2), (int16_t)(-mx + 2), (int16_t)(my + 2), SSD1306_WHITE);
        putLine(d, irisX, irisY, cx, cy, cosA, sinA, upper, lower,
                (int16_t)(-mx + 2), (int16_t)(my - 2), (int16_t)(-mx - 2), (int16_t)(my + 2), SSD1306_WHITE);
        // right × (positive x side)
        putLine(d, irisX, irisY, cx, cy, cosA, sinA, upper, lower,
                (int16_t)(mx - 2), (int16_t)(my - 2), (int16_t)(mx + 2), (int16_t)(my + 2), SSD1306_WHITE);
        putLine(d, irisX, irisY, cx, cy, cosA, sinA, upper, lower,
                (int16_t)(mx + 2), (int16_t)(my - 2), (int16_t)(mx - 2), (int16_t)(my + 2), SSD1306_WHITE);
    }

    // ── Sweat drop below-right of the iris (clipped via putOffset) ───────────
    auto sdx = (int16_t)lroundf(R * 0.65f);
    auto sdy = (int16_t)lroundf(R + 3.5f);
    putOffset(d, irisX, irisY, sdx,       sdy,       cx, cy, cosA, sinA, upper, lower, SSD1306_WHITE);
    putOffset(d, irisX, irisY, (int16_t)(sdx-1), (int16_t)(sdy-1), cx, cy, cosA, sinA, upper, lower, SSD1306_WHITE);
    putOffset(d, irisX, irisY, (int16_t)(sdx+1), (int16_t)(sdy-1), cx, cy, cosA, sinA, upper, lower, SSD1306_WHITE);
    putOffset(d, irisX, irisY, sdx,       (int16_t)(sdy-2), cx, cy, cosA, sinA, upper, lower, SSD1306_WHITE);
    putOffset(d, irisX, irisY, sdx,       (int16_t)(sdy-3), cx, cy, cosA, sinA, upper, lower, SSD1306_WHITE);
}

AlternatePupilDrawFn eyeStyleSickWavyFn = drawSickWavyPupil;
