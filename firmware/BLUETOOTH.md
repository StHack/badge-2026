# Bluetooth signals

Two independent BLE mechanisms coexist: **beacon broadcast** (one-way, no pairing) and **GATT server** (bidirectional, requires pairing).

---

## Beacon format

Signals are broadcast as BLE advertisements with manufacturer-specific data in plain ASCII:

```
ST:<COMMAND>[:<BADGE_ID>][|<PAYLOAD>][~P][~B]
```

| Field | Description |
|-------|-------------|
| `ST:` | Fixed prefix, identifies a Sthack badge signal |
| `<COMMAND>` | Command name (see table below) |
| `:<BADGE_ID>` | Optional, 6-char uppercase hex (last 3 bytes of MAC). If absent, signal applies to all badges |
| `\|<PAYLOAD>` | Optional command-specific data |
| `~P` | Proximity flag, badge ignores this signal if RSSI < -65 dBm |
| `~B` | Boot flag, only processed during the targeted boot scan (hold CENTER at power-on) |

Flags can be combined: `ST:WAKE:A3F201~P~B`

---

## Beacon commands

| Command | Full format | Effect |
|---------|-------------|--------|
| `WAKE` | `ST:WAKE[:<ID>]` | Switch badge to **FullMode** (eyes open, menu accessible) |
| `SLEEP` | `ST:SLEEP[:<ID>]` | Return badge to **SleepMode** |
| `BG_ON` | `ST:BG_ON[:<ID>]` | Badge stays awake and scans continuously, ignores sleep timer |
| `BG_OFF` | `ST:BG_OFF[:<ID>]` | Resume normal sleep cycle |
| `CRY` | `ST:CRY[:<ID>]` | Play the badge's unique Pokemon-style cry |
| `MSG` | `ST:MSG[:<ID>]\|<text>` | Cry + scroll text across both displays |
| `OTA` | `ST:OTA[:<ID>]\|<version>` | Connect to the hardcoded WiFi AP and update firmware if badge version < target |
| `TAMA` | `ST:TAMA[:<ID>]` | Force-trigger a tamagotchi interaction immediately |
| `CLEAR` | `ST:CLEAR[:<ID>]` | Reset dedup state, allows re-sending the last command |

### Examples

```
ST:WAKE                        # wake all badges
ST:WAKE:A3F201                 # wake only badge A3F201
ST:MSG|Welcome to Sthack!      # broadcast message to all
ST:MSG:A3F201|Good luck        # targeted message
ST:OTA|0.6                     # OTA update if badge is older than 0.6
ST:TAMA~P                      # trigger tama only for nearby badges (RSSI >= -65 dBm)
ST:WAKE~B                      # only processed during targeted boot scan
```

---

## Deduplication

The badge ignores repeated identical beacons to avoid acting on the same signal multiple times during a scan window. Send `ST:CLEAR` (or `ST:CLEAR:<ID>`) to reset this state before re-sending a command.

---

## GATT server

The GATT server is started from **Settings > Bluetooth** in FullMode. It requires a PIN confirmation on the badge before accepting commands (4-digit PIN displayed on screen, enter via buttons).

**Service UUID:** `19b10020-e8f2-537e-4f6c-d104768a1214`

| Characteristic | UUID | Access | Purpose |
|----------------|------|--------|---------|
| Info | `19b10021-...` | Read | Badge ID, name, firmware version |
| Command | `19b10023-...` | Write | Control commands (see below) |
| Music | `19b10024-...` | Write | Play RTTTL melody |
| PIN | `19b10025-...` | Write | Submit PIN for authentication |
| OTA data | `19b10027-...` | Write | BLE OTA firmware transfer (244-byte chunks) |

### GATT command strings (write to `19b10023`)

| Command | Effect |
|---------|--------|
| `EYE:<N>` | Set eye animation (0=FSM, 1=closed, 2=awake, 3=sleepy, 4=hearts, 5=scroll, 6=blinking, 7=calm) |
| `LED:<N>` | Set LED animation mode |
| `TEXT:<text>` | Scroll text across both displays |
| `MUSIC:<rtttl>` | Play an RTTTL melody |
| `CRY` | Trigger badge cry |
| `STYLE:<N>` | Set eye style |
| `GAME:<N>` | Launch a minigame (0=Simon, 1=Stop, 2=Snake, 3=Mastermind, 4=Tetris, 5=Flappy, 6=Breakout, 7=Pac-Man, 8=Doodle, 9=Maze) |
| `TAMA_TRIGGER` | Force tamagotchi interaction |
| `NVS_RESET` | Erase all NVS data and reboot (factory reset) |
| `FSM_WAKE` | Switch to FullMode |
| `FSM_SLEEP` | Switch to SleepMode |

---

## Proximity gating (`~P`)

Any beacon can be restricted to nearby badges by appending `~P`. The badge measures the RSSI of the advertisement and ignores it if RSSI < -65 dBm (approximately 1-2 m in open space). Adjust `kProximityRssiThreshold` in `src/ble/ble_types.h` to tune the range.

---

## Boot-time targeted scan (`~B`)

If CENTER is held at power-on, the badge enters a 2-second targeted scan that only reacts to `~B`-flagged beacons. This allows an organiser to send a command to a specific badge without triggering all nearby badges. Normal broadcast beacons are ignored during this window.
