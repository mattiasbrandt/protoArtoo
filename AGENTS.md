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

External profile tips (for example token-efficiency profiles) may be adopted only
when they do not conflict with AGENTS safety invariants, verification gates, or
interactive workflow requirements.

## Project Context

- Project: `protoArtoo` (ESP32 body controller firmware for MK4 astromech droids)
- Build system: PlatformIO (`protoArtoo` target + `native` tests)
- Companion dome firmware: `mattiasbrandt/AstroPixelsPlus`
- Safety-critical domain: drive/failsafe changes require conservative handling

## Source of Truth Files

- Public planning baseline (commit/push allowed): `docs/status.md`, `docs/goal.md`
- Internal planning/agent working docs (local only, never commit/push): `tasks/**`
- Phase 3 RC diagnostics/mapping contract: `tasks/rc_diagnostics_contract.md`
- Hardware truth: `docs/pin_map.md`, `include/config.h`
- Shared state truth: `include/robot_state.h`
- Action registry: `docs/action-registry.yaml`
- SBUS protocol truth: `docs/spec-sheets/sbus-protocol.md`
- ESP-IDF5 RMT driver truth: `docs/spec-sheets/rmt-esp32-idf5.md`
- HOTRC profile truth: `docs/spec-sheets/hotrc-sbus-spec.md`
- Long-term project memory: MemPalace MCP (`mempalace_search`, `mempalace_status`)
- Espressif MCP servers (repo-level): `espressif-documentation`, `esp-component-registry`
- Project custom subagent definitions: `.claude/agents/*.md`
- Project reusable skills: `.claude/skills/*/SKILL.md`

## Spec Compliance Gate (SBUS/RMT)

For any task that touches SBUS parsing, SBUS framing/flags/timing acceptance,
or ESP-IDF5 RMT driver behavior, agents MUST:

1. Read `docs/spec-sheets/sbus-protocol.md` and `docs/spec-sheets/rmt-esp32-idf5.md` before proposing code changes.
2. Treat those docs as implementation authority unless superseded by a higher-precedence source in this file.
3. Mark unresolved values as `UNKNOWN` and stop dependent implementation changes rather than guessing.
4. Avoid trial-and-error loops that retest previously rejected mechanisms unless new contradictory telemetry is captured.

If HOTRC-specific behavior is involved, consult `docs/spec-sheets/hotrc-sbus-spec.md` after the two protocol/driver docs above.

## Espressif MCP Protocol

Use Espressif MCP servers to speed up external ESP-IDF/component research, but keep
repo docs as implementation authority.

Available servers (repo-level):
- `espressif-documentation` (`https://mcp.espressif.com/docs`)
- `esp-component-registry` (`https://components.espressif.com/mcp`)

When to use which server:
- Use `espressif-documentation` for ESP-IDF API/driver behavior, version notes,
  and hardware capability checks.
- Use `esp-component-registry` for third-party/official component discovery,
  metadata, and example lookup.

Required usage pattern:
1. If a task depends on ESP-IDF behavior or component choices not already proven
   in repo docs, query the relevant Espressif MCP server before coding.
2. Prefer concise, targeted queries (API name, peripheral, chip variant,
   IDF major version).
3. Record unresolved values as `UNKNOWN`; do not guess.
4. For SBUS/RMT and project-specific contracts, repository docs remain primary:
   `docs/spec-sheets/sbus-protocol.md`, `docs/spec-sheets/rmt-esp32-idf5.md`, and `docs/spec-sheets/hotrc-sbus-spec.md`.

Not in scope:
- Do not use RainMaker MCP in this repository workflow unless explicitly requested.

## MemPalace Memory Protocol

MemPalace is installed and the `protoArtoo` project has been mined.
The MCP server exposes 19 tools. Agents with MCP access MUST follow this protocol.

### Session start

1. Call `mempalace_status` once at the beginning of every session.
   - This loads the memory protocol and AAAK spec into context.
   - It also reveals the palace structure (wings, rooms) for this project.
   - Do not skip this step — the memory protocol is self-taught from the response.

2. If the user's opening message references past decisions, prior conversations,
   or asks "why did we..." / "what was the reason for..." style questions:
   - Call `mempalace_search` with a targeted query before answering.
   - Prefer wing-scoped searches (`--wing protoArtoo` or equivalent wing name
     as revealed by `mempalace_status`) over unscoped global searches.

### During work

- **Search before speculating.** If a design decision, prior constraint, or
  architectural rationale is referenced but not in the current context, search
  before guessing: `mempalace_search "<topic>" --wing protoArtoo`.
- **Search before duplicating.** Before proposing a new approach that might
  conflict with past decisions, check for prior art:
  `mempalace_search "<approach>" --wing protoArtoo`.
- **Do not search for things already in context.** If the relevant file has been
  read or the fact was stated in this session, use the session context — do not
  re-query MemPalace for it.

### Saving memories

- Use `mempalace_add_drawer` to persist significant findings, decisions, or
  constraints discovered during a session that are not already captured in
  `tasks/lessons.md` or a task spec.
- Save at natural checkpoints: after resolving a non-obvious bug, after a design
  decision that has cross-task implications, or when the user explicitly confirms
  a conclusion worth keeping.
- Do NOT save routine implementation steps, intermediate errors, or content that
  is already captured verbatim in `tasks/phase5-tasks.md` or other task files.
- Filing format: use the wing for this project (from `mempalace_status`) and the
  most relevant room (hall) — `hall_facts` for locked decisions, `hall_discoveries`
  for breakthroughs, `hall_events` for notable sessions.

### Knowledge graph

- Use `mempalace_kg_query` when the question is about relationships between
  entities (e.g. which task introduced a constraint, which component owns a pin).
- Use `mempalace_kg_add` to record a new fact when a constraint is confirmed
  (e.g. "UART1 is owned by DriveTask post-T01").
- Use `mempalace_kg_timeline` to reconstruct the history of a component or
  decision when debugging a regression.

### Specialist agents

MemPalace supports specialist agents — each with its own wing and diary in the
palace. Agent definitions live in `~/.mempalace/agents/`; do not embed agent
role or focus definitions in `AGENTS.md` or `CLAUDE.md` — the palace is the
agent memory layer.

- Call `mempalace_list_agents` after `mempalace_status` to discover available
  specialist agents.
- If a relevant agent exists for the domain being worked on (e.g. a reviewer,
  architect, or ops agent), read its recent diary before starting:
  `mempalace_diary_read("<agent_name>", last_n=10)`.
- After significant domain work, write a concise AAAK diary entry:
  `mempalace_diary_write("<agent_name>", "<aaak_entry>")`.
- Diary entries are compressed in AAAK — keep them structured and entity-coded
  per the AAAK spec from `mempalace_status`.

### What NOT to do

- Do not store MemPalace facts or agent definitions in `AGENTS.md`, `CLAUDE.md`,
  or `copilot-instructions.md` — the palace is the memory layer.
- Do not call `mempalace_search` for facts that are clearly in the current
  session context — this wastes tokens and latency.
- Do not use MemPalace as a substitute for reading the actual source files;
  search results are closet pointers — always verify against the drawer (source).

Do not treat `.sisyphus/plans/` as authoritative when equivalent local task docs
exist in `tasks/`.

Documentation publication rule:
- Only `docs/goal.md` and `docs/status.md` are public planning docs.
- `docs/goal.md` and `docs/status.md` must not contain agent/tool/model wording
  (for example: "agent", "LLM", "model", "Copilot", "Claude").
- Any `tasks/*.md` file is internal operator/agent working context and must remain
  untracked in git.

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


## Action Registry

All robot actions, API endpoints, SSE events, and RC-bindable targets are defined in
`docs/action-registry.yaml`. That file is the source of truth for naming and the
authoritative reference when adding, renaming, or cross-referencing any action.

### Naming convention

  {domain}.{type}.{verb-noun}     e.g.  sound.action.play-track
                                        drive.action.move
                                        system.action.estop

  Domains:  drive | dome | sound | servo | system | rc
  Types:    action (command/intent) | status (observable state) |
            event (SSE) | config (NVS-backed setting)
  Format:   dots between structural segments; kebab-case within segments

### audio <-> sound naming boundary

  - Registry names, display labels, web UI copy: use "sound"
  - C++ symbols (enums, class names, file names): use "audio"
    Rationale: the Arduino/IDF library layer uses "audio"; renaming C++ symbols
    would diverge from the library vocabulary without user-visible benefit.

### C++ bindable-action enum

  `RobotActionId` in `include/rc_mapping.h` is the C++ form of the bindable-action
  subset of the registry (entries with cpp_file: include/rc_mapping.h).
  Values follow DOMAIN_ACTION_VERB_NOUN: DRIVE_ACTION_SPEED, SERVO_ACTION_ARM1_TOGGLE.

When adding a new bindable action:
1. Add the YAML entry to docs/action-registry.yaml.
2. Add the enum value to RobotActionId in include/rc_mapping.h.
3. Add the ActionEntry row to ACTION_REGISTRY[] in include/action_registry.h.
4. Add dispatch handling in src/tasks/rc_input.cpp.
5. Run `make check-action-drift` to verify YAML metadata, C++ token mapping,
   runtime registry rows, and the RC page fallback list remain aligned. This is
   a drift checker only; it must not generate or rewrite source files.

### Runtime registry

  `GET /api/actions` returns all bindable actions as JSON (id, name, display_name,
  domain, description, safety_critical). The RC mapping UI uses this to build
  action dropdowns dynamically — do not hardcode action lists in the frontend.

### What NOT to do

  - Do not introduce a new action, command type, or RC target without a registry entry.
  - Do not use ad-hoc names (soft_uart, manual_command, MC_*-style prefixes) for
    symbols that represent user-visible actions — derive the symbol from the registry name.
  - Do not rename NVS keys to match registry names — migration cost outweighs gain.
    Document the nvs_key mapping in the registry entry instead.


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

### Regression Debug Mode (Iterate, Commit, Fail Fast)

Use this mode by default for parser/protocol regressions (for example T19 SBUS).

1. Start from latest known-good baseline + one clear hypothesis
2. Apply the smallest change that tests only that hypothesis
3. Run the fastest relevant verification immediately (build/tests + targeted runtime probe)
4. If pass, commit the slice immediately with the observed effect
5. If fail, stop, capture evidence, and switch to the next single hypothesis

Rules:
- **Diff-first**: before forming any hypothesis or writing any code, run
  `git diff <last-known-good-sha> -- <relevant-files>` and read the diff.
  Do not theorize from memory — compare against a concrete baseline.
- One hypothesis per iteration; do not stack multiple parser changes in one slice.
- Prefer 10-30 minute loops over long speculative analysis rounds.
- After two failed iterations without new evidence, pause and require new telemetry before further edits.
- Keep each commit reversible and scoped to one mechanism (timing, alignment, calibration, telemetry, etc.).
- Preserve safety invariants and do not bypass watchdog/failsafe gates as a workaround.
- Do not retry previously rejected mechanisms unless new telemetry directly contradicts the original rejection evidence.
- Maintain an explicit rejected-approaches list in the active task notes; treat that list as blocked scope for future micro-iterations.

Mandatory test-effort policy for this mode:
- Precedence: while Regression Debug Mode is active, this policy overrides generic guidance that might otherwise imply test authoring/updates on every small code change.
- Do not require new/updated tests for every micro-iteration during active regression troubleshooting.
- Each micro-iteration must still run the fastest relevant verification check (targeted existing test, focused build, or runtime probe).
- Add or update tests when a fix is confirmed and committed, when a larger feature/task slice is completed, or when safety-critical behavior changes.
- For larger feature implementations, tests and coverage updates are required before marking the task complete.
- If two iterations fail without new telemetry, stop expanding tests and gather fresh runtime evidence first.

Parallelization rules:

- Parallelize only independent tasks with no file/contract conflicts
- Run sequentially when tasks share modules, APIs, or safety-critical state

Clarification policy:

- Ask concise multi-choice questions only when ambiguity materially affects
  correctness/safety/design
- If multiple valid interpretations exist, present them — do not pick silently
- For minor details, state assumptions and proceed
- **Always use the tool's structured ask/questions mechanism** (e.g. `vscode_askQuestions`,
  `ask_followup_question`, or equivalent) when posing choices to the user — never
  print lettered/numbered option lists ("A:", "B:", "1.", "2.") in plain text and
  expect a typed reply. Structured questions surface as native UI pickers; plain-text
  lists force the user to type manually and break the interaction contract.

### Subagent Orchestration Policy

Role-specific behavior belongs in project subagent files under `.claude/agents/`.
Keep AGENTS.md as the canonical policy/invariant source and avoid duplicating
full policy blocks across individual agent definitions.

- Default mode for non-trivial work is planner-orchestrator:
  - Main model owns deep reasoning, architecture, risk checks, and a detailed TODO packet.
  - Subagents execute scoped tasks from that packet.
- Do not collapse delegated work back to main-model solo execution unless the user explicitly asks.
- Delegate by default when work is parallelizable, read-heavy, repetitive, or review-oriented.
- Subagent tasks must be narrowly scoped and deliverable-driven:
  - one objective per subagent,
  - concrete inputs and expected outputs,
  - explicit boundaries (files/contracts the subagent may touch),
  - required verification artifact (test/build/output summary).
- Use bounded batches to reduce usage-cap risk: prefer a small number of focused subagents per wave.
- If a subagent times out, is cancelled, or hits usage cap:
  - do not reinterpret this as task invalidation,
  - preserve completed results and checkpoint remaining TODOs,
  - resume delegation in a new wave instead of shifting all remaining work to the main model,
  - ask the user before changing strategy from delegated to solo execution.

Delegation cues (auto-routing hints):
- Route to `backend-coder` when prompts mention firmware, ESP32 tasks, PlatformIO, API handlers, safety/failsafe logic, or upload/build/test tooling.
- Route to `frontend-designer` when prompts mention UI/UX, layout/copy, operator workflow, responsive behavior, or Playwright/web validation.
- Route to `Explore` for read-only discovery, codebase search, and pattern reconnaissance before edits.

## Flashing and Monitoring

### Quick reference

### OTA — standard in-PCB flash path (preferred)
```bash
pio run -e protoArtoo_ota --target upload    # firmware
pio run -e protoArtoo_ota --target uploadfs  # filesystem (LittleFS web UI)
```
- `protoArtoo_ota` defaults to `upload_port = artoo.local` (mDNS host).
- Override with `--upload-port <host-or-ip>`. Do **not** use `192.168.4.1` (AP IP) by default.
- ArduinoOTA starts automatically on Core 0 when WiFi comes up (port 3232).

### USB flash
```bash
pio run -e protoArtoo --target upload --upload-port /dev/ttyUSB0
```

### Web UI OTA
- Firmware: `POST /upload/firmware` — filesystem: `POST /upload/filesystem`
- Both available on the Firmware page (`/firmware.html`).

### Build Commands (Quick Reference)
```bash
pio run -e protoArtoo           # compile firmware
pio run -e protoArtoo -t upload # USB flash (auto-reset, no button needed)
pio run -e protoArtoo_ota -t upload    # OTA firmware (in-PCB, artoo.local)
```

Detailed flashing troubleshooting and transport notes live in
`tasks/lessons.md` and `docs/pin_map.md`.

## Verification and Reporting

Before marking complete (as applicable):

1. `pio run -e protoArtoo`
2. `pio test -e native`
3. `pio check`
4. Hardware checks for hardware-touching behavior

**Upload gate:** `pio test -e native` MUST pass and all tests must be green before
issuing any `upload` or `uploadfs` command. A compile-only build does not qualify
as a pre-upload verification step.

**JSON response test rule:** Any function that builds a JSON API response — whether
via `snprintf` into a fixed buffer or via `JsonDocument` — MUST have a corresponding
native test covering the typical case and confirming the serialized output fits within
its intended size budget.

**Static analysis suppression rule:** Any `pio check` suppression or analysis-only build flag (for example in `platformio.ini`) MUST include an adjacent inline comment that explains why it is needed and how its scope is constrained. Prefer file-targeted suppressions over global suppressions.

Always classify verification status explicitly:

- `usb-standalone-verified`
- `partial`
- `full-hardware-required`

`usb-standalone-verified` means validation on an ESP32 connected over USB only,
without additional droid hardware/serial peripherals attached.

If hardware validation is deferred, record blockers and closure checklist in
planning/status docs.

## Web/UI Target Platform

The web UI targets **PC desktop first, tablet second.**
Small-screen mobile phone support is explicitly out of scope.

- Do NOT constrain layout, component size, whitespace, or information density to
  accommodate narrow phone viewports.
- Do NOT add mobile-first breakpoints, collapse menus for small screens, or
  reduce functionality to fit a phone form factor.
- Minimum supported viewport is a modern tablet in landscape (~1024 px wide).
  Any responsive behavior below that is unintentional and should not be defended.
- The operator uses this UI seated at a PC or with a tablet on a bench — design
  for that context: data-dense, direct controls, no large-tap-target padding.
- If a CSS framework or snippet introduces mobile-first defaults that compromise
  the desktop layout, override them — do not accept the mobile-first default.

## Web/UI Copy Rules

- Avoid internal planning language in operator-facing text
- Keep copy focused on device state, controls, and diagnostics
- Prefer symbols and related emoji over verbose text labels where meaning is clear at a glance — reduces visual clutter and aids quick scanning

## Change Hygiene

- Use smallest safe change that solves the task
- Every changed line should trace directly to the user's request; if it can't, remove it
- When your changes leave orphaned imports, variables, or functions unused, remove them;
  do not remove pre-existing dead code unless asked
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

### Invariants

- Never commit directly to `main`
- Always identify the active phase branch before making changes
- One active phase at a time — do not begin a new phase until the current one merges to `main`
- Phase branch merges to `main` require PM approval; merge method is non-fast-forward
- Ad-hoc incidental improvements are permitted commits without plan amendment; formal scope additions require PM approval
