// simon_game.cpp — Simon Says minigame for badge_v2.
// 6 levels (0→5): level 0 has no sequence, level 5 has 5 LEDs.
// Countdown and GO shown on left eye; sequence on right eye.
// GO persists during input; player may press as soon as GO appears.
#include "simon_game.h"
#include "game_result.h"
#include "minigame_ui.h"
#include "inactivity.h"
#include "../hal/buttons.h"
#include "../hal/leds.h"
#include "../hal/display.h"
#include "../config/hardware.h"
#include "../eyes/eye_geometry.h"
#include "../sound/rtttl.h"
#include <Arduino.h>
#include <esp_random.h>
#include <string.h>

namespace {

constexpr uint8_t  kWinLevel        = 5;   // built-in default win level
constexpr uint8_t  kMaxWinLevel     = 8;   // array bound (non-infinite)
constexpr uint8_t  kSeqBufSize      = 32;  // max sequence length in infinite mode
constexpr uint32_t kFlashOnMs       = 380;
constexpr uint32_t kFlashGapMs      = 120;
constexpr uint32_t kWinDisplayMs    = 4000;
constexpr uint32_t kIntermissionMs  = 1400;
constexpr uint32_t kCountdownStepMs = 600;
constexpr uint32_t kInputTailMinMs  = 450;
constexpr uint32_t kTourFlashMs     = 700;
constexpr uint32_t kLoseCrossMs     = 900;
constexpr uint32_t kLoseSolutionMs  = 2200;
constexpr int16_t  kTextY           = 38;   // left eye: count / GO
constexpr int16_t  kSeqTextY        = 38;   // right eye: sequence

enum class Phase : uint8_t {
    Countdown,
    Showing,
    WaitingInput,
    LevelCompleteTail,
    BetweenLevels,
    LoseFeedback,
    Won,
};

bool     sActive            = false;
Phase    sPhase             = Phase::Showing;
uint8_t  sSeq[kSeqBufSize];
uint8_t  sWinLevel      = kWinLevel;  // runtime win level (set by simonGameStart)
uint8_t  sLen               = 0;
uint8_t  sShowIdx           = 0;
bool     sShowLit           = false;
uint32_t sNextTransitionMs  = 0;
uint8_t  sInputIdx          = 0;
bool     sPrevBtn[3]        = {};
uint32_t sPhaseEndMs        = 0;
uint32_t sLoseCrossUntilMs  = 0;
uint8_t  sCdCount           = 3;
uint32_t sLevelTailMinMs    = 0;
char     sLineBuf[16]       = "";
bool     sGoVisible         = false;  // GO shown on left eye during WaitingInput
InactivityTimer sInactivity;

// ── Helpers ───────────────────────────────────────────────────────────────────

char symForSlot(uint8_t slot) {
    const char* syms = "<V>";
    return (slot < 3) ? syms[slot] : '?';
}

void appendSym(uint8_t slot) {
    size_t n = strlen(sLineBuf);
    if (n + 1U < sizeof(sLineBuf)) {
        sLineBuf[n]     = symForSlot(slot);
        sLineBuf[n + 1] = '\0';
    }
}

void clearLineBuf() { sLineBuf[0] = '\0'; }

static const char* kTones[3] = {
    "s1:d=16,o=5,b=400:16e",
    "s2:d=16,o=5,b=400:16g",
    "s3:d=16,o=5,b=400:16c6",
};

void playTone(uint8_t slot) { if (slot < 3) rtttlStart(kTones[slot]); }

// Mouth bar: progress toward sWinLevel, cycling every kLedMouth rounds.
// Cycle 0 = blue, cycle 1 = red, cycle 2 = blue, ...
void syncMouthLeds() {
    uint8_t done   = (sLen > 0) ? sLen - 1 : 0;
    uint8_t cycle  = done / kLedMouth;          // which fill cycle we're in
    uint8_t nMouth = done % kLedMouth;           // LEDs lit in current cycle
    uint32_t c = (cycle % 2 == 0)
               ? gLedStrip.Color(0,  90, 200)   // blue
               : gLedStrip.Color(200, 20,  0);  // red
    for (uint8_t i = 0; i < kLedMouth; ++i) {
        gLedStrip.setPixelColor(kLedMouthStart + i, (i >= kLedMouth - nMouth) ? c : 0);
    }
    gLedStrip.show();
}

// ── Drawing ───────────────────────────────────────────────────────────────────

void drawLeftText(uint32_t nowMs, const char* txt, uint8_t sz = 2) {
    EyeUiGeom g;
    if (!eyeUiBeginFrame(gLeftDisplay, false, nowMs, &g)) return;
    eyeUiDrawTextCenteredNudged(gLeftDisplay, kTextY, sz, txt);
    gLeftDisplay.display();
}

// When launched from tama (sWinLevel != kWinLevel), show "X/N" round progress.
// done = rounds fully completed (sequences validated).
void drawLeftOutline(uint32_t nowMs) {
    EyeUiGeom g;
    if (!eyeUiBeginFrame(gLeftDisplay, false, nowMs, &g)) return;
    if (sWinLevel != kWinLevel) {
        char buf[8];
        uint8_t done = (sLen > 0) ? sLen - 1 : 0;
        if (sWinLevel > 0)
            snprintf(buf, sizeof(buf), "%u/%u", (unsigned)done, (unsigned)sWinLevel);
        else
            snprintf(buf, sizeof(buf), "%u", (unsigned)done);
        eyeUiDrawTextCenteredNudged(gLeftDisplay, kTextY, 2, buf);
    }
    gLeftDisplay.display();
}

void drawRightOutline(uint32_t nowMs) {
    EyeUiGeom g;
    if (eyeUiBeginFrame(gRightDisplay, true, nowMs, &g)) gRightDisplay.display();
}

void drawSequenceRight(uint32_t nowMs) {
    EyeUiGeom g;
    if (!eyeUiBeginFrame(gRightDisplay, true, nowMs, &g)) return;
    if (sLineBuf[0] != '\0') {
        uint8_t tz = (strlen(sLineBuf) > 6U) ? 1U : 2U;
        eyeUiDrawTextCenteredNudged(gRightDisplay, kSeqTextY, tz, sLineBuf);
    }
    gRightDisplay.display();
}

void drawLoseSolution(uint32_t nowMs) {
    drawLeftOutline(nowMs);
    char sol[16] = {};
    for (uint8_t i = 0; i < sLen && i + 1U < sizeof(sol); ++i)
        sol[i] = symForSlot(sSeq[i]);
    EyeUiGeom g;
    if (!eyeUiBeginFrame(gRightDisplay, true, nowMs, &g)) return;
    uint8_t tz = (strlen(sol) > 7U) ? 1U : 2U;
    eyeUiDrawTextCenteredNudged(gRightDisplay, kSeqTextY, tz, sol);
    gRightDisplay.display();
}

void drawAllUi(uint32_t nowMs) {
    switch (sPhase) {
        case Phase::Won:
            minigameUiDrawWinBothEyes(nowMs);
            return;
        case Phase::LoseFeedback:
            if (nowMs < sLoseCrossUntilMs)
                minigameUiDrawLoseBothEyes(nowMs);
            else
                drawLoseSolution(nowMs);
            return;
        case Phase::BetweenLevels:
            // Plain outline — no score text between checkmark and next countdown.
            { EyeUiGeom g;
              if (eyeUiBeginFrame(gLeftDisplay,  false, nowMs, &g)) gLeftDisplay.display();
              if (eyeUiBeginFrame(gRightDisplay, true,  nowMs, &g)) gRightDisplay.display(); }
            return;
        case Phase::LevelCompleteTail:
            minigameUiDrawWinBothEyes(nowMs);
            return;
        case Phase::Countdown: {
            char digit[2] = {(char)('0' + sCdCount), '\0'};
            drawLeftText(nowMs, digit);
            drawRightOutline(nowMs);
            return;
        }
        case Phase::WaitingInput:
            // In tama mode show "X/N" so the player sees their round progress.
            // In free-play (default win level) show "GO".
            if (sWinLevel != kWinLevel) {
                drawLeftOutline(nowMs);  // drawLeftOutline already renders "X/N"
            } else {
                drawLeftText(nowMs, "GO");
            }
            drawSequenceRight(nowMs);
            return;
        default:
            drawLeftOutline(nowMs);
            drawSequenceRight(nowMs);
            return;
    }
}

// ── Phase transitions ─────────────────────────────────────────────────────────

void enterWaitingInput(uint32_t nowMs) {
    sPhase     = Phase::WaitingInput;
    sInputIdx  = 0;
    sGoVisible = true;
    clearLineBuf();
    sPrevBtn[0] = gButtonPressedLatched[0];
    sPrevBtn[1] = gButtonPressedLatched[1];
    sPrevBtn[2] = gButtonPressedLatched[2];
}

void beginShowing(uint32_t nowMs) {
    // Level 0: no sequence to show — go straight to WaitingInput
    if (sLen == 0) {
        enterWaitingInput(nowMs);
        drawAllUi(nowMs);
        return;
    }
    sPhase    = Phase::Showing;
    sShowIdx  = 0;
    sShowLit  = true;
    clearLineBuf();
    playTone(sSeq[0]);
    appendSym(sSeq[0]);
    sNextTransitionMs = nowMs + kFlashOnMs;
    drawAllUi(nowMs);
}

void enterCountdown(uint32_t nowMs) {
    sPhase    = Phase::Countdown;
    sCdCount  = 3;
    sGoVisible = false;
    sNextTransitionMs = nowMs + kCountdownStepMs;
}

void resetToLevelZero(uint32_t nowMs) {
    sLen      = 0;
    sInputIdx = 0;
    clearLineBuf();
    rtttlStop();
    enterCountdown(nowMs);
    drawAllUi(nowMs);
    syncMouthLeds();
}

void appendRandomStep() {
    if (sLen < kSeqBufSize) sSeq[sLen++] = (uint8_t)(esp_random() % 3U);
}

} // namespace

// ── Public API ────────────────────────────────────────────────────────────────

void simonGameStart(uint8_t numRounds) {
    sWinLevel = (numRounds == 0) ? 0
              : (numRounds <= kMaxWinLevel) ? numRounds : kWinLevel;
    rtttlStop();
    for (uint8_t i = 0; i < kLedTotal; ++i) gLedStrip.setPixelColor(i, 0);
    gLedStrip.show();

    sActive    = true;
    sLen       = 0;
    sInputIdx  = 0;
    sGoVisible = false;
    clearLineBuf();
    sPrevBtn[0] = gButtonPressedLatched[0];
    sPrevBtn[1] = gButtonPressedLatched[1];
    sPrevBtn[2] = gButtonPressedLatched[2];
    gGameResult = GameResult::None;

    appendRandomStep();   // first round has 1 element

    const uint32_t t = millis();
    sInactivity.reset(t);
    enterCountdown(t);
    drawAllUi(t);
    syncMouthLeds();
    Serial.println("[simon] started — 5 levels (1→5 LEDs)");
}

bool simonGameIsActive() { return sActive; }

void simonGameStop() {
    if (!sActive) return;
    sActive = false;
    rtttlStop();
    for (uint8_t i = 0; i < kLedTotal; ++i) gLedStrip.setPixelColor(i, 0);
    gLedStrip.show();
}

void simonGameTick(uint32_t nowMs) {
    if (!sActive) return;

    // ── LoseFeedback ──────────────────────────────────────────────────────────
    if (sPhase == Phase::LoseFeedback) {
        drawAllUi(nowMs);
        syncMouthLeds();
        if (nowMs >= sPhaseEndMs) {
            // Free-play (default win level): restart from round 1.
            // All other modes (tama fixed target, infinite): game over.
            if (sWinLevel == kWinLevel) {
                resetToLevelZero(nowMs);
            } else {
                gGameResult = GameResult::Lost;
                simonGameStop();
            }
        }
        return;
    }

    // ── Won ───────────────────────────────────────────────────────────────────
    if (sPhase == Phase::Won) {
        drawAllUi(nowMs);
        syncMouthLeds();
        if (nowMs >= sPhaseEndMs) {
            gGameResult = GameResult::Won;
            simonGameStop();
        }
        return;
    }

    // ── BetweenLevels ─────────────────────────────────────────────────────────
    if (sPhase == Phase::BetweenLevels) {
        drawAllUi(nowMs);
        syncMouthLeds();
        if (nowMs >= sNextTransitionMs) {
            appendRandomStep();
            enterCountdown(nowMs);
            drawAllUi(nowMs);
        }
        return;
    }

    // ── LevelCompleteTail ─────────────────────────────────────────────────────
    if (sPhase == Phase::LevelCompleteTail) {
        drawAllUi(nowMs);
        syncMouthLeds();
        if (nowMs >= sLevelTailMinMs && !rtttlIsPlaying()) {
            clearLineBuf();
            rtttlStop();
            sPhase = Phase::BetweenLevels;
            sNextTransitionMs = nowMs + kIntermissionMs;
            drawAllUi(nowMs);
        }
        return;
    }

    // ── Countdown ─────────────────────────────────────────────────────────────
    if (sPhase == Phase::Countdown) {
        drawAllUi(nowMs);
        syncMouthLeds();
        if (nowMs < sNextTransitionMs) return;
        if (sCdCount <= 1) {
            beginShowing(nowMs);
            return;
        }
        --sCdCount;
        sNextTransitionMs = nowMs + kCountdownStepMs;
        drawAllUi(nowMs);
        return;
    }

    // ── Showing ───────────────────────────────────────────────────────────────
    if (sPhase == Phase::Showing) {
        syncMouthLeds();
        if (nowMs < sNextTransitionMs) return;
        if (sShowLit) {
            rtttlStop();
            sShowLit = false;
            sNextTransitionMs = nowMs + kFlashGapMs;
        } else {
            ++sShowIdx;
            if (sShowIdx >= sLen) {
                // Sequence fully shown — enter input phase immediately (GO on left)
                enterWaitingInput(nowMs);
                drawAllUi(nowMs);
            } else {
                sShowLit = true;
                playTone(sSeq[sShowIdx]);
                appendSym(sSeq[sShowIdx]);
                sNextTransitionMs = nowMs + kFlashOnMs;
                drawAllUi(nowMs);
            }
        }
        return;
    }

    // ── WaitingInput ──────────────────────────────────────────────────────────
    {
        InactivityState inact = sInactivity.tick(nowMs);
        if (inact == InactivityState::Timeout) { simonGameStop(); return; }
        syncMouthLeds();
        drawAllUi(nowMs);   // keep redrawing GO + current input string
        if (inact == InactivityState::Warning)
            minigameUiDrawInactivityWarning(nowMs, sInactivity.secondsLeft(nowMs));
    }

    for (uint8_t i = 0; i < 3; ++i) {
        const bool cur  = gButtonPressedLatched[i];
        const bool edge = cur && !sPrevBtn[i];
        sPrevBtn[i] = cur;
        if (!edge) continue;

        sInactivity.reset(nowMs);
        playTone(i);

        // Level 0 has no sequence — any press completes it
        if (sLen > 0 && i != sSeq[sInputIdx]) {
            // Wrong button
            rtttlStart("go:d=8,o=3,b=200:8c,8c");
            sPhase            = Phase::LoseFeedback;
            sLoseCrossUntilMs = nowMs + kLoseCrossMs;
            sPhaseEndMs       = nowMs + kLoseCrossMs + kLoseSolutionMs;
            sGoVisible        = false;
            minigameUiArmTourFlashRed(nowMs, kTourFlashMs);
            drawAllUi(nowMs);
            return;
        }

        if (sLen > 0) appendSym(i);
        ++sInputIdx;
        drawAllUi(nowMs);

        if (sInputIdx >= sLen) {
            if (sWinLevel > 0 && sLen >= sWinLevel) {
                // Win!
                rtttlStart("win:d=16,o=5,b=200:16g,16e,16g,16c6");
                sPhase      = Phase::Won;
                sPhaseEndMs = nowMs + kWinDisplayMs;
                sGoVisible  = false;
                minigameUiArmTourFlashGreen(nowMs, kTourFlashMs);
                drawAllUi(nowMs);
                return;
            }
            // Level complete
            sPhase          = Phase::LevelCompleteTail;
            sLevelTailMinMs = nowMs + kInputTailMinMs;
            sGoVisible      = false;
            minigameUiArmTourFlashGreen(nowMs, kTourFlashMs);
            drawAllUi(nowMs);
        }
    }
}
