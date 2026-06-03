#pragma once
#include <stdint.h>
#include <stdbool.h>

// Connect to WiFi then pull the firmware binary from url via HTTP.
// Flashes and reboots on success — never returns true.
// Returns false if WiFi connection or HTTP download failed.
bool otaConnect(const char* ssid, const char* psk, const char* url,
                uint32_t timeoutMs = 15000);

void otaDisconnect();

bool otaIsConnected();

// Always false: HTTP pull is blocking, no in-progress state visible to the loop.
bool otaTransferActive();

// No-op: kept so main.cpp compiles unchanged.
void otaTick();

// Shared eye-aware OTA progress display (left=status label, right=bar+%).
// Used by both WiFi OTA and BLE OTA.
void drawOtaProgress(const char* status, int pct);
