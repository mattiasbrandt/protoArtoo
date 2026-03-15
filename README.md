# protoArtoo

<p align="center">
  <img src="data/r2d2body.svg" alt="protoArtoo logo" width="96" height="96">
</p>

**Open-source ESP32 body controller firmware for hoverboard-driven MK4 astromech droids.**

> An open-source firmware alternative for the [Artoo Controller PCB](https://artoo.uk).
> Written from scratch with full transparency and community extensibility in mind —
> leveraging the same excellent hardware with openly auditable code.

---

## What this is

The Artoo Controller is an ESP32-based body controller for R2-D2 style
astromech droids.

protoArtoo is that alternative firmware. It is written from scratch, properly documented,
tested, and designed to be understood and extended by the wider droid-building community.

### What it controls

- **Drive** — hoverboard motors via custom hoverboard firmware and serial UART3 communication
- **RC input** — three selectable modes: Standard PWM (6-channel), Single SBUS, or Dual SBUS receivers
- **Audio** — DY-SV5W sound module (default); abstract `AudioDriver` interface for alternatives
- **Servo arms** — 2× MG996R utility arm servos via LEDC PWM
- **AUX outputs** — 3× configurable outputs (ARM3-5) supporting MG996R servo, MG90S servo, RGB LED, or disabled
- **Dome motor** — ESC signal via LEDC PWM (tested: ISDT ESC70)
- **Dome link** — bidirectional Marcduino serial to AstroPixelsPlus over slip ring
- **Operation mode** — Driving (full movement) or Stationary (performance mode) via web UI or RC

### What makes it different from a classic MarcDuino build

| | Classic MarcDuino | protoArtoo |
|---|---|---|
| Body controller | ATmega328P | ESP32 (240 MHz, WiFi) |
| Dome controller | ATmega328P | AstroPixelsPlus ESP32 |
| Body→dome serial | TX only (one direction) | **Full-duplex bidirectional** |
| Sound location | Dome | **Body** — sole audio source |
| Drive | Sabertooth / JAW motors | Hoverboard (custom firmware) |
| RC input | PS2 via SHADOW Android app | RC receivers (PWM/SBUS) + any web browser |
| Board count | 2–3 MarcDuino PCBs | 2 ESP32 boards only |
| Firmware | Open source (ATmega328P) | **Open source (ESP32)** |

See [`docs/topology.md`](./docs/topology.md) for the full architectural comparison.
For project terms and abbreviations, see [`docs/terminology.md`](./docs/terminology.md).

---

## Core Hardware

**Required:**
- **Artoo Controller PCB v1.1** — ESP32 D1 Mini-based body controller ([artoo.uk](https://artoo.uk))
- **Hoverboard** with custom firmware — drive motors via UART serial  
  Compatible: [EFeru FOC](https://github.com/EFeru/hoverboard-firmware-hack-FOC) (STM32) or [RoboDurden Gen2.x](https://github.com/RoboDurden/Hoverboard-Firmware-Hack-Gen2.x-GD32) (GD32)

**Tested / Supported:**
- **RC receivers:** Dual SBUS, Single SBUS, or 6-channel PWM (tested: HOTRC 650)
- **Audio:** DY-SV5W (default), DFPlayer Mini via `AudioDriver` interface
- **Servos:** MG996R/MG90S utility arms + 3× configurable AUX outputs
- **Dome motor:** Standard 50 Hz RC ESC (tested: ISDT ESC70)
- **Dome controller:** [AstroPixelsPlus fork](https://github.com/mattiasbrandt/AstroPixelsPlus) with bidirectional body link

GPIO assignments and wiring details: [`docs/pin_map.md`](./docs/pin_map.md)

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
     ├── terminology.md         # Project glossary (RobotState, NVS, SBUS, etc.)
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
                                 ↓                 serial link
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

All Marcduino-compatible commands flow over the bidirectional serial link.
The dome has no local sound module. The body is the sole audio authority.

---

## Key Features

**Modular API Architecture**
API routes organized into focused modules (`api_estop.cpp`, `api_drive.cpp`, `api_config.cpp`,
`api_status.cpp`, `api_system.cpp`) replacing the original monolithic structure.

**Operator-Quality Web Interface**
Web UI formally signed off by project manager (2026-03-15). Operator-facing copy with no
developer/planning language, component-scoped logging, and responsive design.

**Three RC Input Modes**
Runtime-selectable: Standard PWM (6-channel), Single SBUS, or Dual SBUS. All modes
support full channel remapping via web UI without firmware rebuild.

**Configurable AUX Outputs**
Three AUX channels (ARM3-5) independently configurable as MG996R servo, MG90S servo,
RGB LED strip, or disabled. Type selection auto-applies safe calibration defaults.

**Operation Mode & Mood Selector**
- Driving ↔ Stationary modes via dedicated `/api/mode` endpoint
- Four mood profiles (Quiet, Mid-Awake, Full-Awake, Awake+) coordinating body audio
  and dome visuals over bidirectional link

**Safety-First Architecture**
- Five independent failsafe layers from hardware through application
- Latching estop requiring explicit clear — never auto-resets
- Component-scoped logging with source tracing (`[SERVO] [WEB] Arm1 opened`) for full auditability

**Bidirectional Dome Link**
- Full-duplex serial over slip ring (body ↔ dome, not just body → dome)
- Dome can trigger body actions (sounds, arm sequences) — coordinated cross-controller sequences
- Heartbeat protocol with connection state tracking (Connected / Lost / Not seen)

**Universal Browser Control**
- Works on any phone/tablet/laptop — no app installation required
- WiFi AP mode for field use + STA mode for home network
- Real-time dashboard with live logs, health indicators, and manual command interface

**Hardware Flexibility**
- Abstract `AudioDriver` interface — swap audio modules without code changes
- Component type system — AUX outputs configurable per-channel (servo types or RGB)
- Runtime RC mode selection — PWM, Single SBUS, or Dual SBUS without rebuild

**Modern Development Practices**
- 315+ native unit tests covering LEDC math, dome math, SBUS protocol, Marcduino helpers
- PlatformIO build system with static analysis (`pio check`)
- FreeRTOS core isolation: Core 0 (network/web), Core 1 (real-time control)
- Modular architecture with focused API route modules

---

## Safety

This firmware controls a 20 kg wheeled robot. The failsafe system has five
independent layers:

1. **RC receiver hardware failsafe** — receiver firmware, ~100 ms
2. **RC software watchdog** — 200 ms timeout in RC input task (all modes: PWM, SBUS)
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
runs on. The board provides a practical, purpose-built platform for MK4 astromech
droid body electronics: an ESP32 module with dedicated headers for RC receivers,
servo outputs, audio module, dome serial, and hoverboard UART — all in a compact
form factor. This ready-made hardware foundation made it possible to focus on
firmware development rather than board design. Steve's work on the hardware side
deserves full credit and recognition.

If you are building a droid and considering the Artoo Controller PCB:
👉 **[artoo.uk](https://www.artoo.uk/)**

---

### Firmware

**Hoverboard Firmware**

| Project | MCU | Description |
|---------|-----|-------------|
| [EFeru/hoverboard-firmware-hack-FOC](https://github.com/EFeru/hoverboard-firmware-hack-FOC) | STM32 | Original open-source hoverboard firmware with FOC (Field Oriented Control). Introduced the 8-byte UART protocol used by protoArtoo. |
| [RoboDurden/Hoverboard-Firmware-Hack-Gen2.x-GD32](https://github.com/RoboDurden/Hoverboard-Firmware-Hack-Gen2.x-GD32) | GD32F130 | Port for newer Gen2.x hoverboard mainboards. Compatible protocol. |

**Dome Controller**

| Project | Description |
|---------|-------------|
| [reeltwo/AstroPixelsPlus](https://github.com/reeltwo/AstroPixelsPlus) | Base dome controller firmware. Uses Reeltwo library for Marcduino command handling. |
| [mattiasbrandt/AstroPixelsPlus](https://github.com/mattiasbrandt/AstroPixelsPlus) | Fork with bidirectional body link support (heartbeat protocol, body command dispatch). |

### Libraries

**External Dependencies (via PlatformIO)**

| Library | Version | Purpose |
|---------|---------|---------|
| [ESPAsyncWebServer](https://github.com/ESP32Async/ESPAsyncWebServer) | 3.6.0 | Asynchronous HTTP/WebSocket server for web UI and REST API |
| [AsyncTCP](https://github.com/ESP32Async/AsyncTCP) | 3.3.2 | Async TCP library for ESP32 (required by ESPAsyncWebServer) |
| [ArduinoJson](https://github.com/bblanchon/ArduinoJson) | 7.4.3 | JSON serialization/deserialization for API payloads |

**ESP32 Arduino Core (Built-in)**

| Component | Purpose |
|-----------|---------|
| `Preferences` | NVS (Non-Volatile Storage) for configuration persistence |
| `LittleFS` | Flash filesystem for web UI assets |
| `WiFi` | WiFi AP + STA mode support |
| `LEDC` | PWM generation for servos and ESC |
| `RMT` | Remote Control peripheral for SBUS decoding (no hardware UART consumed) |
| `FreeRTOS` | Task scheduling with core isolation (Core 0: network/web, Core 1: real-time control) |

---

### Community

**[Mr Baddeley](https://www.patreon.com/c/mrbaddeley)** — Creator of the
3D-printable MK4 astromech droid design. His print files, assembly guides, and
ongoing refinement work have made R2-D2 replica building accessible to thousands
of makers worldwide. The [Mr Baddeley Facebook Community](https://www.facebook.com/groups/MrBaddeley/)
is the primary hub for builders sharing progress, troubleshooting, and celebrating
their droids.

**[astromech.net](https://astromech.net/)** — The broader astromech building
community. The collective open knowledge around MarcDuino, SHADOW, panel wiring,
dome mechanics, and electronics is what makes personal droid builds possible.
This firmware is intended as a contribution back to that community.
