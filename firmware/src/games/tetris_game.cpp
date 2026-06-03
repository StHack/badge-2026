// tetris_game.cpp — Tetris minigame for badge_v2.
#include "tetris_game.h"
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

constexpr uint16_t kWinLines            = 22;
constexpr uint32_t kCountdownStepMs     = 700;
constexpr uint32_t kGoCueMs             = 700;
constexpr uint32_t kFallMs              = 800;
constexpr uint32_t kFastFallMs          = 80;
constexpr uint32_t kMoveRepeatMs        = 140;
constexpr uint32_t kMoveInitialMs       = 220;
constexpr uint32_t kCenterTapRotateMaxMs = 240;
constexpr uint32_t kTourFlashMs         = 500;
constexpr uint32_t kAnimMs              = 1500;
constexpr int16_t  kHudTextY            = 36;

constexpr int     kCols    = 10;
constexpr int     kRows    = 12;
constexpr uint8_t kCellPx  = 3;
constexpr int16_t kFieldX  = static_cast<int16_t>((128 - kCols * (int)kCellPx) / 2);
constexpr int16_t kFieldY  = 24;

enum class Phase : uint8_t { Countdown, GoCue, Playing, SuccessAnim, FailAnim, Done };

bool     sActive              = false;
Phase    sPhase               = Phase::Countdown;
uint8_t  sCdCount             = 3;
uint32_t sNextMs              = 0;
uint32_t sPhaseEndMs          = 0;
uint32_t sLastFallMs          = 0;
uint32_t sLastMoveMs          = 0;

uint8_t sBoard[kRows][kCols];

uint8_t sPieceType = 0;
uint8_t sRot       = 0;
int8_t  sPx        = 0;
int8_t  sPy        = 0;

uint16_t sLines    = 0;
uint16_t sWinLines     = kWinLines;  // runtime win target (set by tetrisGameStart)
bool     sTargetReached = false;

uint8_t sBag[7];
uint8_t sBagPos = 7;

bool    sPrevBtn[3]         = {};
bool    sHeldMove           = false;
uint8_t sMoveDir            = 0;
bool    sCenterChordActive  = false;
uint32_t sCenterChordStartMs = 0;

static EyeUiGeom sGeom;

// Type order: I, O, T, S, Z, J, L — rotation 0 in a 4×4 grid.
static const uint8_t kShape[7][4][4] = {
    {{0,0,0,0},{1,1,1,1},{0,0,0,0},{0,0,0,0}},
    {{0,1,1,0},{0,1,1,0},{0,0,0,0},{0,0,0,0}},
    {{0,1,0,0},{1,1,1,0},{0,0,0,0},{0,0,0,0}},
    {{0,1,1,0},{1,1,0,0},{0,0,0,0},{0,0,0,0}},
    {{1,1,0,0},{0,1,1,0},{0,0,0,0},{0,0,0,0}},
    {{1,0,0,0},{1,1,1,0},{0,0,0,0},{0,0,0,0}},
    {{0,0,1,0},{1,1,1,0},{0,0,0,0},{0,0,0,0}},
};

void rotateCw(bool d[4][4]) {
    bool t[4][4];
    memcpy(t, d, sizeof(t));
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 4; ++j)
            d[j][3 - i] = t[i][j];
}

void cellMask(uint8_t type, uint8_t rot, bool out[4][4]) {
    memset(out, 0, sizeof(bool) * 16);
    if (type > 6U) return;
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 4; ++j)
            out[i][j] = kShape[type][i][j] != 0;
    for (uint8_t r = 0; r < (rot & 3U); ++r) rotateCw(out);
}

bool pieceFits(int8_t px, int8_t py, uint8_t type, uint8_t rot) {
    bool m[4][4];
    cellMask(type, rot, m);
    for (int i = 0; i < 4; ++i) {
        for (int j = 0; j < 4; ++j) {
            if (!m[i][j]) continue;
            const int gx = (int)px + j;
            const int gy = (int)py + i;
            if (gx < 0 || gx >= kCols || gy >= kRows) return false;
            if (gy >= 0 && sBoard[gy][gx] != 0U) return false;
        }
    }
    return true;
}

void refillBag() {
    for (uint8_t i = 0; i < 7U; ++i) sBag[i] = i;
    for (int i = 6; i > 0; --i) {
        const int j = (int)(esp_random() % (uint32_t)(i + 1));
        uint8_t tmp = sBag[i]; sBag[i] = sBag[j]; sBag[j] = tmp;
    }
    sBagPos = 0;
}

uint8_t takeNextType() {
    if (sBagPos >= 7U) refillBag();
    return sBag[sBagPos++];
}

bool trySpawn() {
    sPieceType = takeNextType();
    // Try spawn at top centre.
    const int8_t spawnX = static_cast<int8_t>(kCols / 2 - 2);
    for (int8_t tryPy = -3; tryPy <= 1; ++tryPy) {
        if (pieceFits(spawnX, tryPy, sPieceType, 0)) {
            sPx  = spawnX;
            sPy  = tryPy;
            sRot = 0;
            return true;
        }
    }
    return false;
}

bool lockPiece() {
    bool overflowTop = false;
    bool m[4][4];
    cellMask(sPieceType, sRot, m);
    for (int i = 0; i < 4; ++i) {
        for (int j = 0; j < 4; ++j) {
            if (!m[i][j]) continue;
            const int gx = (int)sPx + j;
            const int gy = (int)sPy + i;
            if (gy < 0) { overflowTop = true; continue; }
            if (gx >= 0 && gx < kCols && gy < kRows)
                sBoard[gy][gx] = static_cast<uint8_t>(sPieceType + 1U);
        }
    }
    return !overflowTop;
}

void clearCompletedLines() {
    int dest    = kRows - 1;
    int cleared = 0;
    for (int y = kRows - 1; y >= 0; --y) {
        bool full = true;
        for (int x = 0; x < kCols; ++x) {
            if (sBoard[y][x] == 0U) { full = false; break; }
        }
        if (full) { ++cleared; continue; }
        if (y != dest)
            for (int x = 0; x < kCols; ++x) sBoard[dest][x] = sBoard[y][x];
        --dest;
    }
    for (int y = dest; y >= 0; --y)
        for (int x = 0; x < kCols; ++x) sBoard[y][x] = 0;
    if (cleared > 0) sLines = static_cast<uint16_t>(sLines + cleared);
}

bool tryRotate() {
    const uint8_t nr = static_cast<uint8_t>((static_cast<unsigned>(sRot) + 1U) % 4U);
    static const int8_t kKicks[] = {0, -1, 1, -2, 2};
    for (int8_t kx : kKicks) {
        if (pieceFits(static_cast<int8_t>(sPx + kx), sPy, sPieceType, nr)) {
            sPx  = static_cast<int8_t>(sPx + kx);
            sRot = nr;
            return true;
        }
    }
    return false;
}

void clearAllLeds() {
    for (uint8_t i = 0; i < kLedTotal; ++i) gLedStrip.setPixelColor(i, 0);
}

void syncLeds(uint32_t nowMs) {
    clearAllLeds();
    minigameUiPaintTourFlashIfActive(nowMs);
}

// ── Drawing ───────────────────────────────────────────────────────────────────

void drawLeftHud(uint32_t nowMs) {
    if (!eyeUiBeginFrame(gLeftDisplay, false, nowMs, &sGeom)) return;
    char buf[10];
    if (sWinLines > 0)
        snprintf(buf, sizeof(buf), "%u/%u", static_cast<unsigned>(sLines), static_cast<unsigned>(sWinLines));
    else
        snprintf(buf, sizeof(buf), "%u", static_cast<unsigned>(sLines));
    eyeUiDrawRotatedKeyword(gLeftDisplay, &sGeom, buf);
    gLeftDisplay.display();
}

void drawPixelCell(int col, int row) {
    const int16_t x = static_cast<int16_t>(kFieldX + col * (int16_t)kCellPx);
    const int16_t y = static_cast<int16_t>(kFieldY + row * (int16_t)kCellPx);
    gRightDisplay.fillRect(x, y, kCellPx, kCellPx, SSD1306_WHITE);
}

void drawFieldAndPiece() {
    for (int r = 0; r < kRows; ++r)
        for (int c = 0; c < kCols; ++c)
            if (sBoard[r][c] != 0U) drawPixelCell(c, r);
    bool m[4][4];
    cellMask(sPieceType, sRot, m);
    for (int i = 0; i < 4; ++i) {
        for (int j = 0; j < 4; ++j) {
            if (!m[i][j]) continue;
            const int gx = (int)sPx + j;
            const int gy = (int)sPy + i;
            if (gx < 0 || gx >= kCols || gy < 0 || gy >= kRows) continue;
            drawPixelCell(gx, gy);
        }
    }
}

void drawPlaying(uint32_t nowMs) {
    gRightDisplay.clearDisplay();
    const int16_t fw = static_cast<int16_t>(kCols * (int)kCellPx);
    const int16_t fh = static_cast<int16_t>(kRows * (int)kCellPx);
    gRightDisplay.drawRect(static_cast<int16_t>(kFieldX - 1), static_cast<int16_t>(kFieldY - 1),
                           static_cast<int16_t>(fw + 2), static_cast<int16_t>(fh + 2),
                           SSD1306_WHITE);
    drawFieldAndPiece();
    gRightDisplay.display();
    drawLeftHud(nowMs);
}

void drawPhaseUi(uint32_t nowMs) {
    switch (sPhase) {
        case Phase::Countdown: {
            drawLeftHud(nowMs);
            if (!eyeUiBeginFrame(gRightDisplay, true, nowMs, &sGeom)) break;
            char digit[2] = {static_cast<char>('0' + sCdCount), '\0'};
            eyeUiDrawTextCenteredNudged(gRightDisplay, kHudTextY, 2, digit, 0);
            gRightDisplay.display();
            break;
        }
        case Phase::GoCue: {
            drawLeftHud(nowMs);
            if (!eyeUiBeginFrame(gRightDisplay, true, nowMs, &sGeom)) break;
            eyeUiDrawTextCenteredNudged(gRightDisplay, kHudTextY, 2, "GO", 0);
            gRightDisplay.display();
            break;
        }
        case Phase::Playing:
            drawPlaying(nowMs);
            break;
        case Phase::SuccessAnim:
            minigameUiDrawWinBothEyes(nowMs);
            break;
        case Phase::FailAnim:
            minigameUiDrawLoseBothEyes(nowMs);
            break;
        case Phase::Done:
            break;
    }
}

void enterPlaying(uint32_t nowMs) {
    memset(sBoard, 0, sizeof(sBoard));
    sLines          = 0;
    refillBag();
    sLastFallMs     = nowMs;
    sLastMoveMs     = nowMs;
    sHeldMove       = false;
    sMoveDir        = 0;
    sCenterChordActive = false;
    if (!trySpawn()) {
        rtttlStart("go:d=8,o=3,b=200:8c,8c");
        minigameUiArmTourFlashRed(nowMs, kTourFlashMs);
        sPhase      = Phase::FailAnim;
        sPhaseEndMs = nowMs + kAnimMs;
    }
}

}  // namespace

// ── Public API ────────────────────────────────────────────────────────────────

void tetrisGameStart(uint8_t winLines) {
    sWinLines      = winLines;  // 0 = infinite
    sTargetReached = false;
    rtttlStop();
    clearAllLeds();
    gLedStrip.show();

    sActive   = true;
    sPhase    = Phase::Countdown;
    sCdCount  = 3;
    sBagPos   = 7;
    sPrevBtn[0] = gButtonState[0];
    sPrevBtn[1] = gButtonState[1];
    sPrevBtn[2] = gButtonState[2];

    const uint32_t t = millis();
    sNextMs = t + kCountdownStepMs;
    drawPhaseUi(t);
    syncLeds(t);
}

void tetrisGameStop() {
    if (!sActive) return;
    sActive = false;
    sPhase  = Phase::Countdown;
    rtttlStop();
    clearAllLeds();
    gLedStrip.show();
}

bool tetrisGameIsActive() { return sActive; }

void tetrisGameTick(uint32_t nowMs) {
    if (!sActive) return;

    if (sPhase == Phase::SuccessAnim) {
        drawPhaseUi(nowMs);
        syncLeds(nowMs);
        if (nowMs >= sPhaseEndMs) {
            gGameResult = GameResult::Won;
            tetrisGameStop();
        }
        return;
    }

    if (sPhase == Phase::FailAnim) {
        drawPhaseUi(nowMs);
        syncLeds(nowMs);
        if (nowMs >= sPhaseEndMs) {
            gGameResult = sTargetReached ? GameResult::Won : GameResult::Lost;
            tetrisGameStop();
        }
        return;
    }

    if (sPhase == Phase::GoCue) {
        drawPhaseUi(nowMs);
        syncLeds(nowMs);
        if (nowMs < sNextMs) return;
        sPhase = Phase::Playing;
        enterPlaying(nowMs);
        sPrevBtn[0] = gButtonPressedLatched[0];
        sPrevBtn[1] = gButtonPressedLatched[1];
        sPrevBtn[2] = gButtonPressedLatched[2];
        drawPhaseUi(nowMs);
        syncLeds(nowMs);
        return;
    }

    if (sPhase == Phase::Countdown) {
        drawPhaseUi(nowMs);
        syncLeds(nowMs);
        if (nowMs < sNextMs) return;
        if (sCdCount <= 1) {
            sPhase  = Phase::GoCue;
            sNextMs = nowMs + kGoCueMs;
        } else {
            --sCdCount;
            sNextMs = nowMs + kCountdownStepMs;
        }
        drawPhaseUi(nowMs);
        return;
    }

    // Playing — input.
    // Use gButtonState (held) so hold-to-repeat and soft-drop work continuously.
    const bool curL = gButtonState[0];
    const bool curC = gButtonState[1];
    const bool curR = gButtonState[2];
    const bool edgeL = curL && !sPrevBtn[0];
    const bool edgeR = curR && !sPrevBtn[2];

    // Center chord: press = start timer; release = rotate if short tap.
    if (curC && !sPrevBtn[1]) {
        sCenterChordActive  = true;
        sCenterChordStartMs = nowMs;
    }
    if (!curC && sPrevBtn[1]) {
        if (sCenterChordActive &&
            (uint32_t)(nowMs - sCenterChordStartMs) <= kCenterTapRotateMaxMs) {
            tryRotate();
        }
        sCenterChordActive = false;
    }

    if (edgeL) {
        if (pieceFits(static_cast<int8_t>(sPx - 1), sPy, sPieceType, sRot)) --sPx;
        sHeldMove = true; sMoveDir = 0;
        sLastMoveMs = nowMs;   // first repeat fires after kMoveRepeatMs, not immediately
    } else if (edgeR) {
        if (pieceFits(static_cast<int8_t>(sPx + 1), sPy, sPieceType, sRot)) ++sPx;
        sHeldMove = true; sMoveDir = 2;
        sLastMoveMs = nowMs;
    }

    if (!curL && !curR) {
        sHeldMove = false;
    } else if (sHeldMove && (curL || curR)) {
        const uint8_t dir = curL ? 0U : 2U;
        if (dir == sMoveDir && (int32_t)(nowMs - sLastMoveMs) >= (int32_t)kMoveRepeatMs) {
            sLastMoveMs = nowMs;
            if (curL && pieceFits(static_cast<int8_t>(sPx - 1), sPy, sPieceType, sRot)) --sPx;
            if (curR && pieceFits(static_cast<int8_t>(sPx + 1), sPy, sPieceType, sRot)) ++sPx;
        }
    }

    sPrevBtn[0] = curL;
    sPrevBtn[1] = curC;
    sPrevBtn[2] = curR;

    // Gravity / soft drop.
    const bool softDrop =
        curC && ((uint32_t)(nowMs - sCenterChordStartMs) > kCenterTapRotateMaxMs);
    const uint32_t fallEvery = softDrop ? kFastFallMs : kFallMs;
    if ((int32_t)(nowMs - sLastFallMs) >= (int32_t)fallEvery) {
        sLastFallMs = nowMs;
        if (pieceFits(sPx, static_cast<int8_t>(sPy + 1), sPieceType, sRot)) {
            ++sPy;
        } else {
            if (!lockPiece()) {
                rtttlStart("go:d=8,o=3,b=200:8c,8c");
                minigameUiArmTourFlashRed(nowMs, kTourFlashMs);
                sPhase      = Phase::FailAnim;
                sPhaseEndMs = nowMs + kAnimMs;
                drawPhaseUi(nowMs);
                syncLeds(nowMs);
                return;
            }
            const uint16_t prevLines = sLines;
            clearCompletedLines();
            if (sLines > prevLines) {
                rtttlStart("s2:d=16,o=5,b=400:16e6");
                minigameUiArmTourFlashGreen(nowMs, kTourFlashMs);
            }
            if (sWinLines > 0 && sLines >= sWinLines && !sTargetReached) {
                sTargetReached = true;
                rtttlStart("win:d=16,o=5,b=200:16g,16e,16g,16c6");
                minigameUiArmTourFlashGreen(nowMs, kTourFlashMs);
            }
            if (!trySpawn()) {
                rtttlStart("go:d=8,o=3,b=200:8c,8c");
                minigameUiArmTourFlashRed(nowMs, kTourFlashMs);
                sPhase      = Phase::FailAnim;
                sPhaseEndMs = nowMs + kAnimMs;
                drawPhaseUi(nowMs);
                syncLeds(nowMs);
                return;
            }
        }
    }

    drawPhaseUi(nowMs);
    syncLeds(nowMs);
}
