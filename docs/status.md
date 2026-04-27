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

protoArtoo is currently being prepared for the `v1.0.0` release (internally tracked as Phase 5). Core architecture and initial hardware goals are already validated and working. Final release validation is now focused on full integrated hardware checks, with drive-motor hardware availability still a key dependency.

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

- The main schedule-sensitive dependency is complete drive hardware integration for final system-level validation.
- Until that is closed, some checks remain controller-level or bench-level rather than full integrated confirmation.

## Release History

| Release | Key additions |
|---|---|
| `v0.1.0` | Hoverboard drive, RC receiver input, failsafe |
| `v0.2.0` | WiFi, web UI, OTA firmware updates |
| `v0.3.0` | Arm servos, dome motor, RC diagnostics and channel mapping |
| `v0.4.0` | Audio system, bidirectional dome link, web UI improvements |
| `v1.0.0` _(in progress)_ | Full integrated validation, release hardening, public release |

For detailed per-change history, see `CHANGELOG.md`.
