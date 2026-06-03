#pragma once
#include <stdint.h>

// ── Independent per-zone LED animations ──────────────────────────────────────
// Tour  = left ring (LEDs 0-2) + right ring (LEDs 8-10)
// Mouth = mouth LEDs (LEDs 3-7)
// Both zones animate simultaneously and independently.

enum class LedTourAnim : uint8_t {
    Off        = 0,
    BluePulse  = 1,   // slow breathing blue (default)
    CyanPulse  = 2,   // cyan breathing
    Rainbow    = 3,   // color wheel spinning across 6 LEDs
    Fire       = 4,   // red/orange flicker
    Police     = 5,   // alternating red left / blue right
    Comet      = 6,   // light sweeping around the ring
    Breathing  = 7,   // slow white breathing
    Disco      = 8,   // random colors per LED
    Strobe     = 9,   // rapid white strobe
    GreenPulse = 10,  // slow green breathing
    Aurora     = 11,  // shifting green/purple aurora borealis
    Sunset     = 12,  // warm orange/pink pulse
    Thunder    = 13,  // sharp white-blue lightning bursts
    Heartbeat  = 14,  // double-pulse red (ba-dum)
    Count
};

enum class LedMouthAnim : uint8_t {
    Off       = 0,
    Pink      = 1,   // pink wave (default)
    Rainbow   = 2,   // color wheel wave
    Lava      = 3,   // red/orange lava lamp
    Chill     = 4,   // slow blue/teal wave
    Candle    = 5,   // warm orange flicker
    Cyber     = 6,   // cyan/green chase
    VU        = 7,   // center-out green (fake VU meter)
    Bounce    = 8,   // color bounces back and forth
    Sparkle   = 9,   // random color twinkles
    Ocean     = 10,  // deep blue rolling waves
    Toxic     = 11,  // green/yellow acid pulse
    Aurora    = 12,  // shifting purple/teal northern lights
    Heartbeat = 13,  // double-pulse red (ba-dum)
    Matrix    = 14,  // green rain drops
    Count
};

void ledZonesSetTour(LedTourAnim a);
void ledZonesSetMouth(LedMouthAnim a);
LedTourAnim  ledZonesGetTour();
LedMouthAnim ledZonesGetMouth();

// Load/save selected animations from/to NVS.
void ledZonesLoad();
void ledZonesSave();

// Drive both zones. Call once per frame.
void ledZonesTick(uint32_t nowMs);

// Name tables (for BLE info / debug).
const char* ledTourAnimName(LedTourAnim a);
const char* ledMouthAnimName(LedMouthAnim a);
