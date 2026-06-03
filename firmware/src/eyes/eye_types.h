#pragma once
#include <stdint.h>
#include <Adafruit_SSD1306.h>

// ── Eye window calibration ──────────────────────────────────────────────────
// Screen-space landmarks for the left eye window (right eye is mirrored).
struct EyeWindow {
    int16_t tlX, tlY;  // top-left corner of visible area
    int16_t brX, brY;  // bottom-right corner
    int16_t bmX, bmY;  // bottom-mid control point (lower arc vertex)
};

// ── Expression state ─────────────────────────────────────────────────────────
enum class EyeExpression : uint8_t {
    Sleep,     // eyes closed / slit
    HalfOpen,  // drowsy / transitioning
    Awake,     // normal open
    Hearts,    // tamagotchi success
    Angry,     // raised inner brows
    Text,      // scrolling message mode
};

// ── Per-frame eye pose ────────────────────────────────────────────────────────
struct EyePose {
    float openness;    // 0.0 (closed) → 1.0 (wide open)
    float gazeX;       // -1.0 (left) → 1.0 (right)
    float gazeY;       // -1.0 (up) → 1.0 (down)
    float browLift;    // 0.0 (neutral) → 1.0 (raised)
    float browTilt;    // negative = angry inner brow
    float innerPinch;  // 0.0 neutral → 1.0 inner corners pinched toward center
    float lowerLift;   // 0.0 neutral → 1.0 lower lid lifted
    float irisScale;   // 1.0 = normal iris size
    float sparkle;     // 0.0 = none, 1.0 = bright highlight
};

inline EyePose eyePoseDefault() {
    return {0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.8f};
}

// ── Eye geometry constants ────────────────────────────────────────────────────
// Number of samples across the eye opening (must be odd for symmetric centering)
static constexpr int kEyeSpan    = 181;
// Symmetric half-width (samples on each side of center)
static constexpr int kEyeHalfWidth = 90;
// Number of Catmull-Rom control nodes for upper/lower/brow curves
static constexpr int kCurveNodes = 9;
// Base iris radius in local pixels
static constexpr int kIrisRadius = 8;
// Openness at which the eye shape exactly matches the calibrated landmarks
static constexpr float kOpennessAtFullLandmark = 0.80f;
// Closed slit = this fraction of the calibrated opening height
static constexpr float kClosedSlitFrac = 0.07f;
// Local drop (px) of upper eyelid outside the plateau — side "high" corners
static constexpr float kUpperSideDropLocal = 26.0f;
// Physical tilt angle of the PCB eye cutout (degrees)
static constexpr float kEyeTiltDegrees = 26.0f;

// ── EyeUiGeom — pre-computed geometry for one eye frame ──────────────────────
// Filled by eyeGeomCompute(); consumed by games and the renderer.
struct EyeUiGeom {
    int16_t cx, cy;              // screen-space center of this eye
    float   cosA, sinA;          // rotation for this eye (±kEyeTiltDegrees)
    int16_t upper[kEyeSpan];     // local Y of upper eyelid at each span index
    int16_t lower[kEyeSpan];     // local Y of lower eyelid at each span index
    int16_t brow[kEyeSpan];      // local Y of brow at each span index
    int16_t opening;             // lower[mid] - upper[mid] — eye open height
};

// ── AlternatePupilDrawFn — custom pupil drawing callback ─────────────────────
// Signature for all alternate pupil implementations (eye_styles.cpp).
// Called from drawAlternateFramedEye() when a custom style is selected.
using AlternatePupilDrawFn = void (*)(
    Adafruit_SSD1306& display,
    int16_t irisX, int16_t irisY, uint8_t irisRadius,
    int16_t cx, int16_t cy,
    const int16_t* upper, const int16_t* lower,
    float cosA, float sinA,
    const EyePose& pose,
    uint32_t nowMs, uint8_t texturePhase, bool isRightEye
);
