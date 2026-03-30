# Project Status

protoArtoo is open-source ESP32 body controller firmware for MK4 astromech droids.
This page tracks the current development state across firmware phases.
For detailed release notes see `CHANGELOG.md`.

---

## Build

Run `make help` for a full list of build, test, flash, and OTA targets.
For first-time setup:
- `make setup` writes a local `user.mk` with OTA IP, upload port, and audio backend choice.
- `make setup-wifi` writes `src/secrets.h` with masked WiFi credential prompts.
---


## Phase Overview

| Phase | Description | Status |
|---|---|---|
| Architecture & planning | Firmware design and hardware research | Complete |
| Dome firmware (AstroPixelsPlus fork) | Body-link protocol for the dome controller | Complete |
| Phase 1 — Drive | Hoverboard drive, RC receiver input, failsafe | Complete — `v0.1.0` |
| Phase 2 — Web interface | WiFi, web UI, OTA firmware updates | Complete |
| Phase 3 — Servos + dome motor | Arm servos, dome motor, RC diagnostics and mapping | Software complete — hardware validation partially deferred |
| **Phase 4 — Audio + dome link** | Sound playback, bidirectional dome communication, web UI improvements | Complete — `v0.4.0`; hardware validation deferred to Phase 5 |
| Phase 5 — Community release | Documentation, polish, public release | Planned |

---

## Current Version

Latest release: `v0.4.0` — see `CHANGELOG.md` for full history.
Development builds are versioned from the git history and build timestamp.

---

## What Works

### Drive and safety

- Hoverboard drive with speed and steering control from an RC transmitter
- Configurable speed limit from the web interface
- Three RC receiver modes: standard PWM, single SBUS, or dual SBUS; in Single SBUS mode,
  the active receiver (SBUS1 or SBUS2) is selectable from the RC page and persists across reboots
  without affecting channel mapping
- Failsafe automatically stops the motors if the RC signal is lost
- Emergency stop with latching behavior — requires a deliberate clear before drive resumes

### Web interface

- WiFi configuration: standalone access point or connection to an existing network
- Home dashboard: drive mode switcher, mood selector, and live status indicators
- Setup page: configure connected hardware components, RC receiver mode, and channel mapping
- Drive page: manual drive and dome control from the browser
- Dome page: direct dome motor control
- Servo page: arm and accessory servo control
- Sound page: trigger named sounds, configure track assignments, and set mood chatter rates
- RC diagnostics: live channel values, binding editor, and per-mode calibration
- Firmware and web UI updates directly from the browser
- Runtime log verbosity control without reflashing
- All settings persist across reboots

### Arms and servos

- Up to six servo channels supported: two main arms, three auxiliary outputs, one RGB indicator
- Open/close calibration per channel, configurable from the web interface
- Servo type selection (MG996R, MG90S, RGB, or none) per channel

### Audio

- Pluggable audio module support: DY-SV5W (confirmed on hardware) and CHIRP (implemented — hardware validation pending)
- Plays named sound cues: scream, Leia message, Short Circuit, Cantina, Imperial March, Star Wars theme, and more
- Sounds triggered from RC transmitter, web interface, or dome serial commands
- Random ambient chatter with per-mood frequency — each mood preset has its own chatter rate
- Volume configurable from the Sound page and persisted across reboots
- Sound module connection state and playback status visible on the Sound page

### Moods and sequences

- 15 mood and sequence presets selectable from the home dashboard or RC transmitter
- Mood selection triggers body sounds and, when the dome is connected, the corresponding dome lighting sequence
- Last active mood is restored on reboot

### Dome link

- Bidirectional serial communication with the dome controller over the slip ring
- Body sends a regular heartbeat to the dome; connection state shown on the dashboard
- Commands received from the dome (sounds, arm sequences) are dispatched to the body automatically
- Body-side communication confirmed; full end-to-end requires the dome board connected via slip ring

---

## Supported Hardware

protoArtoo supports two hardware targets using the same Artoo Controller PCB:

**Artoo Controller PCB v1.1/v1.2 — ESP32 D1 Mini** (default, fully validated)
- Build environments: `protoArtoo` (USB flash), `protoArtoo_ota` (OTA)
- All features supported

**WEMOS LOLIN S3 Mini** (drop-in alternative — optional upgrade)
- Build environments: `protoArtoo_s3` (USB flash), `protoArtoo_s3_ota` (OTA)
- No USB upload problem when seated in the PCB — GPIO 15 is not a strapping pin on S3
- 2 MB built-in PSRAM for additional web server headroom
- All RC modes, servos, audio, dome link, and web interface work identically
- One accepted limitation: AUX1 spare servo output (GPIO 19) is not available;
  all other servo channels are fully supported

---


## Known Limitations


- **Sound module status varies by backend:** Modules with bidirectional UART (DY-SV5W, CHIRP) report module state on the Sound page. DY-SV5W status is manually polled to avoid disrupting its RX state machine during playback. CHIRP status updates automatically every 2 seconds and is safe to query at any time.
---

## Pending Hardware Validation

The following features are implemented and software-verified but require a fully assembled
droid for final confirmation. They are planned for the Phase 5 hardware validation pass.

- **Drive and failsafe** — hoverboard response, RC failsafe, and speed limit; hoverboard
  is not currently connected
- **Dome motor** — RC-driven dome motor response; requires the full wiring harness connected
- **RC mapping with a physical transmitter** — channel mapping and calibration across all
  receiver modes; save/restore across reboots
- **Dome link end-to-end** — requires both the body board and dome board connected over
  the slip ring
- **Audio edge cases** — most audio paths confirmed on hardware; S2 enable/disable toggle
  and boot mood restore require hardware reconnect
- **Firmware and web UI update flows** — upload progress indication and post-reboot
  reconnect with the updated version

- **SBUS + hoverboard simultaneous operation** — RMT SBUS decoder replaces
  UART-based decoder; all SBUS modes can now run alongside hoverboard drive;
  requires hardware confirmation on a connected droid
- **Dual SBUS + dome link simultaneous operation** — SBUS2 moved off UART2;
  dome link and dual SBUS should now coexist; requires hardware confirmation

---

## Roadmap

- **Phase 4 (software complete):** audio system, full dome link, and web UI quality improvements;
  hardware validation deferred to Phase 5
- **Phase 5 (in progress):** hardware validation, final documentation, and initial public release as `v1.0.0`
