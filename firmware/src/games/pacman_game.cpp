// pacman_game.cpp — Pac-Man minigame for badge_v2, rebuilt from scratch.
//
// Field     : eye interior, 3-px cells (42 × 21 grid)
// Pellets   : checkerboard pattern on playable cells (every (x+y)%2==0 cell)
// Win       : eat sWinPellets dots
// Lose      : ghost occupies same cell as Pac-Man
//
// Controls  : LEFT  = turn left relative to heading
//             RIGHT = turn right relative to heading
//             CENTER = 180° reverse
//
// Ghost A   : pure Manhattan chaser (hollow square visual)
// Ghost B   : wanders 5 s then chases (filled square visual)
// Pac-Man   : moves continuously, stops at eye boundary until redirected
//             pending direction is retried each tick (cornering)
//
// Play mask is computed once at init and never updated — eye outline still
// animates visually but game bounds stay stable throughout the match.

#include "pacman_game.h"
#include "game_result.h"
#include "minigame_ui.h"
#include "../hal/buttons.h"
#include "../hal/leds.h"
#include "../hal/display.h"
#include "../hal/buzzer.h"
#include "../config/hardware.h"
#include "../eyes/eye_geometry.h"
#include "../sound/rtttl.h"
#include <Arduino.h>
#include <string.h>
#include <algorithm>
#include <esp_random.h>

namespace {

// ── Tunables ──────────────────────────────────────────────────────────────────
constexpr uint32_t kCountdownStepMs = 700;
constexpr uint32_t kGoCueMs         = 600;
constexpr uint32_t kAnimMs          = 1500;
constexpr uint32_t kTourFlashMs     = 500;
constexpr uint32_t kPacTickMs       = 175;   // Pac move interval
constexpr uint32_t kGhostATickMs    = 270;   // Ghost A (pure chaser)
constexpr uint32_t kGhostBTickMs    = 320;   // Ghost B (wander → chase)
constexpr uint32_t kGhostBChaseMs   = 5000;  // Ghost B starts chasing after this
constexpr uint8_t  kWinPellets      = 20;    // default win target

constexpr uint8_t  kCellPx          = 3;
constexpr int      kGridW           = 42;
constexpr int      kGridH           = 21;
constexpr int      kMidGx           = 21;
constexpr int      kMidGy           = 10;
constexpr int16_t  kMargin          = 1;     // shrink eye boundary slightly

// Directions: 0=up 1=right 2=down 3=left
constexpr int8_t kDx[4] = {  0,  1,  0, -1 };
constexpr int8_t kDy[4] = { -1,  0,  1,  0 };

constexpr uint8_t kNoPendDir = 0xFF;

enum class Phase : uint8_t { Countdown, GoCue, Playing, SuccessAnim, FailAnim, Done };

// ── State ─────────────────────────────────────────────────────────────────────
static bool     sActive          = false;
static Phase    sPhase           = Phase::Countdown;
static uint8_t  sCdCount         = 3;
static uint32_t sNextMs          = 0;
static uint32_t sPhaseEndMs      = 0;
static uint32_t sGameStartMs     = 0;
static uint8_t  sWinPellets      = kWinPellets;

// Fixed play mask (computed once at init)
static bool     sPlay[kGridH][kGridW];

// Pellets
static bool     sPellet[kGridH][kGridW];
static uint8_t  sPelletsEaten    = 0;

// Pac-Man
static uint8_t  sPacGx, sPacGy;
static uint8_t  sPacDir   = 1;       // current heading (0-3)
static uint8_t  sPendDir  = kNoPendDir; // buffered direction request
static bool     sMouthOpen = false;
static uint32_t sLastPacMs = 0;

// Ghosts [0]=A chaser  [1]=B wanderer
static uint8_t  sGx[2], sGy[2], sGDir[2];
static uint32_t sLastGhostMs[2];

// Input
static bool sPrevL = false, sPrevR = false, sPrevC = false;
static bool sTargetReached = false;

static EyeUiGeom sPacGeom;

// ── Pixel helpers ─────────────────────────────────────────────────────────────

inline int16_t cellX(int g)  { return (int16_t)(g * kCellPx); }
inline int16_t cellY(int g)  { return (int16_t)(g * kCellPx); }
inline int16_t dotX(int g)   { return (int16_t)(g * kCellPx + 1); }
inline int16_t dotY(int g)   { return (int16_t)(g * kCellPx + 1); }

inline bool inBounds(int x, int y) {
    return x >= 0 && x < kGridW && y >= 0 && y < kGridH;
}
inline bool playable(int x, int y) {
    return inBounds(x, y) && sPlay[y][x];
}

// ── LED helpers ───────────────────────────────────────────────────────────────

static void clearAllLeds() {
    for (uint8_t i = 0; i < kLedTotal; ++i) gLedStrip.setPixelColor(i, 0);
}
static void syncLeds(uint32_t t) {
    clearAllLeds();
    minigameUiPaintTourFlashIfActive(t);
}

// ── Drawing ───────────────────────────────────────────────────────────────────

// Pac-Man: filled 3×3 block. When mouth open, clear the centre pixel
// on the leading edge to create a visible notch.
static void drawPacman() {
    int16_t x = cellX(sPacGx), y = cellY(sPacGy);
    gRightDisplay.fillRect(x, y, kCellPx, kCellPx, SSD1306_WHITE);
    if (sMouthOpen) {
        int16_t mx = x + 1, my = y + 1;  // centre by default
        switch (sPacDir) {
            case 0: my = y;           break;  // up    → top   centre
            case 1: mx = x + 2;       break;  // right → right centre
            case 2: my = y + 2;       break;  // down  → bottom centre
            case 3: mx = x;           break;  // left  → left  centre
        }
        gRightDisplay.drawPixel(mx, my, SSD1306_BLACK);
    }
}

// Ghost A: hollow 3×3 outline square.
static void drawGhostA() {
    int16_t x = cellX(sGx[0]), y = cellY(sGy[0]);
    gRightDisplay.drawRect(x, y, kCellPx, kCellPx, SSD1306_WHITE);
}

// Ghost B: filled 3×3 block (no mouth notch — distinguishable from Pac
// because it never animates and spawns far from Pac-Man's start).
static void drawGhostB() {
    int16_t x = cellX(sGx[1]), y = cellY(sGy[1]);
    gRightDisplay.fillRect(x, y, kCellPx, kCellPx, SSD1306_WHITE);
}

// Pellets: single pixel at each cell centre.
static void drawPellets() {
    for (int y = 0; y < kGridH; ++y)
        for (int x = 0; x < kGridW; ++x)
            if (sPellet[y][x])
                gRightDisplay.drawPixel(dotX(x), dotY(y), SSD1306_WHITE);
}

// Left eye: score "eaten/target"
static void drawScore(uint32_t t) {
    if (!eyeUiBeginFrame(gLeftDisplay, false, t, &sPacGeom)) return;
    char buf[8];
    snprintf(buf, sizeof(buf), "%u/%u",
             (unsigned)sPelletsEaten, (unsigned)sWinPellets);
    eyeUiDrawRotatedKeyword(gLeftDisplay, &sPacGeom, buf);
    gLeftDisplay.display();
}

static void drawPlaying(uint32_t t) {
    if (eyeUiBeginFrame(gRightDisplay, true, t, &sPacGeom)) {
        drawPellets();
        drawGhostA();
        drawGhostB();
        drawPacman();   // Pac on top so visible when on a ghost cell (collision frame)
        gRightDisplay.display();
    }
    drawScore(t);
}

static void drawPhaseUi(uint32_t t) {
    switch (sPhase) {
        case Phase::Countdown:
            drawScore(t);
            if (eyeUiBeginFrame(gRightDisplay, true, t, &sPacGeom)) {
                char d[2] = { (char)('0' + sCdCount), '\0' };
                eyeUiDrawTextCenteredNudged(gRightDisplay, 38, 2, d, 0);
                gRightDisplay.display();
            }
            break;
        case Phase::GoCue:
            drawScore(t);
            if (eyeUiBeginFrame(gRightDisplay, true, t, &sPacGeom)) {
                eyeUiDrawTextCenteredNudged(gRightDisplay, 38, 2, "GO", 0);
                gRightDisplay.display();
            }
            break;
        case Phase::Playing:     drawPlaying(t);                  break;
        case Phase::SuccessAnim: minigameUiDrawWinBothEyes(t);    break;
        case Phase::FailAnim:    minigameUiDrawLoseBothEyes(t);   break;
        case Phase::Done:                                          break;
    }
}

// ── Init ──────────────────────────────────────────────────────────────────────

static bool placeGhost(int gi, int minManhattan) {
    // Random placement with minimum distance from Pac-Man
    for (int a = 0; a < 600; ++a) {
        int gx = (int)(esp_random() % (uint32_t)kGridW);
        int gy = (int)(esp_random() % (uint32_t)kGridH);
        if (!sPlay[gy][gx]) continue;
        if (abs(gx - (int)sPacGx) + abs(gy - (int)sPacGy) < minManhattan) continue;
        if (gi > 0 && sGx[0] == (uint8_t)gx && sGy[0] == (uint8_t)gy) continue;
        sGx[gi] = (uint8_t)gx;  sGy[gi] = (uint8_t)gy;
        sGDir[gi] = (uint8_t)(esp_random() % 4);
        return true;
    }
    // Fallback: linear scan with reduced minimum distance
    for (int gy = 0; gy < kGridH; ++gy)
        for (int gx = 0; gx < kGridW; ++gx) {
            if (!sPlay[gy][gx]) continue;
            if (abs(gx - (int)sPacGx) + abs(gy - (int)sPacGy) < 6) continue;
            if (gi > 0 && sGx[0] == (uint8_t)gx && sGy[0] == (uint8_t)gy) continue;
            sGx[gi] = (uint8_t)gx;  sGy[gi] = (uint8_t)gy;
            sGDir[gi] = (uint8_t)(esp_random() % 4);
            return true;
        }
    return false;
}

static bool initLevel(uint32_t nowMs) {
    eyeUiComputeGeomForMenu(true, nowMs, &sPacGeom);
    if (sPacGeom.opening <= 3) return false;

    // Build play mask — fixed for the whole game, not updated per-frame.
    for (int y = 0; y < kGridH; ++y)
        for (int x = 0; x < kGridW; ++x)
            sPlay[y][x] = eyeUiPointInsideEyeOpening(
                &sPacGeom, dotX(x), dotY(y), kMargin);

    // Pac spawn: nearest playable cell to centre of eye
    sPacGx = 0; sPacGy = 0;
    bool found = false;
    for (int r = 0; r <= 20 && !found; ++r)
        for (int dy = -r; dy <= r && !found; ++dy)
            for (int dx = -r; dx <= r && !found; ++dx) {
                if (std::max(std::abs(dx), std::abs(dy)) != r) continue;
                int gx = kMidGx + dx, gy = kMidGy + dy;
                if (!inBounds(gx, gy) || !sPlay[gy][gx]) continue;
                sPacGx = (uint8_t)gx;  sPacGy = (uint8_t)gy;  found = true;
            }
    if (!found) return false;

    sPacDir  = 1;           // start heading right
    sPendDir = kNoPendDir;

    // Checkerboard pellets: every playable cell where (x + y) is even.
    memset(sPellet, 0, sizeof(sPellet));
    int total = 0;
    for (int y = 0; y < kGridH; ++y)
        for (int x = 0; x < kGridW; ++x)
            if (sPlay[y][x] && ((x + y) & 1) == 0) { sPellet[y][x] = true; ++total; }

    // No pellet at Pac start
    if (sPellet[sPacGy][sPacGx]) sPellet[sPacGy][sPacGx] = false;

    if (total < (int)sWinPellets) return false;

    // Place ghosts: A far from Pac, B further apart from each other
    if (!placeGhost(0, 14)) return false;
    if (!placeGhost(1, 12)) return false;

    // No pellets under ghosts
    sPellet[sGy[0]][sGx[0]] = false;
    sPellet[sGy[1]][sGx[1]] = false;

    sPelletsEaten   = 0;
    sMouthOpen      = false;
    sLastPacMs      = nowMs;
    sLastGhostMs[0] = nowMs;
    sLastGhostMs[1] = nowMs;
    sGameStartMs    = nowMs;
    return true;
}

// ── Game logic ────────────────────────────────────────────────────────────────

static bool ghostAtPac() {
    return (sGx[0] == sPacGx && sGy[0] == sPacGy)
        || (sGx[1] == sPacGx && sGy[1] == sPacGy);
}

// Move Pac-Man one step.
// Pending direction is tried first to allow cornering; if it succeeds the
// heading updates. If current heading is blocked Pac-Man stops silently —
// the player must press a new direction.
static void stepPac() {
    // Try applying the buffered direction change
    if (sPendDir < 4U) {
        int nx = (int)sPacGx + kDx[sPendDir];
        int ny = (int)sPacGy + kDy[sPendDir];
        if (playable(nx, ny)) {
            sPacDir  = sPendDir;
            sPendDir = kNoPendDir;
        }
        // If not playable yet, keep buffered — retry next tick (cornering)
    }

    int nx = (int)sPacGx + kDx[sPacDir];
    int ny = (int)sPacGy + kDy[sPacDir];
    if (!playable(nx, ny)) return;  // blocked: wait for new direction

    sPacGx = (uint8_t)nx;
    sPacGy = (uint8_t)ny;
    sMouthOpen = !sMouthOpen;

    if (sPellet[sPacGy][sPacGx]) {
        sPellet[sPacGy][sPacGx] = false;
        ++sPelletsEaten;
    }
}

// Move a ghost one step.
//   chase=true  → pure Manhattan direction toward Pac-Man
//   chase=false → prefer current heading, 25 % chance of random drift
static void stepGhost(int gi, bool chase) {
    int gx = sGx[gi], gy = sGy[gi];

    int cands[4];  int nc = 0;
    for (int d = 0; d < 4; ++d) {
        int nx = gx + kDx[d], ny = gy + kDy[d];
        if (playable(nx, ny)) cands[nc++] = d;
    }
    if (nc == 0) return;

    int chosen;
    if (chase) {
        // Direction that minimises Manhattan distance to Pac-Man
        chosen = cands[0];
        int best = 99999;
        for (int i = 0; i < nc; ++i) {
            int nx = gx + kDx[cands[i]], ny = gy + kDy[cands[i]];
            int dist = abs(nx - (int)sPacGx) + abs(ny - (int)sPacGy);
            if (dist < best) { best = dist; chosen = cands[i]; }
        }
    } else {
        // Prefer current heading, random drift 25 % of the time
        chosen = cands[esp_random() % (uint32_t)nc];
        for (int i = 0; i < nc; ++i)
            if (cands[i] == sGDir[gi]) { chosen = cands[i]; break; }
        if ((esp_random() & 3U) == 0U)
            chosen = cands[esp_random() % (uint32_t)nc];
    }

    sGDir[gi] = (uint8_t)chosen;
    sGx[gi]   = (uint8_t)(gx + kDx[chosen]);
    sGy[gi]   = (uint8_t)(gy + kDy[chosen]);
}

static void doLose(uint32_t t) {
    rtttlStart("go:d=8,o=3,b=200:8c,8c");
    minigameUiArmTourFlashRed(t, kTourFlashMs);
    sPhase = Phase::FailAnim;  sPhaseEndMs = t + kAnimMs;
}

static void doWin(uint32_t t) {
    sTargetReached = true;
    rtttlStart("win:d=16,o=5,b=200:16g,16e,16g,16c6");
    minigameUiArmTourFlashGreen(t, kTourFlashMs);
    // game continues; result decided at natural death
}

}  // namespace

// ── Public API ────────────────────────────────────────────────────────────────

void pacmanGameStart(uint8_t winPellets) {
    rtttlStop();
    clearAllLeds();
    gLedStrip.show();

    sWinPellets    = (winPellets > 0) ? winPellets : kWinPellets;
    sTargetReached = false;
    sActive        = true;
    sPhase      = Phase::Countdown;
    sCdCount    = 3;
    sPrevL      = gButtonPressedLatched[kBtnLeft];
    sPrevR      = gButtonPressedLatched[kBtnRight];
    sPrevC      = gButtonPressedLatched[kBtnCenter];

    const uint32_t t = millis();
    sNextMs = t + kCountdownStepMs;
    drawPhaseUi(t);
    syncLeds(t);
}

void pacmanGameStop() {
    if (!sActive) return;
    sActive = false;
    rtttlStop();
    clearAllLeds();
    gLedStrip.show();
}

bool pacmanGameIsActive() { return sActive; }

void pacmanGameTick(uint32_t nowMs) {
    if (!sActive) return;

    // ── End-phase: show animation then signal result ───────────────────────────
    if (sPhase == Phase::SuccessAnim || sPhase == Phase::FailAnim) {
        drawPhaseUi(nowMs);
        syncLeds(nowMs);
        if (nowMs >= sPhaseEndMs) {
            gGameResult = sTargetReached ? GameResult::Won : GameResult::Lost;
            pacmanGameStop();
        }
        return;
    }

    // ── Countdown 3-2-1 ───────────────────────────────────────────────────────
    if (sPhase == Phase::Countdown) {
        drawPhaseUi(nowMs);
        syncLeds(nowMs);
        if (nowMs >= sNextMs) {
            if (sCdCount <= 1) { sPhase = Phase::GoCue; sNextMs = nowMs + kGoCueMs; }
            else               { --sCdCount;             sNextMs = nowMs + kCountdownStepMs; }
            drawPhaseUi(nowMs);
        }
        return;
    }

    // ── GO cue → init level ───────────────────────────────────────────────────
    if (sPhase == Phase::GoCue) {
        drawPhaseUi(nowMs);
        syncLeds(nowMs);
        if (nowMs >= sNextMs) {
            if (!initLevel(nowMs)) {
                doLose(nowMs);
                drawPhaseUi(nowMs);
                syncLeds(nowMs);
                return;
            }
            sPhase = Phase::Playing;
            sPrevL = gButtonPressedLatched[kBtnLeft];
            sPrevR = gButtonPressedLatched[kBtnRight];
            sPrevC = gButtonPressedLatched[kBtnCenter];
            drawPhaseUi(nowMs);
        }
        return;
    }

    // ── Playing ───────────────────────────────────────────────────────────────

    // Input: edge-detect on latched buttons
    const bool L = gButtonPressedLatched[kBtnLeft];
    const bool R = gButtonPressedLatched[kBtnRight];
    const bool C = gButtonPressedLatched[kBtnCenter];
    if (L && !sPrevL) sPendDir = (uint8_t)((sPacDir + 3U) % 4U);  // turn left
    if (R && !sPrevR) sPendDir = (uint8_t)((sPacDir + 1U) % 4U);  // turn right
    if (C && !sPrevC) sPendDir = (uint8_t)((sPacDir + 2U) % 4U);  // reverse
    sPrevL = L;  sPrevR = R;  sPrevC = C;

    // Pac-Man step
    if (nowMs - sLastPacMs >= kPacTickMs) {
        sLastPacMs = nowMs;
        stepPac();
        if (ghostAtPac())                                      { doLose(nowMs); drawPhaseUi(nowMs); syncLeds(nowMs); return; }
        if (sPelletsEaten >= sWinPellets && !sTargetReached)   { doWin(nowMs); }
    }

    // Ghost A step (pure chaser)
    if (nowMs - sLastGhostMs[0] >= kGhostATickMs) {
        sLastGhostMs[0] = nowMs;
        stepGhost(0, true);
        if (ghostAtPac()) { doLose(nowMs); drawPhaseUi(nowMs); syncLeds(nowMs); return; }
    }

    // Ghost B step (wanders then chases)
    if (nowMs - sLastGhostMs[1] >= kGhostBTickMs) {
        sLastGhostMs[1] = nowMs;
        stepGhost(1, (nowMs - sGameStartMs) >= kGhostBChaseMs);
        if (ghostAtPac()) { doLose(nowMs); drawPhaseUi(nowMs); syncLeds(nowMs); return; }
    }

    drawPhaseUi(nowMs);
    syncLeds(nowMs);
}
