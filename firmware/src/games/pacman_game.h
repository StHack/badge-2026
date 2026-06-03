#pragma once
#include <stdint.h>

// winPellets=0 → default (kWinPellets)
void pacmanGameStart(uint8_t winPellets = 0);
bool pacmanGameIsActive();
void pacmanGameTick(uint32_t nowMs);
void pacmanGameStop();
