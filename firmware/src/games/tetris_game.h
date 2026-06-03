#pragma once
#include <stdint.h>

// winLines: 0 = use built-in default (22), no target display.
void tetrisGameStart(uint8_t winLines = 0);
bool tetrisGameIsActive();
void tetrisGameTick(uint32_t nowMs);
void tetrisGameStop();
