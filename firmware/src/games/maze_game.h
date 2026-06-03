#pragma once
#include <stdint.h>

// cols=0, rows=0 → show size selector (menu mode, 3 choices: PETIT/MOYEN/GRAND)
// cols>0, rows>0 → start directly with given size (e.g. tama mode: 16×16)
void mazeGameStart(uint8_t cols, uint8_t rows);
bool mazeGameIsActive();
void mazeGameTick(uint32_t nowMs);
void mazeGameStop();
