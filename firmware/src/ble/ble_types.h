#pragma once
#include <stdint.h>

// ── Beacon command codes ──────────────────────────────────────────────────────
enum class BeaconCmd : uint8_t {
    Unknown = 0,
    Wake,        // ST:WAKE[:<ID>]          → activate FULL_MODE
    Sleep,       // ST:SLEEP[:<ID>]         → return to SLEEP_MODE
    BgOn,        // ST:BG_ON[:<ID>]         → stay scanning, skip sleep
    BgOff,       // ST:BG_OFF[:<ID>]        → resume normal sleep loop
    Ota,         // ST:OTA[:<ID>]|<target_version>   (WiFi/password hardcoded in version.h)
    Cry,         // ST:CRY[:<ID>]           → play badge cry
    Message,     // ST:MSG[:<ID>]|<text>    → play cry + scroll message
    Tama,        // ST:TAMA[:<ID>]          → force-trigger a tamagotchi interaction
    Clear,       // ST:CLEAR[:<ID>]         → reset dedup state (allow re-sending last command)
};

// RSSI threshold for proximity-gated beacons (dBm).
// Beacons tagged ~P are ignored if the measured RSSI is below this value.
// -65 dBm ≈ 1–2 m in open space; raise toward 0 for stricter proximity.
inline constexpr int8_t kProximityRssiThreshold = -65;

struct ParsedBeacon {
    BeaconCmd cmd;
    char badgeId[7];         // 6 hex chars + NUL; empty string = broadcast
    char payload[128];       // signal-specific data (OTA: "ver|ssid|psk|url"; MSG: text)
    bool proximityRequired;  // ~P → only process if RSSI >= kProximityRssiThreshold
    bool bootRequired;       // ~B → only processed during targeted boot scan (CENTER held)
};

// ── GATT service UUIDs ────────────────────────────────────────────────────────
// Primary service: 19b10020-e8f2-537e-4f6c-d104768a1214
inline constexpr const char* kGattServiceUuid   = "19b10020-e8f2-537e-4f6c-d104768a1214";
inline constexpr const char* kGattCmdCharUuid   = "19b10023-e8f2-537e-4f6c-d104768a1214";
inline constexpr const char* kGattInfoCharUuid  = "19b10021-e8f2-537e-4f6c-d104768a1214";
inline constexpr const char* kGattMusicCharUuid = "19b10024-e8f2-537e-4f6c-d104768a1214";
inline constexpr const char* kGattPinCharUuid    = "19b10025-e8f2-537e-4f6c-d104768a1214";
inline constexpr const char* kGattOtaDataCharUuid = "19b10027-e8f2-537e-4f6c-d104768a1214";
