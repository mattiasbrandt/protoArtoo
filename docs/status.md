# Project Status

protoArtoo is open-source ESP32 body controller firmware for MK4 astromech droids.
This page covers features, supported hardware, and known limitations.
For a full change history, see `CHANGELOG.md`.

---

## Build

Run `make help` for a full list of build, flash, and OTA targets.
For first-time setup:
- `make setup` writes a local `user.mk` with OTA IP, upload port, and audio backend choice.
- `make setup-wifi` writes `src/secrets.h` with WiFi credentials.

---

## Release History

| Release | Key additions |
|---------|---------------|
| `v0.1.0` | Hoverboard drive, RC receiver input, failsafe |
| `v0.2.0` | WiFi, web UI, OTA firmware updates |
| `v0.3.0` | Arm servos, dome motor, RC diagnostics and channel mapping |
| `v0.4.0` | Audio system (DY-SV5W, CHIRP, MP3 Trigger), bidirectional dome link, web UI improvements |
| `v1.0.0` _(upcoming)_ | AUX LED strip, sound categories, system event sounds, hardware validation on full droid build, community release |

---

## Current Version

Latest release: `v0.4.0` — see `CHANGELOG.md` for full history.

---

## Features

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
- Sleep/wake mode that pauses cosmetic subsystems (sound chatter, dome animations, servo idle) while keeping RC, drive, and web control fully active; body and dome automatically stay in sync when either side initiates a sleep or wake
- Drive page: manual drive and dome control from the browser
- Dome page: direct dome motor control
- Servo page: arm and accessory servo control
- Sound page: trigger over 20 named sounds, configure sound categories with per-category track ranges, assign tracks to system events, and set per-mood chatter rates
- RC diagnostics: live channel values, binding editor, and per-mode calibration
- Firmware and web UI updates directly from the browser
- Runtime log verbosity control without reflashing
- All settings persist across reboots

### Arms and servos

- Up to five servo channels: two main arms (ARM1/ARM2) and three auxiliary outputs (AUX1/AUX2/AUX3)
- Open/close calibration per channel, configurable from the web interface
- Servo type selection (MG996R, MG90S, or none) per channel

### AUX LED strip

- One WS2812B LED strip can be connected to a selectable AUX header (AUX1, AUX2, or AUX3)
- Color (RGB) and effect (solid, blink, pulse, off) controllable from the web interface
- Header selection and LED count configured from the Setup page and persisted across reboots
- The selected AUX header is reserved for the LED strip; remaining AUX headers remain servo outputs

### Audio

- Pluggable audio module support: DY-SV5W (confirmed on hardware), CHIRP, and SparkFun MP3 Trigger (both implemented; hardware confirmation pending)
- Over 20 named sound cues: scream, Leia message, Short Circuit, Cantina, Imperial March, Star Wars theme, Disco, Macho Man, Gangnam Style, and more
- 12 sound categories (General, Chatty, Happy, Processing, Sad, Sentimental, Humming, Scream, Surprised, Alert, Snarky, Whistle) — each with a configurable track range and an RC-bindable "play random from this category" action
- System event sounds: configurable tracks for boot, mode changes (Normal/Slow/Turbo), drives engaged, and dome link connected — all opt-in with a default of silent
- Sounds triggered from RC transmitter, web interface, or dome serial commands
- Random ambient chatter with per-mood frequency — each mood preset has its own chatter rate
- Volume configurable from the Sound page and persisted across reboots
- Sound module connection state and playback status visible on the Sound page
- CHIRP Sound Catalog on the Sound page can refresh the module catalog, browse bank/page/track names, map sounds to Named and System slots, bulk-map checked sounds into a category range, apply directory-based mapping suggestions, and play test sounds directly from the catalog
- CHIRP mapping is optional and non-destructive: if no CHIRP mapping is set for a slot or category, protoArtoo continues to use the standard numbered track settings

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

protoArtoo targets a single hardware platform:

**Artoo Controller PCB v1.1/v1.2 — ESP32 D1 Mini clone** (`wemos_d1_mini32`)
- Build environments: `protoArtoo` (USB flash), `protoArtoo_ota` (OTA)
- All features supported
- The Artoo PCB is purpose-built for the dual-header ESP32 D1 Mini clone; no other ESP32 board fits the PCB socket

---

## Known Limitations

- **Sound module status varies by backend:** Modules with bidirectional UART (DY-SV5W, CHIRP) report module state on the Sound page. DY-SV5W status is manually polled to avoid disrupting playback. CHIRP status auto-polls every 10 seconds and is safe to query while playing.
- **CHIRP catalog refresh takes time:** Large SD-card catalogs can take around a minute to scan. While a refresh is running, catalog actions are temporarily paused to avoid conflicting requests.
- **Bank 1 General is configured by range:** The catalog focuses on browsable SD pages. Bank 1 General sounds are set using the General category track range, with a hint shown on the Sound page for the detected count.
---

## Not Yet Confirmed on Full Hardware

The following features are implemented and work in bench testing but have not been
exercised on a complete droid build. If you encounter unexpected behavior with any
of these, please open an issue.

- **Drive and failsafe** — hoverboard motor response, RC failsafe, and speed limit cap; requires the hoverboard wired to the S1 header
- **Dome motor** — RC-driven dome rotation; requires the dome ESC and motor wired to the DOME header
- **RC mapping with a physical transmitter** — channel mapping, calibration, and NVS persistence across receiver modes; requires a transmitter and receiver connected
- **Dome link end-to-end** — requires both the Artoo PCB and AstroPixelsPlus dome board connected via the slip ring
- **Audio: S2 enable/disable and boot mood restore** — most audio paths are confirmed; the S2 hardware toggle behavior and boot-time mood restore require a reconnected audio module to re-verify
- **CHIRP and MP3 Trigger audio backends** — implemented and bench-compiled; require the respective board wired to the S2 header for full confirmation
- **CHIRP catalog-assisted mapping + banked playback across Bank 2+** — API/UI paths compile and bench-run; audible cross-bank validation requires CHIRP hardware with populated multi-bank media
- **Sound categories and system event sounds** — track ranges, RC triggers, and system hooks are implemented; audible playback per category and event requires hardware with a loaded SD card
- **Firmware and web UI OTA update flow** — upload progress and post-reboot reconnect; requires a live device on the network
- **AUX LED strip** — color and effect control are implemented; actual LED behavior requires a WS2812B strip connected to an AUX header
- **SBUS + hoverboard simultaneous operation** — all SBUS receiver modes can run alongside hoverboard drive; requires a connected droid to confirm during real operation
- **Dual SBUS + dome link simultaneous operation** — dual SBUS and dome link can now run at the same time; requires a connected droid to confirm

---

## Roadmap

- **v1.0.0 (in progress):** end-to-end hardware validation on a complete droid build, documentation polish, and public community release
