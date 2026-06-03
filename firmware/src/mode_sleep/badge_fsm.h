#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "../eyes/eye_types.h"

// ── Operating modes ───────────────────────────────────────────────────────────
enum class BadgeMode : uint8_t {
    SleepMode,  // initial limited mode — tamagotchi active, badge mostly sleeping
    FullMode,   // unlocked — eyes always open, mouth animated, menu accessible
};

// ── Wake animation stage ──────────────────────────────────────────────────────
enum class WakeStage : uint8_t {
    Sleeping,   // in light sleep or just woken — BLE scan pending
    Peeking,    // button wake: eye flutter open + hold
    Closing,    // eye-close animation plays; sleep entry blocked until done
    WakingUp,   // BLE wake signal received: intro animation
    Awake,      // fully active
    Dozing,     // awake → sleep transition
};

extern BadgeMode  gBadgeMode;
extern WakeStage  gWakeStage;
extern uint32_t   gStageSinceMs;
extern bool       gBgScanActive;  // true = BG_ON signal, skip sleep

void badgeFsmInit();

// Targeted boot scan: only executed when CENTER is held at boot.
// Scans kBootTargetScanMs exclusively for ~B-flagged beacons.
// Provides visual feedback on both displays while waiting.
void badgeFsmTargetedBootScan();

// Main per-frame update. Must be called every loop iteration.
void badgeFsmTick(uint32_t nowMs);

// Signal handlers (safe to call from BLE callback — they only set flags).
void badgeFsmSignalWake();
void badgeFsmSignalSleep();
void badgeFsmSignalBgOn();
void badgeFsmSignalBgOff();
void badgeFsmSignalGattOn();
void badgeFsmSignalGattOff();
void badgeFsmSignalCry();
void badgeFsmSignalMessage(const char* text);
void badgeFsmSignalOta(const char* payload); // "version|ssid|psk|url"

// Returns true while a MSG overlay is being displayed.
// When true, callers should render eyes instead of their own UI.
bool badgeFsmMsgActive();

// Returns true if the FSM decided to enter light sleep this frame.
// If true, the caller should return immediately from loop().
// doScan: whether to run the pre-sleep BLE scan window (false at cold boot).
bool badgeFsmHandleSleepEntry(bool doScan = true);

// Pose / expression / scroll text for the current state (idle path only).
EyePose       badgeFsmComputePose(uint32_t nowMs);
EyeExpression badgeFsmComputeExpression(uint32_t nowMs);
const char*   badgeFsmScrollText(uint32_t nowMs);
