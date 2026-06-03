#pragma once
#include <stdint.h>
#include <stdbool.h>

// Simon Says: follow an LED sequence of increasing length.
// numRounds: 0 = use built-in default (5), no target display.
// Pass non-zero from tamagotchi to set the win target and show XX/XX HUD.
void simonGameStart(uint8_t numRounds);
bool simonGameIsActive();
void simonGameTick(uint32_t nowMs);   // handles input + drawing
void simonGameStop();
