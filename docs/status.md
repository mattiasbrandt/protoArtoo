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
| Internal planning label | Phase 5 |
| Latest tagged release | `v0.4.0` |
| Main release focus now | integrated hardware validation and release hardening |

Public summary:

protoArtoo is currently being prepared for the `v1.0.0` release (internally tracked as Phase 5). Core architecture has passed automated checks, controller testing is in place where hardware is available, and drive hardware checks are still to be completed.

## Release Validation Matrix

| Area | Tested evidence | Still to verify |
|---|---|---|
| Drive command and safety logic | Automated checks are passing; drive frame generation and safety gating are in place. | Full motor hardware confirmation on the complete droid, including live wheel response. |
| Hoverboard motor integration | Pending full hardware validation. | Hoverboard controller, kill switch, and live drive behavior on the integrated droid. |
| RC decoding and diagnostics | Tested on an ESP32 controller; RC modes and diagnostics are implemented. | Remaining live receiver confirmation on hardware for the not-yet-complete areas. |
| RC-to-action dispatch and live controls | Tested on an ESP32 controller; action registry and mapping UI are implemented. | Live transmitter verification for all bindings on hardware. |
| Audio backend and control logic | Automated checks are passing; module control paths and catalog/mapping flows exist. | Audible playback on each supported sound module family. |
| Dome serial/control logic | Tested on an ESP32 controller. | Live serial confirmation with the dome controller. |
| Dome motion/ESC | Tested on an ESP32 controller. | Physical dome motor and ESC behavior on the dome assembly. |
| Dome-body link integration | Tested on an ESP32 controller. | Full slip-ring / integrated droid confirmation. |
| Servo command logic | Tested on an ESP32 controller. | Physical servo motion on installed servos. |
| Servo setup/persistence | Save/apply round-trips exist. | Reboot survival and physical actuation confirmation. |
| Network connectivity | WiFi connectivity and OTA paths exist. | End-to-end recovery and upload behavior on the target controller. |
| Web API/UI | Browser-accessible setup and control surfaces exist. | Live update and save/apply flows on deployed hardware. |
| Firmware/filesystem update flow | OTA and filesystem upload surfaces exist. | Full recovery after failed or partial upload on target hardware. |
| Drive failsafe | Safety logic is implemented and tested on an ESP32 controller. | Live motor reaction under drive hardware and RC loss. |
| Estop | Latching estop is implemented and tested on an ESP32 controller. | End-to-end confirmation with all actuators connected. |
| Watchdog recovery | Recovery behavior is implemented. | Live reset/reboot confirmation on the complete droid. |
| Boot safety defaults | SBUS-safe boot defaults and estop-on-reboot behavior are in place. | Full boot behavior with the drive hardware connected. |

## What Is Confirmed Working

- Core firmware architecture and task model are operational on the target controller platform.
- Web UI, API surfaces, and persisted configuration workflows are operational.
- RC modes, safety logic, and latching emergency-stop behavior are implemented and validated at controller level.
- Body-owned audio model is implemented, including modular backend support and operator workflows.
- Body-dome communication path is implemented; body-side handling is validated.

## What Is In Progress Toward v1.0.0

- End-to-end validation on a complete integrated droid build.
- Final drive-system confirmation with full motor hardware connected.
- Remaining hardware confirmation passes for optional/variant peripherals.
- Documentation and release packaging polish for public `v1.0.0` publication.

## Hardware Dependency Notes

- Drive hardware integration is the main open verification item for final system-level validation.
- Until that is closed, some checks remain limited to automated checks or ESP32 controller testing rather than full integrated confirmation.
- If final drive hardware is still unavailable at release time, the release notes will say the drive hardware checks are still to be completed and list the remaining checks.

## Release History

| Release | Key additions |
|---|---|
| `v0.1.0` | Hoverboard drive, RC receiver input, failsafe |
| `v0.2.0` | WiFi, web UI, OTA firmware updates |
| `v0.3.0` | Arm servos, dome motor, RC diagnostics and channel mapping |
| `v0.4.0` | Audio system, bidirectional dome link, web UI improvements |
| `v1.0.0` _(in progress)_ | Full integrated validation, release hardening, public release |

For detailed per-change history, see `CHANGELOG.md`.
