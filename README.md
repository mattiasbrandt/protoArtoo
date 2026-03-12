# protoArtoo

**Open-source ESP32 body controller firmware for hoverboard-driven MK4 astromech droids.**

> An alternative firmware to the firmware shipped on the [Artoo Controller PCB](https://artoo.uk)
> that is fully owned, auditable, and extensible — same hardware, completely
> transparent code.

---

## What this is

The Artoo Controller is an ESP32-based body controller for R2-D2 style
astromech droids.

protoArtoo is that alternative firmware. It is written from scratch, properly documented,
tested, and designed to be understood and extended by the wider droid-building community.

### What it controls

- **Drive** — hoverboard motors via custom hoverboard firmware and serial UART3 communication
- **RC input** — dual SBUS receivers: one for drive, one for dome spin
- **Audio** — DY-SV5W sound module (default); abstract `AudioDriver` interface for alternatives
- **Servo arms** — 2× MG996R utility arm servos via LEDC PWM
- **Dome motor** — ESC signal via LEDC PWM (tested: ISDT ESC70)
- **Dome link** — bidirectional Marcduino serial to AstroPixelsPlus over slip ring

### What makes it different from a classic MarcDuino build

| | Classic MarcDuino | protoArtoo |
|---|---|---|
| Body controller | ATmega328P | ESP32 (240 MHz, WiFi) |
| Dome controller | ATmega328P | AstroPixelsPlus ESP32 |
| Body→dome serial | TX only (one direction) | **Full-duplex bidirectional** |
| Sound location | Dome | **Body** — sole audio source |
| Drive | Sabertooth / JAW motors | Hoverboard via Gen2.x UART |
| RC input | PS2 via SHADOW Android app | SBUS receivers + phone browser |
| Board count | 2–3 MarcDuino PCBs | 2 ESP32 boards only |
| Firmware | Closed-source vendor | **Fully open source** |

See [`docs/topology.md`](./docs/topology.md) for the full architectural comparison.

---

## Hardware requirements

| Component | Notes |
|---|---|
| Artoo Controller PCB v1.1 | ESP32 D1 Mini module |
| Hoverboard with custom firmware | https://github.com/EFeru/hoverboard-firmware-hack-FOC or https://github.com/RoboDurden/Hoverboard-Firmware-Hack-Gen2.x |
| 2× SBUS RC receivers | One drive, one dome spin (tested: HOTRC 650) |
| DY-SV5W audio module | Default driver; DFPlayer Mini also supported |
| 2× hobby servos | Utility arms (tested: MG996R) |
| Dome motor ESC | Standard RC ESC, 50 Hz PWM (tested: ISDT ESC70) |
| Slip ring (12-conductor) | Body↔dome bidirectional serial + power |
| mattiasbrandt/AstroPixelsPlus | Dome controller — fork of reeltwo/AstroPixelsPlus |

See [`docs/pin_map.md`](./docs/pin_map.md) for confirmed GPIO assignments (populated in Phase 0).

---

## Repository structure

```
protoArtoo/
├── platformio.ini             # Build configuration — two envs: protoArtoo + native
├── CHANGELOG.md               # All releases, conventional commits format
├── CONTRIBUTING.md            # Commit format, branch strategy, PR checklist
├── include/
│   ├── config.h               # GPIO pin assignments — populate from PCB trace
│   ├── robot_state.h          # RobotState struct, queues, mutexes
│   ├── marcduino.h            # Command string constants
│   └── audio_driver.h         # AudioDriver abstract interface
├── src/
│   ├── main.cpp
│   ├── tasks/                 # FreeRTOS tasks (Core 0: WiFi/Web; Core 1: RT control)
│   └── drivers/               # Hardware abstractions (UART, audio, Marcduino parser)
├── data/                      # LittleFS web UI assets
├── test/
│   ├── test_native/           # Pure-logic tests — run on dev machine, no hardware
│   └── test_embedded/         # On-device tests — requires flashed ESP32
├── tools/
│   └── command_compat.py      # Marcduino command verification script
└── docs/
    ├── pin_map.md             # GPIO assignments (filled in Phase 0)
    ├── api.md                 # REST API reference
    └── topology.md            # Classic MarcDuino vs protoArtoo architecture
```

---

## Build prerequisites

- [VS Code](https://code.visualstudio.com/) + [PlatformIO extension](https://platformio.org/install/ide?install=vscode)
- Python 3.8+ (for `tools/command_compat.py`)
- `clang-format` (for code style enforcement — see `.clang-format`)

Open the repo in VS Code and accept the recommended extensions when prompted
(see `.vscode/extensions.json`). Workspace settings for formatting, linting,
and file hygiene are checked in at `.vscode/settings.json`.

---

## Building and flashing

```bash
# Clone
git clone https://github.com/mattiasbrandt/protoArtoo.git
cd protoArtoo

# Copy and edit credentials (never commit this file)
cp src/secrets.h.example src/secrets.h
# Edit src/secrets.h with your WiFi SSID/password if using STA mode

# Build (no hardware required)
pio run -e protoArtoo

# Flash via USB
pio run -e protoArtoo --target upload

# Flash via OTA (device must be on network)
pio run -e protoArtoo --target upload --upload-port 192.168.4.1

# Open serial monitor
pio device monitor
```

---

## Running tests

```bash
# Fast — logic tests on dev machine, no hardware required
pio test -e native

# On-device — requires flashed ESP32
pio test -e protoArtoo

# Verbose output
pio test -e native -v

# Static analysis
pio check
```

---

## Companion project

The dome controller is a fork of [reeltwo/AstroPixelsPlus](https://github.com/reeltwo/AstroPixelsPlus):

**[mattiasbrandt/AstroPixelsPlus](https://github.com/mattiasbrandt/AstroPixelsPlus)**

Changes from upstream:
- Sound player disabled (`PREFERENCE_MARCSOUND = kNone`) — audio is handled by protoArtoo body
- `sendBodyCommand()` — dome sends arm/sound commands to body during coordinated sequences
- `handleBodySerial()` — bidirectional body link with `#PAHB`/`#APHB` heartbeat protocol
- 4-state body link badge in settings UI (Connected / Lost / Not seen / Disabled)

---

## Architecture overview

```
RC Transmitter ──SBUS──→  [protoArtoo — Artoo Controller PCB]  ←──WiFi── Phone browser
                                │                    │
                           UART1 (115200)        UART2 (9600)
                                │                    │ bidirectional
                                ↓                 slip ring
                         Hoverboard              [AstroPixelsPlus — dome]
                                                  • NeoPixel dome lights
                                                  • Panel servos
                                                  • Holoprojectors
                                                  • Logic displays
                          ↑
                     UART (9600)
                    DY-SV5W audio module
                    (body = sole audio source)
```

All Marcduino-compatible commands flow over the bidirectional slip ring link.
The dome has no local sound module. The body is the sole audio authority.

---

## Safety

This firmware controls a 20 kg wheeled robot. The failsafe system has five
independent layers:

1. **SBUS receiver hardware failsafe** — receiver firmware, ~100 ms
2. **SBUS software watchdog** — 200 ms timeout in SBUSInputTask
3. **Web API drive timeout** — 500 ms; client must re-send to keep driving
4. **ESP32 Task Watchdog Timer** — 3 s reset; post-reset boot sets estop=true
5. **Hoverboard own UART timeout** — ~500 ms, independent of ESP32

`estop` requires explicit `POST /api/estop/clear` to clear. It does not auto-clear.

---

## Versioning

protoArtoo uses [Semantic Versioning 2.0.0](https://semver.org/) and
[Conventional Commits](https://www.conventionalcommits.org/).

- `feat:` → MINOR version bump
- `fix:` → PATCH version bump
- `feat!:` / `BREAKING CHANGE:` footer → MAJOR version bump

See [CHANGELOG.md](./CHANGELOG.md) for all releases.

---

## Contributing

See [CONTRIBUTING.md](./CONTRIBUTING.md) for commit format, branch strategy,
and pull request checklist.

Key rules:
- All commits follow [Conventional Commits](https://www.conventionalcommits.org/) format
- All code passes `pio check` before commit
- All native tests pass before merge to `dev`
- No WiFi credentials, no TBD GPIO guesses, no code that doesn't compile

---

## Licence

MIT — see [LICENSE](./LICENSE)

---

## Acknowledgements

### Hardware — Artoo Controller PCB

**[Steve](https://www.artoo.uk/)** designed the Artoo Controller PCB that protoArtoo
runs on. The board is a genuinely clever piece of hardware engineering: an ESP32
module with dedicated headers for dual SBUS receivers, servo outputs, audio module
serial, dome serial, and hoverboard UART — all in a compact form factor designed
specifically for MK4 astromech droid body electronics. The thoughtful layout, connector
choices, and peripheral routing on this PCB made the task of writing an alternative
firmware significantly easier than starting from scratch hardware. Steve's work on
the hardware side deserves full credit and recognition.

If you are building a droid and considering the Artoo Controller PCB:
👉 **[artoo.uk](https://www.artoo.uk/)**

---

### Firmware and libraries

**[EFeru](https://github.com/EFeru)** —
[hoverboard-firmware-hack-FOC](https://github.com/EFeru/hoverboard-firmware-hack-FOC).
The original open-source custom hoverboard firmware that introduced the 8-byte UART
serial protocol used by protoArtoo's drive system. Supports FOC (Field Oriented Control)
on older-generation hoverboard mainboards based on STM32.

**[RoboDurden](https://github.com/RoboDurden)** —
[Hoverboard-Firmware-Hack-Gen2.x-GD32](https://github.com/RoboDurden/Hoverboard-Firmware-Hack-Gen2.x-GD32).
A port of the EFeru firmware for newer Gen2.x hoverboard mainboards based on GD32F130.

Uses the same 8-byte UART serial protocol, making both firmwares compatible with
protoArtoo's drive system — which one you need depends on your hoverboard generation.

**[reeltwo](https://github.com/reeltwo)** —
[AstroPixelsPlus](https://github.com/reeltwo/AstroPixelsPlus) and the
[Reeltwo library](https://github.com/reeltwo/Reeltwo). The dome controller that
protoArtoo communicates with is built on this foundation. The Reeltwo library's
Marcduino command handling and its clean peripheral abstraction made it straightforward
to implement the bidirectional body link in the dome fork.

**Arduino ESP32 core** —
the RMT support in the Arduino ESP32 core made it possible to implement the
in-repo SBUS decoder without consuming a hardware UART.

---

### Community

The broader **[astromech building community](https://astromech.net/)** — the
collective open knowledge around MarcDuino, SHADOW, panel wiring, dome mechanics,
and the MK4 print files is what makes personal droid builds possible at all. This
firmware is intended as a contribution back to that community.
