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
