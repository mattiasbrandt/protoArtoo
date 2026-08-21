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
- Droid parts catalog: `docs/droid-parts.yaml`
- Operator-facing copy voice: `docs/ui-copy-voice.md`
- REST API contracts: `docs/api.md`
- Core error-signalling conventions: `docs/core-error-signalling.md`
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

### Wrap-up trigger

When the user says "wrap up", "wrap this up", "wrap-up", or an obvious close-out
variant, treat it as a request to close the active work loop. Interpret trailing
context as instructions for the close-out style. For example, "wrap up for the
night, it's late" means the user cannot continue now, so produce a resumable
handoff with the exact next step for tomorrow. Do not ask the user to restate the
checklist.

If the trailing context indicates the work is paused mid-implementation because of
context-window pressure, compaction risk, or a fresh-session handoff need, suggest
or invoke the community `handoff` skill after the normal wrap-up bookkeeping.
Reserve `handoff` for volatile in-progress context that is not yet captured well
by issues, commits, docs, or MemPalace. The `handoff` skill should create a
temporary prompt/document for the next session and should reference, not
duplicate, durable artifacts such as issues, commits, plans, ADRs, and MemPalace
entries.

Wrap-up means:

1. Inspect current state:
  - `git status --short --branch`
  - relevant recent diff/log context
  - any verification results already produced in the session
2. Update the running record:
  - if an active GitHub issue or task issue is known, add a concise status comment
    with completed work, commit refs, verification evidence, and remaining risk
  - otherwise update `docs/status.md` or `docs/goal.md` only when the public
    planning baseline actually changed
  - keep `tasks/**` as local-only internal context
  - make the next session restartable from a durable source of truth: either a
    formal task record such as a GitHub issue, or MemPalace status/search entries
3. Preserve memory:
  - file significant decisions, outcomes, and unresolved constraints in MemPalace
    under the project wing (`hall_events`, `hall_discoveries`, or `hall_facts`)
  - write the relevant specialist diary entry when a specialist domain was used
4. Leave the repo understandable:
  - commit completed verified slices that are ready to keep
  - do not commit `tasks/**`
  - report uncommitted or unverified work explicitly
5. Final response:
  - include verification status using the approved labels
  - list updated issue/docs/memory targets
  - list remaining next steps or blockers
  - when the user indicates they are stopping for the night/day or must leave,
    include the first command or file to open when resuming
  - when context-window pressure or mid-task interruption is the reason for
    pausing, include the temporary handoff path or explicitly suggest using the
    `handoff` skill
  - state the intended resume source, for example GitHub issue number, docs path,
    MemPalace room/query, or temporary handoff path

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
- Each subagent MUST commit its completed slice (per-file `git add`, commit scope format) before
  the next subagent runs in the same tree - see Change Hygiene > Incremental slice workflow.
  Never start a second subagent over another subagent's uncommitted work; it has been silently
  wiped in the past. Verify the tree (git status/log + grep the new symbol), not the summary.

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

**Dual-target builds — always go through `make`, never bare `pio`.** The
artoo-esp32 and ESP32-P4 targets pin different pioarduino platform versions
that require different versions of the same packages, so each gets its own
`PLATFORMIO_CORE_DIR`. The Makefile selects it from `BUILD_ENV`:

```bash
make build                          # artoo-esp32  -> $(PIO_CORE_DIR_ARTOO)
make build BUILD_ENV=protoArtoo_p4  # ESP32-P4     -> $(PIO_CORE_DIR_P4)
```

A bare `pio run -e protoArtoo_p4` uses the default core dir and swaps the
artoo-esp32 Arduino core in place — slow every time, and corrupting if any two
builds overlap. Run **one PlatformIO build at a time**, machine-wide: the core
dirs are separate, but a single core dir is still not safe against concurrent
package installs.

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

**Worker slice gate:** after committing a slice, workers must run
`python3 tools/slice_verify.py --base <base-ref>` (plus any `--fenced` pathspecs
and the `--mutations` patches the coordinator's brief specifies) and paste its
full block verbatim into the issue status comment — including the opening
provenance lines (blob hashes of both verifier scripts, HEAD sha, DIRTY
marker, merge-base, diff size, toolchain). The coordinator re-runs the same
command on the branch and compares blocks, provenance lines included;
divergence marks the slice unverified. The gate runs the native suite, the web
suite (`make test-web` semantics: process exit code and `# cancelled` decide,
never the TAP `# fail` line), the mutation stage, the firmware build, drift
and diff checks, and fails on deleted test files or a shrinking test total. A
flat test total over production changes also fails: `data/` changes must grow
the web suite and `src/`/`include/` changes the native suite. A diff touching
web production JS must carry mutation patches via `--mutations` (files or a
directory of `*.patch`); the gate runs `tools/mutation_verify.py` itself,
requires every patch KILLED and every changed JS file hit by at least one
patch, and folds the verdict into the block — a passing block implies killed
mutations. Its diff checks compare merge-base..HEAD, so commit before running
it; build-stamped working-tree changes to `data/*version.json` are ignored by
design. Editing `tools/slice_verify.py` or `tools/mutation_verify.py` inside
a slice fails the gate; `--expect-gate-edit` is for coordinator-sanctioned
gate work only. The waiver flags — `--expect-gate-edit`,
`--expect-no-new-tests`, `--expect-no-mutations` — are granted by the
coordinator in the brief, never self-granted by a worker, and every ACK is
visible in the block.

**Evidence rules:** pasted evidence must carry process exit codes, never a
hand-summarised pass/fail line. A test that fails only by hanging or timing out
is not acceptable coverage — the failure must be an assertion. Mutation
evidence is the gate block itself (`slice_verify.py --mutations` runs the
mutation stage and fails unless every mutation is KILLED by assertion);
standalone `python3 tools/mutation_verify.py <patches>` runs are for authoring
patches, and a hand-written mutation table is never evidence. If a stated
requirement of the
ticket cannot be met, stop and report on the issue — shipping the remainder
while reporting the ticket complete is a reject, not a partial pass. Never edit
a shared test harness to accommodate the code under test; fix the code or
report the conflict.

**CI gate:** `verification` workflow runs on every PR to `main` — do not bypass it.

**JSON API test rule:** JSON API response builders that are new or materially
changed should have high-signal native coverage for the typical case and serialized
size budget. Avoid low-value tests that only mirror implementation details.

**Web behavior test rule:** before writing or reviewing tests under
`test/test_web/`, read `test/test_web/README.md`. A suite is vacuous until a
production-code mutation turns it red; bug-fix coverage additionally requires
red against the pre-fix commit. Green alone is a claim, not evidence.

Classify verification status explicitly — use only these labels:

- `software-verified` — build/tests/checks passed; no upload implied
- `controller-upload-verified` — flashed to ESP32 controller; smoke checks passed
- `full-hardware-verified` — verified on integrated droid hardware
- `partial` — some evidence exists; controller or hardware checks still open
- `full-hardware-required` — physical hardware needed before closure

Never use "bench verified" or "bench-tested" — too ambiguous. Public docs use plain
evidence phrases ("Automated checks are passing", "Tested on an ESP32 controller").


## Web/UI Copy Rules

- Before writing or editing ANY operator-facing text (UI labels, help text,
  hints, toasts, errors, wizard steps, release notes), apply the maker-voice
  rules in `docs/ui-copy-voice.md`. Gate: a maker with no firmware knowledge
  must get every sentence on first read.
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

Through the `v1.0.0` release, protoArtoo used a phase-oriented branch model —
one long-lived `phase/vX.Y.Z` branch carrying all work for a development
phase, merged to `main` (PM-approved, non-fast-forward) at phase completion.
That model is retired as of `v1.0.0`. It's documented here only as history;
it does not describe current practice.

**Current model (post-`v1.0.0`): short-lived feature branches.**

| Branch | Purpose |
|---|---|
| `main` | Stable, released state. Tagged at every version. Substantive changes land only via a Mattias-approved PR merge; docs, chore, and agent-facing maintenance commits may land directly (CONTEXT.md "Post-Release Main Workflow"). |
| `feature/<what>` | New user-facing functionality. |
| `fix/<what>` | Bug fixes. |
| `refactor/<what>` | Code restructuring, no behavior change. |
| `chore/<what>` | Build config, deps, CI, tooling. |
| `docs/<what>` | Documentation only. |
| `epic/<name>` | Rare multi-ticket effort tracked by one epic/wayfinder issue (e.g. `epic/esp32-p4` for #182). See "Epic branches" below. |
| `exp/<topic>` | Disposable experiments. Never merged directly to `main`. |

Multiple independent feature branches may coexist — the phase model's "one
active phase at a time" constraint has no replacement; it simply no longer
applies. Branch off `main`, PR back into `main`, delete after merge.

**Epic branches** (`epic/<name>`): the documented exception for an effort too
large for one short-lived branch — a multi-ticket epic tracked by a single
epic/wayfinder issue. Sub-issue work is committed directly to the epic branch
as verified slices under the normal commit discipline; sub-issues do **not**
get per-ticket PRs. The branch reaches `main` through one normal
Mattias-approved PR at epic closure (`Closes #<epic>`); the PM may
additionally call an intermediate milestone merge PR (for example after a
go/no-go gate). This is not a phase revival: an epic branch is scoped to one
epic issue, coexists with ordinary feature branches, and carries no
release-versioning role of its own.

**Release cadence:** post-`v1.0.0` releases are small and frequent — `main`
is tagged per semver as merged work accrues (fixes → patch, features →
minor). No umbrella releases accumulating months of features; an epic merge
typically warrants a minor release on its own.

**Merge strategy:** "Rebase and merge" for this ongoing feature-branch
workflow. This is distinct from the one-time `phase/v1.0.0` -> `main` merge,
which used "Create a merge commit" specifically to satisfy the
non-fast-forward invariant for that historical branch (squash flattens
history; rebase replays as fast-forward — neither preserves it). Don't reuse
that mechanism for ongoing feature-branch PRs.

**Approval:** Mattias approves every PR merge to `main`, unconditionally — no
agent self-merge under any circumstance, regardless of how low-risk a change
appears.

### Push and remote policy

The gate binds on **what receives the push**, not on the act of pushing. An
earlier blanket "nothing is pushed until the operator says so" was unfollowable
in practice — `gh issue develop`, which the issue workflow requires to create the
native Development link, is itself a remote write — and a rule that the
prescribed workflow breaks on its first step gets read loosely, which is how an
agent eventually rationalizes a push to a shared branch.

| Action | Gate |
|---|---|
| Push a `feature/`, `fix/`, `refactor/`, `chore/`, `docs/`, `test/`, or `exp/` branch you own | Free — no approval |
| `--force-with-lease` onto that same branch after a rebase | Free — no approval |
| `gh issue develop` branch creation | Free — no approval |
| Push to any shared integration branch (`phase/*` is retired history) | Explicit operator approval |
| Open **or** merge a PR | Explicit operator approval |
| Push a tag | Explicit operator approval |
| Push to `main`, or self-merge any PR | Never, unconditionally |

Rationale for the free tier: no workflow triggers on a branch push. The
`verification` and `dependency-review` workflows run on `pull_request` into
`phase/v*`/`main`; `verification` and `version-sync` additionally run on push to
`main`; `release` runs on `v*.*.*` tags. A worker branch push therefore consumes
no CI and publishes no project state.

Pushing worker branches is **encouraged, not merely tolerated**. This repo's
documented worst failure mode is losing work to a parallel agent operating over
another agent's tree (see "Incremental slice workflow"). Commits that exist only
in one local worktree have no backup; origin is the backup. Push each branch once
its slice commits land.

Note the repo is public: work pushed to a branch is world-visible before review.
That is an accepted trade against losing it.

### Commit scope format (required)

All commits use plain [Conventional Commits](https://www.conventionalcommits.org)
format:

```
type(scope): summary
```

- `type`: `feat`, `fix`, `docs`, `refactor`, `chore`, `test`, `style`, `perf`
- `scope`: from CONTRIBUTING.md's domain-scope table (`drive`, `sbus`,
  `failsafe`, `dome`, `audio`, `servo`, `web`, `nvs`, `wifi`, `hw`, `plan`,
  `test`, `ci`)

The phase-era `type(phase:vX.Y.Z/T<NN>): summary` token (and its
`/slice:a` variant) is dropped entirely going forward — it will still appear
throughout this repo's git history through `v1.0.0` as a historical artifact.
Per-slice tracking now lives entirely in the Incremental slice workflow's
per-issue checklist comment (below), not in the commit scope.

### Incremental slice workflow (required)

Work non-trivial tasks as thin slices, and treat each slice as durable before moving on.
Prior work was lost when a later agent run executed over an uncommitted slice in the same tree
(it git-restored "stray" files). Every slice MUST complete this loop, in order:

1. Implement the slice.
2. Verify it (targeted test/build/Playwright; live-device smoke for device-visible UI).
   Do not proceed on a red slice.
3. Commit immediately - explicit per-file `git add` of the changed files, using the commit
   scope format above. Never leave a completed slice uncommitted.
4. Confirm the tree, not the summary: `git status` clean for the slice, `git log -1` shows the
   commit, and the new symbol is grepped on disk.
5. Record the commit ref on the tracking issue (a checklist comment:
   `Slice N - <short SHA> <subject> - verified <how>`). The issue is the running ledger of
   completed slices.
6. Only then start the next slice.

Hard rules: one slice = one (or few) atomic commit(s); never run a second implementation pass
over uncommitted work; never `git checkout`/`restore`/`stash`/`clean` another slice's files.
If a slice must be split, commit the safe part first.

### Version JSON workflow

`data/fw-version.json` and `data/fs-version.json` are tracked, 100% generated web
assets — they are never hand-edited, on any branch. A PreToolUse hook
(`.claude/hooks/version_json_edit_guard.py`) hard-blocks Edit/Write/MultiEdit against
either file to make this structurally impossible for an agent to violate.

Two independent regeneration paths, both authoritative for their scope:

- **Local dev builds**: `tools/extract_version.py` runs on every `pio run` and
  writes both files from `git describe --tags --always --long --match 'v[0-9]*'`
  for the current local `HEAD`, plus an explicit dirty check that ignores the two
  stamp files themselves (so a build never marks itself dirty). The `--match`
  filter keeps non-release tags (`safepoint/*` and similar markers) out of the
  version string. Non-`main` branches get a `+<branch>` semver
  build-metadata suffix (slashes replaced with dashes); `main` builds stay bare.
  You do not need to stage or commit these files as part of feature-branch work —
  just build normally.
- **`main` is the single source of truth**: a CI step on every push to `main`
  regenerates both files from the actual merge commit and bot-commits them back.
  This replaces any per-commit judgment call about whether a build "belongs to
  the active slice" — there is no such judgment call anymore.

Files downloaded via GitHub's "Download ZIP" (no `.git` directory) still carry a
correct, CI-regenerated value from the last `main` push, instead of falling back
to a meaningless default.

### Invariants

- Never commit directly to `main`
- Ad-hoc incidental improvements are permitted commits without plan amendment; formal scope additions require PM approval
- Mattias approves every PR merge to `main`, unconditionally — no agent self-merge regardless of change size or risk
- Merge method for ongoing feature-branch PRs is "Rebase and merge" (see "Branch model" above for the one-time historical exception)
- Pushing your own work branch is free; pushing to a shared branch, opening or merging a PR, and pushing tags each need explicit operator approval (see "Push and remote policy")

## Agent skills

### Issue tracker

Issues are tracked in GitHub Issues on `github.com/mattiasbrandt/protoArtoo`. See `docs/agents/issue-tracker.md`.

### Triage labels

All five canonical triage labels use their default names. See `docs/agents/triage-labels.md`.

### Domain docs

Single-context layout — one `CONTEXT.md` and `docs/adr/` at the repo root. See `docs/agents/domain.md`.
