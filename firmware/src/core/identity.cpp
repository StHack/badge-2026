#include "identity.h"
#include <Arduino.h>
#include <esp_system.h>
#include <stdio.h>
#include <string.h>

// ── Name generation — prefix[2:0] + middle[5:3] + suffix[8:6] ───────────────
// 8×8×8 = 512 distinct names, 7 chars max, fantasy-word style.

static const char* const kPrefix[] = {"ka","zo","mi","ra","ta","lu","ne","fi"};
static const char* const kMiddle[] = {"na","ri","lo","va","ki","to","me","sa"};
static const char* const kSuffix[] = {"dor","tek","ron","lia","mus","vek","nor","zen"};

// ── Cry: descending pitch-sweep (Pokémon-style) ───────────────────────────────
// Fast 32nd notes stepping down a diatonic scale, mimicking a pitch glide.

static const char* const kSweepNotes[] = {
    "b6","a6","g6","f6","e6","d6","c6",
    "b5","a5","g5","f5","e5","d5","c5",
};
static constexpr uint8_t kSweepCount = sizeof(kSweepNotes) / sizeof(kSweepNotes[0]);

// ── Music: varied scales and melodic shapes ───────────────────────────────────
//
// 12 scales × 5 contour shapes × 5 rhythm templates × BPM 80-199
// → high inter-badge variation even for sequential MAC addresses.

struct MusicScale {
    const char* notes[7];  // unused trailing slots are nullptr (never accessed)
    uint8_t     len;
};

static const MusicScale kScales[12] = {
    { {"c5","d5","e5","g5","a5",nullptr,nullptr}, 5 },   // C maj pentatonic
    { {"g5","a5","b5","d6","e6",nullptr,nullptr}, 5 },   // G maj pentatonic
    { {"a4","c5","d5","e5","g5",nullptr,nullptr}, 5 },   // A min pentatonic
    { {"d5","e5","g5","a5","b5",nullptr,nullptr}, 5 },   // D maj pentatonic
    { {"e5","g5","a5","b5","d6",nullptr,nullptr}, 5 },   // E min pentatonic
    { {"c5","d#5","f5","g5","a#5",nullptr,nullptr}, 5 }, // C blues pentatonic
    { {"c5","d5","e5","f5","g5","a5","b5"}, 7 },         // C major
    { {"a4","b4","c5","d5","e5","f5","g5"}, 7 },         // A natural minor
    { {"d5","e5","f5","g5","a5","b5","c6"}, 7 },         // D dorian
    { {"g5","a5","b5","c6","d6","e6","f6"}, 7 },         // G mixolydian
    { {"f5","g5","a5","a#5","c6","d6","e6"}, 7 },        // F major
    { {"c5","d5","f5","g5","a5",nullptr,nullptr}, 5 },   // C minor pentatonic
};
static constexpr uint8_t kMusicScaleCount = 12;

// Rhythm templates cycling over 4 slots: 0=quarter, 1=eighth, 2=sixteenth
static const uint8_t kRhythmPats[5][4] = {
    {1, 1, 1, 1},  // steady eighths
    {0, 1, 0, 1},  // slow-fast alternating
    {2, 2, 1, 0},  // fast run → slow close
    {1, 2, 1, 2},  // eighth-sixteenth swing
    {0, 2, 2, 1},  // quarter → fast run
};
static const uint8_t kMusDurs[3] = {4, 8, 16};

// ── Persistent state ──────────────────────────────────────────────────────────

static char sHexId[7];
static char sName[8];
static char sCryRtttl[200];
static char sMusicRtttl[200];

static uint32_t sId24 = 0;

// ── LCG ──────────────────────────────────────────────────────────────────────

static uint32_t lcg(uint32_t& state) {
    state = state * 1664525u + 1013904223u;
    return state;
}

// ── Cry RTTTL: Pokémon-style pitch sweep ─────────────────────────────────────
//
// Structure: [optional tremolo intro] + descending sweep + [held tail]
//   - tremolo: 2-3 reps of alternating adjacent high notes
//   - sweep:   8-12 fast 32nd notes stepping down the scale
//   - tail:    held (8th) lowest note, grounding the sound
//
static void buildCryRtttl(uint32_t seed) {
    uint32_t s = seed;
    uint16_t bpm     = 200 + (uint16_t)(lcg(s) % 61);   // 200-260
    uint8_t startPos = (uint8_t)(lcg(s) % 4);            // 0-3: starting altitude
    uint8_t sweepLen = 8 + (uint8_t)(lcg(s) % 5);        // 8-12 notes in sweep
    bool hasTremolo  = (lcg(s) % 2) == 0;
    bool hasTail     = (lcg(s) % 3) != 0;

    char buf[200];
    int pos = snprintf(buf, sizeof(buf), "CRY:d=32,o=5,b=%u:", bpm);

    bool first = true;
    auto emit = [&](const char* dur, const char* note) {
        if (pos >= 185) return;
        if (!first) buf[pos++] = ',';
        first = false;
        pos += snprintf(buf + pos, sizeof(buf) - pos, "%s%s", dur, note);
    };

    // Optional tremolo intro: rapidly alternate two adjacent high notes
    if (hasTremolo && startPos + 1 < kSweepCount) {
        uint8_t reps = 2 + (uint8_t)(lcg(s) % 2);  // 2-3 alternations
        for (uint8_t i = 0; i < reps; i++) {
            emit("32", kSweepNotes[startPos]);
            emit("32", kSweepNotes[startPos + 1]);
        }
    }

    // Descending sweep
    for (uint8_t i = 0; i < sweepLen; i++) {
        uint8_t idx = startPos + i;
        if (idx >= kSweepCount) break;
        emit("32", kSweepNotes[idx]);
    }

    // Held tail: last note of sweep sustained as 8th
    if (hasTail) {
        uint8_t endIdx = startPos + sweepLen - 1;
        if (endIdx >= kSweepCount) endIdx = kSweepCount - 1;
        emit("8", kSweepNotes[endIdx]);
    }

    strncpy(sCryRtttl, buf, sizeof(sCryRtttl) - 1);
}

// ── Music RTTTL: varied melodic shapes ────────────────────────────────────────
//
// Contour shapes: 0=arc, 1=zigzag, 2=random walk, 3=staircase(up2/down1), 4=descend
// Rhythm templates and wider BPM range ensure distinct feel per badge.
//
static void buildMusicRtttl(uint32_t seed) {
    uint32_t s = seed ^ 0xDEADBEEFu;

    uint8_t  scaleI  = (uint8_t)(lcg(s) % kMusicScaleCount);
    uint16_t bpm     = 80 + (uint16_t)(lcg(s) % 120);   // 80–199
    uint8_t  nNotes  = 8 + (uint8_t)(lcg(s) % 7);       // 8–14 notes
    uint8_t  shape   = (uint8_t)(lcg(s) % 5);           // contour
    uint8_t  rhythmI = (uint8_t)(lcg(s) % 5);           // rhythm template

    const MusicScale& sc = kScales[scaleI];
    int8_t cur = (int8_t)(lcg(s) % sc.len);             // start anywhere in scale

    char buf[220];
    int pos = snprintf(buf, sizeof(buf), "MUSIC:d=8,o=5,b=%u:", bpm);

    for (uint8_t i = 0; i < nNotes && pos < 200; i++) {
        uint8_t r = (uint8_t)(lcg(s) % 12);
        int8_t step;
        switch (shape) {
            case 0:  // arc: rise then fall
                step = (i < nNotes/2)
                    ? (r<7?1:r<10?0:-1)
                    : (r<7?-1:r<10?0:1);
                break;
            case 1:  // zigzag: alternate each note
                step = (i & 1) ? 1 : -1;
                if (r < 3) step = 0;
                break;
            case 2:  // random walk, no bias
                step = (r<4)?1:(r<8)?-1:0;
                break;
            case 3:  // staircase: up 2, down 1
                step = (i % 3 == 2) ? -1 : 1;
                break;
            default: // descend
                step = (r<7)?-1:(r<10)?0:1;
                break;
        }
        cur += step;
        if (cur < 0)               cur = 0;
        if (cur >= (int8_t)sc.len) cur = (int8_t)(sc.len - 1);

        // Occasional 2-step leap for variety
        uint8_t r2 = (uint8_t)(lcg(s) % 16);
        if      (r2 == 0 && cur >= 2)                     cur -= 2;
        else if (r2 == 1 && cur <= (int8_t)(sc.len - 3))  cur += 2;

        // Duration from rhythm template; occasional random surprise
        uint8_t durIdx = kRhythmPats[rhythmI][i % 4];
        uint8_t r3 = (uint8_t)(lcg(s) % 8);
        if (r3 == 0) durIdx = (uint8_t)(lcg(s) % 3);

        if (i > 0) buf[pos++] = ',';
        pos += snprintf(buf + pos, sizeof(buf) - pos,
                        "%u%s", (unsigned)kMusDurs[durIdx], sc.notes[cur]);
    }

    // Resolve to root
    if (pos < 212) {
        buf[pos++] = ',';
        pos += snprintf(buf + pos, sizeof(buf) - pos, "4%s", sc.notes[0]);
    }

    strncpy(sMusicRtttl, buf, sizeof(sMusicRtttl) - 1);
}

// ── Public API ────────────────────────────────────────────────────────────────

void badgeIdInit() {
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);

    sId24 = ((uint32_t)mac[3] << 16) | ((uint32_t)mac[4] << 8) | mac[5];
    snprintf(sHexId, sizeof(sHexId), "%02X%02X%02X", mac[3], mac[4], mac[5]);

    // Mix all 24 bits so that IDs differing in any byte get distinct names.
    // Without mixing, only bits 0-8 were used and mac[3] had no effect.
    uint32_t h = sId24 ^ (sId24 >> 9) ^ (sId24 >> 18);
    snprintf(sName, sizeof(sName), "%s%s%s",
             kPrefix[h        & 7u],
             kMiddle[(h >> 3) & 7u],
             kSuffix[(h >> 6) & 7u]);
    if (sName[0] >= 'a' && sName[0] <= 'z') sName[0] -= 32;  // capitalize

    buildCryRtttl(sId24);
    buildMusicRtttl(sId24);

    Serial.printf("[identity] ID=%s name=%s\n", sHexId, sName);
    Serial.printf("[identity] cry=%s\n", sCryRtttl);
    Serial.printf("[identity] music=%s\n", sMusicRtttl);
}

const char* badgeIdHexString() { return sHexId; }
const char* badgeIdName()      { return sName; }
uint32_t    badgeIdCrySeed()   { return sId24; }
const char* badgeIdCryRtttl()  { return sCryRtttl; }
const char* badgeIdMusicRtttl(){ return sMusicRtttl; }
