# Contributing to protoArtoo

Thank you for your interest in contributing. protoArtoo is a safety-critical
firmware for a 20 kg wheeled robot. Quality, clarity, and traceability are not
optional here — they are part of the deliverable.

This document covers:
- Commit message format (Conventional Commits)
- Branch strategy
- Pull request checklist
- Code standards summary
- What never goes in a commit

For project-specific terms and abbreviations (for example `RobotState`, NVS,
Marcduino, SBUS, AP/STA), see `docs/terminology.md`.

## Getting started

For a new clone, run the setup wizard to configure your build environment:

```bash
make setup
```

This writes `user.mk` (gitignored) with your OTA IP, USB upload port, and audio
backend choice. Run `make help` to see all available targets.

If you do not have `make` installed, you can run PlatformIO commands directly —
the Makefile is a convenience wrapper only and is never required.

---

## Commit message format — Conventional Commits

Every commit in this repository follows
[Conventional Commits 1.0.0](https://www.conventionalcommits.org/en/v1.0.0/).

### Structure

```
<type>(<scope>): <short description>

[body — what and why, not how. Wrap at 72 characters.]

[footer(s) — BREAKING CHANGE:, Closes #n, Refs #n]
```

The short description must be:
- Imperative mood ("add", "fix", "remove" — not "added", "fixes", "removed")
- Lowercase
- No trailing period
- 72 characters or fewer including the `type(scope): ` prefix

### Types

| Type | Use for | Version effect |
|---|---|---|
| `feat` | New user-facing feature | MINOR bump |
| `fix` | Bug fix or safety correction | PATCH bump |
| `feat!` / `fix!` | Feature/fix with breaking change | MAJOR bump |
| `refactor` | Code restructure, no behaviour change | none |
| `test` | Add or fix tests | none |
| `docs` | Documentation, comments, README, plan | none |
| `chore` | Build config, deps, `.gitignore`, CI | none |
| `style` | Formatting only (clang-format pass) | none |
| `perf` | Performance improvement, no behaviour change | none |

A breaking change is anything that requires changes to hardware wiring, breaks
the REST API, renames an NVS key, or changes the heartbeat protocol before the
dome fork is updated. It must be declared with either:
- `!` after the type: `feat!(drive): rename speed field to spd_raw`
- Or a `BREAKING CHANGE:` footer in the body

### Scopes

Always include one:

| Scope | Covers |
|---|---|
| `drive` | DriveTask, hoverboard UART, failsafe |
| `sbus` | SBUSInputTask, both receivers, CH8 dial |
| `failsafe` | Any of the 5 safety layers, estop, TWDT |
| `dome` | DomeLinkTask, bidirectional serial, heartbeat |
| `audio` | AudioTask, AudioDriver, DY-SV5W, track mapping |
| `servo` | ServoTask, arm servos, LEDC PWM |
| `web` | Web server, REST API, web UI, SSE |
| `nvs` | NVS config, Preferences, boot sequence |
| `wifi` | AP/STA, OTA, network manager |
| `hw` | Hardware config, pin map, PCB trace results |
| `plan` | Firmware plan document, architecture decisions |
| `test` | Test files, mocks, test infrastructure |
| `ci` | platformio.ini, static analysis, build flags |

### Examples

```
feat(drive): add CH8 speed-limit dial with linear scaling

CH8 on receiver #1 scales drive output linearly from 0 to SPEED_LIMIT_MAX.
CH8 at minimum completely locks drive. Gives the operator a physical
confidence dial for tight spaces.

NVS key ch8_mode_lock (default false) enables optional binary mode-lock.
See Section 6.7 of firmware plan for full spec.
```

```
fix(failsafe): set sbusSignalLost=true as boot default

Previously defaulted to false, allowing web API drive commands before any
SBUS frame was confirmed. This is a safety regression — the droid could be
driven before the RC system is confirmed present.

Boot default is now true; clears on first valid SBUS frame only.
```

```
feat(hw)!: confirm UART1 hoverboard pins from PCB trace — update config.h

BREAKING CHANGE: PIN_HOVERBOARD_TX and PIN_HOVERBOARD_RX now have confirmed
values from Phase 0 PCB trace. TBD placeholder values are removed.
Any build relying on the old TBD compile-error guards will fail differently.

PIN_HOVERBOARD_TX = 17
PIN_HOVERBOARD_RX = 16
```

```
docs(plan): mark dome fork implementation complete (Section 3)

Dome-side body link protocol fully implemented in
mattiasbrandt/AstroPixelsPlus. 95%+ plan alignment confirmed.
Two minor acceptable deviations documented in Section 3.5.
All Phase 0 dome items closed.
```

### What is NOT acceptable

```
fix stuff
update
wip
asdf
.
fixed the bug
made it work
```

These will not be accepted in a pull request.

---

## Branch strategy

protoArtoo uses a phase-oriented branch model. See `tasks/dev-workflow-change-spec.md`
for the full specification.

```
main
└── phase/v1.0.0        ← active phase branch (all work lands here)
    ├── (feat commits)
    └── (fix/docs/chore commits)

exp/<topic>            ← disposable experiments (never merged to main)
```

### Branch naming

| Branch | Purpose |
|---|---|
| `main` | Stable, released state only. Tagged at every version. |
| `phase/vX.Y.Z` | All work for the active development phase. |
| `exp/<topic>` | Exploratory or speculative work. Start here if you\'re not sure where your contribution fits. |

`dev`, `feature/<phase>-<what>`, and `fix/<what>` branches are retired.

### Rules for contributors

- Fork the repository and create your branch from the active `phase/vX.Y.Z` branch, not from `main`.
- `main` is protected and only updated at phase completion via a PM-approved merge — pull requests directly targeting `main` will not be accepted during active development.
- If you are unsure which phase branch is active, check `docs/status.md` or open an issue to ask.
- After your work is merged, your branch will be deleted.

### Commit scope for external contributors

All commits must use the Conventional Commits format. The scope should include
the active phase. If your contribution is not tied to a specific phase plan task,
use `T00`:

```
type(phase:vX.Y.Z/T00): summary
```

Examples:
```
fix(phase:v1.0.0/T00): correct typo in README
docs(phase:v1.0.0/T00): add DFPlayer Mini to supported audio module list
feat(phase:v1.0.0/T00): add example config for single-SBUS wiring
```

If your contribution directly addresses a specific task in the phase plan,
you are welcome to use the task number (e.g. `T03`).

---

## Pull request checklist

Before opening a PR from your branch to the active `phase/vX.Y.Z` branch, confirm all items:

**Builds**
- [ ] `pio run -e protoArtoo` completes with no errors and no warnings
  (build flags include `-Werror` — warnings are errors)

**Tests**
- [ ] `pio test -e native` — all native tests pass
- [ ] `pio test -e protoArtoo` — all on-device tests pass (if hardware available)

Regression troubleshooting policy (parser/protocol iterate-fix loops):
- Precedence: this policy overrides any implied expectation to author/update tests on every micro-change.
- Each iteration must still run a fast relevant verification step (targeted existing test, focused build, or runtime probe).
- Add/update tests at confirmed-fix commit boundaries, for safety-critical behavior changes, and when a larger feature/task slice is completed.
- For larger feature implementations, tests must be kept up to date before marking work complete.

SBUS/RMT spec compliance gate:
- For any change touching SBUS parsing, SBUS framing/flags/timing acceptance, or ESP-IDF5 RMT behavior, review these docs before coding:
  - `docs/spec-sheets/rmt-esp32-idf5.md`
  - `docs/spec-sheets/sbus-protocol.md`
  - `docs/spec-sheets/hotrc-sbus-spec.md` (when HOTRC profile behavior is in scope)
- Resolution order: RMT driver-level -> SBUS protocol-level -> HOTRC profile-level.
- If a required value is unresolved, mark it `UNKNOWN` and stop dependent implementation changes instead of guessing.
- Do not reopen previously rejected parser mechanisms unless new contradictory telemetry is captured.

**Static analysis**
- [ ] `pio check` — no high or medium severity findings
- [ ] Any `pio check` suppression or analysis-only build flag in `platformio.ini` has an inline comment explaining rationale and scope (no broad/global suppressions unless unavoidable)

**Code style**
- [ ] `clang-format -i src/**/*.cpp src/**/*.h` applied
- [ ] All new functions have a header comment (see Section 8.1 of firmware plan)
- [ ] No inline `//TODO` items that disable or bypass safety logic

**Safety**
- [ ] If the change affects any failsafe layer, the body of the commit explains
  which layer and how the behaviour changes
- [ ] No `portMAX_DELAY` in real-time tasks (use timeout 0 for queue sends)
- [ ] No `new` / `malloc` inside task loops
- [ ] New `constrain()` calls on any new external inputs (SBUS, web API, serial)

**Git hygiene**
- [ ] Commit messages follow Conventional Commits format
- [ ] No credentials in any file
- [ ] No `config.h` TBD placeholder values replaced with guesses
- [ ] `CHANGELOG.md` updated in the `[Unreleased]` section

**Breaking changes**
- [ ] If this is a breaking change: `BREAKING CHANGE:` footer in commit body
- [ ] If NVS keys are added/renamed: documented in commit body and CHANGELOG

---

## Code standards summary

The full standard is in Section 8 of `artoo_firmware_replacement_plan.md`.
The most important rules:

**Comments** — every non-trivial function has a header comment explaining what
it does, why it exists, which task calls it, and a reference link if applicable.
Inline block comments use the three-level indent pattern (what / why / detail).

**FreeRTOS** — always measure stack high-water mark in development. All reads
and writes to shared `RobotState` fields use `portMUX`. Queue sends from
real-time tasks use timeout 0 (non-blocking). Core assignment is documented
in each `xTaskCreatePinnedToCore()` call.

**Memory** — no dynamic allocation (`new` / `malloc`) inside task loops. All
buffers are statically sized. Free heap monitored in `SafetyMonitorTask`.

**Defensive** — bounds-check every buffer write. `static_assert` for
compile-time invariants. `constrain()` on all external inputs before use.

**Logging** — TAG-prefixed format `[TAG] event — value`. Per-frame verbose
logging gated by `#ifdef PA_VERBOSE_<TASK>` build flag.

---

## What never goes in a commit

| What | Why |
|---|---|
| WiFi credentials | Use `src/secrets.h` (listed in `.gitignore`) |
| Real GPIO values replacing TBD in `config.h` as guesses | Must be confirmed by Phase 0 PCB trace — wrong values destroy hardware |
| Code that doesn't compile | Breaks every other contributor's build |
| `//TODO` that disables or bypasses safety logic | Safety is not optional |
| Commit messages like `"fix stuff"`, `"wip"`, `"."` | History is permanent — treat it accordingly |
| Binaries, `.elf`, `.bin`, `.map` | Generated by build; ignored via `.gitignore` |
| `.pio/` build cache | Large, generated, ignored via `.gitignore` |

---

## Versioning and releases

protoArtoo uses [Semantic Versioning 2.0.0](https://semver.org/).

Releases are tagged on `main` after a phase milestone is confirmed working:

```bash
git tag -a v0.1.0 -m "Phase 1 complete — drive via RC with full failsafe"
git push origin v0.1.0
```

A GitHub Release is created from the tag with the relevant `CHANGELOG.md`
section as the release body.

See `CHANGELOG.md` for the full release plan and milestone conditions.

---

## References

- [Conventional Commits 1.0.0](https://www.conventionalcommits.org/en/v1.0.0/)
- [Semantic Versioning 2.0.0](https://semver.org/)
- [Keep a Changelog 1.1.0](https://keepachangelog.com/en/1.1.0/)
- [PlatformIO Unit Testing](https://docs.platformio.org/en/latest/advanced/unit-testing/)
- [PlatformIO Static Analysis](https://docs.platformio.org/en/latest/advanced/static-code-analysis/)
- [ESP32 FreeRTOS SMP Guide](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-guides/freertos-smp.html)
