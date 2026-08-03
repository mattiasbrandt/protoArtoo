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
- Static-response TCP enqueue diagnostics for issue #60: `/api/status` reports
  boot-lifetime zero-progress attempts, recovery episodes, and retry-budget
  exhaustions without allocating in the response path, including whether each
  zero-progress attempt had TCP send space immediately beforehand and the exact
  lwIP error/send-queue state when `AsyncClient::add()` fails.
- Request-lifecycle evidence instrumentation for issue #52/#54: `/api/status` now
  reports live/peak inflight request depth, peak SSE clients, and refusal counts
  by admission class (inflight cap, SSE cap, heap floor diagnostic/non-diagnostic);
  `/api/profiler` (PA_HEAP_PROFILE builds) additionally reports a bounded
  request-start/handler-done/disconnect trace. Evidence-gathering only — no
  admission caps, floors, or weights were changed.

### Fixed
- Static files with a declared content length now keep final buffered bytes
  retryable until TCP accepts them and retry bounded transient zero reads on
  the same response instead of ending early with a truncated body. Repeated
  zero-progress TCP enqueue attempts are also bounded through AsyncTCP's
  five-second ACK window, while partial progress remains correctly accounted.

## [1.0.0] - 2026-08-01

First stable release. Feature-complete for day-to-day operation — audio, RC
control, dome control, servos, the web control panel, backups, and firmware
updates are all confirmed working on real hardware. Full drive-motor
(hoverboard) validation on a completely assembled droid is tracked as
follow-up work, not a blocker for this release (see `docs/status.md`).

### Added
- Runtime action registry now powers RC bindings and the action picker in the web API/UI.
- Sleep mode now keeps body and dome state in sync, with wake and sleep control from the web UI and RC bindings.
- CHIRP playback got broader sound coverage: category ranges, named tracks, banked playback, and system sound mapping.
- Full-droid sequence actions and dome sequence controls now support more complex show playback.
- AUX LED strip support now includes configurable header selection and setup-page controls.
- The setup page now supports NVS backup and restore.
- Shell controls now include estop and reboot actions.
- Random dome rotation now has web configuration and RC control support.
- Coredump partition and HTTP retrieval (`/api/coredump`) lets operators download
  crash dumps for offline analysis alongside the live serial log.
- Static web assets are now gzip-compressed at build time, reducing dashboard load size.
- The sequence editor's dome panel picker is now an SVG rendering of real dome
  geometry, matching the live dome render on the home page, with pie panels
  directly selectable.
- The home page now has quick-access controls for dome commands and recently-used sequences.
- New `POST /api/seq/stop` provides a non-latching way to abort a running
  sequence, distinct from estop.
- The dashboard now shows which dome link transport (WiFi or serial UART) is active.
- Dome visual commands (Holo Effect, Logic/PSI, Logic Text, Visual Preset) are
  now validated server-side before dispatch, catching malformed dome commands
  before they reach the dome.
- The sequence editor now supports authoring `DV:<name>` visual presets and
  Holo Effect / Logic-PSI / Logic-Text steps, completing the dome sequence vocabulary.
- Every built-in sequence now shows its intended use on the Factory card.
- Operators can now set a custom droid name from the web UI, shown on the status page and in logs.
- GitHub Actions now gate pull requests with build, test, static-analysis, and
  dependency-review checks.

### Changed
- Drive and RC control paths now use a shared output arbiter, safer source handling, and latching safety behavior.
- Configuration flows now separate save/apply, runtime use, and reboot survival through clearer state handling.
- Audio now uses a shared driver interface and playback policy, with cleaner catalog ownership and volume handling.
- Dome handling is now split into serial control, motion control, and body-link coordination.
- Web and dashboard surfaces now have clearer live feedback, more reliable status derivation, and better log handling.
- The public status page (`docs/status.md`) was rewritten for a plain-language
  operator audience, with per-subsystem evidence and a clear list of what's
  still unverified.
- CHIRP audio and the dome serial link now coexist reliably on a shared UART,
  instead of intermittently dropping audio or dome commands.
- Sequence and CHIRP catalog memory is now allocated based on actual data size
  instead of static worst-case buffers, reducing baseline heap usage.
- AsyncTCP and AsyncWebServer were upgraded to the actively maintained
  `ESP32Async/` fork, fixing SSE memory leaks and TCP teardown issues.
- The dome panel picker UX is more direct (fewer confirmation steps for pie
  panels) and more readable (contrast, label sizing) for low-light operation.
- SSE connections are now rate-limited and paced to avoid overloading the
  device under dense dashboard polling.
- OTA update timeouts were extended for reliability over slow connections.

### Fixed
- Audio control and playback regressions across supported backends.
- RC mapping, config apply, and UI synchronization bugs.
- Status and diagnostics issues that could hide the actual runtime state.
- A heap-exhaustion crash (OOM) affecting CHIRP and Learned Sequences under
  larger catalogs/libraries — memory is now right-sized on demand instead of
  over-allocated up front.
- CHIRP audio could silently fail to initialize when the dome serial link held
  the shared UART; it now retries instead of falling back permanently.
- Dome link UART recovery — reconnects now clear stale errors and no longer
  hang indefinitely contending for the shared UART.
- Server-side crashes and connection issues under live-status (SSE) load, and
  under general API load.
- Dome panel availability messages now correctly explain *why* a panel is
  unavailable instead of a generic "not reachable".
- The dome panel picker no longer renders blank on first load.
- Pie panel label contrast now meets accessibility guidelines for low-light use.
- A spurious failsafe trigger in single-SBUS mode with a secondary channel enabled.
- Sequence save no longer drops the sequence body, fixing a save/round-trip regression.

### Still to verify
- Drive-motor (hoverboard) behavior on a completely assembled droid. Drive control,
  safety logic, and failsafe are implemented and tested, but live wheel response
  has not yet been confirmed on a finished build. This is planned as follow-up
  work after `v1.0.0` and will be documented when complete.
- The MP3 Trigger audio module (an alternative to CHIRP) is implemented but has
  not been re-confirmed on hardware for this release.
- Drive hardware checks are still to be completed on the hoverboard and complete droid hardware. This is the remaining hardware verification item before release.

## [0.4.0] - 2026-03-29

### Added
- **Audio system** — pluggable audio module support with three backends: DY-SV5W binary-frame
  driver (confirmed on hardware), CHIRP Audio Trigger ASCII driver, and SparkFun MP3 Trigger
  binary driver. All audio routes through a single `AudioTask` queue accepting commands from
  RC, web API, and dome serial. Backend selected at compile time via `PA_AUDIO_DRIVER`.
- **Sound page** — dedicated `/sound.html` with volume slider, named sound triggers (scream,
  Leia message, Short Circuit, Cantina variants, Star Wars theme, Imperial March, startup),
  direct track play by number, random chatter pool range, mood sound intervals, and a live
  Audio Module Status card showing link state, device type, play state, and track count.
- **Per-mood random sound intervals** — each mood state has its own configurable chatter
  interval: Quiet = 0 s (silent), Mid-Awake = 30 s, Full-Awake = 20 s, Awake+ = 10 s.
  All four are configurable from the Sound page and persisted across reboots.
- **Dome serial link** — body sends `#PAHB` heartbeat to the dome at 1 Hz; receives and
  dispatches Marcduino commands from the dome (sounds, arm sequences, mood triggers).
  Dome link health shown on the dashboard.
- **Mood presets** — mood selection dispatches the audio command locally and forwards the
  dome lighting sequence over the serial link when the dome is connected. Last active mood
  is restored on reboot (audio component only; dome TX deferred until link is up).
- **Runtime log verbosity selector** — log level adjustable from the Setup page (Error only,
  Info, Debug) without reflashing; persisted across reboots; takes effect immediately.
- **Audio Module Status card** — Sound page shows confirmed live state from the module
  itself (link OK / no response, device type, play state, total tracks, current track);
  badge distinguishes unreachable module from idle module.
- **`/api/validation` endpoint** — single-call hardware validation snapshot covering drive,
  dome link, audio, and RC source health; useful for scripted validation sessions.
- **Failsafe timing telemetry** — trigger-to-zero latency fields added to `/api/status`
  (`failsafeTriggerMs`, `failsafeZeroMs`, `failsafeTriggerToZeroMs`, `failsafeTriggerSource`);
  makes the 200 ms safety requirement measurable rather than inferred.
- **Hardware validation script** — `tools/phase4_hw_check.py` runs a repeatable validation
  pass against a connected robot and writes machine-readable and human-readable reports.
- **Heap fragmentation monitoring** — `heapLargestBlock` tracked in `SafetyMonitorTask`
  and exposed in `/api/status` and `/api/health`; catches fragmentation before total-free
  looks healthy but WiFi cannot allocate a contiguous block.
- **`protoArtoo_prod` build environment** — AP-only WiFi mode (`PA_ENABLE_STA_WIFI=0`) for
  field deployment; saves ∼7 KB steady-state heap vs the default STA client mode.

### Changed
- **`/api/config` response is now grouped** — fields organized into `drive`, `rc`,
  `components`, `dome`, and `system` objects; all frontend pages updated accordingly.
- **Platform upgraded to IDF 5.5.2 / arduino-esp32 3.3.7** via
  `pioarduino/platform-espressif32@55.03.37`. Zero source changes required. Brings the
  IDF 5.x WiFi stack, improved power management, and 2026 toolchain improvements.
- **AsyncWebServer and AsyncTCP migrated to `ESP32Async/` maintained fork**
  (`ESPAsyncWebServer@3.10.3`, `AsyncTCP@3.4.10`). The `me-no-dev/` namespace has had no
  activity since 2022; the `ESP32Async` fork carries SSE memory-leak fixes, proper TCP
  buffer teardown, and `SSE_MAX_QUEUED_MESSAGES` queue-cap support.
- **WiFi mode is now a build-time decision** — AP and STA modes are mutually exclusive;
  the prior AP+STA simultaneous mode with AP-fallback logic is removed. `PA_ENABLE_STA_WIFI`
  selects the mode at compile time; a `#error` guard fires if credentials are missing.
- **Soft-UART TX is now interrupt-protected** — each byte is wrapped in a portMUX critical
  section, preventing FreeRTOS tick and WiFi radio ISRs from stretching bit periods and
  corrupting DY-SV5W frames.
- **Single SBUS receiver is now selectable at runtime** — in `single_sbus` mode, the active
  physical receiver (`sbus1` / `sbus2`) can be changed from the RC page and persists across
  reboots without modifying any existing channel mappings.
- **Dashboard live status now uses SSE** — target pages subscribe to the existing
  `/api/events` stream instead of polling; reconnects automatically on visibility restore.
- **Shared web API client** — all pages use `PAApi` (`data/web_api.js`) for consistent
  error handling, timeout behaviour, and operator feedback.
- **Shared page chrome** — navigation and footer rendered from a single source (`shell.js`,
  `footer.js`); one edit point for all nav changes.
- **Footer shows both firmware and web bundle versions** — helps detect stale UI after a
  firmware flash or filesystem update.
- **`/api/config` JSON serialized via ArduinoJson** — eliminates the hand-sized 2 KB static
  buffer (`configJsonBuf`) that had caused production truncation twice; 2 KB BSS reclaimed.
- **RC diagnostics JSON also migrated to ArduinoJson** — eliminates `rcBuf[3072]` and the
  associated `WebEvents` task stack pressure that caused SSE-connect crashes.

### Fixed
- **DY-SV5W audio regression** — driver was sending wrong play opcode (`0x06` next-track
  instead of `0x07` play-specified), a self-cancelling stop sequence, and dual-dialect frame
  wrapping producing four frames per command. All corrected with source-verified opcodes from
  the DYPlayer/BetterDuino reference. `switchDrive` now uses the device the module reports,
  not a hardcoded SD/TF value (FLASH modules were silently breaking status queries on boot).
- **Volume not persisted** — `POST /api/audio action=volume` now writes to NVS; the Sound
  page slider hydrates from the persisted value on load instead of defaulting to 20.
- **WebEvents task crash under SSE load** — task stack was sized against an idle measurement
  taken before any SSE client connected, giving a false low-water mark. Corrected to 4096
  bytes; under-load measurement confirmed 1320 words free.
- **AsyncTCP stack canary faults on `/api/config`** — `CONFIG_ASYNC_TCP_STACK_SIZE=8192`
  in build flags prevents stack overflow when the config handler runs under connection load.
- **RC mode and channel changes now apply without reboot** — `RcInputTask` re-reads config
  each loop; mode and channel assignment changes from the web interface take effect
  immediately.
- **AP security in AP-only builds** — AP mode now requires `PA_AP_PASSWORD` from
  `src/secrets.h` with a minimum-length compile-time assertion; open AP removed.
- **Status payload overflow** — `buildStatusJson` returns failure on overflow and emits
  explicit fallback JSON instead of silently truncating; buffer sizes increased to 3072 bytes.
- **Optimistic mode/mood state** — mode and mood controls no longer flip state before API
  confirmation; on failure the UI rolls back to the last confirmed state.
- **`board_upload.before_reset` placement** — moved from `upload_flags` to `board_upload.*`
  in `platformio.ini`; esptool 4.x ignores `--before` when appended after `write_flash`.

### Hardware Validated
- DY-SV5W audio output on Artoo PCB: named sounds, random chatter, play/stop/volume,
  module status card, volume NVS persistence confirmed
- Dome ESC GPIO25 PWM path: spins correctly at 50/70/90% command from web API (unloaded and
  loaded ring tests); ESC baseline parameters confirmed and locked

### Deferred
- Dome serial link end-to-end (requires slip-ring connection to AstroPixelsPlus board)
- Audio edge cases: S2 enable/disable toggle, boot mood restore, mood interval NVS persistence
- Drive, hoverboard, and SBUS failsafe validation (hoverboard not yet connected)
- RC mapping and upload UX with physical transmitter/receiver

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
