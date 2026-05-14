# Project Status

protoArtoo is open-source ESP32 body-controller firmware for MK4 astromech droids.
This page is a high-level status snapshot.

For technical target state and architecture direction, see `docs/goal.md`.
For API and subsystem details, see `docs/api.md`, `docs/failsafe.md`,
`docs/commands.md`, and `docs/sound_playback.md`.

## Current Snapshot

| Status item | Current state |
|---|---|
| Active release target | `v1.0.0` |
| Latest tagged release | `v0.4.0` |
| Main release focus now | drive hardware validation before the `v1.0.0` release |

Public summary:

protoArtoo firmware is feature-complete for `v1.0.0`. Automated checks are passing, controller-level testing covers all areas reachable without drive motors, and audio hardware confirmation is complete. The remaining item before the `v1.0.0` release is drive hardware validation, which requires the hoverboard to be connected.

## Release Validation Matrix

| Area | Tested evidence | Still to verify |
|---|---|---|
| Drive command and safety logic | Automated checks are passing; drive frame generation and safety gating are in place. | Full motor hardware confirmation on the complete droid, including live wheel response. |
| Hoverboard motor integration | Pending full hardware validation. | Hoverboard controller, kill switch, and live drive behavior on the integrated droid. |
| RC decoding and diagnostics | Tested on an ESP32 controller; RC modes and diagnostics are implemented. | Remaining live receiver confirmation on hardware for the not-yet-complete areas. |
| RC-to-action dispatch and live controls | Tested on an ESP32 controller; RC-mapped sound triggers confirmed on hardware. | RC-to-drive binding confirmation once drive hardware is available. |
| Audio backend and control logic | Audible playback confirmed on hardware for CHIRP module: named sounds, random chatter, boot sounds, and sleep suppression all tested. DY-SV5W has prior hardware evidence for playback and volume. | Audible confirmation for MP3 Trigger module (community hardware). |
| Dome serial/control logic | Tested on an ESP32 controller. | Live serial confirmation with the dome controller. |
| Dome motion/ESC | Tested on an ESP32 controller. | Physical dome motor and ESC behavior on the dome assembly. |
| Dome-body link integration | Tested on an ESP32 controller. | Full slip-ring / integrated droid confirmation. |
| Servo command logic | Tested on an ESP32 controller. | Physical servo motion on installed servos. |
| Servo setup/persistence | Save/apply round-trips exist. | Reboot survival and physical actuation confirmation. |
| Network connectivity | WiFi connectivity, OTA paths, and configurable droid hostname confirmed on an ESP32 controller. | End-to-end recovery after failed or partial upload on target hardware. |
| Web API/UI | Browser UI, setup and control pages, backup and restore, and droid identity all confirmed on an ESP32 controller. | Live update and save/apply flows for any remaining pages on deployed hardware. |
| Firmware/filesystem update flow | Browser firmware upload, CLI OTA upload, and filesystem OTA upload confirmed end-to-end on an ESP32 controller. | Recovery after failed or partial upload on target hardware. |
| Drive failsafe | Safety logic is implemented and tested on an ESP32 controller. | Live motor reaction under drive hardware and RC loss. |
| Estop | Latching estop is implemented and tested on an ESP32 controller. | End-to-end confirmation with all actuators connected. |
| Watchdog recovery | Recovery behavior is implemented. | Live reset/reboot confirmation on the complete droid. |
| Boot safety defaults | SBUS-safe boot defaults and estop-on-reboot behavior are in place. | Full boot behavior with the drive hardware connected. |

## What Is Confirmed Working

- Core firmware architecture and task model are operational on the target controller platform.
- Web UI, API surfaces, and persisted configuration workflows are operational.
- Firmware and filesystem OTA updates confirmed end-to-end from both browser and CLI.
- Backup and restore confirmed end-to-end for all configuration sections.
- Droid identity can be configured from the Setup page; lowercase names persist through reboot and can be used as the `.local` hostname.
- RC modes, safety logic, and latching emergency-stop behavior are implemented and validated at controller level.
- RC-mapped sound triggers confirmed on hardware.
- Body-owned audio model is implemented; CHIRP module audible playback confirmed on hardware including named sounds, random chatter, boot sounds, and sleep suppression.
- Body-dome communication path is implemented; body-side handling is validated.

## What Is In Progress Toward v1.0.0

- Drive hardware validation: hoverboard motor integration, live failsafe, and safety confirmation with motors connected. This is the remaining item before the `v1.0.0` release.

## Hardware Dependency Notes

- The firmware is feature-complete. All areas reachable without drive motors have been confirmed through automated checks or ESP32 controller testing.
- Drive hardware integration is the only remaining item before the `v1.0.0` release. The release notes will describe what drive hardware checks were completed and any that remain open at that point.

## Release History

| Release | Key additions |
|---|---|
| `v0.1.0` | Hoverboard drive, RC receiver input, failsafe |
| `v0.2.0` | WiFi, web UI, OTA firmware updates |
| `v0.3.0` | Arm servos, dome motor, RC diagnostics and channel mapping |
| `v0.4.0` | Audio system, bidirectional dome link, web UI improvements |
| `v1.0.0` _(pending drive hardware validation)_ | Full architecture, audio confirmed on hardware, OTA and backup confirmed, configurable droid identity, drive hardware validation pending |

For detailed per-change history, see `CHANGELOG.md`.
