#pragma once
#include <stdint.h>

void dinoGameStart(uint8_t winScore = 0);
void dinoGameStop();
bool dinoGameIsActive();
void dinoGameTick(uint32_t nowMs);
