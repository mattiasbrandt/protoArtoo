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
| Web control status | Previously validated workflows remain available, but an ordinary page load can currently stall before controller data appears |

The main `v1.0.0` capabilities have been tested on real hardware, including
audio, RC control, dome control, servos, web workflows, backups, and firmware
updates. Release readiness remains open because ordinary controller web-page
loading is not yet reliable enough: a styled page can remain stuck on
**Loading** without filling in controller data.

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
- **Web control workflows** — setup, live control, backup/restore, and droid
  identity (custom `.local` name) have completed successfully in hardware
  validation. The ordinary-load reliability limitation below still applies.
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
- **Ordinary web-page load reliability.** A single page open can lose an early
  required script while the controller status endpoint remains reachable. The
  page then looks styled but stays on **Loading** with empty fields. A recovery
  bootstrap experiment on the AsyncWebServer stack showed this failure could be
  made worse by the admission guard and was rolled back. The current PsychicHttp
  stack (#75 migration) has not yet demonstrated this issue under the measurement
  scope defined in [ADR 0017](adr/0017-page-load-memory-recovery-acceptance-envelope.md).
  The underlying HTTP resource-delivery pressure on this stack remains under
  investigation in [the web recovery map](https://github.com/mattiasbrandt/protoArtoo/issues/52).

## Release History

| Release | Key additions |
|---|---|
| `v0.1.0` | Hoverboard drive, RC receiver input, failsafe |
| `v0.2.0` | WiFi, web UI, OTA firmware updates |
| `v0.3.0` | Arm servos, dome motor, RC diagnostics and channel mapping |
| `v0.4.0` | Audio system, bidirectional dome link, web UI improvements |
| `v1.0.0` | Full architecture, audio confirmed on hardware, OTA and backup confirmed, configurable droid identity. Full drive-motor validation follows as a separate, documented pass. |

For detailed per-change history, see `CHANGELOG.md`.
