#pragma once
#include <stdint.h>

enum class LedAnimMode : uint8_t {
    Off,         // all LEDs off
    RingPulse,   // rings breathe blue/white; mouth dim — idle FULL_MODE
    MouthPink,   // mouth animates pink/magenta — FULL_MODE idle
    TamaCry,     // all LEDs pulsing urgently — tamagotchi alert
    TamaSuccess, // celebration rainbow — tamagotchi level-up
    WakeAccent,  // blue rings + purple mouth — wake intro
    GameFlash,   // brief green (win) or red (lose) flash on rings
};

void ledAnimSetMode(LedAnimMode mode);
LedAnimMode ledAnimGetMode();

// Drive LED animations. Call once per frame.
void ledAnimTick(uint32_t nowMs);

// Trigger a one-shot ring flash (used by games for win/lose feedback).
// Returns to previous mode after durationMs.
void ledAnimFlashRings(uint32_t r, uint32_t g, uint32_t b, uint32_t durationMs);
