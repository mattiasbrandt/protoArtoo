# Changelog

All notable changes to `protoArtoo` are documented here.

Format follows [Keep a Changelog 1.1.0](https://keepachangelog.com/en/1.1.0/).
Versioning follows [Semantic Versioning 2.0.0](https://semver.org/).

This changelog is tag-based. Entries are added only when a version is actually
released and tagged in git.

Every semantic version release belongs here:
- patch releases for bug fixes
- minor releases for new backwards-compatible features
- major releases for breaking changes

## [Unreleased]

### Added
- **Heap fragmentation monitoring** — `heap_caps_get_largest_free_block(MALLOC_CAP_8BIT)`
  tracked in `SafetyMonitorTask` alongside `getFreeHeap()`; fragmentation ratio logged
  every 6 seconds; `heapLargestBlock` field added to SSE status JSON and `/api/health`
- **`PA_ENABLE_STA_WIFI` build flag** — controls whether WiFi STA client is compiled in.
  New `protoArtoo_prod` env uses `PA_ENABLE_STA_WIFI=0` (AP-only field deployment,
  saves ~7 KB steady-state heap and ~18 KB boot watermark vs STA+AP mode)

### Changed
- **Platform upgraded: `espressif32@5.2.0` → `6.13.0`** — arduino-esp32 2.0.5 → 2.0.17
  (IDF 4.4.x → 4.4.7, esptool 4.2.1 → 4.11.0). Same 2.x API family — zero source
  changes required. Benefit: 12 patch releases of arduino-esp32 bug fixes, more stable
  WiFi stack, +7 KB heap minimum watermark at boot.
- **AsyncWebServer and AsyncTCP migrated from abandoned `me-no-dev/` namespace to
  maintained `ESP32Async/` fork** (`ESPAsyncWebServer@3.10.3`, `AsyncTCP@3.4.10`).
  The `me-no-dev` repos have had no activity since 2022. The `ESP32Async` fork (formerly
  `mathieucarbou/`) carries confirmed SSE memory leak fixes, proper TCP buffer teardown,
  and `SSE_MAX_QUEUED_MESSAGES` queue cap support.
- **`CONFIG_ASYNC_TCP_STACK_SIZE=4096`** in build_flags — reduces AsyncTCP internal
  task stack from 16 KB default to 4 KB, saving 12 KB runtime heap
- **`SSE_MAX_QUEUED_MESSAGES=8`** in build_flags — caps per-client SSE outbox to
  prevent unbounded heap growth when browser tabs fall behind
- **`PA_LOG_LEVEL` lowered from 3 (debug) to 2 (info)** in production build — saves
  4 KB BSS (debug ring buffer 6.1 KB → 2.0 KB)
- **`board_upload.before_reset = default_reset`** replaces old `upload_flags = --before
  no_reset_no_sync`. PlatformIO appends `upload_flags` after the `write_flash` subcommand
  where `--before` is ignored by esptool 4.x; `board_upload.*` inserts it correctly.
  DTR/RTS auto-reset confirmed working on this USB-serial adapter when ESP32 is unseated.
- **`StaticJsonDocument<64>` replaced with `JsonDocument`** in `api_rc.cpp` — removes
  ArduinoJson v7 deprecation warning

### Fixed
- **Heap monitoring blind spot** — `safety.cpp` and SSE status previously reported only
  total free bytes; fragmentation could drive the largest contiguous block below WiFi's
  minimum needs while total free looked healthy. `heapLargestBlock` closes this gap.

## [0.3.0] - 2026-03-16

### Added
- **ServoTask** for ARM1/ARM2 utility arms via LEDC PWM at 50 Hz
- **ServoComponentType** enum (NONE/MG996R/MG90S/RGB) with per-type calibration defaults
- **DomeTask** for dome motor ESC control (tested: ISDT ESC70) via LEDC PWM
- **RC diagnostics surface** with live channel visualization and SSE streaming
- **Three RC input modes**: `standard_pwm`, `single_sbus`, `dual_sbus` (runtime selectable)
- **Modular API architecture**: split into focused route handlers (api_config, api_drive, api_estop, api_rc, api_servo, api_status, api_system)
- **New web pages**: drive.html, dome.html, servo.html, rc.html, firmware.html
- **Dashboard operation mode** card: Driving ⇔ Stationary toggle via `/api/mode`
- **Dashboard mood selector** card: Quiet, Mid-Awake, Full-Awake, Awake+ buttons
- **CH17/CH18 digital channel decoding** from SBUS flags byte
- **SBUS flag parsing helpers** (`sbus_flags.h`, `sbus_unpack.h`)
- **Marcduino RX parser** (`:OP`, `:CL`, `:MV`, `:SE30-:SE36`) for dome→body commands
- **RC channel binding model**: backbone bindings (fixed role) and trigger bindings (configurable action)
- **NVS-backed RC calibration**: min/center/max/deadband/reverse per channel
- **Version extraction script** (`tools/extract_version.py`): inject PA_VERSION from CHANGELOG at build
- **Project favicon** (`r2d2body-favicon.png`) on all HTML pages
- **Artoo Controller PCB photo** on setup page for reference
- **Terminology glossary** (`docs/terminology.md`)
- **336 native unit tests** covering LEDC math, dome math, servo helpers, SBUS flags, RC diagnostics, Marcduino helpers

### Changed
- Extended `RobotState` with servo, dome, and RC diagnostics fields
- Reduced API handler buffer sizes for memory optimization (heap 135KB free, 127KB min)
- Updated task initialization to create ServoTask and DomeTask on Core 1
- Refactored web_server.cpp into modular API route files
- Updated docs/api.md with new servo, dome, RC endpoints
- Updated docs/pin_map.md with servo/dome GPIO assignments
- Updated docs/failsafe.md with 5-layer safety model

### Fixed
- **SBUS flag bit positions**: `lost_frame` corrected to bit 2 (0x04), `failsafe` to bit 3 (0x08)
- **TWDT crash**: removed SSE onConnect log sync loop causing watchdog timeout
- **Memory optimization**: consolidated API buffers, reduced SSE body sizes
- **Button underline CSS**: exclude buttons from `[title]` selector
- **Live logs SSE**: restored log streaming via `copyNewLogLinesSince()`
- **OTA environment**: removed duplicate `lib_deps` override in `protoArtoo_ota`

### Hardware Validated
- Dual SBUS live data confirmed on real hardware
- Arm servos respond via SBUS CH4/CH5 triggers
- RC diagnostics page working with real transmitter input

### Deferred
- Dome ESC response validation (requires full wiring harness)
- SBUS Layer 1/2 failsafe deliberate tests
- Full hoverboard drive path validation

## [0.2.0] - 2026-03-13

### Added
- Bench-tested Phase 2 web stack with LittleFS-served `Home`, `Setup`, `WiFi`, `Firmware`, and `Serial` pages
- Expanded HTTP API with config, WiFi, serial, health, logs, manual-command, reboot, and OTA firmware upload endpoints
- Dashboard health surfaces including heap status, WiFi quality, movement status, live log console, and manual command controls
- Explicit browser-control mode for web drive commands when SBUS is unavailable
- NVS-backed config persistence for web-configurable settings, surviving reboot
- OTA firmware upload flow and browser-triggered reboot support on the bench controller
- Native test coverage for web/API helper parsing, log buffer behavior, and JSON formatter helpers

### Changed
- Switched the Phase 2 bench controller board definition to `wemos_d1_mini32` with a reliable upload workflow at `115200`
- Added leveled USB debug logging with clearer boot health, WiFi bring-up, and web-server bring-up output
- Refined the multi-page UI toward a more coherent operator-facing control panel with less internal planning language
- Moved visible planning/status source-of-truth files into `tasks/`

### Fixed
- Corrected API/static route ordering so `/api/*` requests are no longer intercepted by the LittleFS static handler
- Fixed estop-clear route behavior on the live board
- Fixed dashboard hydration issues caused by removed `Live Feed` dependencies in the page script
- Fixed malformed status payload metadata used by firmware version/uptime footer rendering

## [0.1.0] - 2026-03-12

### Added
- PlatformIO project scaffold for the ESP32 firmware, including `.clang-format`,
  OTA partitions, and build/test environments
- Core firmware headers for shared state, config, Marcduino constants, task
  interfaces, and web server declarations
- Hoverboard Gen2.x drive transport and `DriveTask` with 50 Hz command output,
  zero-frame behavior, and watchdog feeding
- Custom RMT-based dual SBUS input stack with mapping helpers and
  `SBUSInputTask` support for HOTRC SBUS-A receivers
- `SafetyMonitorTask` boot/runtime orchestration and native unit tests for
  hoverboard frame packing and SBUS math
- Phase 1 WiFi AP and minimal REST API with `POST /api/estop`,
  `POST /api/estop/clear`, and `GET /api/status`
- Phase 1 documentation covering API behavior, failsafes, traced PCB pin map,
  topology, project setup, and body-dome serial link references

### Changed
- Confirmed traced UART routing so hoverboard drive uses `UART1` on PCB header
  `S1`, while dome serial is reserved for `UART2` on header `S3`
- Replaced earlier UART-based SBUS assumptions with the in-repo RMT decoder
- Pinned `ESPAsyncWebServer` to `3.6.0` and `AsyncTCP` to `3.3.2`

### Fixed
- Prevented `Serial` logging while holding `robotStateMux` in the SBUS watchdog path
- Removed handler-time dynamic allocation from the `/api/status` response path
- Corrected dome heartbeat helper logic so "connected" requires a real heartbeat
