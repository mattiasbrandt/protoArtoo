# Phase 2 - Web Server + OTA (v0.2.0)

Status: In progress
Baseline: Released `v0.1.0`
Scope: Expand Phase 1 AP + estop/status server into a proper web control and OTA phase

## Starting Point from v0.1.0

- Bench flash verification confirmed the controller MCU as `ESP32-D0WD-V3`
  (revision 3) with a 40 MHz crystal
- WiFi AP mode exists and uses SSID `protoArtoo`
- `src/web/api_estop.cpp` already exposes:
  - `POST /api/estop`
  - `POST /api/estop/clear`
  - `GET /api/status`
- `webServerInit()` is currently a Phase 1 minimal server entry point
- Phase 1 docs explicitly say drive/config/OTA/SSE endpoints are not implemented yet

## Bench-Tested Phase 2 Progress

- PlatformIO flashing and LittleFS uploads are working on the bench ESP32 D1 Mini
- USB debug serial output is now readable enough for boot health and web bring-up troubleshooting
- AP + WiFi client mode is working with credentials from `src/secrets.h`
- LittleFS-served pages are live: `index.html`, `setup.html`, `wifi.html`, `firmware.html`, `serial.html`
- Live controller APIs are working on the bench board:
  - `GET /api/status`
  - `GET /api/config`
  - `GET /api/wifi`
  - `GET /api/serial`
  - `GET /api/health`
  - `GET /api/logs`
  - `GET /api/events`
  - `POST /api/estop`
  - `POST /api/estop/clear`
  - `POST /api/drive`
  - `POST /api/web-control/enable`
  - `POST /api/web-control/disable`
  - `POST /api/manual-command`
  - `POST /api/reboot`
  - `POST /upload/firmware`
- NVS-backed config persistence is working and survives reboot
- OTA upload path is working on the bench controller
- Dashboard now includes health, log, manual command, heap, and WiFi-quality surfaces in a bench-tested form

## Current Automated Coverage

- `pio run -e protoArtoo`
- `pio test -e native`
- `pio check`
- Native tests currently cover:
  - SBUS mapping math
  - hoverboard frame packing/checksum
  - web/API helper parsing and manual-command resolution
  - log buffer behavior
  - config / wifi / serial / health JSON formatter helpers

## Browser and UI Verification Workflow

- At the current bench stage, browser/UI verification should be performed against the flashed controller over AP or LAN.
- Use both of these when possible:
  - real browser checks on the live board
  - Playwright/browser automation checks when the local runtime is available
- Minimum browser verification for Phase 2 surfaces:
  - Home page loads and hydrates status
  - Home page health dashboard, log console, manual command, and footer details hydrate correctly
  - Setup page loads current config and saves changes
  - WiFi page shows current AP/WiFi-client status
  - Firmware page exposes reboot/OTA controls
  - Serial page reflects traced hardware assignments and live transport status
- Browser/UI verification should explicitly record whether the result is:
  - bench-tested
  - partial
  - full-hardware-required

- Operator-facing pages should avoid internal phase/planning language and stay focused on device state, controls, and useful diagnostics.

## Phase 2 Goal

Provide safe phone-browser control and field update support without regressing Phase 1 drive/failsafe behavior.

Milestone:
- Droid can be controlled from phone browser over the `protoArtoo` AP
- Web server serves a real UI from LittleFS
- OTA is working

## Constraints Carried Forward

- `docs/pin_map.md` and `include/config.h` remain the hardware ground truth
- Bench upload uses PlatformIO board `wemos_d1_mini32` and manual boot entry
  with `upload_speed = 115200`
- Hoverboard drive stays on `UART1` (`S1`, GPIO 16/17)
- Dome serial remains reserved for `UART2` (`S3`, GPIO 33/34)
- SBUS remains the in-repo custom RMT implementation
- No dynamic allocation after `setup()` in real-time/safety-critical paths
- `estop` stays latching and must never auto-clear
- Async web handlers must not touch hardware directly; they should update shared state or queues only
- Web/API authentication is a lower-priority hardening item, not a current Phase 2
  blocker, because the droid normally operates on a closed local LAN

## Phase 2 Tasks

- [x] 2.1 Define Phase 2 web architecture
  - Decide how AP/STA, AsyncWebServer, SSE, and OTA are initialised
  - Keep boot ordering compatible with the existing safety boot path

- [x] 2.2 Add filesystem-backed web assets
  - Create LittleFS layout for the Phase 2 UI
  - Serve static assets from the firmware instead of JSON-only endpoints

- [x] 2.3 Expand status and control API
  - Keep existing estop/status endpoints stable
  - Add browser-control endpoints needed for safe drive interaction
  - Add config/status surfaces needed by the Phase 2 UI

- [x] 2.4 Add SSE/live state updates
  - Stream status changes for browser dashboard updates
  - Avoid polling-only dashboard design where SSE is a better fit

- [x] 2.5 Add OTA support
  - Enable OTA update flow consistent with `partitions_ota.csv`
  - Ensure OTA startup does not interfere with safety-critical tasks

- [x] 2.6 Add web/manual verification coverage
  - Verify AP startup, page load, estop flow, status flow, and OTA entry path
  - Keep `pio run -e protoArtoo`, `pio test -e native`, and `pio check` green

## Remaining Phase 2 Focus

- Expand the Setup/WiFi/Firmware/Serial surfaces from the current bench-tested baseline into more complete operator flows
- Keep browser UX tightening focused on real operator value and avoid project-phase/internal planning language in the UI
- Add any additional web-side capabilities only if they remain honest about bench-stage vs full-hardware-required scope

## Deferred Hardening

- Authentication and broader network exposure controls are intentionally deferred
  until a later phase unless the deployment model changes beyond the current
  closed-LAN assumption

## Forward Impact on Later Phases

- Phase 3 should consume the existing web setup/config/status framework instead of introducing parallel configuration paths
- Phase 4 should integrate audio/body-link visibility into the existing dashboard/log surfaces where possible
- Future reviews and validation must continue to classify findings as `bench-tested`, `partial`, or `full-hardware-required`

## Exit Criteria

- [x] Browser UI loads from the ESP32 over the `protoArtoo` AP and LAN client mode
- [x] Existing Phase 1 estop/status behavior still works
- [x] Live dashboard updates are available
- [x] OTA update path is functional on the bench controller
- [x] Build, native tests, and static analysis pass
- [ ] `CHANGELOG.md` receives a real `0.2.0` entry only when Phase 2 is released
