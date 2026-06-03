#pragma once
#include <stdint.h>
#include <stdbool.h>

// RTTTL (Nokia ringtone) player.
// Uses buzzerToneStart/Stop from hal/buzzer.h internally.

// Start playing an RTTTL string. Returns false if the string is invalid.
bool rtttlStart(const char* rtttl);

// Stop playback immediately.
void rtttlStop();

// Drive playback. Call every frame. Returns true while still playing.
bool rtttlTick(uint32_t nowMs);

bool rtttlIsPlaying();
