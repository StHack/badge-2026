#pragma once
#include <stdint.h>
#include <stdbool.h>

// Stop game: a moving cursor sweeps across the display.
// Press CENTER when the cursor is inside the target zone.
// targetWidth controls difficulty (smaller = harder).

// numLevels: 0 = use built-in default (5). Pass non-zero from tamagotchi.
void stopGameStart(uint8_t numLevels);
bool stopGameIsActive();
void stopGameTick(uint32_t nowMs);
void stopGameStop();
