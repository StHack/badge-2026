# User Guide

## Menu

Hold **CENTER** for 0.5 s in awake mode to open the menu.

```
Menu
├── Info
│   ├── Name
│   ├── ID
│   └── Version
├── Games
│   └── [Games list]
├── Skin
│   ├── Eyes
│   ├── Mouth
│   ├── Rings
│   └── Blink
├── Settings
│   ├── LEDs
│   ├── Sound
│   └── Bluetooth
│       ├── Server
│       └── Remote control
└── Bonus
```

- **Navigate:** LEFT / RIGHT to browse entries
- **Confirm:** CENTER (short press)
- **Back / exit:** hold CENTER

### Info

Displays your badge's identity:

- **Name:** your badge's unique name (plays its unique cry when clicked)
- **ID:** its 6-character identifier
- **Version:** firmware version

### Games

Play any game freely, without waiting for a nightmare. The first 10 are part of the tamagotchi progression.

| Game            | How to play                                              |
| --------------- | -------------------------------------------------------- |
| **Simon**       | Memorise and repeat the button sequence                  |
| **Stop**        | Stop the cursor inside the zone                          |
| **Snake**       | Eat food without hitting borders or yourself             |
| **Mastermind**  | Deduce the hidden sequence                               |
| **Tetris**      | Clear horizontal lines                                   |
| **Flappy Bird** | CENTER to flap through the pipes                         |
| **Breakout**    | Destroy all bricks without letting the ball fall         |
| **Pac-Man**     | Eat dots, avoid ghosts                                   |
| **Doodle Jump** | Keep jumping upward without falling                      |
| **Maze**        | LEFT/RIGHT to rotate, CENTER to move forward             |
| **Dino**        | Jump over obstacles                                      |
| **Space Inv.**  | Shoot down waves of descending aliens                    |

### Skin

Customise the visual appearance of your badge.

| Option    | Available styles |
| --------- | ---------------- |
| **Eyes**  | Standard, Hearts, Stars, Matrix, Hypnosis, Bolt, Reptile, Infinity, Anime, Scope, Clock, Robot, Lock, Wifi, Cat, Human |
| **Mouth** | Off, Pink, Rainbow, Lava, Chill, Candle, Cyber, VU, Bounce, Sparkle, Ocean, Toxic, Aurora, Heartbeat, Matrix |
| **Rings** | Off, BluePulse, CyanPulse, Rainbow, Fire, Police, Comet, Breathing, Disco, Strobe, GreenPulse, Aurora, Sunset, Thunder, Heartbeat |
| **Blink** | ON / OFF : enable or disable automatic eye blinking |

### Settings

| Setting                        | What it does                                            |
| ------------------------------ | ------------------------------------------------------- |
| **LEDs**                       | Overall LED brightness, from 0 (off) to 10 (maximum)   |
| **Sound**                      | Mute or unmute the buzzer (ON / OFF)                    |
| **Bluetooth**                  | Submenu for wireless connection options                 |
| &emsp;Server                   | Enable the GATT server to accept Bluetooth connections  |
| &emsp;Remote control           | Allow the badge to receive remote BLE commands          |

---

## Bluetooth interface

When the GATT server is enabled (Settings > Bluetooth > Server), the badge can be controlled from a browser using the [`badge.html`](badge.html) page.

Open the file locally in Chrome or Edge (Web Bluetooth is not supported in other browsers), click **Connect**, and select your badge from the device list. A PIN is displayed on the badge for a few seconds to confirm the pairing.

During the event, a BLE broadcast system was used to send commands to all badges simultaneously. This can be disabled in Settings > Bluetooth > Remote control. See [`BLUETOOTH.md`](BLUETOOTH.md) for details.

---

## Power

In sleep mode the badge wakes every 30 seconds, scans BLE beacons for 2 seconds, then goes back to sleep. This keeps average current around **17 mA**, giving a comfortable runtime on 3 AA batteries across a full night event. When fully awake (displays on, LEDs animating), consumption rises to around **150 mA**.

---

## Development

### Build

The firmware uses [PlatformIO](https://platformio.org/). From the `firmware/` directory:

```bash
pio run
```

The compiled binary is written to `.pio/build/esp32dev/firmware.bin`.

### Flash over Bluetooth

1. Enable the GATT server on the badge (Settings > Bluetooth > Server).
2. Open `tools/badge.html` in Chrome or Edge and connect to the badge.
3. In the OTA section, drag and drop `firmware.bin` onto the upload area and confirm.

The badge reboots automatically once the transfer is complete.

### Flash via USB

Connect the badge to USB and run:

```bash
pio run --target upload
```

For serial output:

```bash
pio device monitor
```

### Rollback

The flash is split into two OTA slots (see [`partitions_badge.csv`](partitions_badge.csv)):

```
app0  ota_0  0x010000  ~1.97 MB
app1  ota_1  0x200000  ~1.97 MB
```

Each OTA update writes to the inactive slot and switches the boot pointer to it. The previously running firmware stays intact in the other slot.

To roll back to the previous firmware, hold **LEFT + CENTER + RIGHT** while powering on the badge. The bootloader switches the active slot back and reboots into the older firmware.
