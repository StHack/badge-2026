#pragma once
#include <stdint.h>
#include <stdbool.h>

// Thin wrappers around Preferences (NVS).
// Each module uses its own namespace — this file provides a shared open/close helper.

#include <Preferences.h>

// Returns a Preferences instance opened on the given namespace in RW mode.
// Caller must call prefs.end() when done.
Preferences storageOpen(const char* ns, bool readOnly = false);
