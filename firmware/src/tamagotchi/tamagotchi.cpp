#include "tamagotchi.h"
#include "../config/hardware.h"
#include "../config/timing.h"
#include "../core/identity.h"
#include "../hal/buttons.h"
#include "../games/simon_game.h"
#include "../games/stop_game.h"
#include "../games/snake_game.h"
#include "../games/mastermind_game.h"
#include "../games/tetris_game.h"
#include "../games/flappy_game.h"
#include "../games/breakout_game.h"
#include "../games/pacman_game.h"
#include "../games/doodle_game.h"
#include "../games/maze_game.h"
#include "../games/dino_game.h"
#include "../games/space_game.h"
#include "../games/game_result.h"
#include "../eyes/eye_animation.h"
#include "../leds/led_anim.h"
#include "../sound/rtttl.h"
#include "../sound/cry.h"
#include "../hal/leds.h"
#include "../hal/display.h"
#include <Preferences.h>
#include <Arduino.h>

// ── CTF flag (embedded — change before event) ────────────────────────────────
static constexpr const char* kCtfFlag = "STHACK{l3v3l_UP!}";

// ── Victory jingles ───────────────────────────────────────────────────────────
static constexpr const char* kVictoryRtttl =
    "VIC:d=8,o=5,b=180:c5,e5,g5,c6,4e6,4g6,4e6,2c6";
// Epic fanfare for max-level (level 10) clear
static constexpr const char* kEpicVictoryRtttl =
    "EPIC:d=8,o=5,b=160:c5,e5,g5,c6,e6,g6,c7,4g6,e6,c6,4g5,e5,c5,2c6,4.c7";

// ── State ─────────────────────────────────────────────────────────────────────
TamaState gTama = {1, 0, 0};

static bool     sNeedActive   = false;
static uint32_t sNeedStartMs  = 0;
static bool     sWaitingGame  = false;
static bool     sSuccess      = false;
static bool     sFlagUnlocked = false;


// ── Celebration FSM ───────────────────────────────────────────────────────────
// Hearts → LvlScroll → [FlagScroll] → Closing → Idle
enum class CelebPhase : uint8_t { Idle, Hearts, LvlScroll, FlagScroll, Closing };
static CelebPhase sCelebPhase   = CelebPhase::Idle;
static uint32_t   sCelebStartMs = 0;
static bool       sCelebNewFlag = false;  // flag just unlocked this level-up?

// ── NVS ───────────────────────────────────────────────────────────────────────
static void saveNvs() {
    Preferences p;
    p.begin(kNvsTama, false);
    p.putUChar("level", gTama.level);
    p.putULong("wakes", gTama.wakeCount);
    p.end();
}

// ── Helpers ───────────────────────────────────────────────────────────────────
static uint32_t responseWindowMs() {
    return kTamaWindowGame;   // press to launch game at all levels
}

static void scheduleNextNeed() {
    uint32_t minWakes = kTamaNeedMinSec / kSleepIntervalSec;
    uint32_t maxWakes = kTamaNeedMaxSec / kSleepIntervalSec;
    if (minWakes < 1) minWakes = 1;
    if (maxWakes < minWakes) maxWakes = minWakes;
    gTama.needAt = gTama.wakeCount
                 + (uint32_t)(random(minWakes, maxWakes + 1));
}

static void triggerNeed() {
    sNeedActive   = true;
    sNeedStartMs  = millis();
    sWaitingGame  = false;
    sSuccess      = false;

    eyeAnimSetMode(EyeAnimMode::ForceSick);
    ledAnimSetMode(LedAnimMode::TamaCry);
    cryStart(badgeIdCrySeed());

    Serial.printf("[tama] need triggered at level %u (window %ums)\n",
                  gTama.level, responseWindowMs());
}

static void levelUp() {
    gTama.level++;
    if (gTama.level > kTamaMaxLevel) gTama.level = kTamaMaxLevel;
    gTama.wakeCount = 0;
    saveNvs();
    scheduleNextNeed();

    // Flag is unlocked from level 8 onwards but only displayed at max level (10)
    if (gTama.level >= kTamaFlagLevel) sFlagUnlocked = true;
    sCelebNewFlag = (gTama.level >= kTamaMaxLevel);

    // Phase 1: hearts + victory jingle (tamaTick drives the rest)
    sCelebPhase   = CelebPhase::Hearts;
    sCelebStartMs = 0;  // set on first tick

    eyeAnimSetMode(EyeAnimMode::ForceHearts);
    ledAnimSetMode(LedAnimMode::TamaSuccess);
    rtttlStart(gTama.level >= kTamaMaxLevel ? kEpicVictoryRtttl : kVictoryRtttl);

    Serial.printf("[tama] level up → %u%s\n", gTama.level,
                  sFlagUnlocked ? " FLAG UNLOCKED" : "");
}

static void failNeed() {
    sNeedActive  = false;
    sWaitingGame = false;
    ledAnimSetMode(LedAnimMode::Off);
    eyeAnimSetMode(EyeAnimMode::ForceClosed);
    scheduleNextNeed();
    Serial.println("[tama] need failed / timeout");
}

// ── Public API ────────────────────────────────────────────────────────────────

void tamaInit() {
    Preferences p;
    p.begin(kNvsTama, true);
    gTama.level     = p.getUChar("level", 0);
    gTama.wakeCount = p.getULong("wakes", 0);
    p.end();
    if (gTama.level > kTamaMaxLevel) gTama.level = 0;  // guard only, no lower bound
    if (gTama.level > kTamaMaxLevel) gTama.level = kTamaMaxLevel;
    sFlagUnlocked = (gTama.level >= kTamaFlagLevel);  // flag unlocked at max level (10)
    scheduleNextNeed();
    Serial.printf("[tama] init: level=%u wakes=%u nextNeed=%u\n",
                  gTama.level, gTama.wakeCount, gTama.needAt);
}

bool tamaOnWake() {
    gTama.wakeCount++;
    if (!sNeedActive && gTama.wakeCount >= gTama.needAt) {
        triggerNeed();
        return true;
    }
    return false;
}

bool tamaTick(uint32_t nowMs) {
    // ── Celebration FSM (runs regardless of sNeedActive) ─────────────────────
    if (sCelebPhase != CelebPhase::Idle) {
        // A new TAMA need was triggered (by BLE signal via badgeFsmTick) while
        // the celebration was still running. Abort the animation immediately so
        // triggerNeed()'s ForceSick is not overridden by the closing animation.
        if (sNeedActive) {
            sCelebPhase = CelebPhase::Idle;
            // ForceSick + TamaCry already set by triggerNeed() — do not touch them.
        } else {
            if (sCelebStartMs == 0) sCelebStartMs = nowMs;

            switch (sCelebPhase) {
                case CelebPhase::Hearts:
                    // Wait for victory jingle to finish, then scroll the level
                    if (!rtttlIsPlaying()) {
                        static char lvlMsg[20];
                        snprintf(lvlMsg, sizeof(lvlMsg), " LEVEL %u/%u! ",
                                 gTama.level, kTamaMaxLevel);
                        eyeAnimSetMode(EyeAnimMode::ForceScrollText, lvlMsg);
                        sCelebPhase   = CelebPhase::LvlScroll;
                        sCelebStartMs = nowMs;
                    }
                    return true;

                case CelebPhase::LvlScroll:
                    if (nowMs - sCelebStartMs >= kCelebLvlScrollMs) {
                        if (sCelebNewFlag) {
                            // Newly unlocked: scroll the CTF flag
                            eyeAnimSetMode(EyeAnimMode::ForceScrollText, kCtfFlag);
                            sCelebPhase   = CelebPhase::FlagScroll;
                            sCelebStartMs = nowMs;
                        } else {
                            ledAnimSetMode(LedAnimMode::Off);
                            eyeAnimSetMode(EyeAnimMode::ForceClosing);
                            sCelebPhase   = CelebPhase::Closing;
                            sCelebStartMs = nowMs;
                        }
                    }
                    return true;

                case CelebPhase::FlagScroll:
                    if (nowMs - sCelebStartMs >= kCelebFlagScrollMs) {
                        ledAnimSetMode(LedAnimMode::Off);
                        eyeAnimSetMode(EyeAnimMode::ForceClosing);
                        sCelebPhase   = CelebPhase::Closing;
                        sCelebStartMs = nowMs;
                    }
                    return true;

                case CelebPhase::Closing:
                    if (nowMs - sCelebStartMs >= kCelebClosingMs) {
                        eyeAnimSetMode(EyeAnimMode::ForceClosed);
                        sCelebPhase = CelebPhase::Idle;
                    }
                    return true;

                default:
                    break;
            }
        }
    }

    if (!sNeedActive) return false;

    // Check window timeout — only before the game is launched.
    // Once sWaitingGame is true, the game manages its own duration.
    if (!sWaitingGame && nowMs - sNeedStartMs > responseWindowMs()) {
        failNeed();
        return false;
    }

    // Poll game results (check all games)
    if (sWaitingGame) {
        const bool anyActive = simonGameIsActive()    || stopGameIsActive()
                            || snakeGameIsActive()    || mastermindGameIsActive()
                            || tetrisGameIsActive()   || flappyGameIsActive()
                            || breakoutGameIsActive() || pacmanGameIsActive()
                            || doodleGameIsActive()   || mazeGameIsActive()
                            || dinoGameIsActive()     || spaceGameIsActive();
        if (!anyActive) {
            // Game ended
            sWaitingGame = false;
            if (gGameResult == GameResult::Won) {
                sNeedActive = false;
                sSuccess = true;
                levelUp();
            } else {
                failNeed();
            }
            gGameResult = GameResult::None;
        }
        return true;
    }

    return true;
}

void tamaOnButton(uint8_t buttonMask) {
    if (!sNeedActive || sWaitingGame) return;
    (void)buttonMask;  // levels 1-3 accept any button; games are launched on any press

    uint8_t level = gTama.level;

    if (level == 0) {
        // Level 0 → gain 1: Stop game — 3 rounds
        stopGameStart(3);
        sWaitingGame = true;

    } else if (level == 1) {
        // Level 1 → gain 2: Dino — 5 cacti
        dinoGameStart(5);
        sWaitingGame = true;

    } else if (level == 2) {
        // Level 2 → gain 3: Maze
        mazeGameStart(16, 16);
        sWaitingGame = true;

    } else if (level == 3) {
        // Level 3 → gain 4: Pac-Man
        pacmanGameStart();
        sWaitingGame = true;

    } else if (level == 4) {
        // Level 4 → gain 5: Snake — 15 points
        snakeGameStart(10);
        sWaitingGame = true;

    } else if (level == 5) {
        // Level 5 → gain 6: Tetris — 10 lines
        tetrisGameStart(8);
        sWaitingGame = true;

    } else if (level == 6) {
        // Level 6 → gain 7: Breakout
        breakoutGameStart(40);
        sWaitingGame = true;

    } else if (level == 7) {
        // Level 7 → gain 8: Simon — 8 rounds
        simonGameStart(8);
        sWaitingGame = true;

    } else if (level == 8) {
        // Level 8 → gain 9: Flappy Bird — 15 points
        flappyGameStart(10);
        sWaitingGame = true;

    } else {
        // Level 9 → gain 10: Mastermind
        mastermindGameStart();
        sWaitingGame = true;
    }
}

bool tamaNeedActive() { return sNeedActive; }
bool tamaBusy()       { return sNeedActive || sCelebPhase != CelebPhase::Idle; }

void tamaTriggerNeed() {
    if (sNeedActive) return;
    triggerNeed();
}

bool tamaConsumeSuccess() {
    if (sSuccess) { sSuccess = false; return true; }
    return false;
}

bool    tamaFlagUnlocked()     { return sFlagUnlocked; }
const char* tamaCtfFlag()      { return kCtfFlag; }
bool    tamaMaxLevelReached()  { return gTama.level >= kTamaMaxLevel; }

void tamaSetLevel(uint8_t level) {
    if (level > kTamaMaxLevel) level = kTamaMaxLevel;
    gTama.level     = level;
    gTama.wakeCount = 0;
    sFlagUnlocked   = (gTama.level >= kTamaFlagLevel);
    saveNvs();
    scheduleNextNeed();
    Serial.printf("[tama] debug setLevel → %u\n", gTama.level);
}
