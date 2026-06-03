// mastermind_game.cpp — Mastermind minigame for badge_v2.
#include "mastermind_game.h"
#include "game_result.h"
#include "minigame_ui.h"
#include "inactivity.h"
#include "../hal/buttons.h"
#include "../hal/leds.h"
#include "../hal/display.h"
#include "../hal/buzzer.h"
#include "../config/hardware.h"
#include "../eyes/eye_geometry.h"
#include "../sound/rtttl.h"
#include <Arduino.h>
#include <string.h>
#include <esp_random.h>

namespace {

constexpr uint8_t  kCodeLen        = 4;
constexpr uint8_t  kSymbolCount    = 3;
constexpr uint8_t  kMaxAttempts    = 10;
constexpr uint32_t kFeedbackShowMs = 1400;
constexpr uint32_t kAnimMs         = 1500;
constexpr uint32_t kTourFlashMs    = 700;
constexpr int16_t  kGameTextY      = 35;

// Symbols for slots 0=L, 1=C, 2=R → '<', 'V', '>'
char symForSlot(uint8_t slot) {
    const char* syms = "<V>";
    return (slot < 3) ? syms[slot] : '?';
}

enum class Phase : uint8_t { Entering, Feedback, SuccessAnim, FailAnim, Done };

bool     sActive          = false;
Phase    sPhase           = Phase::Entering;
uint8_t  sSecret[kCodeLen];
uint8_t  sGuess[kCodeLen];
uint8_t  sGuessFill       = 0;
uint8_t  sAttemptsLeft    = kMaxAttempts;
uint8_t  sLastExact       = 0;
uint8_t  sLastPartial     = 0;
uint32_t sFeedbackEndMs   = 0;
uint32_t sPhaseEndMs      = 0;
bool     sPrevBtn[3]      = {};
InactivityTimer sInactivity;
bool     sInactivityReady = false;

void scoreMastermind(const uint8_t* secret, const uint8_t* guess,
                     uint8_t* outExact, uint8_t* outPartial) {
    bool secUsed[kCodeLen] = {};
    bool gueUsed[kCodeLen] = {};
    uint8_t exact = 0;
    for (uint8_t i = 0; i < kCodeLen; ++i) {
        if (guess[i] == secret[i]) {
            exact++;
            secUsed[i] = true;
            gueUsed[i] = true;
        }
    }
    uint8_t partial = 0;
    for (uint8_t gi = 0; gi < kCodeLen; ++gi) {
        if (gueUsed[gi]) continue;
        for (uint8_t si = 0; si < kCodeLen; ++si) {
            if (secUsed[si]) continue;
            if (guess[gi] == secret[si]) {
                partial++;
                secUsed[si] = true;
                gueUsed[gi] = true;
                break;
            }
        }
    }
    *outExact   = exact;
    *outPartial = partial;
}

void clearAllLeds() {
    for (uint8_t i = 0; i < kLedTotal; ++i) gLedStrip.setPixelColor(i, 0);
}

void syncLeds(uint32_t nowMs) {
    clearAllLeds();
    minigameUiPaintTourFlashIfActive(nowMs);
}

// ── Drawing ───────────────────────────────────────────────────────────────────

void drawLeftEye(uint32_t nowMs) {
    EyeUiGeom geom;
    if (!eyeUiBeginFrame(gLeftDisplay, false, nowMs, &geom)) return;
    char num[4];
    snprintf(num, sizeof(num), "%u", static_cast<unsigned>(sAttemptsLeft));
    eyeUiDrawRotatedKeyword(gLeftDisplay, &geom, num);
    gLeftDisplay.display();
}

void buildGuessLine(char* out, size_t outSz) {
    if (outSz < (size_t)(kCodeLen + 1)) { if (outSz > 0) out[0] = '\0'; return; }
    for (uint8_t i = 0; i < kCodeLen; ++i)
        out[i] = (i < sGuessFill) ? symForSlot(sGuess[i]) : '.';
    out[kCodeLen] = '\0';
}

void drawRightEntering(uint32_t nowMs) {
    EyeUiGeom geom;
    if (!eyeUiBeginFrame(gRightDisplay, true, nowMs, &geom)) return;
    char line[kCodeLen + 2];
    buildGuessLine(line, sizeof(line));
    eyeUiDrawTextCenteredNudged(gRightDisplay, kGameTextY, 2, line, 0);
    gRightDisplay.display();
}

void drawRightFeedback(uint32_t nowMs) {
    EyeUiGeom geom;
    if (!eyeUiBeginFrame(gRightDisplay, true, nowMs, &geom)) return;
    char lineExact[4], linePartial[4];
    snprintf(lineExact,   sizeof(lineExact),   "=%u", static_cast<unsigned>(sLastExact));
    snprintf(linePartial, sizeof(linePartial),  "~%u", static_cast<unsigned>(sLastPartial));
    eyeUiDrawTextCenteredNudged(gRightDisplay, 27, 2, lineExact,   0);
    eyeUiDrawTextCenteredNudged(gRightDisplay, 43, 2, linePartial, 0);
    gRightDisplay.display();
}

void drawAllUi(uint32_t nowMs) {
    switch (sPhase) {
        case Phase::SuccessAnim:
            minigameUiDrawWinBothEyes(nowMs);
            break;
        case Phase::FailAnim: {
            char secretLine[kCodeLen + 2];
            for (uint8_t i = 0; i < kCodeLen; ++i) secretLine[i] = symForSlot(sSecret[i]);
            secretLine[kCodeLen] = '\0';
            minigameUiDrawLoseBothEyes(nowMs, secretLine);
            break;
        }
        case Phase::Feedback:
            drawLeftEye(nowMs);
            drawRightFeedback(nowMs);
            break;
        case Phase::Entering:
        default:
            drawLeftEye(nowMs);
            drawRightEntering(nowMs);
            break;
    }
}

void pickSecret() {
    for (uint8_t i = 0; i < kCodeLen; ++i)
        sSecret[i] = static_cast<uint8_t>(esp_random() % kSymbolCount);
}

void playToneForSlot(uint8_t slot) {
    const char* tunes[3] = {
        "s1:d=16,o=5,b=400:16e",
        "s2:d=16,o=5,b=400:16g",
        "s3:d=16,o=5,b=400:16c6",
    };
    if (slot < 3) rtttlStart(tunes[slot]);
}

void submitGuess(uint32_t nowMs) {
    rtttlStop();
    uint8_t ex = 0, partial = 0;
    scoreMastermind(sSecret, sGuess, &ex, &partial);
    sLastExact   = ex;
    sLastPartial = partial;

    if (ex == kCodeLen) {
        rtttlStart("win:d=16,o=5,b=200:16g,16e,16g,16c6");
        minigameUiArmTourFlashGreen(nowMs, kTourFlashMs);
        sPhase      = Phase::SuccessAnim;
        sPhaseEndMs = nowMs + kAnimMs;
        return;
    }

    if (sAttemptsLeft > 0) --sAttemptsLeft;

    if (sAttemptsLeft == 0) {
        rtttlStart("go:d=8,o=3,b=200:8c,8c");
        minigameUiArmTourFlashRed(nowMs, kTourFlashMs);
        sPhase      = Phase::FailAnim;
        sPhaseEndMs = nowMs + kAnimMs;
        return;
    }

    sPhase         = Phase::Feedback;
    sFeedbackEndMs = nowMs + kFeedbackShowMs;
    if (ex > 0 || partial > 0)
        minigameUiArmTourFlashGreen(nowMs, kTourFlashMs);
    else
        minigameUiArmTourFlashRed(nowMs, kTourFlashMs);
}

}  // namespace

// ── Public API ────────────────────────────────────────────────────────────────

void mastermindGameStart() {
    rtttlStop();
    for (uint8_t i = 0; i < kLedTotal; ++i) gLedStrip.setPixelColor(i, 0);
    gLedStrip.show();

    sActive        = true;
    sPhase         = Phase::Entering;
    sGuessFill     = 0;
    sAttemptsLeft  = kMaxAttempts;
    pickSecret();
    sPrevBtn[0]    = gButtonPressedLatched[0];
    sPrevBtn[1]    = gButtonPressedLatched[1];
    sPrevBtn[2]    = gButtonPressedLatched[2];

    sInactivityReady = false;
    const uint32_t t = millis();
    drawAllUi(t);
    syncLeds(t);
}

void mastermindGameStop() {
    if (!sActive) return;
    sActive = false;
    sPhase  = Phase::Entering;
    rtttlStop();
    for (uint8_t i = 0; i < kLedTotal; ++i) gLedStrip.setPixelColor(i, 0);
    gLedStrip.show();
}

bool mastermindGameIsActive() { return sActive; }

void mastermindGameTick(uint32_t nowMs) {
    if (!sActive) return;

    if (sPhase == Phase::SuccessAnim) {
        drawAllUi(nowMs);
        syncLeds(nowMs);
        if (nowMs >= sPhaseEndMs) {
            gGameResult = GameResult::Won;
            mastermindGameStop();
        }
        return;
    }

    if (sPhase == Phase::FailAnim) {
        drawAllUi(nowMs);
        syncLeds(nowMs);
        if (nowMs >= sPhaseEndMs) {
            gGameResult = GameResult::Lost;
            mastermindGameStop();
        }
        return;
    }

    if (sPhase == Phase::Feedback) {
        drawAllUi(nowMs);
        syncLeds(nowMs);
        if (nowMs >= sFeedbackEndMs) {
            sGuessFill = 0;
            sPhase     = Phase::Entering;
            rtttlStop();
            sPrevBtn[0] = gButtonPressedLatched[0];
            sPrevBtn[1] = gButtonPressedLatched[1];
            sPrevBtn[2] = gButtonPressedLatched[2];
            drawAllUi(nowMs);
        }
        return;
    }

    // Entering: detect button edges.
    if (!sInactivityReady) { sInactivityReady = true; sInactivity.reset(nowMs); }
    InactivityState inact = sInactivity.tick(nowMs);
    if (inact == InactivityState::Timeout) { mastermindGameStop(); return; }

    for (uint8_t i = 0; i < 3; ++i) {
        const bool cur  = gButtonPressedLatched[i];
        const bool edge = cur && !sPrevBtn[i];
        sPrevBtn[i]     = cur;
        if (!edge || sGuessFill >= kCodeLen) continue;

        sInactivity.reset(nowMs);
        sGuess[sGuessFill] = i;
        ++sGuessFill;
        playToneForSlot(i);
        drawAllUi(nowMs);

        if (sGuessFill == kCodeLen) {
            submitGuess(nowMs);
            drawAllUi(nowMs);
        }
    }

    drawAllUi(nowMs);
    syncLeds(nowMs);
    if (sPhase == Phase::Entering && inact == InactivityState::Warning)
        minigameUiDrawInactivityWarning(nowMs, sInactivity.secondsLeft(nowMs));
}
