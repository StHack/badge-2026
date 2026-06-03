#pragma once
#include <stdint.h>

// Derives the badge's identity from the last 3 bytes of the MAC address.
// Must be called once at startup before any other identity function.
void badgeIdInit();

// 6-character hex string (uppercase), e.g. "A3F201"
const char* badgeIdHexString();

// Human-readable badge name, e.g. "KIRO"
const char* badgeIdName();

// 24-bit seed derived from MAC (used by cryStart() for Pokémon glide)
uint32_t    badgeIdCrySeed();

// Short RTTTL cry melody string (discrete-note approximation — kept for compatibility)
const char* badgeIdCryRtttl();

// Longer RTTTL music melody string (celebration / level-up sound)
const char* badgeIdMusicRtttl();
