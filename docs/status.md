# Project Status

protoArtoo is open-source ESP32 body-controller firmware for MK4 astromech droids.
This page gives builders and operators a plain-language snapshot of what's ready
to use and what isn't, yet.

For setup instructions, see the project `README`. For a full history of changes,
see `CHANGELOG.md`.

## Current Snapshot

| Status item | Current state |
|---|---|
| Active release target | `v1.0.0` |
| Latest tagged release | `v0.4.0` |

protoArtoo is feature-complete for `v1.0.0`. Everything a builder does day to day
— audio, RC control, dome control, servos, the web control panel, backups, and
firmware updates — has been tested and confirmed working on real hardware.

Full drive-motor (hoverboard) validation on a completely assembled droid is not
part of this release. Drive control and safety logic are implemented and tested,
but haven't yet been confirmed with a hoverboard actually driving wheels on a
finished build. This is tracked as follow-up work after `v1.0.0`, not a blocker
for it.

## What's Confirmed Working

- **Audio** — sound playback, named sounds, random chatter, boot sounds, and
  sleep behavior confirmed on real hardware with the CHIRP audio module.
  The DY-SV5W audio module has also been confirmed on hardware for playback
  and volume control.
- **RC control** — transmitter and receiver setup, channel mapping, and
  RC-triggered actions (sounds, dome moves, etc.) confirmed on real hardware.
- **Dome** — dome rotation, panel sequences, lights, and body-to-dome
  communication confirmed on real hardware.
- **Servos** — arm and other servo movement confirmed on real hardware.
- **Web control panel** — setup, live control, backup/restore, and droid
  identity (custom `.local` name) confirmed working.
- **Firmware and filesystem updates** — updating over WiFi and over USB both
  confirmed end-to-end, including recovering cleanly from a failed update.
- **Safety systems** — emergency stop and RC-signal-loss failsafe confirmed,
  outside of live drive-motor behavior (see below).

## What's Not Yet Verified

- **Drive-motor (hoverboard) behavior on a complete droid.** Drive control and
  safety logic exist and have been tested, but live wheel response, drive
  failsafe with motors connected, and kill-switch behavior have not yet been
  confirmed on an assembled droid with a hoverboard installed. This is planned
  as follow-up work after `v1.0.0` and will be documented when complete.
- **MP3 Trigger audio module** — an alternative to the CHIRP module some
  builders use. Not re-confirmed on hardware for this release.

## Release History

| Release | Key additions |
|---|---|
| `v0.1.0` | Hoverboard drive, RC receiver input, failsafe |
| `v0.2.0` | WiFi, web UI, OTA firmware updates |
| `v0.3.0` | Arm servos, dome motor, RC diagnostics and channel mapping |
| `v0.4.0` | Audio system, bidirectional dome link, web UI improvements |
| `v1.0.0` | Full architecture, audio confirmed on hardware, OTA and backup confirmed, configurable droid identity. Full drive-motor validation follows as a separate, documented pass. |

For detailed per-change history, see `CHANGELOG.md`.
