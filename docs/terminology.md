# protoArtoo Terminology

This document explains common project terms used in code, docs, logs, and planning.

## Table of Contents

- [Quick Glossary](#quick-glossary)
- [ESTOP](#estop)
- [Failsafe](#failsafe)
- [UART](#uart)
- [RMT](#rmt)
- [Marcduino](#marcduino)
- [ReelTwo (Reeltwo)](#reeltwo-reeltwo)
- [AstroPixels and AstroPixelsPlus](#astropixels-and-astropixelsplus)
- [RobotState](#robotstate)
- [NVS](#nvs)
- [STA](#sta)
- [AP](#ap)
- [SBUS](#sbus)
- ["Native" Tests vs `protoArtoo` Tests](#native-tests-vs-protoartoo-tests)
- [Validation Language](#validation-language)

## Quick Glossary

| Term | Meaning in protoArtoo | Why it matters |
|---|---|---|
| ESTOP | Latching emergency stop state | Prevents unintended movement restart |
| failsafe | Layered safety stop behavior when control health is bad | Core motion safety model |
| UART | Serial transport used for hoverboard, dome link, audio, debug | Defines key inter-device communication paths |
| RMT | ESP32 peripheral used for precise pulse capture (SBUS decoding here) | Enables reliable SBUS timing capture |
| Marcduino | Prefix-based serial command style used across droid ecosystems | Governs body/dome command routing |
| ReelTwo / Reeltwo | C++ framework/library ecosystem used heavily by dome-side stacks | Shapes compatibility boundaries with AstroPixelsPlus |
| AstroPixels / AstroPixelsPlus | Dome-controller firmware lineage and project family | Defines dome-side responsibilities in this topology |
| RobotState | Shared runtime state contract across tasks | Primary state/control coordination object |
| NVS | ESP32 non-volatile key-value storage | Persists settings across reboots |
| STA / AP | WiFi station mode vs access-point mode | Changes connectivity model and operator workflow |
| SBUS | RC serial protocol for receiver input | Primary RC transport in single/dual-SBUS modes |

## ESTOP

`ESTOP` means emergency stop.

In protoArtoo:
- `estop` is a latching safety state in runtime state.
- Typical set path is `POST /api/estop`.
- Clear requires an explicit action (`POST /api/estop/clear` or equivalent manual-command clear path).
- It does not auto-clear just because signal/control returns.

Why it matters:
- Prevents accidental movement restart after a critical event.
- Is intentionally stricter than timeout-based temporary failsafe states.

## Failsafe

`failsafe` is the layered safety behavior that forces safe output (zero drive intent)
when control integrity is degraded.

In this project, failsafe is not one single trigger. It is a layered model, including:
- RC receiver hardware failsafe signaling
- SBUS software watchdog timeout
- web-drive timeout
- watchdog-reset recovery posture
- hoverboard-side timeout behavior

Why it matters:
- Multiple independent layers reduce single-point failure risk.
- Status surfaces expose source/health details for diagnostics.

Related distinction:
- `lost_frame` is a frame-quality indicator and not always equivalent to full failsafe by itself.

## UART

`UART` means Universal Asynchronous Receiver/Transmitter: asynchronous serial communication.

In protoArtoo topology:
- debug console uses UART0
- hoverboard drive link uses UART path on S1
- dome link uses UART path on S3 (bidirectional over slip ring)
- sound module uses UART-style serial path on S2

Why it matters:
- UART links are core integration boundaries between body controller and peripherals.
- Correct baud, direction, and TX/RX wiring are required for predictable behavior.

## RMT

`RMT` is an ESP32 peripheral (Remote Control Transceiver) used for precise pulse timing work.

In this project context:
- SBUS capture/decoding paths use RMT timing behavior.
- This avoids contention with primary hardware UART paths used by other subsystems.

Why it matters:
- SBUS decode quality depends on timing correctness.
- Misconfiguration can look like random RC instability.

## Marcduino

`Marcduino` refers to the command protocol style used for droid control messages
and the wider ecosystem it came from.

In this repo, "Marcduino command" usually means:
- ASCII command strings with a prefix (for example `:`, `$`, `#`, `*`, `@`, `%`)
- carriage-return terminated lines (`\r`)
- routed based on prefix ownership (body-owned vs dome-owned)

Where it appears:
- protocol docs: `docs/goal.md`, `docs/commands.md`
- body parser path: `src/drivers/marcduino_rx.cpp`
- shared helpers: `include/marcduino.h`, `include/marcduino_helpers.h`

Why it matters:
- It is the language used between body and dome over serial.
- Prefix routing decisions determine whether commands are executed, forwarded, or ignored.

## ReelTwo (Reeltwo)

`ReelTwo` (repository/package often spelled `Reeltwo`) is a C++ framework/library ecosystem
used in astromech firmware stacks.

In protoArtoo context:
- ReelTwo is primarily relevant on dome-side firmware ecosystems (for example AstroPixelsPlus lineage).
- Body-side protoArtoo uses its own explicit task/driver architecture and does not rely on a full
  ReelTwo runtime for core body behavior.

Why it matters:
- Helps clarify integration boundaries and why some dome-side conventions differ from body-side implementation.

## AstroPixels and AstroPixelsPlus

`AstroPixels` / `AstroPixelsPlus` refer to dome-controller firmware lineage used in community builds.

In protoArtoo topology:
- dome controller is treated as an AstroPixelsPlus-class peer subsystem.
- body and dome coordinate over bidirectional serial links with explicit responsibility boundaries.
- dome-side effects (lighting/animation ownership) are dome responsibilities.

Why it matters:
- Prevents ambiguous ownership between body and dome behavior paths.
- Keeps body-side and dome-side responsibilities auditable and maintainable.

## RobotState

`RobotState` is the central shared state struct for the firmware.

- Defined in `include/robot_state.h`
- Instantiated globally in `src/main.cpp` as `robotState`
- Protected by `robotStateMux` (`portMUX_TYPE`) for cross-task safety

What it contains:

- Live runtime state: drive commands, failsafe state, timing, heartbeat counters
- Persisted config loaded from NVS: fields prefixed with `cfg_`
- Feature toggles for hardware subsystems: `cfg_enable_*`

Why it matters:

- It is the contract between tasks (`DriveTask`, `SbusInputTask`, web handlers, etc.)
- Incorrect unsynchronized access can cause race conditions
- Most API responses and control decisions ultimately come from this state

Rule of thumb:

- If a value can be read/written by multiple tasks, it should be in `RobotState`
- Access shared fields under `robotStateMux`

## NVS

`NVS` means Non-Volatile Storage on ESP32. In Arduino/ESP32 here, it is accessed via `Preferences`.

- Namespace used by this project: `proto` (`NVS_NAMESPACE`)
- Main load/save path: `loadConfigToState()` and `saveConfigToNvs()` in `src/main.cpp`

What NVS stores in this repo:

- Persistent config such as speed limits, timeouts, servo positions, dome pulse settings
- Feature toggles (`en_arm1`, `en_dome`, `en_s1_hoverboard`, etc.)

Why it matters:

- Settings survive reboot and power loss
- Web setup changes are not temporary; they are persisted and reloaded at boot

Important distinction:

- Tasks should use `robotState.cfg_*` values at runtime
- NVS reads/writes should stay centralized in config paths, not scattered in task loops

## STA

`STA` means Station mode (`WIFI_STA`) on ESP32.

In practical terms:

- The controller joins an existing WiFi network (your router/AP)
- Useful for integrating the droid into a known LAN

Where it shows up:

- Mentioned in project docs and WiFi/status APIs
- Credentials come from `src/secrets.h`

Why it matters:

- Enables access from other devices on the same network
- Changes IP assignment and how you reach the web UI/API

## AP

`AP` means Access Point mode (`WIFI_AP`) on ESP32.

In practical terms:

- The controller itself creates a WiFi network (default SSID: `protoArtoo`)
- Typical default IP is `192.168.4.1`

Why it matters:

- Gives direct phone-to-droid connectivity without external infrastructure
- Common for field setup, debugging, and controlled local operation

STA vs AP quick view:

- `AP`: droid hosts network, client devices connect to it
- `STA`: droid joins another network
- The firmware can support both roles depending on setup

## SBUS

`SBUS` is the RC receiver serial protocol used for control channels.

In this project context:

- Used for RC input paths (`single_sbus` and `dual_sbus` receiver modes)
- Also appears alongside `standard_pwm` mode in planning/config docs

Core protocol characteristics (as documented in project planning/spec docs):

- 25-byte frame
- Inverted serial signaling
- Typical framing includes packed 11-bit channel values and status flags

Where it appears:

- Decoder/driver code: `src/drivers/sbus_decoder.cpp`
- Input task handling: `src/tasks/rc_input.cpp`
- Parsing helpers/tests: `include/sbus_flags.h`, `include/sbus_unpack.h`, `test/test_native/test_sbus_flags/`, `test/test_native/test_sbus_unpack/`

Why it matters:

- SBUS health is part of the safety model (timeouts, failsafe flags)
- Channel mapping drives speed/steer and other behaviors
- Bit-level decoding errors can silently corrupt control input

Common SBUS terms in this repo:

- `lost_frame`: a frame-drop indicator bit, not automatically equivalent to full failsafe
- `failsafe`: receiver-reported signal-loss condition
- `sbusSignalLost`: project state field indicating SBUS availability for control logic

## RcInputProcessor

`RcInputProcessor` is the pure orchestration module that processes all RC inputs on each tick.

- Defined in `include/rc_input_processor.h`
- Instantiated once in `src/tasks/rc_input.cpp`

What it owns:

- `TriggerDebounceState triggerStates[RC_TRIGGER_MAX]` — per-trigger debounce state for all Tier 2 bindings
- `DomeInputFilter domeInputFilter` — dome speed smoothing filter
- `bool lastSoundPressed` — backbone sound edge detection state

What it does not own:

- Hardware (SBUS decoders, PWM reads) — stays in the task shell
- Config loading and caching — task shell injects config per tick
- Queue dispatch — task shell dispatches the output to audio, servo, dome queues

Why it matters:

- All stateful RC orchestration logic lives in one struct instead of scattered static locals inside task functions
- The seam between "resolve what RC inputs mean" and "read hardware / write queues" becomes explicit and testable
- `rcInputProcessorTick()` can be exercised in native unit tests without FreeRTOS or hardware

## "Native" Tests vs `protoArtoo` Tests

This project has two distinct PlatformIO test/build environments.

Source of truth:

- `platformio.ini`
- `[env:native]`
- `[env:protoArtoo]`

`native` (host machine tests):

- Runs on your development computer (Linux/macOS/Windows)
- Fast feedback for pure logic
- No ESP32 hardware required
- Command: `pio test -e native`

Typical `native` scope:

- Parsers, mapping logic, math helpers, formatting helpers
- Files under `test/test_native/`

`protoArtoo` (firmware target):

- Compiles and tests against the ESP32 Arduino target
- Represents real firmware behavior and toolchain constraints
- May require connected hardware for meaningful runtime validation
- Build command: `pio run -e protoArtoo`
- On-device tests command: `pio test -e protoArtoo`

When to use which:

- Use `native` while developing logic quickly and repeatedly
- Use `protoArtoo` before merging/releasing to catch target-specific issues
- For hardware behavior (UART, PWM, failsafe timing), only target/hardware validation is authoritative

## Validation Language

Project verification labels and public-facing release wording live in
`CONTEXT.md`, `AGENTS.md`, and `docs/status.md`.

Use this glossary for technical terms like SBUS, NVS, RMT, Marcduino, RobotState,
and AP/STA. Use the validation docs for status wording and release-note evidence
phrases.
