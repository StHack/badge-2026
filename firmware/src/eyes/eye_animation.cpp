#include "eye_animation.h"
#include "../config/timing.h"
#include <math.h>
#include <string.h>

static EyeAnimMode sMode        = EyeAnimMode::FollowBadgeState;
static char        sScrollText[128] = {};
static uint32_t    sModeStartMs = 0;

// ── Blink scheduler ────────────────────────────────────────────────────────────
static bool        sBlinkEnabled = false; // off by default
static uint32_t    sBlinkNextMs  = 0;   // scheduled time of next blink (0 = not yet set)
static uint32_t    sBlinkStartMs = 0;   // start time of current blink  (0 = not blinking)
static constexpr uint32_t kBlinkDurMs  = 140;   // total duration of one blink
static constexpr uint32_t kBlinkMinMs  = 2500;  // minimum gap between blinks
static constexpr uint32_t kBlinkRangeMs = 4500; // random range on top of minimum

static uint32_t blinkLcg() {
    static uint32_t sSeed = 0xA3F201u;
    sSeed = sSeed * 1664525u + 1013904223u;
    return sSeed;
}

static void blinkScheduleNext(uint32_t nowMs) {
    sBlinkNextMs = nowMs + kBlinkMinMs + (blinkLcg() % kBlinkRangeMs);
}

// Applies a voluntary blink on top of an already-computed awake pose.
static void applyBlink(EyePose& pose, uint32_t nowMs) {
    if (sBlinkNextMs == 0) { blinkScheduleNext(nowMs); return; }
    if (sBlinkStartMs == 0 && nowMs >= sBlinkNextMs) sBlinkStartMs = nowMs;
    if (sBlinkStartMs == 0) return;

    uint32_t elapsed = nowMs - sBlinkStartMs;
    if (elapsed < kBlinkDurMs) {
        float t = (float)elapsed / (float)kBlinkDurMs;
        float close = sinf(t * 3.14159f);   // 0 → 1 → 0 arc
        pose.openness   *= (1.0f - close * 0.97f);
        pose.lowerLift  += close * 0.10f;
        pose.browLift   -= close * 0.04f;
    } else {
        sBlinkStartMs = 0;
        blinkScheduleNext(nowMs);
    }
}

// ── Math helpers ──────────────────────────────────────────────────────────────

static inline float clampUnit(float v) { return v < 0.f ? 0.f : v > 1.f ? 1.f : v; }
static inline float lerp(float a, float b, float t) { return a + (b - a) * t; }

// quarticPulse(v) = max(0,v)^4
static inline float quarticPulse(float v) {
    if (v <= 0.f) return 0.f;
    float v2 = v * v;
    return v2 * v2;
}

// octicPulse(v) = quarticPulse(v)^2
static inline float octicPulse(float v) {
    float q = quarticPulse(v);
    return q * q;
}

// ── Pose generators ───────────────────────────────────────────────────────────

EyePose makeClosedPose(uint32_t nowMs) {
    EyePose pose{};
    float t = nowMs * 0.001f;
    pose.openness    = 0.03f + 0.01f * sinf(t * 1.1f);
    pose.gazeX       = 0.0f;
    pose.gazeY       = 0.08f;
    pose.browLift    = -0.12f;
    pose.browTilt    = 0.60f;
    pose.innerPinch  = 0.66f;
    pose.lowerLift   = 0.40f;
    pose.irisScale   = 0.55f;
    pose.sparkle     = 0.0f;
    return pose;
}

EyePose makeAwakePose(uint32_t nowMs, float alertness) {
    EyePose pose{};
    float t     = nowMs * 0.001f;
    float alert = clampUnit(alertness);

    float drift      = 0.5f + 0.5f * sinf(t * 0.42f + 0.6f);
    float scan       = 0.46f * sinf(t * 0.38f) + 0.22f * sinf(t * 0.94f + 1.1f);
    float snap       = alert * quarticPulse(sinf(t * 2.2f + 0.4f));
    float microBlink = 0.22f * octicPulse(sinf(t * 1.65f + 0.3f))
                     + 0.10f * octicPulse(sinf(t * 4.8f  + 1.2f));
    float flare      = alert * (0.5f + 0.5f * sinf(t * 4.8f + 0.7f));
    float widen      = 0.5f + 0.5f * sinf(t * 2.8f + 0.2f);
    float flutter    = 0.5f + 0.5f * sinf(t * 7.2f + 2.1f);
    float browBounce = 0.5f + 0.5f * sinf(t * 3.3f + 0.5f);
    float wideStare  = fmaxf(octicPulse(sinf(t * 0.92f + 0.8f)),
                             0.85f * octicPulse(sinf(t * 0.53f + 2.4f)));
    float sideLook   = fmaxf(quarticPulse(sinf(t * 0.33f + 0.5f)),
                             0.92f * quarticPulse(sinf(t * 0.24f + 2.0f)));
    float sideLookDir = sinf(t * 0.19f + 1.7f) >= 0.0f ? 1.0f : -1.0f;
    float sideLookX  = sideLookDir * sideLook * (0.64f + alert * 0.20f);
    float sideLookY  = sideLook * (-0.04f + 0.05f * sinf(t * 0.73f + 0.4f));
    float settle     = 0.5f + 0.5f * sinf(t * 1.4f + 2.6f);

    pose.openness = clampUnit(
        0.84f + 0.07f * sinf(t * 1.15f) + 0.04f * sinf(t * 3.8f + 0.2f)
        + alert * (0.10f + flare * 0.06f + widen * 0.05f)
        + wideStare * (0.11f + alert * 0.08f)
        - microBlink * (0.12f + alert * 0.12f));

    pose.gazeX = scan
        + 0.15f * sinf(t * 1.9f + 0.4f)
        + 0.05f * sinf(t * 4.6f + 1.3f)
        + sideLookX
        + alert * (0.20f * sinf(t * 2.1f + 0.8f)
                 + 0.10f * sinf(t * 4.1f + 1.9f) * snap);

    pose.gazeY = -0.05f
        + 0.12f * sinf(t * 0.38f + 0.9f)
        + 0.03f * sinf(t * 2.1f  + 2.4f)
        + sideLookY
        + alert * (-0.05f + 0.07f * sinf(t * 1.5f + 1.4f));

    pose.browLift = 0.11f
        + drift * 0.04f
        + 0.03f * sinf(t * 2.6f + 0.7f)
        + alert * (0.08f + flare * 0.07f + browBounce * 0.04f)
        + wideStare * (0.08f + alert * 0.06f)
        - microBlink * 0.06f;

    pose.browTilt = 0.67f + 0.02f * sinf(t * 1.1f + 0.5f) + alert * 0.06f;

    pose.innerPinch = 0.17f
        + 0.05f * sinf(t * 0.83f + 2.2f)
        - alert * (0.04f * flare + 0.03f * widen)
        + microBlink * 0.03f;

    pose.lowerLift = 0.11f
        + 0.03f * sinf(t * 2.2f + 0.4f)
        + alert * (0.05f * flutter)
        + microBlink * 0.07f;

    pose.irisScale = 0.99f
        + drift * 0.04f
        + 0.03f * sinf(t * 2.6f + 0.2f)
        + alert * (0.06f + flare * 0.04f)
        + wideStare * 0.05f;

    pose.sparkle = clampUnit(
        0.70f + drift * 0.12f + flutter * 0.08f + sideLook * 0.06f
        + alert * (0.12f + flare * 0.10f)
        + settle * 0.04f);

    return pose;
}

EyePose makeMenuEyePose(uint32_t nowMs) {
    EyePose p = makeAwakePose(nowMs, 0.22f);
    p.openness = clampUnit(0.88f);
    p.gazeX    = 0.0f;
    p.gazeY    = 0.0f;
    p.sparkle  = 0.12f;
    return p;
}

EyePose lerpPose(const EyePose& a, const EyePose& b, float t) {
    EyePose r;
    r.openness   = lerp(a.openness,   b.openness,   t);
    r.gazeX      = lerp(a.gazeX,      b.gazeX,      t);
    r.gazeY      = lerp(a.gazeY,      b.gazeY,      t);
    r.browLift   = lerp(a.browLift,   b.browLift,   t);
    r.browTilt   = lerp(a.browTilt,   b.browTilt,   t);
    r.innerPinch = lerp(a.innerPinch, b.innerPinch, t);
    r.lowerLift  = lerp(a.lowerLift,  b.lowerLift,  t);
    r.irisScale  = lerp(a.irisScale,  b.irisScale,  t);
    r.sparkle    = lerp(a.sparkle,    b.sparkle,    t);
    return r;
}

// ── Public API ────────────────────────────────────────────────────────────────

void eyeAnimSetMode(EyeAnimMode mode, const char* text) {
    sMode        = mode;
    sModeStartMs = 0;  // will be initialised on first computePose call
    sBlinkNextMs  = 0;
    sBlinkStartMs = 0;
    if (text) {
        strncpy(sScrollText, text, sizeof(sScrollText) - 1);
        sScrollText[sizeof(sScrollText) - 1] = '\0';
    } else {
        sScrollText[0] = '\0';
    }
}

EyeAnimMode eyeAnimGetMode() { return sMode; }

void eyeAnimSetBlinkEnabled(bool enabled) {
    sBlinkEnabled = enabled;
    if (!enabled) { sBlinkNextMs = 0; sBlinkStartMs = 0; }
}

bool eyeAnimGetBlinkEnabled() { return sBlinkEnabled; }

EyePose eyeAnimComputePose(uint32_t nowMs) {
    if (sModeStartMs == 0) sModeStartMs = nowMs;
    float elapsed = (float)(nowMs - sModeStartMs);

    switch (sMode) {

        case EyeAnimMode::ForceClosed:
            return makeClosedPose(nowMs);

        // ── alert modes: smooth open ramp from mode entry ──────────────────
        case EyeAnimMode::ForceHearts:
            return makeAwakePose(nowMs, 0.7f);

        case EyeAnimMode::ForceAwake:
        case EyeAnimMode::FollowBadgeState: {
            EyePose p = makeAwakePose(nowMs, 0.7f);
            if (elapsed < 400.f) {
                p.openness *= elapsed / 400.f;
            } else if (sBlinkEnabled) {
                applyBlink(p, nowMs);
            }
            return p;
        }

        // ── sleepy: heavy-lidded, slow gaze ───────────────────────────────
        case EyeAnimMode::ForceSleepy: {
            EyePose p = makeAwakePose(nowMs, 0.11f);
            p.openness *= 0.78f;
            p.gazeX   *= 0.30f;
            p.gazeY   *= 0.30f;
            return p;
        }

        // ── closing: animated lerp from awake to closed over kPeekClosingMs ──
        case EyeAnimMode::ForceClosing: {
            float t = clampUnit(elapsed / (float)kPeekClosingMs);
            float smoothT = t * t * (3.f - 2.f * t);  // smoothstep
            EyePose awake  = makeAwakePose(nowMs, 0.11f);
            EyePose closed = makeClosedPose(nowMs);
            return lerpPose(awake, closed, smoothT);
        }

        // ── scroll text: boosted openness so text is visible ──────────────
        case EyeAnimMode::ForceScrollText: {
            EyePose p = makeAwakePose(nowMs, 0.5f);
            p.openness = clampUnit(p.openness + 0.12f);
            return p;
        }

        // ── periodic blink: 3200 ms period, 130 ms shut window ────────────
        case EyeAnimMode::ForceBlinking: {
            const uint32_t kBlinkPeriodMs = 3200;
            const uint32_t kBlinkShutMs   = 130;
            uint32_t phase = nowMs % kBlinkPeriodMs;
            EyePose awake  = makeAwakePose(nowMs, 0.7f);
            if (elapsed < 400.f) awake.openness *= elapsed / 400.f;
            if (phase < kBlinkShutMs) {
                EyePose closed = makeClosedPose(nowMs);
                float t = (float)phase / (float)kBlinkShutMs;
                // ease in + ease out: blink goes closed at mid-point
                float blinkT = (phase < kBlinkShutMs / 2)
                    ? (2.f * t * t)
                    : (1.f - 2.f * (1.f - t) * (1.f - t));
                return lerpPose(awake, closed, blinkT);
            }
            return awake;
        }

        // ── calm: near-neutral, minimal gaze movement ─────────────────────
        case EyeAnimMode::ForceCalm: {
            EyePose p = makeAwakePose(nowMs, 0.22f);
            p.gazeX *= 0.07f;
            p.gazeY *= 0.07f;
            return p;
        }

        // ── sick: heavy drooping lids, trembling gaze, detailed nausea pupils ──
        case EyeAnimMode::ForceSick: {
            float t = (float)nowMs;
            EyePose p = makeAwakePose(nowMs, 0.25f);
            // Slow irregular trembling openness (drooping but still visible)
            p.openness  = 0.53f + 0.05f * sinf(t * 0.007f)
                                + 0.02f * sinf(t * 0.031f + 1.3f);
            // Sluggish wandering gaze
            p.gazeX    += 0.12f * sinf(t * 0.017f)
                        + 0.05f * sinf(t * 0.041f + 0.7f);
            p.gazeY    += 0.10f * sinf(t * 0.013f + 0.9f)
                        + 0.04f * sinf(t * 0.037f + 2.1f);
            // Heavy lower lid (bags under the eyes)
            p.lowerLift = 0.44f + 0.05f * sinf(t * 0.009f);
            // Trembling worried brow
            p.browTilt  = -0.22f + 0.05f * sinf(t * 0.011f + 0.5f);
            p.sparkle   = 0.0f;
            p.irisScale = 0.80f;
            // No ramp-up: sick state must appear immediately (ramp caused
            // openness=0 on first frame, visually identical to closed eyes).
            return p;
        }

        default:
            return makeAwakePose(nowMs, 0.7f);
    }
}

EyeExpression eyeAnimComputeExpression(uint32_t nowMs) {
    switch (sMode) {
        case EyeAnimMode::ForceHearts:
            return EyeExpression::Hearts;
        case EyeAnimMode::ForceScrollText:
            return EyeExpression::Text;
        case EyeAnimMode::ForceSleepy:
        case EyeAnimMode::ForceClosing:
        case EyeAnimMode::ForceClosed:
            return EyeExpression::Sleep;
        case EyeAnimMode::ForceBlinking: {
            // Sleep expression during the blink window
            const uint32_t kBlinkPeriodMs = 3200;
            const uint32_t kBlinkShutMs   = 130;
            uint32_t phase = nowMs % kBlinkPeriodMs;
            return (phase < kBlinkShutMs) ? EyeExpression::Sleep : EyeExpression::Awake;
        }
        default:
            return EyeExpression::Awake;
    }
}

const char* eyeAnimScrollText() {
    if (sMode == EyeAnimMode::ForceScrollText) return sScrollText;
    return nullptr;
}
