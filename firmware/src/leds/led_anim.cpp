#include "led_anim.h"
#include "led_anim_zones.h"
#include "../hal/leds.h"
#include "../config/hardware.h"
#include <Arduino.h>
#include <math.h>

static LedAnimMode sMode       = LedAnimMode::Off;
static LedAnimMode sFlashPrev  = LedAnimMode::Off;
static uint32_t    sFlashStart = 0;
static uint32_t    sFlashDur   = 0;
static uint32_t    sFlashR = 0, sFlashG = 0, sFlashB = 0;

// ── Helpers ───────────────────────────────────────────────────────────────────
static inline uint8_t pulse(uint32_t nowMs, float speed, uint8_t lo, uint8_t hi) {
    float t = (sinf((float)nowMs * speed) + 1.f) * 0.5f;
    return (uint8_t)(lo + (hi - lo) * t);
}

static void setRings(uint8_t r, uint8_t g, uint8_t b) {
    for (uint8_t i = 0; i < kLedRingLeft; i++)
        gLedStrip.setPixelColor(i, r, g, b);
    for (uint8_t i = kLedRightStart; i < kLedTotal; i++)
        gLedStrip.setPixelColor(i, r, g, b);
}

static void setMouth(uint8_t r, uint8_t g, uint8_t b) {
    for (uint8_t i = kLedMouthStart; i < kLedRightStart; i++)
        gLedStrip.setPixelColor(i, r, g, b);
}

static void setAll(uint8_t r, uint8_t g, uint8_t b) {
    gLedStrip.fill(gLedStrip.Color(r, g, b));
}

// ── Mode implementations ──────────────────────────────────────────────────────

static void tickZones(uint32_t nowMs) {
    ledZonesTick(nowMs);
}

static void tickTamaCry(uint32_t nowMs) {
    // Urgent pulsing — all LEDs, red/orange
    uint8_t v = pulse(nowMs, 0.012f, 20, 180);
    uint8_t g = v / 4;
    setAll(v, g, 0);
    gLedStrip.show();
}

static void tickTamaSuccess(uint32_t nowMs) {
    // Neon-pink pulse — rings and mouth offset for shimmer effect
    uint8_t v  = pulse(nowMs, 0.010f, 40, 230);
    uint8_t v2 = pulse(nowMs + 300, 0.010f, 40, 230);
    setRings(v,  v  / 20, v  / 2);
    setMouth(v2, v2 / 20, v2 / 2);
    gLedStrip.show();
}

static void tickWakeAccent(uint32_t nowMs) {
    // Neon-pink fade-in over ~1.5 s, then pulsing
    float age    = fminf((float)nowMs * 0.001f, 1.5f);
    float bright = age / 1.5f;
    uint8_t rv = pulse(nowMs, 0.010f, 20, (uint8_t)(200 * bright));
    uint8_t mv = pulse(nowMs + 500, 0.010f, 0,  (uint8_t)(220 * bright));
    setRings(rv, rv / 20, rv / 2);
    setMouth(mv, mv / 20, mv / 2);
    gLedStrip.show();
}

// ── Public API ────────────────────────────────────────────────────────────────

void ledAnimSetMode(LedAnimMode mode) {
    sMode = mode;
}

LedAnimMode ledAnimGetMode() { return sMode; }

void ledAnimTick(uint32_t nowMs) {
    // Handle one-shot flash
    if (sFlashStart > 0) {
        if (nowMs - sFlashStart < sFlashDur) {
            setRings((uint8_t)sFlashR, (uint8_t)sFlashG, (uint8_t)sFlashB);
            gLedStrip.show();
            return;
        } else {
            sFlashStart = 0;
            sMode = sFlashPrev;
        }
    }

    switch (sMode) {
        case LedAnimMode::Off:          ledsOff(); break;
        case LedAnimMode::RingPulse:    tickZones(nowMs); break;
        case LedAnimMode::MouthPink:    tickZones(nowMs); break;
        case LedAnimMode::TamaCry:      tickTamaCry(nowMs); break;
        case LedAnimMode::TamaSuccess:  tickTamaSuccess(nowMs); break;
        case LedAnimMode::WakeAccent:   tickWakeAccent(nowMs); break;
        case LedAnimMode::GameFlash:    break; // managed by ledAnimFlashRings
    }
}

void ledAnimFlashRings(uint32_t r, uint32_t g, uint32_t b, uint32_t durationMs) {
    sFlashPrev  = sMode;
    sFlashR     = r;
    sFlashG     = g;
    sFlashB     = b;
    sFlashDur   = durationMs;
    sFlashStart = millis();
}
