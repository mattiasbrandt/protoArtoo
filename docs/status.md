# Project Status

protoArtoo is open-source ESP32 body controller firmware for MK4 astromech droids.
This page tracks the current development state across firmware phases.
For detailed release notes see `CHANGELOG.md`.

---

## Phase Overview

| Phase | Description | Status |
|---|---|---|
| Architecture & planning | Firmware design and hardware research | Complete |
| Dome firmware (AstroPixelsPlus fork) | Body-link protocol for the dome controller | Complete |
| Phase 1 — Drive | Hoverboard drive, RC receiver input, failsafe | Complete — `v0.1.0` |
| Phase 2 — Web interface | WiFi, web UI, OTA firmware updates | Complete |
| Phase 3 — Servos + dome motor | Arm servos, dome motor, RC diagnostics and mapping | Software complete — hardware validation partially deferred |
| **Phase 4 — Audio + dome link** | Sound playback, bidirectional dome communication, web UI improvements | **In progress** |
| Phase 5 — Community release | Documentation, polish, public release | Planned |

---

## Current Version

Latest release: `v0.1.0` — see `CHANGELOG.md` for full history.
Development builds are versioned from the git history and build timestamp.

---

## What Works

### Drive and safety

- Hoverboard drive with speed and steering control from an RC transmitter
- Configurable speed limit from the web interface
- Three RC receiver modes: standard PWM, single SBUS, or dual SBUS
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

- Pluggable audio module support: DY-SV5W (confirmed on hardware) and CHIRP (ready for hardware test)
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

## Known Limitations

- **Audio in dual-receiver + dome-link configurations:** Audio playback may be unreliable
  when dual SBUS receiver mode and dome serial link are both active simultaneously
  because these functions share UART resources.

- **S3 Dome Control and Dual SBUS are mutually exclusive:** The dome serial link and
  SBUS receiver #2 (GPIO 13) share UART2 and cannot be used simultaneously. When
  S3 Dome Control is enabled, Dual SBUS mode is unavailable for live use.

- **Hoverboard drive requires Standard PWM input:** The hoverboard drive serial port
  and SBUS RC receivers share UART1. Running hoverboard drive with SBUS receiver mode
  active simultaneously is not supported. Use Standard PWM for RC drive input when
  hoverboard drive is connected.

- **Single SBUS receiver selection:** In Single SBUS mode, either SBUS receiver input
  can be selected as the active source (SBUS1 on GPIO 15 or SBUS2 on GPIO 13). The
  selection is configurable from the RC page and persists across reboots. Changing
  the receiver selection does not affect channel mapping.

---

## Pending Hardware Validation

The following features are implemented and software-verified but require a fully assembled
droid for final confirmation:

- **Drive and failsafe** — hoverboard response, RC failsafe, and speed limit; the hoverboard
  is not currently connected to the test setup
- **Dome motor** — requires the full wiring harness connected
- **RC mapping with a physical transmitter** — channel mapping and calibration across all
  receiver modes; save/restore across reboots
- **Dome link end-to-end** — requires both the body board and dome board connected over
  the slip ring
- **Full audio validation** — most audio paths confirmed on hardware; the enable/disable
  toggle and boot mood restore require hardware reconnect
- **Firmware and web UI update flows** — upload progress indication and post-reboot
  reconnect with the updated version

---

## Roadmap

- **Phase 4 (active):** audio system, full dome link, and web UI quality improvements
- **Phase 5:** final hardware validation, documentation, and initial public release as `v1.0.0`
