# Project Status

protoArtoo is open-source ESP32 body-controller firmware for MK4 astromech droids.
This page gives builders and operators a plain-language snapshot of what's ready
to use and what isn't, yet.

For setup instructions, see the project `README`. For a full history of changes,
see `CHANGELOG.md`.

## Current Snapshot

| Status item | Current state |
|---|---|
| Latest release | [`v1.1.0`](https://github.com/mattiasbrandt/protoArtoo/releases/latest) (2026-09-06) — ready-to-flash firmware and filesystem images per audio module, plus a FireBeetle 2 image |
| Controller boards | artoo-esp32 (build a droid on this one); DFRobot FireBeetle 2 ESP32-P4, supported for developers, not yet a buy recommendation |
| Web control | Working — pages load reliably, and a controller too busy to serve a page says so and offers a retry instead of hanging |
| Next up | Drive-motor validation on an assembled droid |

`v1.0.0` is the first stable release. Its capabilities are confirmed on real
hardware: audio, RC control, dome control, servos, web workflows, backups, and
firmware updates. The page-loading reliability problem that held the release
open is fixed — the web server was replaced for `v1.0.0`, and the fix was
confirmed on the controller including a deliberately induced low-memory
session.

`v1.1.0` adds a second controller board. The full feature set builds for the
DFRobot FireBeetle 2 ESP32-P4 and is confirmed on the board over USB, and the
release ships an image for it. It is supported for developers; it is not yet a
board to buy for a droid, for the reasons in the
[spec sheet](spec-sheets/firebeetle2-esp32-p4-spec-sheet.md#before-you-buy-one).
The artoo-esp32 image behaves as it did in `v1.0.0`.

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
- **Long web sessions** — a three-hour live-update soak and a reconnect storm
  on the FireBeetle 2, and four and a half hours under web load on the
  artoo-esp32, without a reboot or a refused page. The artoo-esp32 runs used a
  build with Bluetooth compiled out of the framework, which is not the release
  configuration; the release configuration passes the same build, size and
  test checks but has not had a soak of its own.

## FireBeetle 2 ESP32-P4

A second controller board, supported for developers.

**Confirmed on the board (over USB, no droid attached):**

- Web control, live status and the dashboard over the board's ESP32-C6 WiFi
  module, including a three-hour live-update soak and a reconnect storm.
- Firmware updates over WiFi, end to end.
- The safety posture: 50 Hz drive frames keep coming, the RC-loss failsafe is
  armed from boot, a watchdog reset arms the emergency stop, and the task
  watchdog is fed.
- A deliberate reset of the WiFi module recovers without restarting the
  controller.
- `firebeetle2.local` next to an artoo controller's `artoo.local` on one
  network.

**Not yet checked:**

- Anything that needs a signal on a pin: RC receiver input, the drive and dome
  serial lanes, servo pulses, I2C and the LED strip. The board has not driven a
  droid.
- The WiFi module's "degraded" announcement (sound cue, dome text, serial log)
  has never been seen firing; it needs a failure the shipping image cannot
  provoke on purpose.
- GPIO 48 to 52 sit on regulator rails that are unmeasured under load; a
  problem there would look like servo jitter or dome ESC throttle, not a log
  line (`docs/pin_map.md`, Known Issue).
- Updating the WiFi module's own firmware: a factory module refuses the
  wire-free route and the wired procedure is marginal (spec sheet, Known
  Issues).
- Moving a droid's saved settings from an artoo-esp32 to a FireBeetle 2.

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
| `v1.1.0` | Second controller board: the DFRobot FireBeetle 2 ESP32-P4 for developers, with WiFi through its ESP32-C6 module and a release image of its own; component switches named for what they control; memory sized per chip; build-size budgets on every pull request; a soak harness. artoo-esp32 behaviour unchanged. |

For detailed per-change history, see `CHANGELOG.md`.
