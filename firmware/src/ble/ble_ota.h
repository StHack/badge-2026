#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

// ── BLE OTA module ────────────────────────────────────────────────────────────
// Firmware update over BLE GATT.
// Protocol (driven by gatt_server.cpp):
//   1. Client sends OTA_BLE_BEGIN:<total_bytes>  on the cmd characteristic
//   2. Client sends raw firmware chunks           on the OTA data characteristic
//   3. Client sends OTA_BLE_END                  on the cmd characteristic
//      → firmware is validated, boot partition set, badge reboots.
//   Abort: OTA_BLE_ABORT on cmd characteristic at any time.

// Called from BLE callback when BEGIN command arrives.
void bleOtaBegin(uint32_t totalBytes);

// Called from BLE callback for each raw firmware chunk.
void bleOtaOnDataChunk(const uint8_t* data, size_t len);

// Called from BLE callback when END command arrives.
void bleOtaRequestCommit();

// Called from BLE callback when ABORT command arrives.
void bleOtaRequestAbort();

// True while a BLE OTA session is open (Active or CommitPending).
bool bleOtaInProgress();

// Bytes written so far; valid only when bleOtaInProgress().
uint32_t bleOtaBytesReceived();

// Progress 0–100; valid only when bleOtaInProgress().
uint8_t bleOtaPercent();

// Drive commit/abort/display from the main loop.
void bleOtaTick(uint32_t nowMs);
