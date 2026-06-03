#include "buzzer.h"
#include "../config/hardware.h"
#include <Arduino.h>
#include <esp32-hal-ledc.h>

bool gBuzzerMuted = false;

void buzzerInit() {
    ledcSetup(kBuzzerChannel, 2000, 10);
    ledcAttachPin(kPinBuzzer, kBuzzerChannel);
    ledcWrite(kBuzzerChannel, 0);
}

void buzzerReinit() {
    // After light sleep the GPIO matrix loses its LEDC routing.
    // Re-attach the pin without touching the timer (no ledcSetup).
    ledcDetachPin(kPinBuzzer);
    ledcAttachPin(kPinBuzzer, kBuzzerChannel);
    ledcWrite(kBuzzerChannel, 0);
}

void buzzerBeep(uint16_t hz, uint16_t durationMs) {
    if (gBuzzerMuted) return;
    ledcWriteTone(kBuzzerChannel, hz);
    ledcWrite(kBuzzerChannel, 512);  // 50% duty = max volume
    delay(durationMs);
    ledcWrite(kBuzzerChannel, 0);
}

void buzzerToneStart(uint16_t hz) {
    if (hz == 0 || gBuzzerMuted) {
        ledcWrite(kBuzzerChannel, 0);
    } else {
        ledcWriteTone(kBuzzerChannel, hz);
        ledcWrite(kBuzzerChannel, 512);  // 50% duty = max volume
    }
}

void buzzerToneStop() {
    ledcWrite(kBuzzerChannel, 0);
}
