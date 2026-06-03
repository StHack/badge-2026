#pragma once
#include <stdint.h>
void spaceGameStart();
void spaceGameStop();
bool spaceGameIsActive();
void spaceGameTick(uint32_t nowMs);
