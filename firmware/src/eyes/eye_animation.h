#pragma once
#include "eye_types.h"
#include <stdint.h>

// Controls how the eye animator generates poses each frame.
enum class EyeAnimMode : uint8_t {
    FollowBadgeState,  // pose follows the badge FSM (sleeping/waking/awake)
    ForceClosed,
    ForceAwake,        // always alert, random gaze drift
    ForceSleepy,       // heavy-lidded, slow gaze
    ForceClosing,      // animated close: lerps from awake to closed over kPeekClosingMs
    ForceHearts,       // heart eyes (tamagotchi success)
    ForceScrollText,   // text mode — expression set to Text
    ForceBlinking,     // normal-open with periodic blink
    ForceCalm,         // near-neutral, minimal gaze movement
    ForceSick,         // half-open trembling, wavy pupils (tamagotchi need)
};

// Set the animation mode; optionally provide a text string for ForceScrollText.
void eyeAnimSetMode(EyeAnimMode mode, const char* text = nullptr);
EyeAnimMode eyeAnimGetMode();

// Enable / disable voluntary eye blinks (default: disabled).
void eyeAnimSetBlinkEnabled(bool enabled);
bool eyeAnimGetBlinkEnabled();

// Compute the current pose based on the active mode and elapsed time.
EyePose     eyeAnimComputePose(uint32_t nowMs);
EyeExpression eyeAnimComputeExpression(uint32_t nowMs);

// Expose scroll text (null if not in scroll mode).
const char* eyeAnimScrollText();

// ── Pose generators (also used by eye_geometry for menu frames) ───────────────
EyePose makeClosedPose(uint32_t nowMs);
EyePose makeAwakePose(uint32_t nowMs, float alertness);
EyePose makeMenuEyePose(uint32_t nowMs);
EyePose lerpPose(const EyePose& a, const EyePose& b, float t);
