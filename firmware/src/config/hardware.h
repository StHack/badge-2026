#pragma once
#include <stdint.h>

// ── I2C buses ───────────────────────────────────────────────────────────────
inline constexpr uint8_t kPinLeftSda  = 19;
inline constexpr uint8_t kPinLeftScl  = 21;
inline constexpr uint8_t kPinRightSda = 25;
inline constexpr uint8_t kPinRightScl = 26;

// ── SSD1306 display ─────────────────────────────────────────────────────────
inline constexpr uint8_t  kDisplayAddr   = 0x3C;
inline constexpr uint8_t  kDisplayWidth  = 128;
inline constexpr uint8_t  kDisplayHeight = 64;

// ── NeoPixel LEDs (WS2812B, GPIO32) ─────────────────────────────────────────
inline constexpr uint8_t kPinLeds       = 32;
inline constexpr uint8_t kLedTotal      = 11;
inline constexpr uint8_t kLedRingLeft   = 3;   // indices 0-2
inline constexpr uint8_t kLedMouth      = 5;   // indices 3-7
inline constexpr uint8_t kLedRingRight  = 3;   // indices 8-10
inline constexpr uint8_t kLedBrightness = 55;

// Derived LED indices
inline constexpr uint8_t kLedMouthStart = kLedRingLeft;
inline constexpr uint8_t kLedRightStart = kLedRingLeft + kLedMouth;

// ── Buttons (active low, internal pull-up) ───────────────────────────────────
inline constexpr uint8_t kPinBtnLeft   = 18;
inline constexpr uint8_t kPinBtnCenter = 17;
inline constexpr uint8_t kPinBtnRight  = 16;

// ── Passive buzzer (LEDC PWM) ────────────────────────────────────────────────
inline constexpr uint8_t kPinBuzzer      = 13;
inline constexpr uint8_t kBuzzerChannel  = 0;
inline constexpr uint16_t kBootBeepHz    = 2500;  // piezo resonance ~2500 Hz = max volume
inline constexpr uint16_t kBootBeepMs    = 40;

// ── NVS namespaces ───────────────────────────────────────────────────────────
inline constexpr const char* kNvsEyeCal   = "eyeScr";
inline constexpr const char* kNvsTama     = "tama";
inline constexpr const char* kNvsLedAnim  = "ledanim";
inline constexpr const char* kNvsEyeStyle = "eyestyle";
inline constexpr const char* kNvsSettings = "settings";
