# Project Status

| Component | Status |
|---|---|
| Firmware plan & architecture | Complete |
| Dome fork (mattiasbrandt/AstroPixelsPlus) | Complete - body link protocol implemented |
| Phase 0 - PCB trace & hardware research | Complete |
| Phase 1 - Drive + SBUS + failsafe | Complete - released as `v0.1.0` |
| Phase 2 - Web server + OTA | In progress - strong bench-tested baseline established |
| Phase 3 - Servos + dome motor | Pending Phase 2 |
| Phase 4 - Audio + full dome link | Pending Phase 3 |
| Phase 5 - Community release | Pending Phase 4 |

Current version: `v0.1.0`
See `CHANGELOG.md` for full release history.

Bench-verified controller module details:
- MCU: `ESP32-D0WD-V3` (revision 3)
- Crystal: 40 MHz

Current proven bench-tested Phase 2 capabilities:
- PlatformIO firmware flashing and LittleFS uploads are working on the ESP32 D1 Mini bench setup
- USB debug serial output is readable and leveled for boot health / web bring-up troubleshooting
- AP + WiFi client mode is working with credentials from `src/secrets.h`
- Home, Setup, WiFi, Firmware, and Serial pages are served from LittleFS
- `GET /api/status`, `GET /api/config`, `GET /api/wifi`, `GET /api/serial`, `GET /api/health`,
  `GET /api/logs`, `POST /api/estop`, `POST /api/estop/clear`, `POST /api/drive`,
  `POST /api/web-control/enable`, `POST /api/web-control/disable`, `POST /api/manual-command`,
  `POST /api/reboot`, and `POST /upload/firmware` are all implemented and bench-tested
- Config persistence through NVS is working and survives reboot
- Browser OTA path has been proven through API/backend and exercised from the UI
- Dashboard now includes health, movement status, live log console, manual command, heap status, and WiFi quality surfaces
- Playwright browser automation is working again and has been used on the live bench board

Planning notes for future phases:
- Phase 3 should build on the current web/config/OTA baseline rather than recreating setup surfaces
- Phase 3+ hardware validation must explicitly distinguish bench-stage controller behavior from full Artoo PCB/peripheral validation
- Authentication and broader network hardening remain intentionally deferred while the droid operates on a closed LAN
