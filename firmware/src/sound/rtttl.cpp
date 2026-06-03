#include "rtttl.h"
#include "cry.h"
#include "../hal/buzzer.h"
#include <string.h>
#include <stdlib.h>
#include <ctype.h>

// ── RTTTL format ─────────────────────────────────────────────────────────────
// <name>:<defaults>:<notes>
// defaults: d=<dur>, o=<oct>, b=<bpm>
// note:     [<dur>]<note>[#][<oct>][.]
// note names: p a b c d e f g

static constexpr uint8_t kMaxNotes = 200;

struct Note {
    uint16_t hz;      // 0 = pause
    uint16_t durationMs;
};

// Note frequencies in Hz for octave 4 (index: p=0 a=1 b=2 c=3 d=4 e=5 f=6 g=7)
static const uint16_t kFreqOct4[] = {0, 440, 494, 262, 294, 330, 349, 392};

static uint16_t noteHz(char note, bool sharp, uint8_t octave) {
    uint8_t idx = 0;
    switch (tolower(note)) {
        case 'p': return 0;
        case 'a': idx = 1; break;
        case 'b': idx = 2; break;
        case 'c': idx = 3; break;
        case 'd': idx = 4; break;
        case 'e': idx = 5; break;
        case 'f': idx = 6; break;
        case 'g': idx = 7; break;
        default:  return 0;
    }
    uint16_t base = kFreqOct4[idx];
    if (sharp && note != 'p') base = (uint16_t)(base * 1.0595f);
    int8_t diff = (int8_t)octave - 4;
    if (diff > 0) base <<= diff;
    else if (diff < 0) base >>= (-diff);
    return base;
}

static Note sNotes[kMaxNotes];
static uint8_t sNoteCount = 0;
static uint8_t sNoteIdx = 0;
static uint32_t sNoteStartMs = 0;
static bool sPlaying = false;

bool rtttlStart(const char* rtttl) {
    rtttlStop();
    cryStop();  // ensure glide cry doesn't overlap
    if (!rtttl || !*rtttl) return false;

    // Skip name
    const char* p = rtttl;
    while (*p && *p != ':') p++;
    if (!*p) return false;
    p++;

    // Parse defaults
    uint8_t defDur = 4, defOct = 6;
    uint16_t bpm = 120;

    while (*p && *p != ':') {
        while (*p == ' ') p++;
        char key = tolower(*p++);
        if (*p != '=') continue;
        p++;
        int val = atoi(p);
        while (isdigit(*p)) p++;
        if (key == 'd') defDur = (uint8_t)val;
        else if (key == 'o') defOct = (uint8_t)val;
        else if (key == 'b') bpm = (uint16_t)val;
        if (*p == ',') p++;
    }
    if (*p != ':') return false;
    p++;

    uint32_t wholeMs = (60000u * 4u) / bpm;
    sNoteCount = 0;

    while (*p && sNoteCount < kMaxNotes) {
        while (*p == ' ') p++;
        if (!*p) break;

        // Duration
        uint8_t dur = defDur;
        if (isdigit(*p)) {
            dur = (uint8_t)atoi(p);
            while (isdigit(*p)) p++;
        }

        // Note
        if (!isalpha(*p)) { if (*p) p++; continue; }
        char noteChar = tolower(*p++);

        // Sharp
        bool sharp = (*p == '#');
        if (sharp) p++;

        // Octave
        uint8_t oct = defOct;
        if (isdigit(*p)) {
            oct = (uint8_t)(*p++ - '0');
        }

        // Dot
        bool dot = (*p == '.');
        if (dot) p++;

        uint32_t durMs = wholeMs / dur;
        if (dot) durMs = durMs * 3 / 2;

        sNotes[sNoteCount].hz = noteHz(noteChar, sharp, oct);
        sNotes[sNoteCount].durationMs = (uint16_t)durMs;
        sNoteCount++;

        if (*p == ',') p++;
    }

    if (sNoteCount == 0) return false;

    sNoteIdx = 0;
    sNoteStartMs = 0;
    sPlaying = true;
    return true;
}

void rtttlStop() {
    sPlaying = false;
    sNoteCount = 0;
    sNoteIdx = 0;
    buzzerToneStop();
}

bool rtttlTick(uint32_t nowMs) {
    if (!sPlaying) return false;

    // First note: arm it
    if (sNoteStartMs == 0) {
        sNoteStartMs = nowMs;
        buzzerToneStart(sNotes[0].hz);
    }

    uint32_t elapsed = nowMs - sNoteStartMs;
    if (elapsed >= sNotes[sNoteIdx].durationMs) {
        sNoteIdx++;
        if (sNoteIdx >= sNoteCount) {
            rtttlStop();
            return false;
        }
        sNoteStartMs = nowMs;
        buzzerToneStart(sNotes[sNoteIdx].hz);
    }
    return true;
}

bool rtttlIsPlaying() { return sPlaying; }
