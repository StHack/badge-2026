#pragma once
#include "ble_types.h"
#include <stdbool.h>

// Callback invoked from BLE stack thread when a matching beacon is seen.
// MUST be fast — only set volatile flags, do not call Arduino functions.
using BeaconCallback = void (*)(const ParsedBeacon& beacon);

// Start a passive BLE scan. Calls cb when a valid ST: beacon is seen.
// Returns false if BLE init failed.
bool bleScanStart(BeaconCallback cb);

// Stop scan and free BLE stack resources (use before light sleep).
void bleScanStop();

// Stop scan but keep the BLE stack alive (no deinit — use during FullMode cycles).
void bleScanStopSoft();

bool bleScanIsRunning();
