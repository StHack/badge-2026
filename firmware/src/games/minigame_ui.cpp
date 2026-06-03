// minigame_ui.cpp — Win/lose overlays and LED tour flash for badge_v2 games.
#include "minigame_ui.h"
#include <stdio.h>
#include "game_result.h"
#include "../hal/display.h"
#include "../hal/leds.h"
#include "../config/hardware.h"
#include "../eyes/eye_geometry.h"
#include <Adafruit_SSD1306.h>

namespace {

uint32_t sTourFlashUntilMs = 0;
uint32_t sTourFlashColor   = 0;

}  // namespace

// ── Win overlay ───────────────────────────────────────────────────────────────

void minigameUiDrawWinBothEyes(uint32_t nowMs) {
    auto drawCheck = [](Adafruit_SSD1306& d) {
        // Thick checkmark: two strokes offset by 1 px for weight 2.
        for (int8_t k = 0; k <= 1; ++k) {
            d.drawLine(static_cast<int16_t>(55 + k), static_cast<int16_t>(45),
                       static_cast<int16_t>(63 + k), static_cast<int16_t>(52),
                       SSD1306_WHITE);
            d.drawLine(static_cast<int16_t>(63 + k), static_cast<int16_t>(52),
                       static_cast<int16_t>(80 + k), static_cast<int16_t>(35),
                       SSD1306_WHITE);
        }
    };

    {
        EyeUiGeom gL;
        if (eyeUiBeginFrame(gLeftDisplay, false, nowMs, &gL)) {
            drawCheck(gLeftDisplay);
            gLeftDisplay.display();
        }
    }
    {
        EyeUiGeom gR;
        if (eyeUiBeginFrame(gRightDisplay, true, nowMs, &gR)) {
            drawCheck(gRightDisplay);
            gRightDisplay.display();
        }
    }
}

// ── Lose overlay ──────────────────────────────────────────────────────────────

void minigameUiDrawLoseBothEyes(uint32_t nowMs, const char* label) {
    auto drawCross = [&](Adafruit_SSD1306& d) {
        // X centred around (64, 40) — 16×16, thickness 3 px
        for (int8_t k = -1; k <= 1; ++k) {
            d.drawLine(static_cast<int16_t>(56 + k), static_cast<int16_t>(32),
                       static_cast<int16_t>(72 + k), static_cast<int16_t>(48),
                       SSD1306_WHITE);
            d.drawLine(static_cast<int16_t>(72 + k), static_cast<int16_t>(32),
                       static_cast<int16_t>(56 + k), static_cast<int16_t>(48),
                       SSD1306_WHITE);
        }
        if (label != nullptr && label[0] != '\0') {
            eyeUiDrawTextCenteredNudged(d, 54, 1, label, 0);
        }
    };

    {
        EyeUiGeom gL;
        if (eyeUiBeginFrame(gLeftDisplay, false, nowMs, &gL)) {
            drawCross(gLeftDisplay);
            gLeftDisplay.display();
        }
    }
    {
        EyeUiGeom gR;
        if (eyeUiBeginFrame(gRightDisplay, true, nowMs, &gR)) {
            drawCross(gRightDisplay);
            gRightDisplay.display();
        }
    }
}

// ── LED tour flash ────────────────────────────────────────────────────────────

void minigameUiArmTourFlashGreen(uint32_t nowMs, uint32_t durationMs) {
    sTourFlashColor   = gLedStrip.Color(0, 220, 50);
    sTourFlashUntilMs = nowMs + durationMs;
}

void minigameUiArmTourFlashRed(uint32_t nowMs, uint32_t durationMs) {
    sTourFlashColor   = gLedStrip.Color(220, 10, 10);
    sTourFlashUntilMs = nowMs + durationMs;
}

void minigameUiDrawInactivityWarning(uint32_t nowMs, uint32_t secsLeft) {
    EyeUiGeom g;
    if (!eyeUiBeginFrame(gLeftDisplay, false, nowMs, &g)) return;
    char buf[8];
    snprintf(buf, sizeof(buf), "%u!", (unsigned)secsLeft);
    eyeUiDrawRotatedKeyword(gLeftDisplay, &g, buf);
    gLeftDisplay.display();
}

void minigameUiPaintTourFlashIfActive(uint32_t nowMs) {
    if (static_cast<int32_t>(nowMs - sTourFlashUntilMs) >= 0) {
        // Deadline passed — clear ring LEDs.
        for (uint8_t i = 0; i < kLedRingLeft; ++i) {
            gLedStrip.setPixelColor(i, 0);
        }
        for (uint8_t i = kLedRightStart; i < kLedTotal; ++i) {
            gLedStrip.setPixelColor(i, 0);
        }
        gLedStrip.show();
        return;
    }
    // Active — paint ring LEDs with stored colour.
    for (uint8_t i = 0; i < kLedRingLeft; ++i) {
        gLedStrip.setPixelColor(i, sTourFlashColor);
    }
    for (uint8_t i = kLedRightStart; i < kLedTotal; ++i) {
        gLedStrip.setPixelColor(i, sTourFlashColor);
    }
    gLedStrip.show();
}
