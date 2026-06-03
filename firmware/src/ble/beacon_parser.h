#pragma once
#include "ble_types.h"
#include <stddef.h>

// Parse manufacturer data bytes into a ParsedBeacon.
// Expected prefix: "ST:" followed by command, optional ":BADGEID", optional "|payload"
// Returns true if the data matches the ST: protocol.
bool beaconParse(const uint8_t* data, size_t len, ParsedBeacon* out);

// Returns true if the beacon is addressed to this badge
// (badgeId empty = broadcast, or matches our ID).
bool beaconIsForMe(const ParsedBeacon& b);
