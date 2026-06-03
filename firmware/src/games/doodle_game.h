#pragma once
#include <stdint.h>

// winScroll=0 → default (kWinScrollDef)
void doodleGameStart(uint16_t winScroll = 0);
bool doodleGameIsActive();
void doodleGameTick(uint32_t nowMs);
void doodleGameStop();
