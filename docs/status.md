# Project Status

protoArtoo is open-source ESP32 body-controller firmware for MK4 astromech droids.
This page gives builders and operators a plain-language snapshot of what's ready
to use and what isn't, yet.

For setup instructions, see the project `README`. For a full history of changes,
see `CHANGELOG.md`.

## Current Snapshot

| Status item | Current state |
|---|---|
| Latest release | [`v1.0.0`](https://github.com/mattiasbrandt/protoArtoo/releases/latest) (2026-08-21) — ready-to-flash firmware and filesystem images per audio module |
| Web control | Working — pages load reliably, and a controller too busy to serve a page says so and offers a retry instead of hanging |
| Next up | Drive-motor validation on an assembled droid |

`v1.0.0` is the first stable release. Its capabilities are confirmed on real
hardware: audio, RC control, dome control, servos, web workflows, backups, and
firmware updates. The page-loading reliability problem that held the release
open is fixed — the web server was replaced for `v1.0.0`, and the fix was
confirmed on the controller including a deliberately induced low-memory
session.

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
  identity (custom `.local` name) confirmed in hardware validation. Page loads
  are reliable, and a controller under memory pressure answers with a plain
  "controller busy" page and a retry instead of leaving the browser hanging.
- **WiFi setup from the browser** — pointing the droid at a home network or
  keeping it on its own hotspot, switched from the WiFi page with a staged
  reboot, confirmed on the controller. Settings survive reboots and firmware
  updates.
- **Firmware and filesystem updates** — updating over WiFi and over USB both
  confirmed end-to-end.
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
- **First boot of a downloaded release on a fresh controller.** The
  boot-into-setup-hotspot flow is covered by automated tests and the release
  builds ship without any developer WiFi shortcut, but the literal "flash,
  join the hotspot, open the WiFi page" pass hasn't been run live yet.
- **Recovery from an interrupted update.** The firmware rejects an upload that
  delivers no usable image instead of rebooting into it; this guard is tested
  in software, but the failure itself hasn't been reproduced on hardware.
- **Long web sessions.** Web behavior is confirmed under bench load and induced
  memory pressure, but a dashboard left open for many hours hasn't been
  soak-tested.
- **Reworked RC input decision logic.** Mode changes, watchdog and
  receiver-failsafe transitions, and zero-frame behavior on signal loss were
  reworked for this release and are covered by automated tests; a hardware
  pass, including a signal-loss drill with a live receiver, is still to come.

## Release History

| Release | Key additions |
|---|---|
| `v0.1.0` | Hoverboard drive, RC receiver input, failsafe |
| `v0.2.0` | WiFi, web UI, OTA firmware updates |
| `v0.3.0` | Arm servos, dome motor, RC diagnostics and channel mapping |
| `v0.4.0` | Audio system, bidirectional dome link, web UI improvements |
| `v1.0.0` | First stable release: reliable page loads on a rebuilt web server, WiFi setup from the browser with a recovery mode, ready-to-flash release downloads per audio module, four log levels, and per-component enable toggles. Full drive-motor validation follows as a separate, documented pass. |

For detailed per-change history, see `CHANGELOG.md`.
