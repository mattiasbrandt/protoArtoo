# Phase 5 - Community Release (v1.0.0)

Status: Pending Phase 4 completion and validation
Baseline: Earlier phases should already have converged on one coherent web/config/status/dashboard architecture
Goal: Production-ready firmware for community use
Milestone: Community release - all tests passing, docs complete, no known safety issues

## Starting Point from Earlier Phases

- Hardware ground truth remains `docs/pin_map.md` and `include/config.h`
- Phase 2 established the web/config/OTA/dashboard baseline
- Phase 3 and 4 should extend that baseline, not fork it
- Release readiness must distinguish clearly between:
  - controller behavior proven at the bench stage
  - full hardware behavior proven on the complete Artoo build

## Phase 5 Focus

- [ ] Complete remaining operator-facing UX polish without reintroducing internal planning language into pages
- [ ] Keep final release surfaces unified; do not create parallel setup/config/debug flows
- [ ] Finish documentation and release notes across all public docs
- [ ] Complete final safety validation across the whole integrated hardware stack
- [ ] Close known regressions and operational rough edges found during Phases 2-4
- [ ] Prepare stable release packaging and community-facing release guidance

## Final Validation Requirements

- [ ] Build, native tests, and static analysis pass cleanly
- [ ] Browser UI and OTA flow are fully validated
- [ ] RC, hoverboard, servo, dome, audio, and body-link behavior are all validated on full hardware
- [ ] Safety invariants are verified end-to-end
- [ ] Documentation is complete and consistent with the released behavior

## Exit Criteria

- [ ] No known blocking safety or operational issues remain
- [ ] Full-hardware-required validation is complete across the integrated droid
- [ ] `CHANGELOG.md` receives a real `1.0.0` entry only when the stable release is actually cut
