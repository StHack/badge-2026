#include "eye_renderer.h"
#include "eye_geometry.h"
#include "eye_animation.h"
#include "eye_catalog.h"
#include "eye_calibration.h"
#include "../hal/display.h"
#include "../config/hardware.h"
#include <Arduino.h>
#include <math.h>
#include <string.h>

// ── Thick-line helper ─────────────────────────────────────────────────────────
// Available for eye_catalog / style draw helpers that need it directly.
[[maybe_unused]]
static void drawThickLine(Adafruit_SSD1306& d,
                          int16_t x0, int16_t y0, int16_t x1, int16_t y1,
                          uint8_t thickness, uint16_t color) {
    int8_t half = (int8_t)(thickness / 2);
    for (int8_t off = -half; off <= half; ++off)
        d.drawLine(x0, y0 + off, x1, y1 + off, color);
}

// ── Pixel-in-eye test (local coordinates) ────────────────────────────────────
// Returns true when the local pixel (localX, localY) is strictly inside the
// eye opening described by the upper/lower curve arrays.
static bool pixelInsideEyeLocal(int16_t localX, int16_t localY,
                                const int16_t* upper, const int16_t* lower) {
    float span = gEyeSpanNegW + gEyeSpanPosW;
    if (span < 1e-4f) return false;
    float tIdx = ((float)localX + gEyeSpanNegW) / span * (float)(kEyeSpan - 1);
    int   idx  = (int)lroundf(tIdx);
    if (idx < 0 || idx >= kEyeSpan) return false;
    return localY > upper[idx] && localY < lower[idx];
}

// ── Scrolling / wake intro text ───────────────────────────────────────────────
// Draws msg scrolling horizontally across the eye interior.
// phaseMs is the time offset used to advance the scroll position.
// Ported from badge_core.cpp drawWakeIntroText (lines 2398-2419).
static void drawWakeIntroText(Adafruit_SSD1306& display,
                              const char* msg, uint32_t phaseMs,
                              int16_t cx, int16_t cy, uint16_t textColor) {
    if (msg == nullptr || msg[0] == '\0') return;

    const uint32_t kScrollPeriodMs = 4000; // one full pass every 4 s
    const uint32_t phase    = phaseMs % kScrollPeriodMs;
    const int16_t  textWidth = (int16_t)(strlen(msg) * 12);  // textSize=2 → 12 px/char
    const int16_t  travel   = (int16_t)lroundf(gEyeSpanNegW + gEyeSpanPosW + 8.0f)
                            + textWidth + 8;
    const int16_t  scroll   = (int16_t)((phase * (uint32_t)travel) / kScrollPeriodMs);
    const int16_t  localTextX = -textWidth / 2 + (travel / 2 - scroll);
    const int16_t  localTextY = -1;

    display.setTextSize(2);
    display.setTextWrap(false);
    display.setTextColor(textColor);
    display.setCursor((int16_t)(cx + localTextX), (int16_t)(cy + localTextY));
    display.print(msg);
}

// ── Matrix rain fill ──────────────────────────────────────────────────────────
// Returns true when screen pixel (sx, sy) should be lit for a matrix rain effect.
// Ported from badge_core.cpp matrixRainScreenBright (lines 2560-2588).
static bool matrixRainScreenBright(int16_t sx, int16_t sy,
                                   uint32_t nowMs, uint8_t tex) {
    const uint32_t colSeed =
        ((uint32_t)((uint16_t)sx + 1u) * 2654435761u)
        ^ ((uint32_t)tex * 1597334677u)
        ^ 0xA5297A4Du;
    const int32_t t = (int32_t)nowMs;

    const int32_t msPerStep = 42 + (int32_t)(colSeed % 23u);
    const int32_t phase     = (int32_t)((colSeed >> 6) & 63u);
    const int32_t trailLen  = 5  + (int32_t)((colSeed >> 12) & 11u);
    const int32_t gap       = 12 + (int32_t)((colSeed >> 18) & 21u);
    const int32_t cycle     = (int32_t)kDisplayHeight + trailLen + gap;

    const int32_t yHead = ((t / msPerStep) + phase) % cycle;
    const int32_t d     = yHead - (int32_t)sy;

    if (d == 0) return true;

    if (d > 0 && d <= trailLen) {
        const uint32_t h =
            (uint32_t)((int32_t)sx  * (int32_t)2246822519u
                     ^ (int32_t)sy  * (int32_t)3266489917u
                     ^ (t >> 4)     * (int32_t)668265263u
                     ^ d            * (int32_t)374761393u);
        const int32_t nearHead = trailLen - d;
        const uint32_t thresh  = 3u + (uint32_t)((nearHead * 11 + trailLen - 1)
                                                 / (uint32_t)trailLen);
        return (h & 15u) < (thresh < 14u ? thresh : 14u);
    }
    return false;
}

// ── Iris texture ──────────────────────────────────────────────────────────────
// Draws the black iris disc with speckle texture and sparkle highlights,
// clipped to the eye opening.  Ported from badge_core.cpp (lines 2195-2313).
static void drawIrisTexture(Adafruit_SSD1306& display,
                            int16_t irisX, int16_t irisY, uint8_t irisRadius,
                            int16_t cx, int16_t cy,
                            const int16_t* upper, const int16_t* lower,
                            float cosA, float sinA,
                            uint8_t texturePhase, float sparkle) {
    static const int8_t kSpeckles[][2] = {
        {-5, -2}, {-3, -4}, {-1, -1}, { 2, -3}, { 4, -1},
        {-4,  2}, {-1,  3}, { 2,  1}, { 4,  3}, {-2,  0},
    };
    const uint8_t kSpeckleCount = (uint8_t)(sizeof(kSpeckles) / sizeof(kSpeckles[0]));

    // ── Fill iris disc (black pixels, clipped to eye opening) ────────────
    for (int16_t dx = -irisRadius; dx <= irisRadius; ++dx) {
        int16_t rem = (int16_t)(irisRadius * irisRadius - dx * dx);
        if (rem < 0) continue;
        int16_t arcH = (int16_t)floorf(sqrtf((float)rem));
        for (int16_t dy = -arcH; dy <= arcH; ++dy) {
            int16_t lx = (int16_t)(irisX - cx + dx);
            int16_t ly = (int16_t)(irisY - cy + dy);
            if (!pixelInsideEyeLocal(lx, ly, upper, lower)) continue;
            int16_t sx, sy;
            eyeGeomLocalToScreen((float)lx, (float)ly, cosA, sinA, cx, cy, &sx, &sy);
            display.drawPixel(sx, sy, SSD1306_BLACK);
        }
    }

    // ── Speckle highlights (white dots inside iris) ───────────────────────
    for (uint8_t i = 0; i < kSpeckleCount; ++i) {
        uint8_t  index  = (i + texturePhase) % kSpeckleCount;
        int16_t  lx     = (int16_t)(irisX - cx + kSpeckles[index][0]);
        int16_t  ly     = (int16_t)(irisY - cy + kSpeckles[index][1]);
        int16_t  ddx    = lx - (irisX - cx);
        int16_t  ddy    = ly - (irisY - cy);
        int16_t  rlim   = (int16_t)(irisRadius - 1);
        if (ddx * ddx + ddy * ddy < rlim * rlim
            && pixelInsideEyeLocal(lx, ly, upper, lower)) {
            int16_t sx, sy;
            eyeGeomLocalToScreen((float)lx, (float)ly, cosA, sinA, cx, cy, &sx, &sy);
            display.drawPixel(sx, sy, SSD1306_WHITE);
        }
    }

    // ── Glint row (top of iris) ────────────────────────────────────────────
    uint8_t glintSize = sparkle > 0.6f ? 3 : (sparkle > 0.2f ? 2 : 1);
    for (uint8_t i = 0; i < glintSize; ++i) {
        int16_t lx = (int16_t)(irisX - cx - 3 + (int16_t)i);
        int16_t ly = (int16_t)(irisY - cy - irisRadius / 2 - (i == 0 ? 0 : 1));
        if (pixelInsideEyeLocal(lx, ly, upper, lower)) {
            int16_t sx, sy;
            eyeGeomLocalToScreen((float)lx, (float)ly, cosA, sinA, cx, cy, &sx, &sy);
            display.drawPixel(sx, sy, SSD1306_WHITE);
        }
    }

    // ── Secondary glint (medium sparkle) ──────────────────────────────────
    if (sparkle > 0.45f) {
        int16_t lx = (int16_t)(irisX - cx + irisRadius / 3);
        int16_t ly = (int16_t)(irisY - cy - irisRadius / 3 + ((texturePhase & 1u) ? 0 : 1));
        if (pixelInsideEyeLocal(lx, ly, upper, lower)) {
            int16_t sx, sy;
            eyeGeomLocalToScreen((float)lx, (float)ly, cosA, sinA, cx, cy, &sx, &sy);
            display.drawPixel(sx, sy, SSD1306_WHITE);
        }
    }

    // ── Tertiary glint (high sparkle) ─────────────────────────────────────
    if (sparkle > 0.75f) {
        int16_t lx = (int16_t)(irisX - cx + irisRadius / 2 - 1);
        int16_t ly = (int16_t)(irisY - cy - irisRadius / 2);
        if (pixelInsideEyeLocal(lx, ly, upper, lower)) {
            int16_t sx, sy;
            eyeGeomLocalToScreen((float)lx, (float)ly, cosA, sinA, cx, cy, &sx, &sy);
            display.drawPixel(sx, sy, SSD1306_WHITE);
        }
    }

    // ── Star sparkles ─────────────────────────────────────────────────────
    uint8_t starCount = sparkle > 0.75f ? 2 : (sparkle > 0.45f ? 1 : 0);
    for (uint8_t i = 0; i < starCount; ++i) {
        int8_t  phaseOff = (int8_t)((texturePhase + i * 3) % 6);
        int16_t lx = (int16_t)(irisX - cx + (i == 0 ? -1 : 2) + ((phaseOff & 1) ? 1 : 0));
        int16_t ly = (int16_t)(irisY - cy + (i == 0 ? -1 : 1) - ((phaseOff > 2) ? 1 : 0));

        if (!pixelInsideEyeLocal(lx, ly, upper, lower)) continue;

        int16_t sx, sy;
        eyeGeomLocalToScreen((float)lx, (float)ly, cosA, sinA, cx, cy, &sx, &sy);
        display.drawPixel(sx, sy, SSD1306_WHITE);

        static const int16_t kNeighbors[][2] = {
            {-1,  0}, { 1,  0}, { 0, -1}, { 0,  1},
            {-1, -1}, { 1, -1}, {-1,  1}, { 1,  1},
        };
        uint8_t nCount = sparkle > 0.6f ? 8 : 4;
        for (uint8_t n = 0; n < nCount; ++n) {
            int16_t nx = (int16_t)(lx + kNeighbors[n][0]);
            int16_t ny = (int16_t)(ly + kNeighbors[n][1]);
            if (!pixelInsideEyeLocal(nx, ny, upper, lower)) continue;
            eyeGeomLocalToScreen((float)nx, (float)ny, cosA, sinA, cx, cy, &sx, &sy);
            display.drawPixel(sx, sy, SSD1306_WHITE);
        }
    }
}

// ── Standard style eye ────────────────────────────────────────────────────────
// White sclera + iris texture (or scrolling text overlay).
// Ported / simplified from badge_core.cpp drawStandardStyleEye (lines 2421-2494).
void drawStandardStyleEye(Adafruit_SSD1306& display,
                          const EyePose& pose, bool isRightEye,
                          uint32_t nowMs, const char* scrollText) {
    const int16_t cx       = kDisplayWidth  / 2;
    const int16_t cy       = kDisplayHeight / 2;
    const int16_t midIndex = kEyeSpan / 2;

    const uint8_t irisRadius = (uint8_t)max(
        5, (int)lroundf(kIrisRadius * pose.irisScale));

    const float eyeAngle = (isRightEye ? -(float)kEyeTiltDegrees
                                        :  (float)kEyeTiltDegrees)
                           * (float)(M_PI / 180.0);
    const float cosA = cosf(eyeAngle);
    const float sinA = sinf(eyeAngle);

    int16_t upper[kEyeSpan];
    int16_t lower[kEyeSpan];
    int16_t brow [kEyeSpan];
    eyeGeomBuildCurves(pose, isRightEye, upper, lower, brow);

    const float irisLocalX = constrain(
        pose.gazeX * 13.5f,
        -gEyeSpanNegW + (float)irisRadius + 6.0f,
         gEyeSpanPosW - (float)irisRadius - 6.0f);
    const float irisLocalY = constrain(pose.gazeY * 7.0f, -18.0f, 18.0f);
    const int16_t irisX = (int16_t)lroundf((float)cx + irisLocalX);
    const int16_t irisY = (int16_t)lroundf((float)cy + irisLocalY);

    const uint8_t texturePhase = (uint8_t)((nowMs / 95u) % 10u);
    const int16_t opening      = lower[midIndex] - upper[midIndex];

    display.clearDisplay();

    if (opening > 3) {
        const bool hasScroll = (scrollText != nullptr && scrollText[0] != '\0');

        // ── Fill white sclera pixel by pixel (rotated) ────────────────────
        for (uint8_t i = 0; i < kEyeSpan; ++i) {
            float localX = eyeLocalXFromIndex(i);
            for (int16_t y = upper[i]; y <= lower[i]; ++y) {
                int16_t sx, sy;
                eyeGeomLocalToScreen(localX, (float)y, cosA, sinA, cx, cy, &sx, &sy);
                display.drawPixel(sx, sy, SSD1306_WHITE);
            }
        }

        if (hasScroll) {
            // Scrolling text rendered in black over the white sclera
            drawWakeIntroText(display, scrollText, nowMs, cx, cy, SSD1306_BLACK);
        } else {
            drawIrisTexture(display, irisX, irisY, irisRadius,
                            cx, cy, upper, lower,
                            cosA, sinA, texturePhase, pose.sparkle);
        }
    }

    // ── Draw outline curves ───────────────────────────────────────────────
    EyeUiGeom g;
    eyeGeomCompute(pose, isRightEye, &g);
    eyeGeomDrawCurve(display, &g, upper, 1, SSD1306_WHITE);
    if (opening > 4) {
        eyeGeomDrawCurve(display, &g, lower, 1, SSD1306_WHITE);
    }

    display.display();
}

// ── Alternate framed eye ──────────────────────────────────────────────────────
// Black background; a custom pupil draw function fills the interior.
// Falls back to scroll text when active.
// Ported / simplified from badge_core.cpp drawAlternateFramedEye (lines 2496-2558).
void drawAlternateFramedEye(Adafruit_SSD1306& display,
                             const EyePose& pose, bool isRightEye,
                             uint32_t nowMs, AlternatePupilDrawFn drawPupil,
                             const char* scrollText) {
    const int16_t cx       = kDisplayWidth  / 2;
    const int16_t cy       = kDisplayHeight / 2;
    const int16_t midIndex = kEyeSpan / 2;

    const uint8_t irisRadius = (uint8_t)max(
        5, (int)lroundf(kIrisRadius * pose.irisScale));

    const float eyeAngle = (isRightEye ? -(float)kEyeTiltDegrees
                                        :  (float)kEyeTiltDegrees)
                           * (float)(M_PI / 180.0);
    const float cosA = cosf(eyeAngle);
    const float sinA = sinf(eyeAngle);

    int16_t upper[kEyeSpan];
    int16_t lower[kEyeSpan];
    int16_t brow [kEyeSpan];
    eyeGeomBuildCurves(pose, isRightEye, upper, lower, brow);

    const float irisLocalX = constrain(
        pose.gazeX * 13.5f,
        -gEyeSpanNegW + (float)irisRadius + 6.0f,
         gEyeSpanPosW - (float)irisRadius - 6.0f);
    const float irisLocalY = constrain(pose.gazeY * 7.0f, -18.0f, 18.0f);
    const int16_t irisX = (int16_t)lroundf((float)cx + irisLocalX);
    const int16_t irisY = (int16_t)lroundf((float)cy + irisLocalY);

    const uint8_t texturePhase = (uint8_t)((nowMs / 95u) % 10u);
    const int16_t opening      = lower[midIndex] - upper[midIndex];

    display.clearDisplay();  // black background

    if (opening > 3) {
        const bool hasScroll = (scrollText != nullptr && scrollText[0] != '\0');

        if (hasScroll) {
            // Scroll text in white on black background
            drawWakeIntroText(display, scrollText, nowMs, cx, cy, SSD1306_WHITE);
        } else if (drawPupil != nullptr) {
            // Pupil is centred slightly below irisY to match original
            int16_t pupilCenterY = (int16_t)(irisY + 10);
            uint8_t pupilRadius  = (uint8_t)max(
                6, (int)lroundf((float)irisRadius * 1.35f));
            drawPupil(display,
                      irisX, pupilCenterY, pupilRadius,
                      cx, cy,
                      upper, lower,
                      cosA, sinA,
                      pose, nowMs, texturePhase, isRightEye);
        }
    }

    // ── Draw outline curves ───────────────────────────────────────────────
    EyeUiGeom g;
    eyeGeomCompute(pose, isRightEye, &g);
    eyeGeomDrawCurve(display, &g, upper, 1, SSD1306_WHITE);
    if (opening > 4) {
        eyeGeomDrawCurve(display, &g, lower, 1, SSD1306_WHITE);
    }

    display.display();
}

// ── Matrix rain eye ───────────────────────────────────────────────────────────
// Fills sclera pixels with procedural 1-bit matrix rain.
// Ported / simplified from badge_core.cpp drawAlternateMatrixEye (lines 2591-2650).
void drawAlternateMatrixEye(Adafruit_SSD1306& display,
                             const EyePose& pose, bool isRightEye,
                             uint32_t nowMs, const char* scrollText) {
    const int16_t cx       = kDisplayWidth  / 2;
    const int16_t cy       = kDisplayHeight / 2;
    const int16_t midIndex = kEyeSpan / 2;

    const float eyeAngle = (isRightEye ? -(float)kEyeTiltDegrees
                                        :  (float)kEyeTiltDegrees)
                           * (float)(M_PI / 180.0);
    const float cosA = cosf(eyeAngle);
    const float sinA = sinf(eyeAngle);

    int16_t upper[kEyeSpan];
    int16_t lower[kEyeSpan];
    int16_t brow [kEyeSpan];
    eyeGeomBuildCurves(pose, isRightEye, upper, lower, brow);

    const uint8_t texturePhase = (uint8_t)((nowMs / 95u) % 10u);
    const int16_t opening      = lower[midIndex] - upper[midIndex];

    display.clearDisplay();

    if (opening > 3) {
        const bool hasScroll = (scrollText != nullptr && scrollText[0] != '\0');

        if (hasScroll) {
            drawWakeIntroText(display, scrollText, nowMs, cx, cy, SSD1306_WHITE);
        } else {
            // Matrix rain: iterate over every span column, light pixels selectively
            for (uint8_t i = 0; i < kEyeSpan; ++i) {
                float localX = eyeLocalXFromIndex(i);
                for (int16_t y = upper[i]; y <= lower[i]; ++y) {
                    int16_t sx, sy;
                    eyeGeomLocalToScreen(localX, (float)y, cosA, sinA, cx, cy, &sx, &sy);
                    if (matrixRainScreenBright(sx, sy, nowMs, texturePhase)) {
                        display.drawPixel(sx, sy, SSD1306_WHITE);
                    }
                }
            }
        }
    }

    // ── Draw outline curves ───────────────────────────────────────────────
    EyeUiGeom g;
    eyeGeomCompute(pose, isRightEye, &g);
    eyeGeomDrawCurve(display, &g, upper, 1, SSD1306_WHITE);
    if (opening > 4) {
        eyeGeomDrawCurve(display, &g, lower, 1, SSD1306_WHITE);
    }

    display.display();
}

// ── Public API ────────────────────────────────────────────────────────────────

void eyeDraw(Adafruit_SSD1306& display, const EyeWindow& /*cal*/,
             const EyePose& pose, EyeExpression expr,
             bool mirrorX, const char* scrollText) {
    // Delegate to the catalog — it owns style dispatch.
    eyeCatalogDraw(display, mirrorX, millis(), pose, expr, scrollText);
}

void eyesDrawBoth(const EyePose& pose, EyeExpression expr, const char* scrollText) {
    eyeDraw(gLeftDisplay,  gEyeCal, pose, expr, false, scrollText);
    eyeDraw(gRightDisplay, gEyeCal, pose, expr, true,  scrollText);
}
