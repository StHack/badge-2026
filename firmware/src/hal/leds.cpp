#include "leds.h"
#include "../config/hardware.h"

Adafruit_NeoPixel gLedStrip(kLedTotal, kPinLeds, NEO_GRB + NEO_KHZ800);

void ledsInit() {
    gLedStrip.begin();
    gLedStrip.setBrightness(kLedBrightness);
    ledsOff();
}

void ledsOff() {
    gLedStrip.clear();
    gLedStrip.show();
}

void ledsSetBrightness(uint8_t level) {
    if (level > 10) level = 10;
    gLedStrip.setBrightness((uint8_t)((uint32_t)level * 255 / 10));
    gLedStrip.show();
}
