#pragma once
#include "eye_types.h"
#include <Adafruit_SSD1306.h>

// Render one eye onto a display.
// mirrorX: true for the right eye (symmetrical face)
// scrollText: null for normal eye; non-null activates scrolling text mode
void eyeDraw(Adafruit_SSD1306& display, const EyeWindow& cal,
             const EyePose& pose, EyeExpression expr,
             bool mirrorX, const char* scrollText = nullptr);

// Convenience: draw both eyes in one call (uses gLeftDisplay, gRightDisplay, gEyeCal).
void eyesDrawBoth(const EyePose& pose, EyeExpression expr,
                  const char* scrollText = nullptr);

// ── Style renderers — called by eye_catalog to render specific styles ─────────

// Standard style: white sclera + iris texture (or scrolling text overlay).
void drawStandardStyleEye(Adafruit_SSD1306& display,
                          const EyePose& pose, bool isRightEye,
                          uint32_t nowMs, const char* scrollText = nullptr);

// Alternate framed style: black background + custom pupil draw function.
// Falls back to scroll text when scrollText is non-null and non-empty.
void drawAlternateFramedEye(Adafruit_SSD1306& display,
                             const EyePose& pose, bool isRightEye,
                             uint32_t nowMs, AlternatePupilDrawFn drawPupil,
                             const char* scrollText = nullptr);

// Matrix rain style: procedural 1-bit matrix rain fills the sclera region.
void drawAlternateMatrixEye(Adafruit_SSD1306& display,
                             const EyePose& pose, bool isRightEye,
                             uint32_t nowMs, const char* scrollText = nullptr);
