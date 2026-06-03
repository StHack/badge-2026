#pragma once
#include <stdint.h>

void breakoutGameStart(uint8_t maxBricks = 0);  // 0 = fill all
bool breakoutGameIsActive();
void breakoutGameTick(uint32_t nowMs);
void breakoutGameStop();
