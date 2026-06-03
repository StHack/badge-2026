#pragma once
#include "eye_types.h"
#include <Adafruit_SSD1306.h>

// ── Eye style catalog ─────────────────────────────────────────────────────────
// Ordered list of all available eye styles.
// To add a style:
//   1. Add an entry before Count
//   2. Add the name in eye_catalog.cpp
//   3. Implement an AlternatePupilDrawFn in eye_styles.cpp
//   4. Dispatch it in eyeCatalogDraw()
enum class EyeStyleId : uint8_t {
    Standard = 0,   // white sclera + iris texture
    Hearts,          // heart pupils (tamagotchi success)
    Stars,
    Matrix,          // matrix rain fill (no pupil fn, special renderer)
    Hypnosis,
    Bolt,
    Reptile,
    InfinityShape,
    Anime,
    Scope,
    Clock,
    Robot,
    Lock,
    Wifi,
    Cat,
    Human,
    Count,
};

// ── Catalog API ───────────────────────────────────────────────────────────────
void        eyeCatalogSetStyle(EyeStyleId id);
EyeStyleId  eyeCatalogGetStyle();
uint8_t     eyeCatalogStyleIndex();
void        eyeCatalogSetStyleByIndex(uint8_t index);
const char* eyeCatalogStyleName(EyeStyleId id);
const char* eyeCatalogStyleNameCurrent();

// Persist / restore selected style in NVS.
void eyeCatalogStyleSave();
void eyeCatalogStyleLoad();

// Called once per eye per frame. Dispatches to the right renderer.
void eyeCatalogDraw(Adafruit_SSD1306& display, bool isRightEye,
                    uint32_t nowMs, const EyePose& pose,
                    EyeExpression expression, const char* scrollText);
