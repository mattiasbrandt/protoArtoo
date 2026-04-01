# Phase 5 - Community Release (v1.0.0)

Status: In progress — branch `phase/v1.0.0`
Baseline: Earlier phases should already have converged on one coherent web/config/status/dashboard architecture
Goal: Production-ready firmware for community use
Milestone: Community release - all tests passing, docs complete, no known safety issues

## Execution Stages (Standard Planning Model)

1. Bench Development Stage
- Close software, docs, UI, and packaging issues that can be validated on bench setups.
- Keep release artifacts and verification records current while hardware validation is pending.

2. Full Hardware Validation Stage
- Validate integrated RC, drive, dome, servo, audio, and body-link behavior on full hardware.
- Confirm end-to-end safety invariants on the complete droid build.

3. Deferred Hardware Validation (Allowed)
- If required hardware is unavailable, bench closure may continue, but missing physical
  validation must be tracked as deferred with blockers and closure plan.
- Deferred full-hardware checks must be closed before a final stable release claim.

## Starting Point from Earlier Phases

- Hardware ground truth remains `docs/pin_map.md` and `include/config.h`
- Phase 2 established the web/config/OTA/dashboard baseline
- Phase 3 and 4 should extend that baseline, not fork it
- Release readiness must distinguish clearly between:
  - controller behavior proven at the bench stage
  - full hardware behavior proven on the complete Artoo build

## Workflow (Phase v1.0.0)

Phase 5 follows the same workflow model as Phase 4, as defined in
`tasks/dev-workflow-change-spec.md`.

- Use dedicated phase integration branch: `phase/v1.0.0`
- Commit scope format: `type(phase:v1.0.0/T<NN>): summary`
  - Example: `feat(phase:v1.0.0/T01): complete final safety validation pass`
- Keep thin-slice delivery with verification evidence per slice
- Merge to `main` (non-fast-forward) only at phase wrap-up once all exit criteria are met; tag `v1.0.0`, publish GitHub release
- Agent instruction files remain governed by `tasks/dev-workflow-change-spec.md`

## Phase 5 Focus

- [ ] Complete remaining operator-facing UX polish without reintroducing internal planning language into pages
- [ ] Keep final release surfaces unified; do not create parallel setup/config/debug flows
- [ ] Finish documentation and release notes across all public docs — T02/T03 docs updated; full release notes deferred
- [ ] Complete final safety validation across the whole integrated hardware stack — deferred, hardware unavailable
- [ ] Close known regressions and operational rough edges found during Phases 2-4
- [x] Prepare stable release packaging and community-facing release guidance — T03 (Makefile + wizard) done

---

## Phase 5 Carried-In Technical Debt (from Phase 4 research)

### T01 — UART congestion audit and resolution (classic ESP32)

**T01a — RMT SBUS decoder: COMPLETE (bench-tested)** — committed `0a1f8c4`, `9855544` on 2026-03-30.
**T01b — Hoverboard RX status read-back: DEFERRED** — depends on T01a hardware validation.
Classic ESP32 (wemos_d1_mini32) remains the primary target. S3 Mini is optional upgrade.

---

#### Full UART resource map (wemos_d1_mini32 / classic ESP32)

The classic ESP32 has 3 hardware UARTs. This is the complete picture of who owns what,
at what baud rate, and in which direction:

| Peripheral | UART | GPIO | Baud | Format | Direction | NVS enable key |
|---|---|---|---|---|---|---|
| USB debug | UART0 | 1 TX / 3 RX | 115200 | 8N1 | TX-only at runtime | always-on |
| Hoverboard | UART1 | 16 TX / 17 RX | 115200 | 8N1 | TX-only today (RX wired, never read — **to be used post-T01**) | `en_s1_hoverboard` |
| SBUS1 drive RX | UART1 | 15 RX | 100000 | 8E2 inv | RX-only | `rc_mode` + `en_rc_ch1` |
| Dome link | UART2 | 33 TX / 34 RX | 9600 | 8N1 | Full-duplex | `en_s3_dome_ctrl` |
| Audio status RX | UART2 | 35 RX | 9600 | 8N1 | RX-only (queries only) | `en_s2_sound` |
| SBUS2 dome RX | UART2 | 13 RX | 100000 | 8E2 inv | RX-only | `rc_mode` + `en_rc_ch2` |
| Audio cmd TX | soft-UART | 26 TX | 9600 | 8N1 | TX-only (bit-bang) | `en_s2_sound` |

**Key findings from the audit:**

1. **Hoverboard is TX-only.** GPIO17 (`PIN_HOVERBOARD_RX`) is wired on PCB S1 but the
   firmware never calls `Serial1.read()`. `hoverboard_uart.cpp` is pure frame-builder
   logic with no read path. The RX capacity on UART1 is unused.

2. **Audio TX is already soft-UART.** All three audio drivers (DY-SV5W, CHIRP,
   MP3Trigger) bit-bang GPIO26 at 9600 baud. UART2 is only used for audio status
   query responses — brief, infrequent RX. Audio drivers already skip UART2 queries
   when dome link or SBUS2 is active (`cfg_enable_s3_dome_ctrl` guard in each driver).

3. **The root cause of both conflicts is baud rate incompatibility, not contention.**
   UART peripheral baud clock is shared between TX and RX. A single UART cannot run
   TX at 115200 and RX at 100kbaud simultaneously, and cannot run RX at 9600 and
   100kbaud simultaneously. Different GPIO pins do not help — it is one clock divider.

---

#### Conflict A — UART1: Hoverboard vs SBUS1

- Hoverboard: 115200 8N1 TX on GPIO16
- SBUS1: 100000 8E2 inverted RX on GPIO15
- Both call `Serial1.begin()` with different parameters. Last call wins, silently
  corrupting the other task's configuration.
- Current workaround: use `standard_pwm` RC mode on classic ESP32 with hoverboard.
  Documented in `docs/status.md` Known Limitations.

**Code carrying this conflict today:**
- `include/sbus_decoder.h` lines 40–46: WARNING comment, `Structural fix: RMT peripheral`
  `(Phase 5 T01)`. `bool begin(HardwareSerial* uart, int rxPin)` signature unchanged.
- `src/drivers/sbus_decoder.cpp` line 10: WARNING comment referencing T01.
- `src/tasks/rc_input.cpp` lines 41–42: `Conflict A persists until T01 (RMT decoder).`
  Lines 646 and 726: `#ifdef PA_BOARD_S3_MINI / &Serial0 / #else / &Serial1 / #endif`
  guards (added by T02; would be removed by T01 when UART is gone entirely).
- `src/tasks/drive.cpp` lines 57–62: comment `Do not run hoverboard S1 and SBUS mode`
  `simultaneously until Phase 5 T01 (RMT SBUS decoder) is in place.`

---

#### Conflict B — UART2: Dome link vs SBUS2

- Dome link: 9600 8N1 full-duplex on GPIO33/34
- SBUS2: 100000 8E2 inverted RX on GPIO13
- Same UART2 peripheral, incompatible baud rates.
- Current workaround: `dual_sbus` and dome link are mutually exclusive.
  Documented in `docs/status.md` Known Limitations.

Note: Audio status RX (UART2 on GPIO35, 9600 baud) shares UART2 with dome link but
at the same baud and format — the conflict is GPIO pin routing, not baud. Audio drivers
already handle this gracefully by skipping queries when dome is active. Not a blocking
issue for droid operation.

---

#### Droid usage configurations on classic ESP32

| Configuration | Hoverboard | SBUS1 drive | SBUS2 dome | Dome link | Works? |
|---|---|---|---|---|---|
| Standard PWM + dome link | ✓ | — | — | ✓ | **Yes — fully functional** |
| Standard PWM, no dome | ✓ | — | — | — | Yes |
| Single SBUS + dome link | ✓? | UART1 conflict | — | ✓ | **No — Conflict A** |
| Single SBUS, no dome | ✓? | UART1 conflict | — | — | No — Conflict A |
| Dual SBUS + dome link | ✓? | UART1 conflict | UART2 conflict | ✓ | No — A + B |
| Dual SBUS, no dome | ✓? | UART1 conflict | ✓ | — | No — Conflict A |

On the **S3 Mini** (optional upgrade target): Conflict A is resolved (UART0 free for
SBUS1); Conflict B remains (UART2 still shared between dome link and SBUS2). S3 Mini
users also benefit from T01 — it fixes Conflict B on that board too.

---

#### Decided direction (confirmed 2026-03-30)

**T01a — RMT decoder for SBUS1 + SBUS2** (fixes Conflict A and Conflict B)
Move SBUS1 (GPIO15) and SBUS2 (GPIO13) off hardware UARTs onto ESP32 RMT channels.
RMT captures edge timestamps on any GPIO; firmware reconstructs SBUS bytes from
10µs-per-bit timing (100kbaud → 10µs per bit). Custom implementation against IDF 5.x
`rmt_new_rx_channel()` / `rmt_receive()` API — no external library exists.

Result on classic ESP32:
- UART1 exclusively owned by DriveTask (hoverboard TX + RX) — Conflict A gone
- UART2 exclusively owned by DomeLinkTask (dome link full-duplex) — Conflict B gone
- SBUS1 + SBUS2 on RMT channels; all four consumers run simultaneously
- Both Known Limitations removed from `docs/status.md`

Files changed:
- `include/sbus_decoder.h`: remove `#include <HardwareSerial.h>`; change `begin()`
  to `bool begin(int rxPin)`; remove `HardwareSerial* _uart` member; add RMT fields
- `src/drivers/sbus_decoder.cpp`: full rewrite — RMT channel init, edge capture,
  bit reconstruction, 25-byte frame assembly; remove all `uart->` calls
- `src/tasks/rc_input.cpp`: remove `#ifdef PA_BOARD_S3_MINI` Serial0/Serial1 guards
  (both `sbus_drive.begin()` sites); remove `&Serial2` from `sbus_dome.begin()` sites;
  update static block comment; remove `Conflict A persists until T01` comment
- `src/tasks/drive.cpp`: remove UART contention comment block (lines 57–62)
- `docs/status.md`: remove both UART-conflict Known Limitations entries

**T01b — Hoverboard RX status read-back** (depends on T01a freeing UART1)
Once UART1 is exclusively owned by DriveTask (no SBUS contention), implement reading
the Gen2.x feedback frames the hoverboard controller sends back.
Surface motor temperature, measured speed, and battery voltage on the dashboard.

Gen2.x controllers send a status frame on the same UART. Exact frame layout must be
confirmed against `EFeru/hoverboard-firmware-hack-FOC` or
`RoboDurden/Hoverboard-Firmware-Hack-Gen2.x-GD32` source before implementing —
do not assume the format.

Files changed:
- `src/drivers/hoverboard_uart.cpp`: add `readFeedback()` function; parse feedback frame
- `include/hoverboard_uart.h`: expose feedback struct and `readFeedback()` declaration
- `include/robot_state.h`: add fields for hoverboard feedback (batVoltage, boardTemp,
  measuredSpeed) with appropriate types
- `src/tasks/drive.cpp`: call `readFeedback()` in task loop; write to RobotState
  under `portMUX` critical section
- Web API / dashboard: surface battery voltage + board temp on the drive or status page

Note: T01b depends on T01a. Do not implement hoverboard RX before UART1 is
exclusively owned — SBUS contention makes the RX reads unreliable.
---

**Acceptance criteria (T01a):**
- `pio run -e protoArtoo` clean; `pio test -e native` passes
- No `HardwareSerial` reference remains in `sbus_decoder.cpp`
- Hardware: SBUS1 + SBUS2 + hoverboard + dome link all active simultaneously
  on classic ESP32; `/api/rc` shows both SBUS sources linked; drive responds to RC
- **Verification classification:** `full-hardware-required`

### T02 — S3 Mini board support (optional second hardware target)

**Status: COMPLETE (bench-tested)** — committed `4c29902` on 2026-03-30.
Hardware validation deferred: requires WEMOS LOLIN S3 Mini board.

**Background:**
The WEMOS LOLIN S3 Mini (ESP32-S3FH4R2) is a drop-in replacement for the
classic ESP32 D1 Mini in the Artoo Controller PCB socket. It resolves Conflict A
(SBUS1 vs hoverboard UART1 contention) without T01 — UART0 is free on S3 because
native USB uses GPIO19/20 instead. Additional benefits: GPIO15 not a strapping pin,
2 MB PSRAM, no ADC2/WiFi conflict. One accepted limitation: AUX1 spare servo
(GPIO19 = USB D−) not available on S3 build; all other features identical.

**Relationship with T01:**
T01 is now deferred. T02 is the primary resolution for Conflict A. On S3, T01 is not
needed. On classic ESP32, the UART contention remains (documented in `docs/status.md`).

**What was done:**
- `platformio.ini`: added `[env:protoArtoo_s3]` and `[env:protoArtoo_s3_ota]`
  (`board = lolin_s3_mini`, `-DPA_BOARD_S3_MINI=1 -DPA_ENABLE_AUX1=0`)
- `include/config.h`: `#ifdef PA_BOARD_S3_MINI` block with 10 GPIO overrides;
  original definitions wrapped in `#ifndef PA_BOARD_S3_MINI` guards
- `src/tasks/rc_input.cpp`: `#ifdef PA_BOARD_S3_MINI` guard on the **two**
  `sbus_drive.begin()` call sites — `&Serial0` on S3, `&Serial1` on classic.
  (Spec said three call sites; actual code has two distinct `begin()` calls.)
  `sbus_dome.begin(&Serial2, ...)` is unchanged — Serial2 is the same on both boards.
- Documentation: `docs/status.md` (Supported Hardware section), `docs/pin_map.md`
  (S3 GPIO table + UART ownership), `AGENTS.md`, `.claude/CLAUDE.md`.
  `docs/goal.md` update (spec §9.2) deferred — lower priority.

**Discovered and fixed during T02:**
- ESP32-S3 LEDC tops out at **14-bit** resolution in low-speed mode;
  `LEDC_TIMER_16_BIT` is not defined for the S3 target (classic ESP32: 16-bit).
  Fixed in `include/ledc_pwm.h`: `LEDC_RESOLUTION_BITS` and `LEDC_RESOLUTION` are now
  `#ifdef PA_BOARD_S3_MINI`-conditional. `LEDC_DUTY_MAX` derives from the macro so
  `pulseUsToDuty()` adapts automatically. `src/drivers/ledc_pwm.cpp` updated to use
  `LEDC_RESOLUTION` from header instead of hardcoded `LEDC_TIMER_16_BIT`.
  14-bit at 50Hz gives ~1.2µs pulse precision — adequate for RC servo/ESC.
- The stale `.begin(pin)` / 'RMT channel' comment in the rc_input.cpp file header
  (aspirational T01 API doc) was corrected to reflect the actual `HardwareSerial*` API.

**Verification:**
- `pio run -e protoArtoo` — SUCCESS
- `pio run -e protoArtoo_s3` — SUCCESS
- `pio test -e native` — 488/488 passed
- Full hardware: deferred (requires S3 Mini board seated in Artoo PCB)

### T03 — Developer tooling: Makefile + first-run setup wizard

**Status: COMPLETE (bench-tested)** — committed `0cae021` on 2026-03-30.

**What was done:**

Files created:
- `Makefile` — repo root; self-documenting via `##` convention; `flash`/`ota` enforce
  native test gate; unseated warning printed before USB flash
- `tools/configure.py` — interactive wizard (questionary when available; stdlib fallback)
- `tools/requirements.txt` — `questionary>=2.0`
- `user.mk.example` — committed reference for all supported variables

Files modified:
- `platformio.ini` — added `protoArtoo_chirp`, `protoArtoo_chirp_ota`,
  `protoArtoo_mp3trigger`, `protoArtoo_mp3trigger_ota` flashable envs.
  (`_chirp_check` / `_mp3trigger_check` compile-only stubs retained.)
- `.gitignore` — added `user.mk`
- `CONTRIBUTING.md` — added `make setup` as recommended first step
- `docs/status.md` — added `make help` as build entry point

Makefile variables (overridable in `user.mk` or on CLI):
```
OTA_IP      ?= 10.0.0.22
BUILD_ENV   ?= protoArtoo
UPLOAD_PORT ?= /dev/ttyUSB0
```

Makefile targets:

| Target | What it runs |
|---|---|
| `make help` (default) | self-documenting target list |
| `make build` | `pio run -e $(BUILD_ENV)` |
| `make test` | `pio test -e native` |
| `make check` | `pio check -e protoArtoo` |
| `make all` | `test` then `build` |
| `make flash` | `test` gate + unseated warning + `pio run ... -t upload` |
| `make ota` | `test` gate + `pio run -e $(BUILD_ENV)_ota -t upload` |
| `make uploadfs` | `pio run -e $(BUILD_ENV)_ota -t uploadfs` (no test gate) |
| `make check-chirp` | `pio run -e protoArtoo_chirp_check` |
| `make check-mp3trigger` | `pio run -e protoArtoo_mp3trigger_check` |
| `make setup` | `python3 tools/configure.py` |
| `make clean` | `pio run -t clean` |
| `make monitor` | `python3 tools/serial_monitor.py` |

Wizard questions (in order):
1. Audio backend — select: DY-SV5W (default), CHIRP, MP3 Trigger
2. OTA target IP address — text, default `10.0.0.22`
3. USB upload port — text, default `/dev/ttyUSB0`
4. Confirm and write `user.mk`

Wizard note: `CHIRP` and `MP3 Trigger` wizard paths map to `protoArtoo_chirp` and
`protoArtoo_mp3trigger` envs added as part of this task. The old spec note
"Phase 5 must add a proper (non-check) CHIRP/MP3Trigger env" is satisfied.

**Verification classification:** `bench-tested`

---

## ⚠️ OPEN HARDWARE ISSUE — S3 Mini: GPIO15 conflict (PIN_HOVERBOARD_RX vs PIN_SBUS1_RX)

**Status: Requires research and hardware validation before S3 is declared production-ready.**

### Problem

On the S3 Mini board target (`PA_BOARD_S3_MINI=1`), two independent pin constants in
`include/config.h` both resolve to **GPIO 15**:

| Symbol | Value | Note in config.h |
|---|---|---|
| `PIN_HOVERBOARD_RX` | 15 | S3 override: "was GPIO17 — left inner pos 7" |
| `PIN_SBUS1_RX` | 15 | Direct match: "left inner pos 7 (UART changes to Serial0)" |

Both entries say **left inner pos 7**. The inline comment claiming "those are different
socket positions" is inconsistent with both being GPIO 15 at the same socket position,
and must not be trusted without physical verification.

If GPIO 15 is one physical pin, connecting both the hoverboard UART RX and SBUS1 RMT
input to it simultaneously is not viable: the RMT peripheral and UART1 would each receive
the same mixed signal at incompatible baud rates, producing garbage in both directions.

### What needs to happen

1. **Physical trace verification** — with an S3 Mini seated in the Artoo PCB, confirm
   the GPIO number at each relevant socket position:
   - PCB header S1 RX (hoverboard UART RX) → which GPIO on S3 Mini?
   - SBUS1 receiver input → which GPIO on S3 Mini?

2. **Update `config.h`** if the GPIO numbers differ from the current overrides.
   If both truly map to GPIO 15, one requires a hardware bodge wire or an explicit
   `#error` documenting that SBUS+hoverboard simultaneous use is not supported on
   S3 without PCB modification.

3. **Add a compile-time guard** if the conflict cannot be resolved in software:
   ```cpp
   #if defined(PA_BOARD_S3_MINI) && (PIN_HOVERBOARD_RX == PIN_SBUS1_RX)
   #  error "S3 Mini: PIN_HOVERBOARD_RX and PIN_SBUS1_RX share the same GPIO."
   #  error "Hardware conflict — see tasks/phase5-tasks.md for resolution steps."
   #endif
   ```

4. **Update `docs/pin_map.md`** once physical truth is confirmed.

### Blocking scope

Does **not** block the classic ESP32 (`wemos_d1_mini32`) release — those GPIO assignments
are confirmed by PCB trace and the conflict does not exist there.
Blocks declaring S3 Mini support production-ready.

**Verification classification:** `full-hardware-required` — cannot be resolved by
bench analysis or code inspection alone.

---

### T04 — Action registry and naming alignment

**Status: COMPLETE (bench-tested)**

#### Goal

Define a formal taxonomy for all robot actions, API endpoints, SSE events, and
RC-bindable targets. Apply it consistently across documentation, code symbols, and
a new runtime registry endpoint the RC mapping UI can query. Replaces the current
ad-hoc naming with a coherent, domain-prefixed convention.

#### Non-goals

- NVS key renames — migration cost outweighs gain; registry documents the mapping instead
- Reeltwo / MarcDuino library internals — external dependency, leave as-is
- `audio` → `sound` rename in C++ symbols — library-level vocabulary stays as-is
- API route restructuring — current routes are functional; registry documents them

#### Naming convention

```
Canonical name:   {domain}.{type}.{verb-noun}    e.g. sound.action.play-track
Display name:     Short label, no domain prefix    e.g. "Play Track"
Separators:       dots between structural segments, kebab within segments
C++ enums:        DOMAIN_TYPE_VERB_NOUN            e.g. DRIVE_ACTION_SPEED
C++ files:        snake_case, device name not transport name
```

Domains: `drive`, `dome`, `sound`, `servo`, `system`, `rc`
Types: `action` (command/intent), `status` (observable state), `event` (SSE emission), `config` (NVS-backed setting)

audio ↔ sound boundary: registry names, display labels, and web UI use `sound`.
C++ symbols (enums, class names, file names) use `audio` — matching the library layer.

---

#### T04/slice:a — Registry document + agent/dev instruction updates

**Zero code changes. Establishes the convention before any renaming begins.**

Files changed:
- `docs/action-registry.yaml` — new file; source of truth for all actions with fully
  populated entries for all domains (drive, dome, sound, servo, system, rc).
  Top of file contains a self-documenting schema comment block covering naming rules,
  the audio/sound boundary, how to add a new entry, and NVS key policy.
- `AGENTS.md` — new `## Action Registry` section (after `## Runtime Contracts`)
  covering: naming convention, audio/sound boundary, `RobotActionId` enum role,
  runtime registry contract, and "what not to do" list.
  `## Source of Truth Files` updated to include `docs/action-registry.yaml`.
- `.claude/CLAUDE.md` — one line added to key reference file list:
  `docs/action-registry.yaml` — canonical action/event registry.

YAML entry schema fields:
```yaml
name:              # {domain}.{type}.{verb-noun}
display_name:      # short UI label — domain/type not repeated
description:       # end-user explanation
domain:            # drive | dome | sound | servo | system | rc
type:              # action | status | event | config
marcduino_cmd:     # Marcduino/Reeltwo equivalent; null if none
api_path:          # "METHOD /api/path"; null if not REST-exposed
sse_event:         # SSE event key; null if not an event
cpp_enum:          # C++ enum value; null if not enumerated
cpp_file:          # header defining the enum
sources:           # [web_api, sbus, dome_rx, internal]
safety_critical:   # true for drive/failsafe/estop
requires_web_control: # true if web_api source needs web control active
params:            # parameter list with name/type/range/required
nvs_key:           # NVS preference key; null if not config type
```

Commit: `724aa51` — `docs(phase:v1.0.0/T04/slice:a): add action registry YAML and update agent/dev docs`

**slice:a: COMPLETE (bench-tested)**

---

#### T04/slice:b — Module naming fixes (no behavioral changes)

Files with wrong-for-the-wrong-reason names corrected:

| Current | New | Reason |
|---|---|---|
| `src/drivers/audio_soft_uart.cpp` | `src/drivers/audio_dy_sv5w.cpp` | Names transport not device |
| `include/audio_soft_uart.h` | `include/audio_dy_sv5w.h` | Same |
| Class `AudioDriverSoftUart` | `AudioDriverDySv5w` | Same |
| `src/drivers/marcduino_rx.cpp` | `src/drivers/dome_rx_parser.cpp` | Dome serial RX, not a generic Marcduino layer |
| `include/marcduino_rx.h` | `include/dome_rx_parser.h` | Same |

All include sites updated in the same slice. Build must pass before proceeding.

Commit: `9d6b8d0` — `refactor(phase:v1.0.0/T04/slice:b): rename audio_soft_uart → audio_dy_sv5w, marcduino_rx → dome_rx_parser`

Note: `audio_soft_uart_tx.h` (shared bit-bang TX primitive used by all three audio drivers) was intentionally left unchanged — the name is accurate for what it is.

**slice:b: COMPLETE (bench-tested)** — `pio run -e protoArtoo` SUCCESS, `pio test -e native` 504/504 passed

---

#### T04 — Test naming audit (2026-03-31)

**Full test inventory: 27 native test suites (`test/test_native/`), 0 embedded test suites (`test/test_embedded/` is empty), 1 web UI fixture (`test/test_rc_learn/index.html` — not a pio test suite). All 27 pio test names audited against `docs/action-registry.yaml`. All names compliant — no renames required.**

| Test | Registry domain | Naming note |
|---|---|---|
| `test_audio_chirp` | `sound` | Uses `audio` — correct per C++ symbol boundary rule |
| `test_audio_dollar` | `sound` | Uses `audio` — correct per C++ symbol boundary rule |
| `test_audio_frames` | `sound` | Uses `audio` — correct per C++ symbol boundary rule |
| `test_audio_mp3trigger` | `sound` | Uses `audio` — correct per C++ symbol boundary rule |
| `test_audio_status_json` | `sound` | Uses `audio` — correct per C++ symbol boundary rule |
| `test_mood_audio_dispatch` | `sound` / `system` | Uses `audio` — correct per C++ symbol boundary rule |
| `test_marcduino_helpers` | `dome` | Tests `marcduino_helpers.h`; distinct from `dome_rx_parser.h` renamed in slice:b; name remains accurate |
| `test_rc_mapping` | `rc` | Tests the `rc_mapping.h` module; module name does not change in slice:c — only enum values inside it; no rename needed |
| `test_hoverboard_feedback` | `drive` | Tests T01b feedback frame parsing; pre-exists, no slice naming conflict |
| `test_hoverboard_frame` | `drive` | Tests frame builder |
| `test_api_config_json` | `system` / `rc` / `drive` | Tests config JSON response formatting |
| `test_api_helpers` | `dome` / `system` | Tests `api_helpers.h`; `ManualCommand` / `MC_*` still in public header pre-slice:c |
| `test_dome_math` | `dome` | Tests dome math utilities |
| `test_json_formatters` | `system` | Tests JSON formatting utilities |
| `test_ledc_pwm_math` | `servo` | Tests LEDC pulse-width math |
| `test_log_buffer` | `system` | Tests log buffer |
| `test_pwm_failsafe` | `drive` / `rc` | Tests PWM signal failsafe detection |
| `test_rc_diagnostics` | `rc` | Tests `rc.status.snapshot` snapshot logic |
| `test_rc_pwm_helpers` | `rc` | Tests RC PWM helpers |
| `test_sbus_channel_map` | `rc` | Tests SBUS channel mapping |
| `test_sbus_flags` | `rc` | Tests SBUS flag parsing |
| `test_sbus_unpack` | `rc` | Tests SBUS frame unpacking |
| `test_sbus_watchdog` | `rc` | Tests SBUS watchdog timer |
| `test_servo_component_type` | `servo` | Tests servo component type parsing |
| `test_servo_helpers` | `servo` | Tests servo helper utilities |
| `test_validation_snapshot` | `system` | Tests validation snapshot struct |
| `test_web_drive_timeout` | `drive` | Tests web-API drive timeout |

**Missing (expected):** `test_action_registry` — does not exist; planned as part of slice:d.
No gap in slice:c or earlier slices.

---

#### T04/slice:c — Enum rename: `RcActionTarget` → `RobotActionId`

`RcActionTarget` is the canonical list of robot-level bindable actions. RC is the
source, not the action. Rename enum and all values to domain-prefixed form.

Value renames:
```
RC_ACTION_NONE           → ROBOT_ACTION_NONE
RC_ACTION_DRIVE_SPEED    → DRIVE_ACTION_SPEED
RC_ACTION_DRIVE_STEER    → DRIVE_ACTION_STEER
RC_ACTION_DOME_SPEED     → DOME_ACTION_SPEED
RC_ACTION_SPEED_LIMIT    → DRIVE_ACTION_SPEED_LIMIT
RC_ACTION_OP_MODE_SWITCH → SYSTEM_ACTION_OP_MODE
RC_ACTION_ARM1_TOGGLE    → SERVO_ACTION_ARM1_TOGGLE
RC_ACTION_ARM2_TOGGLE    → SERVO_ACTION_ARM2_TOGGLE
RC_ACTION_AUX1_TOGGLE    → SERVO_ACTION_AUX1_TOGGLE
RC_ACTION_AUX2_TOGGLE    → SERVO_ACTION_AUX2_TOGGLE
RC_ACTION_AUX3_TOGGLE    → SERVO_ACTION_AUX3_TOGGLE
RC_ACTION_MARCDUINO_SEQ  → DOME_ACTION_MARCDUINO_SEQ
RC_ACTION_MARCDUINO_CMD  → DOME_ACTION_MARCDUINO_CMD
RC_ACTION_ESTOP_LATCH    → SYSTEM_ACTION_ESTOP
RC_ACTION_DOME_SEQ       → DOME_ACTION_SEQ
```

Files: `include/rc_mapping.h` (definition), `src/tasks/rc_input.cpp` (dispatch),
`src/web/api_rc.cpp` (handler), `src/web/rc_diagnostics_snapshot.cpp` (serialization).
Full cutover — no aliases, no backward compat shims.

Also in this slice: `ManualCommand` / `MC_*` moved from `include/api_helpers.h`
into anonymous namespace in `src/web/api_helpers.cpp` (internal parsing detail
should not be in a public header).

Also: add clarifying header comments to `AudioCommandType` and `AudioActionType`
explaining the boundary between the two enums (queue payload vs dollar-parser output).

Commit: `refactor(phase:v1.0.0/T04/slice:c): rename RcActionTarget → RobotActionId with domain-prefixed values`

**slice:c: COMPLETE (bench-tested)** — `ManualCommand`/`MC_*`/`resolveManualCommand()` moved from `include/api_helpers.h` + `src/web/api_helpers.cpp` into anonymous namespace in `src/web/api_drive.cpp`; `resolveManualCommand()` tests removed from `test_api_helpers` (internal detail, no longer testable as public API). `pio run -e protoArtoo` SUCCESS, `pio run -e protoArtoo_s3` SUCCESS, `pio test -e native` 495/495 passed.
**Follow-up (tracked, not blocking slice:c):** `dome.action.set-speed` web-control gate.
`POST /api/dome` does not check `webControlEnabled` — identified during action registry YAML review.
Policy decision: dome is also a motor and should be gated the same as drive.
Fix: add `webControlEnabled` check to the dome handler in `src/web/api_drive.cpp`, then set
`requires_web_control: true` in the registry entry.


---

#### T04/slice:d — C++ runtime registry + `GET /api/actions`

New file `include/action_registry.h`:
```cpp
struct ActionEntry {
    RobotActionId id;           // enum value
    const char*   name;         // "drive.action.speed"
    const char*   display_name; // "Speed"
    const char*   domain;       // "drive"
    const char*   description;
    bool          safety_critical;
};
extern const ActionEntry ACTION_REGISTRY[];
extern const size_t      ACTION_REGISTRY_SIZE;
```

New file `src/web/api_actions.cpp` — `GET /api/actions` handler returning JSON array
of all bindable actions (type: action, sources include sbus). Status/config/event
entries are YAML-only; runtime registry covers bindable actions only.

New test `test/test_native/test_action_registry.cpp`: size > 0, all entries have
non-empty name/display_name, id values are unique.

Commit: `feat(phase:v1.0.0/T04/slice:d): add ActionEntry registry and GET /api/actions endpoint`

**slice:d: COMPLETE (bench-tested)** — `include/action_registry.h` + `src/web/action_registry.cpp` (14 entries: all non-NONE `RobotActionId` values) + `include/api_actions.h` + `src/web/api_actions.cpp` (`GET /api/actions`) created. `platformio.ini` native `build_src_filter` updated to include `action_registry.cpp`. `docs/action-registry.yaml` updated with missing `drive.action.speed` and `drive.action.steer` entries. New test suite `test_action_registry` (4 tests: size > 0, non-empty fields, unique ids, no NONE entry). `pio run -e protoArtoo` SUCCESS, `pio run -e protoArtoo_s3` SUCCESS, `pio test -e native` 499/499 passed.

---

#### T04/slice:e — RC mapping UI uses runtime registry

Current: action dropdowns in the RC mapping editor are hardcoded in the frontend.
New: page load calls `GET /api/actions`; response populates dropdowns dynamically.
Display name shown in UI, canonical `name` (or numeric `id`) sent on save.
Confirm target file during implementation (likely `data/setup.html` + associated JS).

Commit: `feat(phase:v1.0.0/T04/slice:e): RC mapping UI populates action list from GET /api/actions`

**slice:e: COMPLETE (bench-tested)** — `data/rc.js`: `RC_ACTION_TARGETS` const renamed to `HARDCODED_ACTION_TARGETS`; `let actionTargets` starts from hardcoded fallback and is replaced by `buildActionTargetsFromApi()` on successful `GET /api/actions` load. `loadActionTargets()` fires at init alongside `loadRcMode()` / `loadRcDiagnostics()`. `src/web/api_actions.cpp` updated to include `token` (legacy NVS key via `robotActionIdToString()`) in the JSON response so the save path requires no changes. `actionGroup()` derives optgroup from domain+name. `UNAVAILABLE_TOKENS` set preserves the `dome_seq` disabled state. `pio run -e protoArtoo` SUCCESS, `pio run -e protoArtoo_s3` SUCCESS, `pio test -e native` 499/499 passed.

---

#### Slice dependency order

```
slice:a → slice:b → slice:c → slice:d → slice:e
(docs)    (files)   (enums)   (endpoint) (UI)
```

slice:a strictly first — establishes naming rules used in all subsequent slices.
Each slice compiles clean and passes `pio test -e native` before the next begins.
slice:d depends on slice:c (`RobotActionId` must exist).
slice:e depends on slice:d (endpoint must exist).

#### Acceptance criteria (T04)

- `pio run -e protoArtoo` and `pio run -e protoArtoo_s3` compile clean with `-Werror`
- `pio test -e native` green at end of each slice
- No reference to `RcActionTarget`, `RC_ACTION_`, `AudioSoftUart`, or `ManualCommand`
  remains in any header (grep-verified)
- `AGENTS.md` has an `## Action Registry` section; `docs/action-registry.yaml` listed
  under `## Source of Truth Files`
- `.claude/CLAUDE.md` key reference file list includes `docs/action-registry.yaml`
- `GET /api/actions` returns valid JSON with all 14 bindable actions
- RC mapping UI action dropdowns populate from the API (not hardcoded)
- Native test covers registry size, uniqueness, and non-empty fields
- `docs/action-registry.yaml` committed and consistent with C++ registry entries

**Verification classification:**
- slice:a — `bench-tested` (docs only)
- slice:b — `bench-tested` (compile + native tests)
- slice:c — `bench-tested` (compile + native tests; RC behavior unchanged)
- slice:d — `bench-tested` (endpoint testable over WiFi without hardware)
- slice:e — `partial` (UI over bench WiFi; full RC binding behavior needs hardware)

---

## Final Validation Requirements

- [ ] Bench development stage validation complete and documented
- [x] Build, native tests, and static analysis pass cleanly — `pio run -e protoArtoo` +
      `pio run -e protoArtoo_s3` + `pio test -e native` (488/488) green on 2026-03-30
- [ ] Browser UI and OTA flow are fully validated
- [ ] RC, hoverboard, servo, dome, audio, and body-link behavior are all validated on full hardware
- [ ] Safety invariants are verified end-to-end
- [ ] Documentation is complete and consistent with the released behavior
- [ ] Spec conformance matrix completed against:
  - `tasks/SBUS_protocol.md`
  - `tasks/rc_reciever_spec.md`
  - `tasks/marcduino_commands.md`
  - `tasks/servo_communication.md`
  - `tasks/sound_playback.md`
  - `tasks/isdt_esc70_dome_esc.md`

## Exit Criteria

- [ ] No known blocking safety or operational issues remain
- [ ] Full-hardware-required validation is complete across the integrated droid,
      or a deferred validation record exists with blockers and explicit closure steps
- [ ] `CHANGELOG.md` receives a real `1.0.0` entry only when the stable release is actually cut
