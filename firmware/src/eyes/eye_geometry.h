#pragma once
#include "eye_types.h"
#include <stdint.h>
#include <Adafruit_SSD1306.h>

// ── Core geometry API ────────────────────────────────────────────────────────
// Builds the upper, lower, and brow eyelid curves for one eye at a given pose.
// upper/lower/brow must point to arrays of kEyeSpan int16_t values.
// The values are in *local* Y coordinates (not screen space).
void eyeGeomBuildCurves(const EyePose& pose, bool isRightEye,
                        int16_t* upper, int16_t* lower, int16_t* brow);

// Fill EyeUiGeom from a pose (does NOT clear or draw the display).
void eyeGeomCompute(const EyePose& pose, bool isRightEye, EyeUiGeom* g);

// ── Span extents ──────────────────────────────────────────────────────────────
// Dynamic asymmetric half-widths so screen x=0 and x=127 map onto the curve.
// Updated every time buildEyeCurves is called.
extern float gEyeSpanNegW;      // left half-width  (positive)
extern float gEyeSpanPosW;      // right half-width (positive)
extern float gUpperSideDropLocal; // runtime-tunable kUpperSideDropLocal (default 26)

// Local X coordinate for curve index i (0 → kEyeSpan-1).
float eyeLocalXFromIndex(uint8_t i);

// ── Coordinate transforms ─────────────────────────────────────────────────────
// Rotate a local point to screen space for one eye.
void eyeGeomLocalToScreen(float lx, float ly, float cosA, float sinA,
                          int16_t cx, int16_t cy,
                          int16_t* sx, int16_t* sy);

// Convert a screen pixel to local eye coordinates.
void eyeGeomScreenToLocal(const EyeUiGeom* g, int16_t sx, int16_t sy,
                          float* lx, float* ly);

// ── Curve interpolation ───────────────────────────────────────────────────────
int16_t eyeGeomInterpolateAtLocalX(float lxTarget, const int16_t* curveY);

// Returns [lxMin, lxMax] of the span.
void eyeGeomGetLocalXExtents(float* lxMin, float* lxMax);

// ── Drawing helpers ───────────────────────────────────────────────────────────
// Draw thick polyline for one eyelid/brow curve (rotated into screen space).
void eyeGeomDrawCurve(Adafruit_SSD1306& d, const EyeUiGeom* g,
                      const int16_t* curve, uint8_t thickness, uint16_t color);

// Fill the sclera region (between upper and lower) with a solid color.
void eyeGeomFillSclera(Adafruit_SSD1306& d, const EyeUiGeom* g, uint16_t color);

// ── EyeUi helpers (used by games and menu) ───────────────────────────────────

// Returns true if screen pixel (sx, sy) is inside the eye opening.
// margin shrinks the region from all edges.
bool eyeUiPointInsideEyeOpening(const EyeUiGeom* g, int16_t sx, int16_t sy,
                                int16_t margin = 0);

// Curve interpolation (wrapper kept for PoC API compatibility).
int16_t eyeUiInterpolateCurveAtLocalX(float lxTarget, const int16_t* curveY);

// Faceted lower-eyelid Y at a given local X (used by breakout for ball bouncing).
// segmentCount ≥ 2 uses cosine-spaced chord approximation.
int16_t eyeUiLowerEyelidFacetedYAtLocalX(const EyeUiGeom* g, float lx,
                                         uint8_t segmentCount);

// Screen endpoints of the lower-eyelid chord that contains lx.
void eyeUiLowerLidFacetedSegmentScreen(const EyeUiGeom* g, float lx,
                                       uint8_t segmentCount,
                                       int16_t* sx0, int16_t* sy0,
                                       int16_t* sx1, int16_t* sy1);

// Upper paddle (breakout): segment centered at centerLx with halfLenLocal
// half-length, offset yInset pixels below the upper lid.
void eyeUiUpperPaddleSegmentScreen(const EyeUiGeom* g,
                                   float centerLx, float halfLenLocal, float yInset,
                                   int16_t* x0, int16_t* y0,
                                   int16_t* x1, int16_t* y1);

void eyeUiDrawUpperPaddle(Adafruit_SSD1306& d, const EyeUiGeom* g,
                          float centerLx, float halfLenLocal, float yInset);

// ── EyeUi frame setup (called by games before drawing) ───────────────────────
// Fills g, clears display, draws outline. Returns false if eye too closed.
// eyeUiBeginFrame uses makeMenuEyePose() from eye_animation (linked separately).
bool eyeUiBeginFrame(Adafruit_SSD1306& display, bool isRightEye,
                     uint32_t nowMs, EyeUiGeom* g);

// Same but draws only lower lid + brow (upper lid = paddle, used by breakout).
bool eyeUiBeginFrameNoUpper(Adafruit_SSD1306& display, bool isRightEye,
                             uint32_t nowMs, EyeUiGeom* g,
                             uint8_t lowerEyelidFacets = 0);

// Fill g with menu pose geometry (without clearing/drawing anything).
void eyeUiComputeGeomForMenu(bool isRightEye, uint32_t nowMs, EyeUiGeom* g);

// Draw just the outline (upper + lower + brow curves) into display.
void eyeUiDrawOutline(Adafruit_SSD1306& display, bool isRightEye, const EyeUiGeom* g);

// ── In-eye text helpers ───────────────────────────────────────────────────────
// Draw text centred horizontally with optional X nudge (for menu labels).
void eyeUiDrawTextCenteredNudged(Adafruit_SSD1306& d, int16_t y, uint8_t textSize,
                                 const char* txt, int16_t nudgeX = 0);

// Render a keyword rotated to match the eye tilt (uses GFXcanvas1 off-screen).
void eyeUiDrawRotatedKeyword(Adafruit_SSD1306& d, const EyeUiGeom* g, const char* word);

// ── putScreenOffset — pixel helper used by eye_styles ────────────────────────
// Draw a pixel at (irisX+dx, irisY+dy) after clipping to the eye opening.
void eyeGeomPutScreenOffset(Adafruit_SSD1306& d, const EyeUiGeom* g,
                             int16_t irisX, int16_t irisY,
                             int16_t dx, int16_t dy, uint16_t color = SSD1306_WHITE);
