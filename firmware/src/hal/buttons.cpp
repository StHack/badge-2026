#include "buttons.h"
#include "../config/hardware.h"
#include "../config/timing.h"
#include <Arduino.h>
#include <driver/gpio.h>

bool gButtonState[kBtnCount]         = {false, false, false};
bool gButtonPressed[kBtnCount]       = {false, false, false};
bool gButtonPressedLatched[kBtnCount] = {false, false, false};

static const uint8_t kPins[kBtnCount] = {kPinBtnLeft, kPinBtnCenter, kPinBtnRight};
static uint32_t      sLastChangeMs[kBtnCount] = {0, 0, 0};
static bool          sRawPrev[kBtnCount]       = {false, false, false};

void buttonsInit() {
    for (uint8_t i = 0; i < kBtnCount; i++) {
        pinMode(kPins[i], INPUT_PULLUP);
    }
}

void pollButtons() {
    uint32_t now = millis();
    for (uint8_t i = 0; i < kBtnCount; i++) {
        gButtonPressed[i] = false;
        bool raw = (digitalRead(kPins[i]) == LOW);
        if (raw != sRawPrev[i]) {
            sLastChangeMs[i] = now;
            sRawPrev[i] = raw;
        }
        if (now - sLastChangeMs[i] >= kBtnDebounceMs) {
            bool prev = gButtonState[i];
            gButtonState[i] = raw;
            if (!prev && raw) {
                gButtonPressed[i]       = true;
                gButtonPressedLatched[i] = true;
            }
        }
    }
}

void buttonsClearLatched() {
    for (uint8_t i = 0; i < kBtnCount; i++) gButtonPressedLatched[i] = false;
}

void buttonsEnableWake() {
    for (uint8_t i = 0; i < kBtnCount; i++) {
        gpio_wakeup_enable(static_cast<gpio_num_t>(kPins[i]), GPIO_INTR_LOW_LEVEL);
    }
    esp_sleep_enable_gpio_wakeup();
}
