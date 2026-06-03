#pragma once
#include <stdint.h>

// Inactivity timeout shared by static minigames (maze, mastermind, simon).
// Warning fires at 4:30 (30 s to react), timeout at 5:00.

enum class InactivityState : uint8_t { Active, Warning, Timeout };

inline constexpr uint32_t kInactivityWarnMs    = (4 * 60 + 30) * 1000u;  // 4:30
inline constexpr uint32_t kInactivityTimeoutMs = (5 * 60)      * 1000u;  // 5:00

struct InactivityTimer {
    uint32_t lastMs = 0;

    void reset(uint32_t nowMs) { lastMs = nowMs; }

    InactivityState tick(uint32_t nowMs) const {
        uint32_t idle = nowMs - lastMs;
        if (idle >= kInactivityTimeoutMs) return InactivityState::Timeout;
        if (idle >= kInactivityWarnMs)    return InactivityState::Warning;
        return InactivityState::Active;
    }

    uint32_t secondsLeft(uint32_t nowMs) const {
        uint32_t idle = nowMs - lastMs;
        return idle < kInactivityTimeoutMs ? (kInactivityTimeoutMs - idle + 999) / 1000 : 0;
    }
};
