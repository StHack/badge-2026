#pragma once
#include <stdint.h>
#include <stdbool.h>

// Button indices: 0=left  1=center  2=right
inline constexpr uint8_t kBtnLeft   = 0;
inline constexpr uint8_t kBtnCenter = 1;
inline constexpr uint8_t kBtnRight  = 2;
inline constexpr uint8_t kBtnCount  = 3;

// gButtonState[i]        : true while button is held
// gButtonPressed[i]      : true for exactly one pollButtons() call on press edge
// gButtonPressedLatched[i]: true from first rising-edge detection until buttonsClearLatched()
//                           — survives multiple pollButtons() calls (e.g. inside waitNextFrame)
extern bool gButtonState[kBtnCount];
extern bool gButtonPressed[kBtnCount];
extern bool gButtonPressedLatched[kBtnCount];

void buttonsInit();

// Update gButtonState / gButtonPressed / gButtonPressedLatched. Call at 1 ms resolution.
void pollButtons();

// Clear gButtonPressedLatched. Call at the start of waitNextFrame() so latched presses
// from the previous frame are consumed before accumulating presses for the next frame.
void buttonsClearLatched();

// Enable GPIO wakeup from any button (call before light sleep).
void buttonsEnableWake();
