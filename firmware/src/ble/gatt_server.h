#pragma once
#include <stdint.h>
#include <stdbool.h>

// Start the GATT server (BLE must not be scanning simultaneously).
// The server handles eye style, LED, music, and OTA commands via Web Bluetooth.
void gattStart();
void gattStop();
bool gattIsRunning();

// True while the PIN confirmation prompt is being shown to the user.
// When true, the main loop should skip all other rendering and input.
bool gattAuthPending();

// Call every loop iteration when GATT is running.
// Handles advertising restart, PIN auth UI, and button input.
void gattTick(uint32_t nowMs);
