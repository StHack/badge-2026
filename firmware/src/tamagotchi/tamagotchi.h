#pragma once
#include <stdint.h>
#include <stdbool.h>

// ── Tamagotchi state ──────────────────────────────────────────────────────────
// Persisted in NVS "tama" namespace: "level" (u8), "wakes" (u32).
struct TamaState {
    uint8_t  level;      // 1–10
    uint32_t wakeCount;  // total wakes since last level-up
    uint32_t needAt;     // wake count when next need triggers (not persisted)
};

extern TamaState gTama;

// Load from NVS, set up next need trigger.
void tamaInit();

// Called on each sleep-cycle wake: increments counter, checks if need triggers.
// Returns true if a need has just been triggered.
bool tamaOnWake();

// Per-frame tick. Drives need countdown + game result polling.
// Returns true while a need or interaction is active (caller should not sleep).
bool tamaTick(uint32_t nowMs);

// True if a need is currently active (no side effects — safe for sleep checks).
bool tamaNeedActive();

// True during active need OR post-success celebration (hearts + level scroll).
// Use this to block sleep and stage transitions during the full interaction.
bool tamaBusy();

// Call when a button is pressed during an active need (pre-game tasks).
// buttonMask: bit 0=left, 1=center, 2=right
void tamaOnButton(uint8_t buttonMask);

// Force-trigger a need immediately (e.g. via BLE command).
// No-op if a need is already active.
void tamaTriggerNeed();

// True after a successful interaction (poll once, auto-cleared).
bool tamaConsumeSuccess();

bool        tamaFlagUnlocked();
const char* tamaCtfFlag();
bool        tamaMaxLevelReached();

// Debug: force-set level (clamps to [1, kTamaMaxLevel]), resets wake counter,
// persists to NVS, reschedules next need.
void tamaSetLevel(uint8_t level);
