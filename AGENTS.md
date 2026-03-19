# AGENTS.md

Agent-focused instructions for the `protoArtoo` firmware repository.

This file is the model-agnostic canonical instruction source for mixed-agent
workflows.

## Instruction Precedence

1. Explicit user request in chat
2. Nearest `AGENTS.md` in directory tree
3. Tool-specific adapter files (`.claude/CLAUDE.md`, `.github/copilot-instructions.md`)
4. Other project docs

If instructions conflict, follow the highest-precedence source and document the
assumption briefly.

## Project Context

- Project: `protoArtoo` (ESP32 body controller firmware for MK4 astromech droids)
- Build system: PlatformIO (`protoArtoo` target + `native` tests)
- Companion dome firmware: `mattiasbrandt/AstroPixelsPlus`
- Safety-critical domain: drive/failsafe changes require conservative handling

## Source of Truth Files

- Planning baseline: `docs/status.md`, `tasks/phase0-tasks.md`,
  `tasks/phase1-tasks.md`, `tasks/phase2-tasks.md`, `tasks/phase3-tasks.md`,
  `tasks/phase4-tasks.md`, `tasks/phase5-tasks.md`, `docs/goal.md`
- Phase 3 RC diagnostics/mapping contract: `tasks/rc_diagnostics_contract.md`
- Hardware truth: `docs/pin_map.md`, `include/config.h`
- Shared state truth: `include/robot_state.h`

Do not treat `.sisyphus/plans/` as authoritative when equivalent task docs exist
in `tasks/`.

## Safety Invariants (Never Violate)

1. Zero-frame rule: Drive frames continue at 50 Hz; send zero frames when stopped.
2. `SPEED_LIMIT_MAX` cap is always enforced in DriveTask before transmit.
3. SBUS-safe boot default (`sbusSignalLost=true`) is preserved.
4. Estop is latching and must not auto-clear.
5. TWDT reset recovery sets estop on boot.
6. No `portMAX_DELAY` in real-time control loops.

## Runtime Contracts

- RC input modes: `standard_pwm`, `single_sbus`, `dual_sbus`
- Default intent parity:
  - `single_sbus` + `standard_pwm`: CH1 speed, CH2 steer, CH3 dome,
    CH4 ARM1 trigger, CH5 ARM2 trigger, CH6 AUX/sound
  - `dual_sbus`: RX1 CH1/CH2/CH8 for drive + speed-limit, RX2 CH1 dome;
    remaining RX2 channels configurable
- RC bindings/calibration must be NVS-backed and editable from webpage
- RC mapping UX must remain modern/responsive: source badges, inline validation,
  live mapped preview, explicit apply/save feedback

## Architecture Guardrails

- FreeRTOS split: Core 1 real-time, Core 0 network/web/OTA
- Cross-core `RobotState` access must use `portMUX` critical sections
- Queue sends in real-time paths should be non-blocking (`timeout 0`)
- No dynamic allocation in task loops after `setup()`
- Web handlers never touch hardware directly; they validate + route through
  queues/state update paths
- Extend existing setup/config/status/dashboard surfaces; do not create parallel
  configuration/debug pages for same domain

## Execution Model (Non-Blocking)

For non-trivial tasks:

1. Build a task packet:
  - objective
  - scope boundaries (in/out)
  - acceptance criteria
  - reference files/sections
2. Implement thin slices and verify before expanding scope
3. Trust-but-verify:
  - do not rely only on implementation claims
  - inspect changed files
  - run relevant checks
4. Iterate until acceptance + verification both pass

Parallelization rules:

- Parallelize only independent tasks with no file/contract conflicts
- Run sequentially when tasks share modules, APIs, or safety-critical state

Clarification policy:

- Ask concise multi-choice questions only when ambiguity materially affects
  correctness/safety/design
- For minor details, state assumptions and proceed

## Flashing and Monitoring

### USB flash (ESP32 unseated — auto-reset works, no button needed)
```bash
pio run -e protoArtoo --target upload --upload-port /dev/ttyUSB0
```
- DTR/RTS auto-reset works reliably when the ESP32 is unseated — no BOOT button press
  required. The `protoArtoo` env uses `board_upload.before_reset = default_reset`.
- **esptool flag placement:** Always use `board_upload.before_reset = <value>` in
  platformio.ini, never `upload_flags = --before <value>`. PlatformIO appends
  `upload_flags` after the `write_flash` subcommand; `--before` there is ignored
  by esptool 4.x. Full write-up in `tasks/lessons.md`.

> ⚠️ **Seated-PCB USB upload fails.** GPIO 15 (`PIN_SBUS1_RX`) is a strapping pin.
> When the ESP32 is seated in the Artoo Controller PCB with a SBUS receiver attached,
> the receiver can prevent the bootloader from entering download mode — USB upload
> silently fails or times out. Unseat the ESP32 → USB flash → reseat.
> Full write-up: `tasks/lessons.md`, `docs/pin_map.md`.

### OTA — standard in-PCB flash path (preferred)
```bash
pio run -e protoArtoo_ota --target upload    # firmware
pio run -e protoArtoo_ota --target uploadfs  # filesystem (LittleFS web UI)
```
- `protoArtoo_ota` defaults to `upload_port = 10.0.0.22` (STA client IP).
- Override with `--upload-port <ip>`. Do **not** use `192.168.4.1` (AP IP) by default.
- ArduinoOTA starts automatically on Core 0 when WiFi comes up (port 3232).

### Web UI OTA
- Firmware: `POST /upload/firmware` — filesystem: `POST /upload/filesystem`
- Both available on the Firmware page (`/firmware.html`).

## Verification and Reporting

Before marking complete (as applicable):

1. `pio run -e protoArtoo`
2. `pio test -e native`
3. `pio check`
4. Hardware checks for hardware-touching behavior

Always classify verification status explicitly:

- `bench-tested`
- `partial`
- `full-hardware-required`

If hardware validation is deferred, record blockers and closure checklist in
planning/status docs.

## Web/UI Copy Rules

- Avoid internal planning language in operator-facing text
- Keep copy focused on device state, controls, and diagnostics

## Change Hygiene

- Use smallest safe change that solves the task
- Preserve existing patterns unless a change is required for correctness/safety
- Avoid broad refactors during targeted fixes
- Preserve useful code comments by default. Do not remove inline/function/file
  comments just to reduce verbosity.
- Remove/update comments only when they are factually incorrect, stale after a
  code change, or duplicated by clearer nearby documentation.
- LSP/lint fixes (for example `forEach` callback return style or symbol
  redeclaration warnings) must be resolved by changing the flagged code, not by
  deleting nearby comments.

## Git Workflow

All development from Phase v0.4.0 onward follows the phase-branch model
defined in `tasks/dev-workflow-change-spec.md`. The canonical rules:

### Branch model

| Branch | Purpose |
|---|---|
| `main` | Stable, released state only. Updated at phase completion via PM-approved non-fast-forward merge. |
| `phase/vX.Y.Z` | All work for the active phase (e.g. `phase/v0.4.0`). One active phase at a time. |
| `exp/<topic>` | Disposable experiments. Never merged directly to `main`. |

`dev`, `feature/<phase>-<what>`, and `fix/<what>` branches are retired as of Phase v0.4.0.

### Commit scope format (required)

All commits within a phase branch must use:

```
type(phase:vX.Y.Z/T<NN>): summary
```

- `T<NN>` is the zero-padded task number from the phase plan (`T01`, `T02`, ...)
- `T00` = phase scaffolding or admin commits not tied to a specific task
- `type` follows [Conventional Commits](https://www.conventionalcommits.org): `feat`, `fix`, `docs`, `refactor`, `chore`, `test`, `style`, `perf`
- Slice notation: `type(phase:vX.Y.Z/T<NN>/slice:a): summary`

Examples:
```
feat(phase:v0.4.0/T01): implement AudioDriver interface and DY-SV5W driver
fix(phase:v0.4.0/T02): correct dome UART baud rate assignment
docs(phase:v0.4.0/T00): update AGENTS.md with phase-branch workflow
```

### Invariants

- Never commit directly to `main`
- Always identify the active phase branch before making changes
- One active phase at a time — do not begin a new phase until the current one merges to `main`
- Phase branch merges to `main` require PM approval; merge method is non-fast-forward
- Ad-hoc incidental improvements are permitted commits without plan amendment; formal scope additions require PM approval
