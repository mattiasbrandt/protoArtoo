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
| Phase 4 — Audio + full dome link | **In progress** — branch `phase/v0.4.0` active; T01/T02 compile-only (no bench run yet); see Phase 4 section below |
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
- AudioTask queues all audio requests from RC, web API, and dome serial `$` commands
- `parseAudioDollar()` native-tested (26 test cases), mood dispatch native-tested
- Mood system: 15 presets, dual-path (body audio + dome serial `:SE01`–`:SE15`),
  boot restore, NVS-persisted (`last_mood`); `/api/mood` bench-tested
- DomeLinkTask: `#PAHB` 1 Hz heartbeat to dome bench-tested on GPIO 33/34
- `/api/audio` (play, stop, volume), `/api/mood`, `/api/status` dome_link block: bench-tested
- Dashboard: Mood Selector card, audio source indicators
- Static RAM: 20.0% (65,408 / 327,680 B); heapMin ~135 KB post-WiFi (up from ~107 KB)
- 431 native test cases passing (includes audio parser, mood dispatch, frame builder)
- Runtime log level: Setup page Diagnostics card, NVS-backed (`log_level`), 1=Error / 2=Info / 3=Debug
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
| T01 | AudioDriver interface + soft UART backend | **compile-only** — no functional verification |
| T02 | AudioTask, dollar parser, queue helpers | `parseAudioDollar()`: **native-tested** (26 tests); AudioTask / queue helpers / marcduino_rx routing: **compile-only** |
| T03/T04/T05 | DomeLinkTask — UART2 TX/RX, `#PAHB` heartbeat, Marcduino dispatch | `#PAHB` 1 Hz: **bench-tested**; RX/dome connection: **compile-only** (dome not connected) |
| T06 | Status API + dashboard — `dome_link` block, audio health indicators | ✅ **bench-tested** |
| T07 | `/api/audio`, Marcduino routing in manual-command, serial.js fix | ✅ **bench-tested** |
| T08 | Mood dual-path — `/api/mood`, boot restore, dome RX intercept | ✅ **bench-tested** |
| T09 | Parser/track mapping tests + hardware validation plan | **native-tested** (401 tests); hardware checklist in tasks/phase4-tasks.md |
| T10 | Full hardware validation | **formally deferred** — audio module and dome board not connected; checklist recorded |
| T11–T12 | Phase 3 carryover (SBUS failsafe, drive path, RC physical) | Pending hardware availability |
| T13 | Reconcile NVS remapping claim | ✅ **closed** — stale doc; all RC bindings are NVS-backed (full load/save in main.cpp) |
| T14 | RC learning mode re-evaluation | Low priority, post-T11/T12 |
| T15 | CHIRP Audio Trigger backend (`AUDIO_CHIRP`) | ✅ **compile-only** — driver implemented; `pio run -e protoArtoo_chirp_check` passes |
| T16 | Static RAM reduction + heap improvement | ✅ **bench-tested** — BSS −11.5 KB; heapMin +28 KB (107 KB → 135 KB measured) |
| T17 | Runtime log level selector on Setup page | ✅ **bench-tested** — dropdown renders, POST works, NVS-persisted; OTA bug fixed (UPDATE_SIZE_UNKNOWN) |

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
