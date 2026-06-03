#include "cry.h"
#include "rtttl.h"
#include "../hal/buzzer.h"
#include <math.h>
#include <Arduino.h>

// ── LCG (matches identity.cpp and badge_cry.py) ──────────────────────────────

static uint32_t lcg(uint32_t& s) {
    s = s * 1664525u + 1013904223u;
    return s;
}

// ── MIDI → Hz ────────────────────────────────────────────────────────────────

static float midiToHz(float m) {
    // f = 440 * 2^((m-69)/12)  ≡  440 * exp(ln2/12 * (m-69))
    return 440.0f * expf(0.05776226505f * (m - 69.0f));
}

// ── State ─────────────────────────────────────────────────────────────────────

struct CryState {
    float    startHz;
    float    logRatio;       // logf(endHz / startHz)
    float    tremoloHz;
    float    tremoloDepth;
    uint32_t durationMs;
    uint32_t startMs;
    uint32_t lastUpdateMs;
    bool     playing;
};

static CryState sC = {};

// ── Public API ────────────────────────────────────────────────────────────────

void cryStart(uint32_t seed) {
    rtttlStop();

    uint32_t s = seed ^ 0xC0FFEEu;  // differentiate from RTTTL seed

    // Piezo resonance is ~2500 Hz (MIDI ≈ 99).
    // Descending (2/3): start near resonance for maximum loudness at onset.
    // Ascending  (1/3): start mid-range and sweep up through the resonance peak.
    bool  ascending = (lcg(s) % 3u) == 0u;
    float startMidi, endMidi;
    if (ascending) {
        startMidi = 60.0f + (float)(lcg(s) % 28u);            // C4-E6  (261–1319 Hz)
        endMidi   = startMidi + 30.0f + (float)(lcg(s) % 12u);// +30..+41 → sweeps past 2500 Hz
    } else {
        startMidi = 88.0f + (float)(lcg(s) % 14u);            // F6-G#7 (1397–3136 Hz)
        endMidi   = 48.0f + (float)(lcg(s) % 24u);            // C3-B4  (130–494 Hz)
    }
    // Ensure at least 2 octaves of sweep so it never sounds like a flat beep
    if (fabsf(endMidi - startMidi) < 24.0f) {
        endMidi = ascending ? startMidi + 24.0f : startMidi - 24.0f;
    }

    sC.startHz    = midiToHz(startMidi);
    float endHz   = midiToHz(endMidi);
    sC.logRatio   = logf(endHz / sC.startHz);
    sC.durationMs = 450u + (lcg(s) % 550u);           // 450-999 ms

    bool hasTremolo = (lcg(s) % 2u) == 0u;            // 50%
    if (hasTremolo) {
        sC.tremoloHz    = 15.0f + (float)(lcg(s) % 60u);          // 15-74 Hz
        sC.tremoloDepth = 0.04f + 0.002f * (float)(lcg(s) % 80u); // 4-20%
    } else {
        sC.tremoloHz    = 0.0f;
        sC.tremoloDepth = 0.0f;
    }

    uint32_t now    = millis();
    sC.startMs      = now;
    sC.lastUpdateMs = now;
    sC.playing      = true;
    // Arm first tone immediately — no sentinel, no deferred start
    buzzerToneStart((uint16_t)(sC.startHz + 0.5f));
}

void cryStop() {
    sC.playing = false;
    buzzerToneStop();
}

bool cryTick(uint32_t /*nowMs*/) {
    if (!sC.playing) return false;

    // Use millis() directly — nowMs from the main loop is captured before
    // badgeFsmHandleSleepEntry() blocks ~2 s for BLE scan and would be stale.
    uint32_t now = millis();

    // Throttle: update buzzer at ~125 Hz (every 8ms) — smooth glide on hardware
    if (now - sC.lastUpdateMs < 8u) return true;
    sC.lastUpdateMs = now;

    uint32_t elapsed = now - sC.startMs;
    if (elapsed >= sC.durationMs) {
        cryStop();
        return false;
    }

    float t  = (float)elapsed / (float)sC.durationMs;
    float hz = sC.startHz * expf(sC.logRatio * t);

    if (sC.tremoloHz > 0.0f) {
        float phase = 6.28318530f * sC.tremoloHz * (float)elapsed * 0.001f;
        hz *= (1.0f + sC.tremoloDepth * sinf(phase));
    }

    buzzerToneStart((uint16_t)(hz + 0.5f));
    return true;
}

bool cryIsPlaying() { return sC.playing; }
