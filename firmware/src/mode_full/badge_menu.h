#pragma once
#include <stdint.h>
#include <stdbool.h>

// Single badge menu — available in FullMode only.
// Entry : hold CENTER 2.5 s
// Exit  : hold L+R 1 s
bool badgeMenuIsOpen();
void badgeMenuTick(uint32_t nowMs);

// Request a game launch from BLE (safe to call from BLE callback).
// id: 0=Simon 1=Stop 2=Snake 3=Mastermind 4=Tetris 5=Flappy 6=Breakout 7=Pacman
void badgeMenuRequestGame(uint8_t gameId);

// Sync internal button prev-state to current button state so that button presses
// which occurred while the menu was bypassed (e.g. GATT auth) don't cause phantom
// actions when the menu resumes.
void badgeMenuSyncButtons();

// Load persisted settings (vol, brightness) from NVS and apply them.
// Call once at boot before the first frame.
void badgeMenuSettingsLoad();

// True when the FullMode periodic beacon scan is disabled by the user.
// Checked by badge_fsm.cpp — not affected by SleepMode scan or BG_ON.
extern bool gBeaconScanDisabled;
extern bool gGattPersisted;
