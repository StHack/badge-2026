// space_game.cpp — Space Invaders minigame for badge_v2.
//
// Right eye : game field (invaders + ship + bullets)
// Left eye  : kill count
//
// Controls  : LEFT / RIGHT (hold) = move ship
//             CENTER (tap)        = fire

#include "space_game.h"
#include "game_result.h"
#include "minigame_ui.h"
#include "../hal/buttons.h"
#include "../hal/leds.h"
#include "../hal/display.h"
#include "../config/hardware.h"
#include "../eyes/eye_geometry.h"
#include "../sound/rtttl.h"
#include <Arduino.h>
#include <string.h>
#include <esp_random.h>

namespace {

// ── Tunables ──────────────────────────────────────────────────────────────────
constexpr uint8_t  kCols         = 5;
constexpr uint8_t  kRows         = 3;
constexpr int16_t  kInvW         = 7;    // invader sprite width  (px)
constexpr int16_t  kInvH         = 5;    // invader sprite height (px)
constexpr int16_t  kCellW        = 14;   // center-to-center horizontal
constexpr int16_t  kCellH        = 10;   // center-to-center vertical
constexpr int16_t  kGridOriginX  = 33;   // left edge of col-0 at offset 0
constexpr int16_t  kGridOriginY  = 6;    // top  edge of row-0 at offset 0
constexpr int16_t  kGridStepX    = 4;    // px per lateral step
constexpr int16_t  kGridDropY    = 5;    // px drop when reversing at wall
constexpr uint32_t kInvMsBase    = 500;  // step interval with full grid
constexpr uint32_t kInvMsMin     = 110;  // step interval with 1 invader left
constexpr int16_t  kDeathY       = 52;   // invader bottom reaches this → lose
constexpr int16_t  kShipY        = 57;   // top Y of ship sprite
constexpr int16_t  kShipHalfW    = 5;    // half-width of hull
constexpr int16_t  kShipH        = 4;    // total ship height
constexpr int16_t  kShipMinX     = kShipHalfW + 2;
constexpr int16_t  kShipMaxX     = 127 - kShipHalfW - 2;
constexpr int16_t  kShipStep     = 3;
constexpr uint32_t kShipMoveMs   = 35;
constexpr uint32_t kBulletMs     = 28;
constexpr int16_t  kBulSpeedUp   = 5;
constexpr int16_t  kBulSpeedDown = 3;
constexpr uint32_t kEnemyFireMs  = 1300;
constexpr uint32_t kAnimMs       = 1500;
constexpr uint32_t kTourFlashMs  = 500;
constexpr uint32_t kCdStepMs     = 700;
constexpr uint32_t kGoCueMs      = 600;

// ── State ─────────────────────────────────────────────────────────────────────
enum class Phase : uint8_t { Countdown, GoCue, Playing, SuccessAnim, FailAnim };

static bool     sActive      = false;
static Phase    sPhase       = Phase::Countdown;
static uint8_t  sCdCount     = 3;
static uint32_t sNextMs      = 0;
static uint32_t sPhaseEndMs  = 0;

static bool    sInv[kRows][kCols];
static int16_t sGridOffX    = 0;
static int16_t sGridOffY    = 0;
static int8_t  sGridDir     = 1;   // +1 = right, -1 = left
static uint8_t sAlive       = kRows * kCols;
static uint8_t sKilled      = 0;
static uint32_t sLastInvMs  = 0;

static int16_t  sShipX      = 64;
static uint32_t sLastShipMs = 0;

static bool    sBulActive   = false;
static int16_t sBulX        = 0;
static int16_t sBulY        = 0;

static bool    sEBulActive  = false;
static int16_t sEBulX       = 0;
static int16_t sEBulY       = 0;
static uint32_t sLastFireMs = 0;
static uint32_t sLastBulMs  = 0;

static EyeUiGeom sGeom;

// ── Helpers ───────────────────────────────────────────────────────────────────

static uint32_t invStepMs() {
    if (sAlive == 0) return kInvMsMin;
    return kInvMsMin + (uint32_t)(kInvMsBase - kInvMsMin) * sAlive / (kRows * kCols);
}

static void clearAllLeds() {
    for (uint8_t i = 0; i < kLedTotal; ++i) gLedStrip.setPixelColor(i, 0);
}
static void syncLeds(uint32_t t) {
    clearAllLeds();
    minigameUiPaintTourFlashIfActive(t);
}

// Top-left pixel of invader at (col, row)
static int16_t invLeft(uint8_t col) { return kGridOriginX + col * kCellW + sGridOffX; }
static int16_t invTop (uint8_t row) { return kGridOriginY + row * kCellH + sGridOffY; }

// ── Drawing ───────────────────────────────────────────────────────────────────

// 7×5 alien sprite
static void drawInvader(int16_t x, int16_t y) {
    gRightDisplay.drawPixel(x + 1, y,     SSD1306_WHITE); // antennae
    gRightDisplay.drawPixel(x + 5, y,     SSD1306_WHITE);
    gRightDisplay.fillRect( x + 1, y + 1, 5, 1, SSD1306_WHITE); // top body
    gRightDisplay.fillRect( x,     y + 2, 7, 1, SSD1306_WHITE); // mid body
    gRightDisplay.drawPixel(x,     y + 3, SSD1306_WHITE); // bottom row gaps
    gRightDisplay.drawPixel(x + 2, y + 3, SSD1306_WHITE);
    gRightDisplay.drawPixel(x + 4, y + 3, SSD1306_WHITE);
    gRightDisplay.drawPixel(x + 6, y + 3, SSD1306_WHITE);
    gRightDisplay.drawPixel(x + 1, y + 4, SSD1306_WHITE); // feet
    gRightDisplay.drawPixel(x + 5, y + 4, SSD1306_WHITE);
}

// Ship: 1px cannon tip, 3px cannon base, 11px hull (2 rows)
static void drawShip(int16_t cx) {
    gRightDisplay.fillRect(cx,              kShipY,     1, 1, SSD1306_WHITE);
    gRightDisplay.fillRect(cx - 1,          kShipY + 1, 3, 1, SSD1306_WHITE);
    gRightDisplay.fillRect(cx - kShipHalfW, kShipY + 2, kShipHalfW * 2 + 1, 2, SSD1306_WHITE);
}

static void drawPlaying(uint32_t nowMs) {
    if (eyeUiBeginFrame(gRightDisplay, true, nowMs, &sGeom)) {
        for (uint8_t r = 0; r < kRows; ++r)
            for (uint8_t c = 0; c < kCols; ++c)
                if (sInv[r][c]) drawInvader(invLeft(c), invTop(r));
        if (sBulActive)  gRightDisplay.fillRect(sBulX,  sBulY,  1, 3, SSD1306_WHITE);
        if (sEBulActive) gRightDisplay.fillRect(sEBulX, sEBulY, 1, 3, SSD1306_WHITE);
        drawShip(sShipX);
        gRightDisplay.display();
    }
    EyeUiGeom lg;
    if (eyeUiBeginFrame(gLeftDisplay, false, nowMs, &lg)) {
        char buf[8];
        snprintf(buf, sizeof(buf), "%u", (unsigned)sKilled);
        eyeUiDrawRotatedKeyword(gLeftDisplay, &lg, buf);
        gLeftDisplay.display();
    }
}

static void drawCd(uint32_t nowMs) {
    EyeUiGeom g;
    char d[2] = { (char)('0' + sCdCount), '\0' };
    if (eyeUiBeginFrame(gLeftDisplay,  false, nowMs, &g))
        { eyeUiDrawTextCenteredNudged(gLeftDisplay,  g.cy + 8, 2, d); gLeftDisplay.display(); }
    if (eyeUiBeginFrame(gRightDisplay, true,  nowMs, &g))
        { eyeUiDrawTextCenteredNudged(gRightDisplay, g.cy + 8, 2, d); gRightDisplay.display(); }
}

static void drawGo(uint32_t nowMs) {
    EyeUiGeom g;
    if (eyeUiBeginFrame(gLeftDisplay,  false, nowMs, &g))
        { eyeUiDrawTextCenteredNudged(gLeftDisplay,  g.cy + 8, 2, "GO"); gLeftDisplay.display(); }
    if (eyeUiBeginFrame(gRightDisplay, true,  nowMs, &g))
        { eyeUiDrawTextCenteredNudged(gRightDisplay, g.cy + 8, 2, "GO"); gRightDisplay.display(); }
}

static void drawPhaseUi(uint32_t nowMs) {
    switch (sPhase) {
        case Phase::Countdown:   drawCd(nowMs);                         break;
        case Phase::GoCue:       drawGo(nowMs);                         break;
        case Phase::Playing:     drawPlaying(nowMs);                    break;
        case Phase::SuccessAnim: minigameUiDrawWinBothEyes(nowMs);      break;
        case Phase::FailAnim:    minigameUiDrawLoseBothEyes(nowMs);     break;
    }
}

// ── Game events ───────────────────────────────────────────────────────────────

static void doWin(uint32_t t) {
    rtttlStart("win:d=16,o=5,b=200:16g,16e,16g,16c6");
    minigameUiArmTourFlashGreen(t, kTourFlashMs);
    sPhase = Phase::SuccessAnim;  sPhaseEndMs = t + kAnimMs;
}

static void doLose(uint32_t t) {
    rtttlStart("go:d=8,o=3,b=200:8c,8c");
    minigameUiArmTourFlashRed(t, kTourFlashMs);
    sPhase = Phase::FailAnim;  sPhaseEndMs = t + kAnimMs;
}

// Fire from the bottom-most alive invader in a random column
static void enemyFire() {
    if (sEBulActive || sAlive == 0) return;
    uint8_t cols[kCols], rows[kCols]; uint8_t n = 0;
    for (uint8_t c = 0; c < kCols; ++c)
        for (int8_t r = (int8_t)kRows - 1; r >= 0; --r)
            if (sInv[r][c]) { cols[n] = c; rows[n] = (uint8_t)r; ++n; break; }
    if (n == 0) return;
    uint8_t i = (uint8_t)(esp_random() % n);
    sEBulX = invLeft(cols[i]) + kInvW / 2;
    sEBulY = invTop(rows[i]) + kInvH;
    sEBulActive = true;
}

}  // namespace

// ── Public API ────────────────────────────────────────────────────────────────

void spaceGameStart() {
    rtttlStop();
    clearAllLeds();
    gLedStrip.show();

    sActive  = true;
    sPhase   = Phase::Countdown;
    sCdCount = 3;

    for (uint8_t r = 0; r < kRows; ++r)
        for (uint8_t c = 0; c < kCols; ++c)
            sInv[r][c] = true;
    sGridOffX  = 0;
    sGridOffY  = 0;
    sGridDir   = 1;
    sAlive     = kRows * kCols;
    sKilled    = 0;

    sShipX      = 64;
    sBulActive  = false;
    sEBulActive = false;

    const uint32_t t = millis();
    sNextMs     = t + kCdStepMs;
    sLastInvMs  = t;
    sLastShipMs = t;
    sLastBulMs  = t;
    sLastFireMs = t;

    drawPhaseUi(t);
    syncLeds(t);
}

void spaceGameStop() {
    if (!sActive) return;
    sActive = false;
    rtttlStop();
    clearAllLeds();
    gLedStrip.show();
}

bool spaceGameIsActive() { return sActive; }

void spaceGameTick(uint32_t nowMs) {
    if (!sActive) return;

    // ── End animations ────────────────────────────────────────────────────────
    if (sPhase == Phase::SuccessAnim || sPhase == Phase::FailAnim) {
        drawPhaseUi(nowMs);
        syncLeds(nowMs);
        if (nowMs >= sPhaseEndMs) {
            gGameResult = (sPhase == Phase::SuccessAnim) ? GameResult::Won : GameResult::Lost;
            spaceGameStop();
        }
        return;
    }

    // ── GO cue ────────────────────────────────────────────────────────────────
    if (sPhase == Phase::GoCue) {
        drawPhaseUi(nowMs);
        syncLeds(nowMs);
        if (nowMs >= sNextMs) {
            sPhase      = Phase::Playing;
            sLastInvMs  = nowMs;
            sLastShipMs = nowMs;
            sLastBulMs  = nowMs;
            sLastFireMs = nowMs;
        }
        return;
    }

    // ── Countdown ─────────────────────────────────────────────────────────────
    if (sPhase == Phase::Countdown) {
        drawPhaseUi(nowMs);
        syncLeds(nowMs);
        if (nowMs >= sNextMs) {
            if (sCdCount <= 1) { sPhase = Phase::GoCue; sNextMs = nowMs + kGoCueMs; }
            else               { --sCdCount;             sNextMs = nowMs + kCdStepMs; }
        }
        return;
    }

    // ── Playing ───────────────────────────────────────────────────────────────

    // Ship movement (hold L / R)
    if (nowMs - sLastShipMs >= kShipMoveMs) {
        sLastShipMs = nowMs;
        if (gButtonState[kBtnLeft])  sShipX -= kShipStep;
        if (gButtonState[kBtnRight]) sShipX += kShipStep;
        if (sShipX < kShipMinX) sShipX = kShipMinX;
        if (sShipX > kShipMaxX) sShipX = kShipMaxX;
    }

    // Fire (tap center)
    if (gButtonPressedLatched[kBtnCenter] && !sBulActive) {
        sBulActive = true;
        sBulX      = sShipX;
        sBulY      = kShipY - 3;
    }

    // Move bullets
    if (nowMs - sLastBulMs >= kBulletMs) {
        sLastBulMs = nowMs;
        if (sBulActive)  { sBulY  -= kBulSpeedUp;   if (sBulY  < 0)  sBulActive  = false; }
        if (sEBulActive) { sEBulY += kBulSpeedDown;  if (sEBulY > 64) sEBulActive = false; }
    }

    // Player bullet vs invaders
    if (sBulActive) {
        for (uint8_t r = 0; r < kRows && sBulActive; ++r) {
            for (uint8_t c = 0; c < kCols && sBulActive; ++c) {
                if (!sInv[r][c]) continue;
                const int16_t ix = invLeft(c), iy = invTop(r);
                if (sBulX >= ix && sBulX < ix + kInvW &&
                    sBulY + 3 > iy && sBulY < iy + kInvH) {
                    sInv[r][c] = false;
                    sBulActive = false;
                    --sAlive;
                    ++sKilled;
                    minigameUiArmTourFlashGreen(nowMs, kTourFlashMs);
                    if (sAlive == 0) { doWin(nowMs); drawPhaseUi(nowMs); syncLeds(nowMs); return; }
                }
            }
        }
    }

    // Enemy bullet vs ship
    if (sEBulActive) {
        if (sEBulX >= sShipX - kShipHalfW && sEBulX <= sShipX + kShipHalfW &&
            sEBulY + 3 > kShipY && sEBulY < kShipY + kShipH) {
            doLose(nowMs); drawPhaseUi(nowMs); syncLeds(nowMs); return;
        }
    }

    // Enemy fire timer
    if (nowMs - sLastFireMs >= kEnemyFireMs) {
        sLastFireMs = nowMs;
        enemyFire();
    }

    // Invader grid step
    if (nowMs - sLastInvMs >= invStepMs()) {
        sLastInvMs = nowMs;

        // Find current bounds of alive invaders
        int16_t lx = 9999, rx = -9999;
        for (uint8_t r = 0; r < kRows; ++r)
            for (uint8_t c = 0; c < kCols; ++c)
                if (sInv[r][c]) {
                    int16_t x0 = invLeft(c), x1 = x0 + kInvW;
                    if (x0 < lx) lx = x0;
                    if (x1 > rx) rx = x1;
                }

        // Would next lateral step hit a wall?
        const int16_t nextLx = lx + sGridDir * kGridStepX;
        const int16_t nextRx = rx + sGridDir * kGridStepX;
        if (nextLx <= 1 || nextRx >= 127) {
            // Drop and reverse
            sGridOffY += kGridDropY;
            sGridDir   = -sGridDir;
        } else {
            sGridOffX += sGridDir * kGridStepX;
        }

        // Check death line
        for (uint8_t r = 0; r < kRows; ++r)
            for (uint8_t c = 0; c < kCols; ++c)
                if (sInv[r][c] && invTop(r) + kInvH >= kDeathY) {
                    doLose(nowMs); drawPhaseUi(nowMs); syncLeds(nowMs); return;
                }
    }

    drawPhaseUi(nowMs);
    syncLeds(nowMs);
}
