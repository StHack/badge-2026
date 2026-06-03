#pragma once

#define BADGE_VERSION_MAJOR 1
#define BADGE_VERSION_MINOR 0

#define BADGE_VERSION_STRING "1.0"

// ── OTA deployment config ────────────────────────────────────────────────────
// WiFi network used during OTA update (access point on-site).
#define BADGE_OTA_SSID  "Sthack-Badge"
#define BADGE_OTA_PSK   "870d64e2acb5ed7acfbccf93299d4ba0"

// HTTP URL serving the firmware binary (e.g. http://192.168.1.1/firmware.bin).
#define BADGE_OTA_URL   "http://192.168.0.254/firmware.bin"

inline const char* firmwareVersionString() { return BADGE_VERSION_STRING; }
