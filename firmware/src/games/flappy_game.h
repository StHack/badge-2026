#pragma once
#include <stdint.h>

// winScore: 0 = use built-in default (10), no target display.
void flappyGameStart(uint8_t winScore = 0);
bool flappyGameIsActive();
void flappyGameTick(uint32_t nowMs);
void flappyGameStop();
