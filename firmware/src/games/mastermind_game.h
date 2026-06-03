#pragma once
#include <stdint.h>

void mastermindGameStart();
bool mastermindGameIsActive();
void mastermindGameTick(uint32_t nowMs);
void mastermindGameStop();
