# Phase 4 - Audio + Full Dome Link (v0.4.0)

Status: Pending Phase 3 completion and validation
Baseline: Phase 2 web/status/config/dashboard stack already exists; Phase 3 should add physical actuation surfaces on top of it
Goal: Audio system and bidirectional dome communication
Milestone: Dome health shows body present; dome sequences trigger body audio + arms; body RC triggers dome animation

## Starting Point from Phases 0-3

- Hardware ground truth remains `docs/pin_map.md` and `include/config.h`
- Bidirectional dome/body architecture is already defined in `tasks/goal.md`
- Dome-side AstroPixelsPlus fork work is complete and should be treated as the integration partner baseline
- Current dashboard/log/status surfaces should be extended for body-link and audio visibility instead of replaced
- USB debug serial and bench-stage web validation are now good enough to support protocol bring-up work

## Bench Stage vs Full Hardware

- Bench stage can validate parser/dispatcher logic, API shape, and dashboard/log visibility
- Full-hardware-required validation begins for actual audio output, dome heartbeat, body/dome coordination, and slip-ring serial reliability
- Body-link status should be surfaced through the existing dashboard/status model, not a separate hidden channel

## Phase 4 Tasks

- [ ] 4.1 Implement AudioDriver interface and DY-SV5W default driver
- [ ] 4.2 Implement AudioTask with multi-source queue
- [ ] 4.3 Implement Marcduino TX path (body -> dome)
- [ ] 4.4 Implement Marcduino RX parser/dispatcher (dome -> body)
- [ ] 4.5 Implement DomeLinkTask with heartbeat handling
- [ ] 4.6 Extend dashboard/log/status surfaces for body-link and audio visibility
  - extend existing surfaces; do not create parallel setup/config/debug pages
- [ ] 4.7 Add audio and dome-link web APIs on top of existing web foundation
- [ ] 4.8 Add parser/track mapping tests and hardware validation plan

## Exit Criteria

- [ ] Body-link heartbeat/status is visible through the current dashboard/status system
- [ ] Dome-originated commands trigger body audio/arms correctly
- [ ] Audio works from all intended sources on hardware
- [ ] Build, native tests, and static analysis pass
- [ ] Full body/dome hardware validation is complete
- [ ] `CHANGELOG.md` receives a real `0.4.0` entry only when Phase 4 is released
