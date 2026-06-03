#pragma once
#include <stdint.h>

// winFood: 0 = use built-in default (10), no target display.
void snakeGameStart(uint8_t winFood = 0);
bool snakeGameIsActive();
void snakeGameTick(uint32_t nowMs);
void snakeGameStop();
