// led_anim_zones.cpp — independent tour + mouth LED animations.
#include "led_anim_zones.h"
#include "../hal/leds.h"
#include "../config/hardware.h"
#include <Preferences.h>
#include <Arduino.h>
#include <math.h>
#include <stdlib.h>

// ── State ─────────────────────────────────────────────────────────────────────

static LedTourAnim  sTour  = LedTourAnim::BluePulse;
static LedMouthAnim sMouth = LedMouthAnim::Pink;

// ── Helpers ───────────────────────────────────────────────────────────────────

static inline float flerp(float a, float b, float t) { return a + (b - a) * t; }
static inline uint8_t pulse(uint32_t ms, float speed, uint8_t lo, uint8_t hi) {
    float t = (sinf((float)ms * speed) + 1.f) * 0.5f;
    return (uint8_t)(lo + (hi - lo) * t);
}
static inline void hsv(float h, float s, float v, uint8_t& r, uint8_t& g, uint8_t& b) {
    float h6 = fmodf(h, 1.f) * 6.f;
    int   hi = (int)h6;
    float f  = h6 - hi;
    float p = v * (1.f - s);
    float q = v * (1.f - s * f);
    float t2 = v * (1.f - s * (1.f - f));
    float fr, fg, fb;
    switch (hi % 6) {
        case 0: fr=v;  fg=t2; fb=p;  break;
        case 1: fr=q;  fg=v;  fb=p;  break;
        case 2: fr=p;  fg=v;  fb=t2; break;
        case 3: fr=p;  fg=q;  fb=v;  break;
        case 4: fr=t2; fg=p;  fb=v;  break;
        default:fr=v;  fg=p;  fb=q;  break;
    }
    r = (uint8_t)(fr * 255.f);
    g = (uint8_t)(fg * 255.f);
    b = (uint8_t)(fb * 255.f);
}

// ── Tour animations ───────────────────────────────────────────────────────────

static void tourBluePulse(uint32_t ms) {
    uint8_t v = pulse(ms, 0.0025f, 10, 80);
    for (uint8_t i = 0; i < kLedRingLeft; i++)
        gLedStrip.setPixelColor(i, 0, v / 3, v);
    for (uint8_t i = kLedRightStart; i < kLedTotal; i++)
        gLedStrip.setPixelColor(i, 0, v / 3, v);
}

static void tourCyanPulse(uint32_t ms) {
    uint8_t v = pulse(ms, 0.002f, 8, 70);
    for (uint8_t i = 0; i < kLedRingLeft; i++)
        gLedStrip.setPixelColor(i, 0, v, v);
    for (uint8_t i = kLedRightStart; i < kLedTotal; i++)
        gLedStrip.setPixelColor(i, 0, v, v);
}

static void tourRainbow(uint32_t ms) {
    float base = (float)ms * 0.0003f;
    uint8_t idx = 0;
    for (uint8_t i = 0; i < kLedRingLeft; i++, idx++) {
        uint8_t r, g, b;
        hsv(base + idx * 0.167f, 1.f, 0.35f, r, g, b);
        gLedStrip.setPixelColor(i, r, g, b);
    }
    for (uint8_t i = kLedRightStart; i < kLedTotal; i++, idx++) {
        uint8_t r, g, b;
        hsv(base + idx * 0.167f, 1.f, 0.35f, r, g, b);
        gLedStrip.setPixelColor(i, r, g, b);
    }
}

static void tourFire(uint32_t ms) {
    // Each LED flickers independently
    uint8_t idx = 0;
    for (uint8_t i = 0; i < kLedRingLeft; i++, idx++) {
        float flicker = (sinf((float)ms * 0.011f + idx * 1.7f) + 1.f) * 0.5f;
        uint8_t r = (uint8_t)(180.f + 60.f * flicker);
        uint8_t g = (uint8_t)(30.f  + 40.f * flicker);
        gLedStrip.setPixelColor(i, r, g, 0);
    }
    for (uint8_t i = kLedRightStart; i < kLedTotal; i++, idx++) {
        float flicker = (sinf((float)ms * 0.013f + idx * 2.1f) + 1.f) * 0.5f;
        uint8_t r = (uint8_t)(180.f + 60.f * flicker);
        uint8_t g = (uint8_t)(30.f  + 40.f * flicker);
        gLedStrip.setPixelColor(i, r, g, 0);
    }
}

static void tourPolice(uint32_t ms) {
    bool redPhase = ((ms / 300) % 2) == 0;
    uint8_t rl = redPhase ? 200 : 0;
    uint8_t bl = redPhase ? 0   : 200;
    uint8_t rr = redPhase ? 0   : 200;
    uint8_t br = redPhase ? 200 : 0;
    for (uint8_t i = 0; i < kLedRingLeft; i++)
        gLedStrip.setPixelColor(i, rl, 0, bl);
    for (uint8_t i = kLedRightStart; i < kLedTotal; i++)
        gLedStrip.setPixelColor(i, rr, 0, br);
}

static void tourComet(uint32_t ms) {
    // Bright head sweeps around 6 positions (3 left then 3 right)
    float pos = fmodf((float)ms * 0.004f, 6.f);
    uint8_t leds[6] = {0, 1, 2, (uint8_t)kLedRightStart, (uint8_t)(kLedRightStart+1), (uint8_t)(kLedRightStart+2)};
    for (uint8_t j = 0; j < 6; j++) {
        float dist = fabsf(fmodf(pos - j + 6.f, 6.f));
        if (dist > 3.f) dist = 6.f - dist;
        float brightness = fmaxf(0.f, 1.f - dist * 0.7f);
        uint8_t v = (uint8_t)(brightness * brightness * 200.f);
        gLedStrip.setPixelColor(leds[j], 0, v / 2, v);
    }
}

static void tourBreathing(uint32_t ms) {
    uint8_t v = pulse(ms, 0.0015f, 5, 100);
    for (uint8_t i = 0; i < kLedRingLeft; i++)
        gLedStrip.setPixelColor(i, v, v, v);
    for (uint8_t i = kLedRightStart; i < kLedTotal; i++)
        gLedStrip.setPixelColor(i, v, v, v);
}

static void tourDisco(uint32_t ms) {
    // Each LED switches color every 150ms independently
    uint8_t idx = 0;
    for (uint8_t i = 0; i < kLedRingLeft; i++, idx++) {
        uint32_t phase = (ms + idx * 137) / 150;
        uint8_t r, g, b;
        hsv((float)(phase * 73 % 100) * 0.01f, 1.f, 0.4f, r, g, b);
        gLedStrip.setPixelColor(i, r, g, b);
    }
    for (uint8_t i = kLedRightStart; i < kLedTotal; i++, idx++) {
        uint32_t phase = (ms + idx * 137) / 150;
        uint8_t r, g, b;
        hsv((float)(phase * 73 % 100) * 0.01f, 1.f, 0.4f, r, g, b);
        gLedStrip.setPixelColor(i, r, g, b);
    }
}

static void tourStrobe(uint32_t ms) {
    bool on = ((ms / 50) % 2) == 0;
    uint8_t v = on ? 255 : 0;
    for (uint8_t i = 0; i < kLedRingLeft; i++)
        gLedStrip.setPixelColor(i, v, v, v);
    for (uint8_t i = kLedRightStart; i < kLedTotal; i++)
        gLedStrip.setPixelColor(i, v, v, v);
}

static void tourGreenPulse(uint32_t ms) {
    uint8_t v = pulse(ms, 0.0022f, 8, 90);
    for (uint8_t i = 0; i < kLedRingLeft; i++)
        gLedStrip.setPixelColor(i, 0, v, v / 6);
    for (uint8_t i = kLedRightStart; i < kLedTotal; i++)
        gLedStrip.setPixelColor(i, 0, v, v / 6);
}

static void tourAurora(uint32_t ms) {
    float base = (float)ms * 0.00015f;
    uint8_t idx = 0;
    for (uint8_t i = 0; i < kLedRingLeft; i++, idx++) {
        float hue = 0.30f + 0.48f * ((sinf(base * 3.f + idx * 0.8f) + 1.f) * 0.5f);
        float val = 0.25f + 0.20f * ((sinf(base * 2.f + idx * 1.2f) + 1.f) * 0.5f);
        uint8_t r, g, b;
        hsv(hue, 0.9f, val, r, g, b);
        gLedStrip.setPixelColor(i, r, g, b);
    }
    for (uint8_t i = kLedRightStart; i < kLedTotal; i++, idx++) {
        float hue = 0.30f + 0.48f * ((sinf(base * 3.f + idx * 0.8f) + 1.f) * 0.5f);
        float val = 0.25f + 0.20f * ((sinf(base * 2.f + idx * 1.2f) + 1.f) * 0.5f);
        uint8_t r, g, b;
        hsv(hue, 0.9f, val, r, g, b);
        gLedStrip.setPixelColor(i, r, g, b);
    }
}

static void tourSunset(uint32_t ms) {
    float t = (sinf((float)ms * 0.0018f) + 1.f) * 0.5f;
    uint8_t idx = 0;
    for (uint8_t i = 0; i < kLedRingLeft; i++, idx++) {
        float hue = fmodf(0.92f + t * 0.15f + idx * 0.05f, 1.f);
        uint8_t r, g, b;
        hsv(hue, 1.f, 0.40f + 0.15f * t, r, g, b);
        gLedStrip.setPixelColor(i, r, g, b);
    }
    for (uint8_t i = kLedRightStart; i < kLedTotal; i++, idx++) {
        float hue = fmodf(0.92f + t * 0.15f + idx * 0.05f, 1.f);
        uint8_t r, g, b;
        hsv(hue, 1.f, 0.40f + 0.15f * t, r, g, b);
        gLedStrip.setPixelColor(i, r, g, b);
    }
}

static void tourThunder(uint32_t ms) {
    uint32_t cycle = ms % 1700;
    bool flash = (cycle < 60) || (cycle > 80 && cycle < 130);
    uint8_t v  = flash ? 220 : 0;
    uint8_t b2 = flash ? 255 : 0;
    for (uint8_t i = 0; i < kLedRingLeft; i++)
        gLedStrip.setPixelColor(i, v, v, b2);
    for (uint8_t i = kLedRightStart; i < kLedTotal; i++)
        gLedStrip.setPixelColor(i, v, v, b2);
}

static void tourHeartbeat(uint32_t ms) {
    uint32_t cycle = ms % 1000;
    float v = 0.f;
    if      (cycle < 80)  v = (float)cycle / 80.f;
    else if (cycle < 160) v = 1.f - (float)(cycle - 80)  / 80.f;
    else if (cycle < 220) v = (float)(cycle - 160) / 60.f * 0.7f;
    else if (cycle < 300) v = 0.7f - (float)(cycle - 220) / 80.f * 0.7f;
    uint8_t r = (uint8_t)(v * 220.f);
    for (uint8_t i = 0; i < kLedRingLeft; i++)
        gLedStrip.setPixelColor(i, r, 0, (uint8_t)(v * 10.f));
    for (uint8_t i = kLedRightStart; i < kLedTotal; i++)
        gLedStrip.setPixelColor(i, r, 0, (uint8_t)(v * 10.f));
}

// ── Mouth animations ──────────────────────────────────────────────────────────

static void mouthPink(uint32_t ms) {
    for (uint8_t i = 0; i < kLedMouth; i++) {
        float phase = (float)ms * 0.015f + i * 1.05f;
        float t = (sinf(phase) + 1.f) * 0.5f;
        uint8_t r = (uint8_t)(180 + 40 * t);
        uint8_t g = (uint8_t)(20  + 15 * t);
        uint8_t b = (uint8_t)(140 + 40 * t);
        gLedStrip.setPixelColor(kLedMouthStart + i, r, g, b);
    }
}

static void mouthRainbow(uint32_t ms) {
    float base = (float)ms * 0.0004f;
    for (uint8_t i = 0; i < kLedMouth; i++) {
        uint8_t r, g, b;
        hsv(base + i * 0.2f, 1.f, 0.4f, r, g, b);
        gLedStrip.setPixelColor(kLedMouthStart + i, r, g, b);
    }
}

static void mouthLava(uint32_t ms) {
    for (uint8_t i = 0; i < kLedMouth; i++) {
        float t = (sinf((float)ms * 0.009f + i * 0.9f) + 1.f) * 0.5f;
        uint8_t r = (uint8_t)(160 + 70 * t);
        uint8_t g = (uint8_t)(10  + 30 * t);
        gLedStrip.setPixelColor(kLedMouthStart + i, r, g, 0);
    }
}

static void mouthChill(uint32_t ms) {
    for (uint8_t i = 0; i < kLedMouth; i++) {
        float t = (sinf((float)ms * 0.006f + i * 0.8f) + 1.f) * 0.5f;
        uint8_t g = (uint8_t)(40  + 60 * t);
        uint8_t b = (uint8_t)(100 + 80 * t);
        gLedStrip.setPixelColor(kLedMouthStart + i, 0, g, b);
    }
}

static void mouthCandle(uint32_t ms) {
    for (uint8_t i = 0; i < kLedMouth; i++) {
        float flicker = (sinf((float)ms * 0.023f + i * 2.3f) +
                         sinf((float)ms * 0.041f + i * 1.1f)) * 0.25f + 0.5f;
        flicker = fmaxf(0.f, fminf(1.f, flicker));
        uint8_t r = (uint8_t)(200 + 40 * flicker);
        uint8_t g = (uint8_t)(60  + 40 * flicker);
        uint8_t b = (uint8_t)(5   + 10 * flicker);
        gLedStrip.setPixelColor(kLedMouthStart + i, r, g, b);
    }
}

static void mouthCyber(uint32_t ms) {
    // Chase: one bright LED moves across the mouth
    float pos = fmodf((float)ms * 0.006f, (float)kLedMouth);
    for (uint8_t i = 0; i < kLedMouth; i++) {
        float dist = fabsf(pos - (float)i);
        if (dist > kLedMouth / 2.f) dist = kLedMouth - dist;
        float t = fmaxf(0.f, 1.f - dist * 0.8f);
        uint8_t g = (uint8_t)(200 * t * t);
        uint8_t b = (uint8_t)(80  * t * t);
        gLedStrip.setPixelColor(kLedMouthStart + i, 0, g, b);
    }
}

static void mouthVU(uint32_t ms) {
    // Fake VU: level pulses, bar fills from center out
    float level = (sinf((float)ms * 0.007f) + 1.f) * 0.5f;
    uint8_t lit = (uint8_t)(level * (kLedMouth / 2.f) + 0.5f);
    uint8_t center = kLedMouth / 2;
    for (uint8_t i = 0; i < kLedMouth; i++) {
        uint8_t dist = (i <= center) ? (center - i) : (i - center);
        bool on = dist <= lit;
        uint8_t g = on ? (uint8_t)(80 + 120 * level) : 4;
        uint8_t r = on && dist == lit ? 200 : 0;
        gLedStrip.setPixelColor(kLedMouthStart + i, r, g, 0);
    }
}

static void mouthBounce(uint32_t ms) {
    // Color bounces back and forth
    float pos = (sinf((float)ms * 0.004f) + 1.f) * 0.5f * (kLedMouth - 1);
    float hue = fmodf((float)ms * 0.0003f, 1.f);
    for (uint8_t i = 0; i < kLedMouth; i++) {
        float dist = fabsf(pos - (float)i);
        float t = fmaxf(0.f, 1.f - dist * 0.6f);
        uint8_t r, g, b;
        hsv(hue, 1.f, t * 0.6f, r, g, b);
        gLedStrip.setPixelColor(kLedMouthStart + i, r, g, b);
    }
}

static void mouthSparkle(uint32_t ms) {
    for (uint8_t i = 0; i < kLedMouth; i++) {
        float t = (sinf((float)ms * (0.007f + i * 0.003f) + i * 1.57f) + 1.f) * 0.5f;
        t = t * t;
        float hue = fmodf((float)ms * 0.0002f + i * 0.2f, 1.f);
        uint8_t r, g, b;
        hsv(hue, 0.8f, t * 0.7f, r, g, b);
        gLedStrip.setPixelColor(kLedMouthStart + i, r, g, b);
    }
}

static void mouthOcean(uint32_t ms) {
    for (uint8_t i = 0; i < kLedMouth; i++) {
        float w1 =  sinf((float)ms * 0.004f + i * 1.2f);
        float w2 =  sinf((float)ms * 0.007f - i * 0.8f) * 0.4f;
        float t  = ((w1 + w2) / 1.4f + 1.f) * 0.5f;
        uint8_t g = (uint8_t)(40 + 40 * t);
        uint8_t b = (uint8_t)(120 + 100 * t);
        gLedStrip.setPixelColor(kLedMouthStart + i, (uint8_t)(5 * t), g, b);
    }
}

static void mouthToxic(uint32_t ms) {
    for (uint8_t i = 0; i < kLedMouth; i++) {
        float t = (sinf((float)ms * 0.012f + i * 1.4f) + 1.f) * 0.5f;
        uint8_t r = (uint8_t)(30 + 80 * t);
        uint8_t g = (uint8_t)(160 + 70 * t);
        gLedStrip.setPixelColor(kLedMouthStart + i, r, g, 0);
    }
}

static void mouthAurora(uint32_t ms) {
    float base = (float)ms * 0.0002f;
    for (uint8_t i = 0; i < kLedMouth; i++) {
        float hue = 0.45f + 0.30f * ((sinf(base + i * 0.9f) + 1.f) * 0.5f);
        float val = 0.25f + 0.20f * ((sinf(base * 1.5f + i * 0.5f) + 1.f) * 0.5f);
        uint8_t r, g, b;
        hsv(hue, 0.9f, val, r, g, b);
        gLedStrip.setPixelColor(kLedMouthStart + i, r, g, b);
    }
}

static void mouthHeartbeat(uint32_t ms) {
    uint32_t cycle = ms % 1000;
    float v = 0.f;
    if      (cycle < 80)  v = (float)cycle / 80.f;
    else if (cycle < 160) v = 1.f - (float)(cycle - 80)  / 80.f;
    else if (cycle < 220) v = (float)(cycle - 160) / 60.f * 0.7f;
    else if (cycle < 300) v = 0.7f - (float)(cycle - 220) / 80.f * 0.7f;
    uint8_t r = (uint8_t)(v * 230.f);
    for (uint8_t i = 0; i < kLedMouth; i++)
        gLedStrip.setPixelColor(kLedMouthStart + i, r, 0, (uint8_t)(v * 30.f));
}

static void mouthMatrix(uint32_t ms) {
    for (uint8_t i = 0; i < kLedMouth; i++) {
        uint32_t period = 400 + (i * 137u) % 300u;
        uint32_t phase  = (ms + i * 233u) % period;
        float t = (phase < 60u)
            ? (float)phase / 60.f
            : fmaxf(0.f, 1.f - (float)(phase - 60u) / 80.f);
        uint8_t g = (uint8_t)(t * t * 220.f + t * 30.f);
        gLedStrip.setPixelColor(kLedMouthStart + i, 0, g, 0);
    }
}

// ── Name tables ───────────────────────────────────────────────────────────────

static const char* const kTourNames[] = {
    "Off", "BluePulse", "CyanPulse", "Rainbow", "Fire",
    "Police", "Comet", "Breathing", "Disco", "Strobe",
    "GreenPulse", "Aurora", "Sunset", "Thunder", "Heartbeat",
};
static const char* const kMouthNames[] = {
    "Off", "Pink", "Rainbow", "Lava", "Chill",
    "Candle", "Cyber", "VU", "Bounce", "Sparkle",
    "Ocean", "Toxic", "Aurora", "Heartbeat", "Matrix",
};

const char* ledTourAnimName(LedTourAnim a) {
    const uint8_t i = (uint8_t)a;
    return (i < (uint8_t)LedTourAnim::Count) ? kTourNames[i] : "?";
}
const char* ledMouthAnimName(LedMouthAnim a) {
    const uint8_t i = (uint8_t)a;
    return (i < (uint8_t)LedMouthAnim::Count) ? kMouthNames[i] : "?";
}

// ── Public API ────────────────────────────────────────────────────────────────

void ledZonesSetTour(LedTourAnim a) {
    if ((uint8_t)a < (uint8_t)LedTourAnim::Count) sTour = a;
}
void ledZonesSetMouth(LedMouthAnim a) {
    if ((uint8_t)a < (uint8_t)LedMouthAnim::Count) sMouth = a;
}
LedTourAnim  ledZonesGetTour()  { return sTour; }
LedMouthAnim ledZonesGetMouth() { return sMouth; }

void ledZonesLoad() {
    Preferences prefs;
    if (prefs.begin(kNvsLedAnim, true)) {
        uint8_t t = prefs.getUChar("tour",  (uint8_t)LedTourAnim::BluePulse);
        uint8_t m = prefs.getUChar("mouth", (uint8_t)LedMouthAnim::Pink);
        prefs.end();
        ledZonesSetTour(static_cast<LedTourAnim>(t));
        ledZonesSetMouth(static_cast<LedMouthAnim>(m));
    }
}

void ledZonesSave() {
    Preferences prefs;
    if (prefs.begin(kNvsLedAnim, false)) {
        prefs.putUChar("tour",  (uint8_t)sTour);
        prefs.putUChar("mouth", (uint8_t)sMouth);
        prefs.end();
    }
}

void ledZonesTick(uint32_t ms) {
    // Tour
    switch (sTour) {
        case LedTourAnim::Off:       for (uint8_t i=0;i<kLedRingLeft;i++) gLedStrip.setPixelColor(i,0,0,0); for (uint8_t i=kLedRightStart;i<kLedTotal;i++) gLedStrip.setPixelColor(i,0,0,0); break;
        case LedTourAnim::BluePulse: tourBluePulse(ms); break;
        case LedTourAnim::CyanPulse: tourCyanPulse(ms); break;
        case LedTourAnim::Rainbow:   tourRainbow(ms);   break;
        case LedTourAnim::Fire:      tourFire(ms);      break;
        case LedTourAnim::Police:    tourPolice(ms);    break;
        case LedTourAnim::Comet:     tourComet(ms);     break;
        case LedTourAnim::Breathing: tourBreathing(ms); break;
        case LedTourAnim::Disco:      tourDisco(ms);      break;
        case LedTourAnim::Strobe:     tourStrobe(ms);     break;
        case LedTourAnim::GreenPulse: tourGreenPulse(ms); break;
        case LedTourAnim::Aurora:     tourAurora(ms);     break;
        case LedTourAnim::Sunset:     tourSunset(ms);     break;
        case LedTourAnim::Thunder:    tourThunder(ms);    break;
        case LedTourAnim::Heartbeat:  tourHeartbeat(ms);  break;
        default: break;
    }
    // Mouth
    switch (sMouth) {
        case LedMouthAnim::Off:       for (uint8_t i=0;i<kLedMouth;i++) gLedStrip.setPixelColor(kLedMouthStart+i,0,0,0); break;
        case LedMouthAnim::Pink:      mouthPink(ms);      break;
        case LedMouthAnim::Rainbow:   mouthRainbow(ms);   break;
        case LedMouthAnim::Lava:      mouthLava(ms);      break;
        case LedMouthAnim::Chill:     mouthChill(ms);     break;
        case LedMouthAnim::Candle:    mouthCandle(ms);    break;
        case LedMouthAnim::Cyber:     mouthCyber(ms);     break;
        case LedMouthAnim::VU:        mouthVU(ms);        break;
        case LedMouthAnim::Bounce:    mouthBounce(ms);    break;
        case LedMouthAnim::Sparkle:   mouthSparkle(ms);   break;
        case LedMouthAnim::Ocean:     mouthOcean(ms);     break;
        case LedMouthAnim::Toxic:     mouthToxic(ms);     break;
        case LedMouthAnim::Aurora:    mouthAurora(ms);    break;
        case LedMouthAnim::Heartbeat: mouthHeartbeat(ms); break;
        case LedMouthAnim::Matrix:    mouthMatrix(ms);    break;
        default: break;
    }
    gLedStrip.show();
}
