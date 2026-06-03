#pragma once
#include <stdint.h>
#include "../eyes/eye_geometry.h"

// ── Win/lose overlays ─────────────────────────────────────────────────────────
// Draw check-mark (win) or cross (lose) into both eyes using the menu eye pose.
// Both functions call display() on both displays before returning.
void minigameUiDrawWinBothEyes(uint32_t nowMs);
void minigameUiDrawLoseBothEyes(uint32_t nowMs, const char* label = nullptr);

// ── LED tour flash ────────────────────────────────────────────────────────────
// Arms a timed flash of the ring LEDs in green (win) or red (lose).
// minigameUiPaintTourFlashIfActive() must be called each tick; it does nothing
// after the deadline has passed.
void minigameUiArmTourFlashGreen(uint32_t nowMs, uint32_t durationMs = 700);
void minigameUiArmTourFlashRed(uint32_t nowMs, uint32_t durationMs = 700);
void minigameUiPaintTourFlashIfActive(uint32_t nowMs);

// ── Inactivity warning ────────────────────────────────────────────────────────
// Redraws the left display with a countdown ("30!" … "1!") when the player
// has been idle for more than kInactivityWarnMs. Call after the game's normal
// draw each frame where InactivityState == Warning.
void minigameUiDrawInactivityWarning(uint32_t nowMs, uint32_t secsLeft);
