# AGENTS.md

Agent-focused instructions for the `protoArtoo` firmware repository.

This file is the model-agnostic canonical instruction source for mixed-agent
workflows.

## Project Context

- Project: `protoArtoo` (ESP32 body controller firmware for MK4 astromech droids)
- Build system: PlatformIO (`protoArtoo` target + `native` tests)
- Companion dome firmware: `mattiasbrandt/AstroPixelsPlus`

## Source of Truth Files

- Public planning baseline (commit/push allowed): `docs/status.md`, `docs/goal.md`
- Project language decisions (commit/push allowed): `CONTEXT.md`
- Internal planning/agent working docs (local only, never commit/push): `tasks/**`
- Phase 3 RC diagnostics/mapping contract: `tasks/rc_diagnostics_contract.md`
- Hardware truth: `docs/pin_map.md`, `include/config.h`
- Shared state truth: `include/robot_state.h`
- Action registry: `docs/action-registry.yaml`
- REST API contracts: `docs/api.md`
- Crash/coredump + heap troubleshooting procedures: `docs/troubleshooting.md`
- SBUS protocol truth: `docs/spec-sheets/sbus-protocol.md`
- ESP-IDF5 RMT driver truth: `docs/spec-sheets/rmt-esp32-idf5.md`
- HOTRC profile truth: `docs/spec-sheets/hotrc-sbus-spec.md`
- Long-term project memory: MemPalace MCP (`mempalace_search`, `mempalace_status`)
- Espressif MCP servers (repo-level): `espressif-documentation`, `esp-component-registry`
- Project custom subagent definitions: `.claude/agents/*.md`
- Project reusable skills: `.claude/skills/*/SKILL.md`

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
  constraints discovered during a session.
- Save at natural checkpoints: after resolving a non-obvious bug, after a design
  decision that has cross-task implications, or when the user explicitly confirms
  a conclusion worth keeping.
- Do NOT save routine implementation steps, intermediate errors, or content that
  is already captured verbatim.
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

Documentation publication rule:
- Only `docs/goal.md` and `docs/status.md` are public planning docs.
- `docs/goal.md` and `docs/status.md` must not contain agent/tool/model wording
  (for example: "agent", "LLM", "model", "Copilot", "Claude").
- Any `tasks/*.md` file is internal operator/agent working context and must remain
  untracked in git.

## Action Registry

All robot actions, API endpoints, SSE events, and RC-bindable targets are defined in
`docs/action-registry.yaml`. That file is the source of truth for naming and the
authoritative reference when adding, renaming, or cross-referencing any action.

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
- Route to `backend-coder` when prompts mention bounded firmware/backend implementation: ESP32/Arduino code, PlatformIO build/upload plumbing, API handlers, FreeRTOS tasks, `RobotState`/queues, config/NVS, action registry wiring, SBUS/RC, dome/audio backend control, or safety/failsafe logic.
- Route to `frontend-designer` when prompts mention anything visible to operators: UI/UX, dashboard layout, copy, controls/selectors, visual design, accessibility, responsive/tablet behavior, or Playwright/web validation.
- Route to `performance-optimizer` when prompts mention performance, heap, stack, OOM, PANIC, coredump, crash, reset reason, profiler, failed allocations, fragmentation, OTA failures, sluggish HTTP, SSE pressure, CHIRP catalog memory, Learned-sequence buffers, or runtime memory evidence.
- Route to `code-reviewer` after implementation for independent safety/security/architecture/data-flow/maintainability/stale-comment/regression review.
- Route to `Explore` for read-only discovery, codebase search, and pattern reconnaissance before edits.

## Flashing and Monitoring

### Quick reference

Running bare `make` launches the interactive deploy wizard (`tools/deploy.py`) — the
recommended path for day-to-day flashing. It guides build environment selection,
port/IP entry, and runs the upload gate automatically.

Named targets for scripted or power-user use (all run `pio test -e native` first
unless noted):

```bash
make flash                          # USB firmware — default build, /dev/ttyUSB0
make ota                            # OTA firmware — default build, artoo.local
make uploadfs                       # OTA filesystem (LittleFS web UI) — no test gate

make flash-chirp                    # USB — CHIRP audio build
make ota-chirp                      # OTA — CHIRP audio build
make ota-mp3trigger                 # OTA — MP3 Trigger build

make flash-monitor                  # USB flash + capture boot log until "init complete"
make flash-chirp-monitor            # USB CHIRP flash + boot log capture
```

**Overrides** (CLI or `user.mk` for persistence):
```bash
make ota OTA_IP=10.0.0.22           # use IP when mDNS is unavailable
make flash UPLOAD_PORT=/dev/ttyUSB1
make ota BUILD_ENV=protoArtoo_chirp # override build env
```

- ArduinoOTA starts automatically on Core 0 when WiFi comes up (port 3232).
- Do **not** use `192.168.4.1` (AP IP) as `OTA_IP` by default.
- **Seated controller: OTA + HTTP only.** USB flash/read fails in-PCB (GPIO15/SBUS
  strapping); unseat the ESP32 for USB flashing. Collect crash/heap evidence over
  HTTP (`/api/coredump`, `/api/profiler`, `/api/logs`). Full procedures incl.
  coredump decode: `docs/troubleshooting.md`.

## Verification and Reporting

Use risk-based verification. Automated tests are evidence, not the goal. Prefer
high-signal checks for behavior that can create unsafe motion, hard-to-debug field
failures, API contract drift, or repeated regressions. Maintenance cost matters:
do not add or demand brittle tests that mostly encode implementation details.

Default completion evidence:
- Firmware behavior changes: `make build`, then `make test` when safety,
  protocol parsing, shared state, config persistence, JSON/API contracts, or prior
  regression paths are touched.
- UI/web asset changes: relevant local server + Playwright/manual interaction
  evidence is usually higher value than native tests unless API builders changed.
- Docs, comments, copy, agent definitions, and low-risk cleanup: inspection and
  targeted checks are acceptable; do not require PlatformIO tests unless behavior
  changed.

Add `make check-action-drift` when action registry, RC tokens, or
`ACTION_REGISTRY[]` changed. `make test` is still enforced automatically by
`make flash` and `make ota`.

`make check` (cppcheck) is slow — CI runs it on every PR automatically. Only run it
locally when specifically investigating a static analysis issue.

**CI gate:** `verification` workflow runs on every PR to `main` — do not bypass it.

**JSON API test rule:** JSON API response builders that are new or materially
changed should have high-signal native coverage for the typical case and serialized
size budget. Avoid low-value tests that only mirror implementation details.

Classify verification status explicitly — use only these labels:

- `software-verified` — build/tests/checks passed; no upload implied
- `controller-upload-verified` — flashed to ESP32 controller; smoke checks passed
- `full-hardware-verified` — verified on integrated droid hardware
- `partial` — some evidence exists; controller or hardware checks still open
- `full-hardware-required` — physical hardware needed before closure

Never use "bench verified" or "bench-tested" — too ambiguous. Public docs use plain
evidence phrases ("Automated checks are passing", "Tested on an ESP32 controller").


## Web/UI Copy Rules

- Avoid internal planning language in operator-facing text
- Keep copy focused on device state, controls, and diagnostics
- Prefer symbols and related emoji over verbose text labels where meaning is clear at a glance — reduces visual clutter and aids quick scanning

## Change Hygiene

- Write proper, quality code — not quick fixes. If the right solution is larger, do it right.
- Clean up after yourself: remove orphaned imports, variables, and functions your changes leave unused.
  Do not remove pre-existing dead code unless asked.
- Preserve useful code comments. Remove or update them only when factually incorrect,
  stale after a code change, or duplicated by clearer nearby documentation.
- LSP/lint fixes must be resolved by changing the flagged code, not by deleting nearby comments.


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

## Agent skills

### Issue tracker

Issues are tracked in GitHub Issues on `github.com/mattiasbrandt/protoArtoo`. See `docs/agents/issue-tracker.md`.

### Triage labels

All five canonical triage labels use their default names. See `docs/agents/triage-labels.md`.

### Domain docs

Single-context layout — one `CONTEXT.md` and `docs/adr/` at the repo root. See `docs/agents/domain.md`.
