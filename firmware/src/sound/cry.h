#pragma once
#include <stdint.h>
#include <stdbool.h>

// Pokémon-style smooth pitch-glide cry.
// Uses the buzzer PWM directly (bypasses RTTTL).
// Frequency sweeps exponentially from a high to a low pitch over ~350-850ms,
// with optional tremolo. Parameters are derived deterministically from seed.

// Start the cry for the given 24-bit seed (badge's sId24).
void cryStart(uint32_t seed);

// Stop immediately.
void cryStop();

// Drive playback — call every frame. Returns true while playing.
bool cryTick(uint32_t nowMs);

bool cryIsPlaying();
