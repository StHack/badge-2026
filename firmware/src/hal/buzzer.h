#pragma once
#include <stdint.h>

// Mute flag: true = silent. Default false (sound on, always at full duty).
extern bool gBuzzerMuted;

void buzzerInit();

// Re-attach PWM pin after light sleep without reconfiguring the timer.
// Call once per wake. Safe to call multiple times.
void buzzerReinit();

// Blocking beep — only for boot acknowledgement.
void buzzerBeep(uint16_t hz, uint16_t durationMs);

// Non-blocking tone control — used by the RTTTL player.
void buzzerToneStart(uint16_t hz);
void buzzerToneStop();
