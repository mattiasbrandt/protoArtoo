# Phase 3 - Servo Arms + Dome Motor (v0.3.0)

Status: Pending Phase 2 completion and release
Baseline: Phase 2 web/config/status foundation already exists in a bench-tested form
Goal: Utility arm servos and dome rotation motor control
Milestone: Arms via web + RC; dome ESC responds

## Starting Point from Phases 0-2

- Hardware ground truth remains `docs/pin_map.md` and `include/config.h`
- Current controller baseline already includes:
  - web UI pages from LittleFS
  - config API with NVS persistence
  - readable USB debug logging
  - OTA/reboot path on the bench controller
  - dashboard surfaces for health/log/manual-command
- Later servo/dome work must extend those existing surfaces rather than introducing parallel setup pages

## Bench Stage vs Full Hardware

- Bench-stage controller validation can still prove API shape, UI wiring, and config persistence
- Full-hardware-required validation begins as soon as servos and dome ESC are involved
- Do not present servo or dome-motor API success on the standalone ESP32 as proof of physical movement

## Phase 3 Tasks

- [ ] 3.1 Implement ServoTask for utility arms
  - LEDC PWM for ARM1 and ARM2
  - open/close positions from config/NVS

- [ ] 3.2 Implement dome motor ESC control
  - LEDC PWM for dome ESC output
  - controlled from web and later RC integration

- [ ] 3.3 Extend config surfaces for servo/dome settings
  - reuse existing `/api/config` and Setup page structure
  - avoid creating parallel config endpoints/pages

- [ ] 3.4 Add web control surface for arms and dome motor
  - use current Setup/Home page foundation
  - expose only real, testable controls

- [ ] 3.5 Add RC integration for arm/dome behavior
  - align with real Phase 0 pin map and current SBUS model

- [ ] 3.6 Add verification coverage
  - keep `pio run -e protoArtoo`, `pio test -e native`, and `pio check` green
  - bench-test API/UI shape first
  - then perform full-hardware-required motion validation

## Exit Criteria

- [ ] Arm and dome-motor APIs exist and are bench-tested for shape and persistence
- [ ] Servo and dome motor configuration is integrated into existing web/config surfaces
- [ ] Real arm motion is validated on hardware
- [ ] Real dome ESC response is validated on hardware
- [ ] Build, native tests, and static analysis pass
- [ ] `CHANGELOG.md` receives a real `0.3.0` entry only when Phase 3 is released
