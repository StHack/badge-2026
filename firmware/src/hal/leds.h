#pragma once
#include <Adafruit_NeoPixel.h>

// Global LED strip — only led_anim.cpp and hal/leds.cpp touch this directly.
extern Adafruit_NeoPixel gLedStrip;

void ledsInit();

// Turn all LEDs off and push update.
void ledsOff();

// Set brightness level 0-10 (maps to 0-255) and apply immediately.
void ledsSetBrightness(uint8_t level);
