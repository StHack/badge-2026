#pragma once
#include <stdint.h>

// ── Main loop ────────────────────────────────────────────────────────────────
inline constexpr uint16_t kFrameMs          = 33;   // ~30 FPS
inline constexpr uint8_t  kBtnDebounceMs    = 5;

// ── Power / sleep ────────────────────────────────────────────────────────────
inline constexpr uint32_t kSleepIntervalSec = 30;   // RTC wake interval
inline constexpr uint32_t kBeaconScanMs     = 2000; // BLE passive scan window

// ── Button "peek" animation (button wake, no BLE signal found) ────────────────
inline constexpr uint16_t kPeekClosedMs     = 100;
inline constexpr uint16_t kPeekOpeningMs    = 220;
inline constexpr uint16_t kPeekHoldMs       = 3000;
inline constexpr uint16_t kPeekClosingMs    = 650;

// ── Wake intro animation (BLE wake signal received) ──────────────────────────
inline constexpr uint16_t kWakeIntroCloseMs = 220;
inline constexpr uint16_t kWakeIntroOpenMs  = 2800;
inline constexpr uint16_t kWakeIntroHoldMs  = 1200;

// ── Eye animation ────────────────────────────────────────────────────────────
inline constexpr uint16_t kBlinkIntervalMs  = 4500;
inline constexpr uint16_t kBlinkDurationMs  = 120;
inline constexpr float    kGazeDriftSpeed   = 0.0008f; // radians per ms
inline constexpr float    kGazeDriftRadius  = 0.22f;

// ── Tamagotchi ───────────────────────────────────────────────────────────────
inline constexpr uint8_t  kTamaMaxLevel      = 10;
inline constexpr uint8_t  kTamaFlagLevel     = 10;
inline constexpr uint32_t kTamaNeedMinSec    = 300;  // min seconds between needs (5 min)
inline constexpr uint32_t kTamaNeedMaxSec    = 600;  // max seconds between needs (10 min)

// Response window (ms): time to react before the need expires
// Levels 4+: press any button to launch the game (10 s to react)
inline constexpr uint32_t kTamaWindowLvl1    = 20000;  // level 0: click under 20 s
inline constexpr uint32_t kTamaWindowGame    = 15000;  // levels 1+: press to launch game

// Targeted boot scan (CENTER held at boot) — scans for ~B beacons
inline constexpr uint32_t kBootTargetScanMs  = 2000;  // 2 s scan window (exits early on first ~B)

// Success celebration durations
inline constexpr uint32_t kCelebLvlScrollMs  = 10000;  // "LVL X!" scroll (>2 passes)
inline constexpr uint32_t kCelebFlagScrollMs = 10000;  // CTF flag scroll (>2 passes)
inline constexpr uint32_t kCelebClosingMs    = 800;    // gentle eye close after celebration

// ── Menu hold times ──────────────────────────────────────────────────────────
inline constexpr uint16_t kMenuEnterHoldMs   = 500;
inline constexpr uint16_t kCalEnterHoldMs    = 2500;
inline constexpr uint16_t kCalSaveHoldMs     = 1500;
inline constexpr uint16_t kCalDiscardHoldMs  = 2000;
