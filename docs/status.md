# Project Status

> **What this file is for:**
> Single source of truth for the current development state of protoArtoo.
> Keep it current: update the phase table when phases transition, record
> confirmed hardware details as they are discovered, and note known gaps
> so contributors can orient quickly. Detailed task checklists live in
> `tasks/phase*-tasks.md`; detailed release notes live in `CHANGELOG.md`.
> Dependency versions should be re-audited at each phase boundary.

---

## Phase Overview

| Component | Status |
|---|---|
| Firmware plan & architecture | Complete |
| Dome fork (mattiasbrandt/AstroPixelsPlus) | Complete — body link protocol implemented |
| Phase 0 — PCB trace & hardware research | Complete |
| Phase 1 — Drive + SBUS + failsafe | Complete — released as `v0.1.0` |
| Phase 2 — Web server + OTA | Complete — bench-tested baseline established |
| Phase 3 — Servos + dome motor | **Bench-complete** — software bench-verified; hardware validation formally deferred; deferred items carried forward as T11/T12 in Phase 4 |
| **Web UI/UX quality gate** | **CLEARED** — signed off 2026-03-15. See `tasks/web_ui_quality_gate.md` |
| **Phase 4 — Audio + full dome link** | **In progress** — branch `phase/v0.4.0` active; all software tasks T01–T09, T13–T18 complete; T10 audio hardware partially validated (deterministic playback, named sounds, mood intervals confirmed on DY-SV5W); T11–T12 hardware-blocked; see Phase 4 section below |
| Phase 5 — Community release | Pending Phase 4 |

---

## Current Version

Released: `v0.1.0` — see `CHANGELOG.md` for full release history.

Development firmware on bench controller: version string is dynamically generated
from git tag + build timestamp (e.g. `v0.2.0-1-g23008b7-20260315-013914`).

**Bench controller hardware:**
- MCU: `ESP32-D0WD-V3` (revision 3)
- Crystal: 40 MHz
- Board: ESP32 D1 Mini

---

## Confirmed Capabilities

### Phase 2 (Web / OTA baseline)

- PlatformIO firmware flashing and LittleFS uploads working on bench setup
- AP + STA WiFi working; AP fallback clearly logged when STA is configured
- Home, Setup, WiFi, Firmware, and Serial pages served from LittleFS
- Core REST API implemented and bench-tested: `GET /api/status`, `/api/config`,
  `/api/wifi`, `/api/serial`, `/api/health`, `/api/logs`; `POST /api/estop`,
  `/api/estop/clear`, `/api/drive`, `/api/web-control/enable|disable`,
  `/api/manual-command`, `/api/mode`, `/api/reboot`, `/upload/firmware`
- API routes modularized: `api_estop`, `api_drive`, `api_config`, `api_status`,
  `api_system` — see `docs/api.md`
- NVS config persistence survives reboot
- Browser OTA path confirmed through API and web UI
- Dashboard includes: health indicators, movement status, live log console,
  manual command input, heap status, WiFi quality

### Phase 3 (Servos + dome motor + RC diagnostics)

- ServoTask and DomeTask running on controller firmware
- `/api/servo`, `/api/dome`, `/api/rc`, expanded `/api/config` and `/api/status`
  implemented and bench-tested
- Setup page: full 15-peripheral toggle set + RC Mapping/Diagnostics surface
  backed by `GET /api/rc`, SSE `event: rc`, `POST /api/config`
- `rc_input_mode` (`standard_pwm` / `single_sbus` / `dual_sbus`) in state, NVS,
  API, Setup UI, and runtime initialization branching
- Default channel map for `standard_pwm`: CH1 speed, CH2 steer, CH3 dome,
  CH4 ARM1, CH5 ARM2, CH6 sound trigger stub
- Servo component type system: `ServoComponentType` (NONE/MG996R/MG90S/RGB)
  per channel, NVS-backed, auto-defaults on type change
- AUX1/2/3 open/close calibration NVS-backed
- Home dashboard: Operation Mode card (Driving ↔ Stationary) and Mood Selector
  card (Quiet / Mid-Awake / Full-Awake / Awake+)
- Marcduino manual-command dispatch: `:OP`, `:CL`, `:SE`, `:MVxxdddd` implemented;
  `$`, `@`, `*`, `%`, `#`, `&`, `!` stubbed with truthful deferred handling
- Component command logging with scoped origins and source prefixes:
  `[SERVO] [WEB] Arm1 opened`, `[DOME] [SBUS] Dome command: speed 0.50`
- 330 native test cases passing: LEDC math, dome math, SBUS flags, SBUS unpacking,
  Marcduino helpers, servo helpers, PWM helpers, config payloads
- SBUS diagnostics confirmed live on hardware — dual SBUS receiving, RC page
  shows live channel values; arm servo confirmed working from SBUS controller
- RC mapper UX overhauled (Task 3.11): three-panel layout, binding summary table,
  focused live preview, per-slot editor; software-verified, hardware sign-off pending
- RC channel bindings and calibration are fully NVS-backed: all `rcp_*`, `rcs_*`,
  `rc_arm1/2`, `rc_aux1-3`, `rc_sound`, `rc_opmode`, `rc_free0-3` keys load and
  save correctly through `loadConfigToState()` / `saveConfigToNvs()`

### Phase 4 (Audio + Dome Link — v0.4.0, in progress)

- AudioDriver pluggable backend (`PA_AUDIO_DRIVER` build flag): `AUDIO_SOFT_UART`
  (DY-SV5W via 9600-baud soft UART on GPIO 26) and `AUDIO_CHIRP` (CHIRP board) both compile clean
- **DY-SV5W hardware-validated on real droid (2026-03-17):**
  - Deterministic track playback confirmed (play track N → same sound every time)
  - Volume control working and NVS-persisted (survives reboot)
  - Stop command functional
  - Driver aligned to DYPlayerArduino library + BetterDuino reference:
    checksum framing, opcodes `0x07`/`0x04`/`0x13`, `delay(100)` per command
  - Anti-spam guard: 300 ms minimum interval between play commands at AudioTask level
  - Sound page slider synced to persisted volume on page load
  - Named sound SD layout requirement confirmed: contiguous zero-padded `NNN.mp3`, no gaps;
    all named sounds (`$S`/`$F`/`$L`/`$c`/`$C`/`$W`/`$M`/`$B`) verified deterministic
  - HardwareSerial(2) remapped to S2 pins (GPIO 26 TX / GPIO 35 RX); shared with
    dome serial link — only one active at a time
- AudioTask queues all audio requests from RC, web API, and dome serial `$` commands
- `parseAudioDollar()` native-tested (26 test cases), mood dispatch native-tested
- Mood system: 15 presets, dual-path (body audio + dome serial `:SE01`–`:SE15`),
  boot restore, NVS-persisted (`last_mood`); `/api/mood` bench-tested
- DomeLinkTask: `#PAHB` 1 Hz heartbeat to dome bench-tested on GPIO 33/34
- `/api/audio` (play, stop, volume), `/api/mood`, `/api/status` dome_link block: bench-tested
- Dashboard: Mood Selector card, audio source indicators
- Static RAM: 20.6% (67,528 / 327,680 B); heapMin ~126 KB post-WiFi
- 435 native test cases passing (includes audio parser, mood dispatch, frame builder, per-mood interval NVS keys)
- Runtime log level: Setup page Diagnostics card, NVS-backed (`log_level`), 1=Error / 2=Info / 3=Debug
- **Per-mood random sound intervals (T18, bench-tested 2026-03-18):** Quiet=0 s / Mid-Awake=30 s /
  Full-Awake=20 s / Awake+=10 s; NVS-backed per-mood keys; configurable from Sound page Mood Intervals
  section; AudioTask reads `activeMood` on every timer tick
- **Audio driver name API:** `AudioDriver::driverName()` virtual; `audioGetDriverName()` accessor;
  `s2Sound.driver` field in `/api/status`; Setup page S2 row shows `DY-SV5W` or `CHIRP` label.
  Foundation for CHIRP-specific sound page features
- **Named `$` command audit (2026-03-18):** cross-referenced MarcDuino, BetterDuino, Reeltwo,
  SHADOW_MD, Padawan360 — all `$` commands and `:SE` sequences accounted for; `$D` (Disco)
  intentionally omitted (non-Star Wars; documented in `docs/sound_playback.md`)
- **Serial page removed:** absorbed into Setup — Serial Status card with live S1/S2/S3 state,
  GPIO/baud reference, auto-refresh every 5 s; `serial.html`/`serial.js` deleted
- **Web UI polish (2026-03-18):** double-emoji feedback bug fixed across sound.js/servo.js/setup.js;
  nav active button restored with compact sizing; Setup Hardware Components reflowed to one-per-row;
  Setup S2 row shows active driver name label
- Stack HWM bench-measured (first-iteration): DriveTask 2520 B / 4096, SBUSInput 2360 B / 4096,
  DomeTask 3332 B / 2048, ServoTask 4392 B / 3072, AudioTask 2680 B / 3072,
  SafetyMonitor was 476 B / 2048 → stack bumped to 3072, WebEvents 3804 B / 4096 → trimmed to 2048
- OTA via HTTP POST fixed: `UPDATE_SIZE_UNKNOWN` in `Update.begin()` prevents multipart boundary
  size mismatch that was causing silent rollback to old firmware on every upload
- All raw `Serial.printf` in `safety.cpp`, `drive.cpp`, `sbus_input.cpp` replaced with `PA_LOG_*`

---

## Open Items (Phase 3 hardware carryover)

Full deferral record: `tasks/phase3_hardware_validation_deferral.md`
Tracked as T11 and T12 in `tasks/phase4-tasks.md`.

- SBUS Layer 1 / Layer 2 failsafe — deliberate disconnect/timeout test pending
- Full hoverboard drive path — hoverboard disconnected during Phase 3
- Dome ESC response — full wiring harness not connected
- RC mapping with real transmitter/receiver — standard PWM and dual SBUS physical validation
- Upload UX verification — firmware/filesystem upload flow confirmation

---

## Phase 4 Progress (v0.4.0)

| Task | Description | Status |
|---|---|---|
| T00 | Phase branch setup, workflow alignment | Complete |
| T01 | AudioDriver interface + soft UART backend | ✅ **bench-tested** — DY-SV5W playback verified on hardware (DYPlayer-aligned driver) |
| T02 | AudioTask, dollar parser, queue helpers | ✅ **bench-tested** — queue wiring, API routing, random playback, enable/disable; audio hardware output requires T10 |
| T03/T04/T05 | DomeLinkTask — UART2 TX/RX, `#PAHB` heartbeat, Marcduino dispatch | `#PAHB` TX + `#APHB` RX intercept: **compile-only** (dome board not connected); UART2 wiring not validated |
| T06 | Status API + dashboard — `dome_link` block, audio health indicators | ✅ **bench-tested** |
| T07 | `/api/audio`, `/api/audio/tracks`, Marcduino routing | ✅ **bench-tested** |
| T08 | Mood dual-path — `/api/mood`, boot restore, dome RX intercept | ✅ **bench-tested** (audio path); dome TX path requires T10 hardware |
| T09 | Parser/track mapping tests + hardware validation plan | ✅ **native-tested** (435 tests); `$` command audit complete; `$D` intentionally omitted |
| T10 | Full hardware validation | **partially validated** — DY-SV5W: deterministic playback, volume, stop, all named sounds, mood chatter confirmed on hardware; S2 toggle enable/disable + boot mood restore pending hardware reconnect; dome link pending full hardware |
| T11–T12 | Phase 3 carryover (SBUS failsafe, drive path, RC physical) | Pending hardware availability |
| T13 | Reconcile NVS remapping claim | ✅ **closed** — stale doc; all RC bindings are NVS-backed (full load/save in main.cpp) |
| T14 | RC detect-channel mode (was: learning mode) | ✅ **bench-tested** (UI, SSE raw channel arrays, detect logic 25 tests); physical RC button press pending T11/T12 hardware |
| T15 | CHIRP Audio Trigger backend (`AUDIO_CHIRP`) | ✅ **compile-only** — driver implemented; `pio run -e protoArtoo_chirp_check` passes |
| T16 | Sound page (`sound.html`/`sound.js`) + `/api/audio/tracks` | ✅ **bench-tested** — page renders, named tracks NVS roundtrip, volume/stop/play API; audio output requires T10 |
| T17 | Runtime log level selector on Setup page | ✅ **bench-tested** — dropdown renders, POST works, NVS-persisted; OTA bug fixed (UPDATE_SIZE_UNKNOWN) |
| T18 | Per-mood random sound intervals | ✅ **bench-tested** — API, NVS, Sound page UI; audible frequency change per mood requires T10 hardware |

**Verification terminology used in this project:**
- `native-tested` — covered by `pio test -e native` passing
- `compile-only` — builds clean; no functional verification performed
- `bench-tested` — observed working on bare ESP32 over USB/WiFi (no Artoo PCB required)
- `full-hardware-required` — needs Artoo PCB + peripherals connected and powered

---

## Planning Notes

- Hardware validation must explicitly distinguish bench-stage (ESP32 + USB/WiFi only)
  from full hardware (Artoo PCB + receivers + hoverboard + actuators wired).
- Network authentication and hardening are intentionally deferred while the droid
  operates on a closed home LAN.
- **Phase 4 workflow:** branch `phase/v0.4.0`; commit scope
  `type(phase:v0.4.0/T<NN>): summary`. Canonical reference:
  `tasks/dev-workflow-change-spec.md` (Status: Reviewed — Decided).

---

## Dependency State

Last audited: 2026-03-14

| Dependency | Version | Notes |
|---|---|---|
| `espressif32` | `6.13.0` | Arduino core 2.0.17 / IDF 5.5.3 / esptool 4.11.0 |
| `ESP32Async/ESPAsyncWebServer` | `3.6.0` | Canonical namespace (was `me-no-dev/`) |
| `ESP32Async/AsyncTCP` | `3.3.2` | Canonical namespace (was `me-no-dev/`) |
| `bblanchon/ArduinoJson` | `7.4.3` | Pinned |

All `lib_deps` explicitly pinned. `ESP32Servo` removed — never imported; servo
PWM runs through native LEDC channels.
