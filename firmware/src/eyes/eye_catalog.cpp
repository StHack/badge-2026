// eye_catalog.cpp — style registry + per-frame dispatch for badge_v2
#include "eye_catalog.h"
#include "eye_renderer.h"
#include "eye_styles.h"
#include "eye_animation.h"
#include "../config/hardware.h"
#include <Preferences.h>
#include <Arduino.h>

// ── Forward declarations ──────────────────────────────────────────────────────
// These renderers live in the main drawing layer (to be provided by the build).
void drawStandardStyleEye(Adafruit_SSD1306& display, const EyePose& pose,
                          bool isRightEye, uint32_t nowMs,
                          const char* scrollText);

void drawAlternateFramedEye(Adafruit_SSD1306& display, const EyePose& pose,
                            bool isRightEye, uint32_t nowMs,
                            AlternatePupilDrawFn drawPupil,
                            const char* scrollText);

void drawAlternateMatrixEye(Adafruit_SSD1306& display, const EyePose& pose,
                            bool isRightEye, uint32_t nowMs,
                            const char* scrollText);

// ── Internal state ────────────────────────────────────────────────────────────

static EyeStyleId sCurrentStyle = EyeStyleId::Standard;

// ── Style name table ──────────────────────────────────────────────────────────
// Must match EyeStyleId enum order exactly.

static const char* const kStyleNames[] = {
    "Standard",
    "Hearts",
    "Stars",
    "Matrix",
    "Hypnosis",
    "Bolt",
    "Reptile",
    "Infinity",
    "Anime",
    "Scope",
    "Clock",
    "Robot",
    "Lock",
    "Wifi",
    "Cat",
    "Human",
};

static_assert(
    sizeof(kStyleNames) / sizeof(kStyleNames[0]) ==
        static_cast<size_t>(EyeStyleId::Count),
    "kStyleNames length must match EyeStyleId::Count");

// ── Catalog API ───────────────────────────────────────────────────────────────

void eyeCatalogSetStyle(EyeStyleId id) {
    if (static_cast<uint8_t>(id) < static_cast<uint8_t>(EyeStyleId::Count)) {
        sCurrentStyle = id;
    }
}

EyeStyleId eyeCatalogGetStyle() {
    return sCurrentStyle;
}

uint8_t eyeCatalogStyleIndex() {
    return static_cast<uint8_t>(sCurrentStyle);
}

void eyeCatalogSetStyleByIndex(uint8_t index) {
    if (index < static_cast<uint8_t>(EyeStyleId::Count)) {
        sCurrentStyle = static_cast<EyeStyleId>(index);
    }
}

const char* eyeCatalogStyleName(EyeStyleId id) {
    const size_t i = static_cast<size_t>(id);
    if (i < sizeof(kStyleNames) / sizeof(kStyleNames[0])) {
        return kStyleNames[i];
    }
    return "?";
}

const char* eyeCatalogStyleNameCurrent() {
    return eyeCatalogStyleName(sCurrentStyle);
}

void eyeCatalogStyleSave() {
    Preferences prefs;
    if (prefs.begin(kNvsEyeStyle, false)) {
        prefs.putUChar("style", (uint8_t)sCurrentStyle);
        prefs.end();
    }
}

void eyeCatalogStyleLoad() {
    Preferences prefs;
    if (prefs.begin(kNvsEyeStyle, true)) {
        uint8_t v = prefs.getUChar("style", 0);
        prefs.end();
        eyeCatalogSetStyleByIndex(v);
    }
}

// ── Main dispatch ─────────────────────────────────────────────────────────────

void eyeCatalogDraw(Adafruit_SSD1306& display, bool isRightEye,
                    uint32_t nowMs, const EyePose& pose,
                    EyeExpression expression, const char* scrollText) {
    (void)expression; // expression is already folded into pose by eye_animation

    // Tamagotchi overrides — force specific pupil styles regardless of catalog
    if (eyeAnimGetMode() == EyeAnimMode::ForceSick) {
        drawAlternateFramedEye(display, pose, isRightEye, nowMs,
                               eyeStyleSickWavyFn, scrollText);
        return;
    }
    if (eyeAnimGetMode() == EyeAnimMode::ForceHearts) {
        drawAlternateFramedEye(display, pose, isRightEye, nowMs,
                               eyeStyleHeartFn, scrollText);
        return;
    }

    switch (sCurrentStyle) {
        case EyeStyleId::Standard:
            drawStandardStyleEye(display, pose, isRightEye, nowMs, scrollText);
            return;

        case EyeStyleId::Matrix:
            drawAlternateMatrixEye(display, pose, isRightEye, nowMs, scrollText);
            return;

        case EyeStyleId::Hearts:
            drawAlternateFramedEye(display, pose, isRightEye, nowMs,
                                   eyeStyleHeartFn, scrollText);
            return;
        case EyeStyleId::Stars:
            drawAlternateFramedEye(display, pose, isRightEye, nowMs,
                                   eyeStyleStarFn, scrollText);
            return;
        case EyeStyleId::Hypnosis:
            drawAlternateFramedEye(display, pose, isRightEye, nowMs,
                                   eyeStyleHypnosisFn, scrollText);
            return;
        case EyeStyleId::Bolt:
            drawAlternateFramedEye(display, pose, isRightEye, nowMs,
                                   eyeStyleBoltFn, scrollText);
            return;
        case EyeStyleId::Reptile:
            drawAlternateFramedEye(display, pose, isRightEye, nowMs,
                                   eyeStyleReptileFn, scrollText);
            return;
        case EyeStyleId::InfinityShape:
            drawAlternateFramedEye(display, pose, isRightEye, nowMs,
                                   eyeStyleInfinityShapeFn, scrollText);
            return;
        case EyeStyleId::Anime:
            drawAlternateFramedEye(display, pose, isRightEye, nowMs,
                                   eyeStyleAnimeFn, scrollText);
            return;
        case EyeStyleId::Scope:
            drawAlternateFramedEye(display, pose, isRightEye, nowMs,
                                   eyeStyleScopeFn, scrollText);
            return;
        case EyeStyleId::Clock:
            drawAlternateFramedEye(display, pose, isRightEye, nowMs,
                                   eyeStyleClockFn, scrollText);
            return;
        case EyeStyleId::Robot:
            drawAlternateFramedEye(display, pose, isRightEye, nowMs,
                                   eyeStyleRobotFn, scrollText);
            return;
        case EyeStyleId::Lock:
            drawAlternateFramedEye(display, pose, isRightEye, nowMs,
                                   eyeStyleLockFn, scrollText);
            return;
        case EyeStyleId::Wifi:
            drawAlternateFramedEye(display, pose, isRightEye, nowMs,
                                   eyeStyleWifiFn, scrollText);
            return;
        case EyeStyleId::Cat:
            drawAlternateFramedEye(display, pose, isRightEye, nowMs,
                                   eyeStyleCatFn, scrollText);
            return;
        case EyeStyleId::Human:
            drawAlternateFramedEye(display, pose, isRightEye, nowMs,
                                   eyeStyleHumanFn, scrollText);
            return;

        default:
            drawStandardStyleEye(display, pose, isRightEye, nowMs, scrollText);
            return;
    }
}
