# protoArtoo Terminology

This document explains common project terms used in code, docs, logs, and planning.

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

## Marcduino

`Marcduino` refers to the command protocol style used for droid control messages
and the wider ecosystem it came from.

In this repo, "Marcduino command" usually means:

- ASCII command strings with a prefix (for example `:`, `$`, `#`, `*`, `@`, `%`)
- Carriage-return terminated lines (`\r`)
- Routed based on prefix ownership (body-owned vs dome-owned)

Where it appears:

- Protocol/planning docs: `docs/goal.md`, `docs/marcduino_commands.md`
- Body parser code: `src/drivers/marcduino_rx.cpp`
- Shared constants/helpers: `include/marcduino.h`, `include/marcduino_helpers.h`

Why it matters:

- It is the language used between body and dome over serial
- Correct routing determines whether commands are executed, forwarded, or discarded
- Mistakes in parsing or prefix ownership can trigger wrong behavior (or no behavior)

Example mindset in protoArtoo:

- `:` commands are typically motion/sequence related (arms/panels)
- `$` commands are audio-related and body-side in this architecture
- Some prefixes are intentionally ignored on body when they are dome-only in topology

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
- Input task handling: `src/tasks/sbus_input.cpp`
- Parsing helpers/tests: `include/sbus_flags.h`, `include/sbus_unpack.h`, `test/test_native/test_sbus_flags/`, `test/test_native/test_sbus_unpack/`

Why it matters:

- SBUS health is part of the safety model (timeouts, failsafe flags)
- Channel mapping drives speed/steer and other behaviors
- Bit-level decoding errors can silently corrupt control input

Common SBUS terms in this repo:

- `lost_frame`: a frame-drop indicator bit, not automatically equivalent to full failsafe
- `failsafe`: receiver-reported signal-loss condition
- `sbusSignalLost`: project state field indicating SBUS availability for control logic

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

## Common Phrase: Bench-Tested vs Full-Hardware-Validated

You will see these terms frequently in planning docs.

- `bench-tested`: validated with controller-level setup, limited peripherals, or simulated context
- `full-hardware-validated`: validated on integrated real hardware where physical behavior is confirmed

Both are useful, but they are not equivalent. Safety and motion claims require full-hardware validation.
