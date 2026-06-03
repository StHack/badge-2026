#pragma once
#include <stdint.h>

// Shared single-flag result written by games, consumed by the tamagotchi module.
enum class GameResult : uint8_t { None, Won, Lost };
extern volatile GameResult gGameResult;
