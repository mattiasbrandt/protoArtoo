# AGENTS.md

Agent-focused instructions for the `protoArtoo` firmware repository.

This file is the model-agnostic canonical instruction source for mixed-agent
workflows. It outranks the Claude adapter (`.claude/CLAUDE.md`), the agent
definitions under `.claude/agents/`, and any orchestration text pasted into an
epic issue. Material an agent needs only on some paths lives under
`docs/agents/` and is reached from "Disclosed references" at the end.

## Project Context

- Project: `protoArtoo` (ESP32 body controller firmware for MK4 astromech droids)
- Build system: PlatformIO (`artoo_esp32` and `firebeetle2` firmware targets +
  `native` tests)
- Companion dome firmware: `mattiasbrandt/AstroPixelsPlus`

## Source of Truth Files

- Public planning baseline (commit/push allowed): `docs/status.md`, `docs/goal.md`.
  These two carry no agent/tool/model wording ("agent", "LLM", "model",
  "Copilot", "Claude").
- Project language decisions: `CONTEXT.md`; architecture decisions: `docs/adr/`
- Internal planning/agent working docs (local only, never commit/push): `tasks/**`
  — including the RC diagnostics/mapping contract `tasks/rc_diagnostics_contract.md`
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
- Long-term project memory: MemPalace — see "Memory" below
- Espressif MCP servers (repo-level): `espressif-documentation`, `esp-component-registry`
- Project custom subagent definitions: `.claude/agents/*.md`
- Project reusable skills: `.claude/skills/*/SKILL.md`

## Effort Policy (Non-Negotiable)

Binding on every agent in this repo — implementers, reviewers, coordinators and
subagents alike. Operator decision, 2026-08-22, after two worker slices were
rejected for defects that all traced to self-rationed effort.

**You have no token budget to manage, no efficiency target, and no deadline.**
Nobody measures your speed, your tool-call count, or your brevity. Finishing
fast with shallow work is a failure; taking four times as long and getting it
right is a success. Never ration your own effort.

Concrete forms this rationing takes. Each has caused a real defect here:

- **Read the source of truth, every time.** Open the header, the vendor `.cpp`,
  the library source, the live API response. Never hand-write a prototype, wire
  format, API contract or framing convention you could have read. Reading a file
  is never the expensive option, and a guess that happens to be right is luck,
  not engineering. This is the enforcement arm of the "no guessing" rule: if a
  value is unknown, read it or mark it `UNKNOWN` — never fill it with something
  plausible to keep moving.
- **Never swallow an error to keep moving.** `except Exception: pass`, an empty
  `catch`, an ignored return code. On this project those hide exactly the
  failures under test.
- **Never ship a thinner version of what was asked and report it done.** If a
  stated requirement is genuinely blocked, STOP and surface it. Delivering the
  remainder while reporting completion is an automatic reject.
- **Never trim a deliverable because the ticket is long.** The ticket is long
  because the work is real.
- **Never skip verification because it takes time.** The build, the real run,
  the re-read of the diff — the slow check is the one that catches the defect.

If you are about to write or think *"given token limits"*, *"to be efficient"*,
*"for brevity"*, *"for now"*, or *"a simplified version"*, treat that phrase as
a defect alarm: it means you are about to cut something that was asked for. Do
the full thing instead.

This does not license scope creep. Do the whole of what the ticket asks, and
nothing beyond it — depth within scope, never width past it.

## Memory (MemPalace)

MemPalace is **unavailable** (operator, 2026-08-07). Skip every MemPalace step —
no status call, no search, no diary, no CLI probing — and say so once in the
report. Durable memory until it returns is the issue tracker, commits,
`CONTEXT.md` and `docs/adr/`. The full protocol, for when the operator
re-enables it, is `docs/agents/mempalace.md`.

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
- Network is optional and never load-bearing (ADR 0032): a network fault never
  restarts the controller or degrades a droid function; `requestSystemRestart`
  keeps operator-initiated callers only
- Board Capability Gates and Build Feature Flags (ADR 0029) are always defined
  as 0 or 1 and tested with `#if`, declared in the `include/*.inc` manifests and
  annotated in the action registry

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

Wrap-up: when the user says "wrap up" or a close-out variant, follow
`docs/agents/wrap-up.md`.

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

Routing cues for the Claude subagents live in `.claude/CLAUDE.md`.

## Flashing and Monitoring

Bare `make` launches the interactive deploy wizard (`tools/deploy.py`); `make help`
lists every named target. `make flash` and `make ota` run `pio test -e native`
first; `make uploadfs` does not, and goes over USB (`UPLOAD_PORT`) for P4 envs,
which have no `_ota` env, OTA (`OTA_IP`) otherwise. Overrides go on the command
line or in `user.mk`: `OTA_IP`, `UPLOAD_PORT`, `BUILD_ENV`.

Six rules the Makefile cannot enforce for you:

- **Dual-target builds go through `make`, never bare `pio`.** The artoo-esp32 and
  ESP32-P4 targets pin different pioarduino platform versions, so each gets its
  own `PLATFORMIO_CORE_DIR`, selected from `BUILD_ENV`. A bare `pio run -e
  firebeetle2` swaps the artoo-esp32 Arduino core in place.
- **One PlatformIO build at a time, machine-wide.** Two runs in one worktree
  corrupt SCons state and return a plausible wrong answer; a single core dir is
  not safe against concurrent package installs either.
- **Seated controller: OTA + HTTP only.** USB flash/read fails in-PCB
  (GPIO15/SBUS strapping); unseat the ESP32 for USB. Crash/heap evidence comes
  over HTTP (`/api/coredump`, `/api/profiler`, `/api/logs`); procedures incl.
  coredump decode are in `docs/troubleshooting.md`.
- ArduinoOTA starts on Core 0 when WiFi comes up (port 3232). `192.168.4.1` (the
  AP IP) is never the default `OTA_IP`.
- **Only one of the four P4 environments is the firmware.** `tools/build_budgets.json`
  -> `platforms.esp32p4.envs` registers four. `firebeetle2` is the product image.
  `firebeetle2_bringup` and `firebeetle2_hosted_bench` set `build_src_filter = -<*>`
  and compile a single `bringup/` translation unit, so they contain **none** of
  `src/`; `firebeetle2_p4_rt_bench` is `+<*>` plus a harness TU, so it is the
  firmware *and extra code*, not the shipping image. Flashing any of the three
  while expecting the product yields a board that boots and serves almost nothing
  — which **presents as broken firmware rather than as the wrong target**, and that
  misreading is the expensive part. Corollary: a **new** P4 env must be added to
  that registry, because `Makefile:46` reads it to choose `PLATFORMIO_CORE_DIR`;
  an unregistered P4 env silently builds against the artoo-esp32 toolchain pool
  and fails looking like a code problem.
- **`src/secrets.h` does not follow worktrees.** It is gitignored, so a build in a
  fresh worktree **silently falls back to placeholder credentials** instead of
  failing. The resolution order is `-DBENCH_SSID`/`-DBENCH_PASS` -> `src/secrets.h`
  -> placeholder; the middle term simply vanishes in a new tree. This produced an
  uninformative `wifi=DISCONNECTED everConnected=false` on a bench characterisation
  that had been built against the placeholder SSID. Copy it in when you create a
  worktree, and never commit it.

## Verification and Reporting

Use risk-based verification. Automated tests are evidence, not the goal. Prefer
high-signal checks for behavior that can create unsafe motion, hard-to-debug field
failures, API contract drift, or repeated regressions. Maintenance cost matters:
do not add or demand brittle tests that mostly encode implementation details.

**Verification depth follows project stage.** During the PoC/MVP stage of an epic —
the platform is unproven, the go/no-go gate has not returned a verdict, the
hardware has not run the real firmware — test effort is near-zero priority. The
bar is: it builds, it runs on the board, here is the log. Test depth is bought
back deliberately at **epic closing** and during **robustness work**, when there
is something proven enough to be worth protecting.

Two consequences, both binding:

- **Acceptance criteria must not be test-shaped at PoC stage.** "Fourteen isolated
  compiler probes, each asserting its own diagnostic" is epic-closing work written
  into an implementation ticket. Write the outcome ("the guards exist and the build
  is red for every unassigned pin"), not the harness that proves the outcome.
- **Know which driver is which.** `tools/slice_verify.py` fails a `delta +0` when
  `src/`/`include/` changed (`zero_delta_ok`), so any production edit must add at
  least one native test. That floor is small and cheap — one test — and it exists
  because `delta == 0` is the state in which no mutation can be killed. It is *not*
  what produces large harnesses: `test/test_tools/` is not counted by it at all.
  Oversized test work comes from **acceptance criteria**, not from the gate. At PoC
  stage the coordinator may still grant `--expect-no-new-tests` epic-wide to remove
  even that floor, and revoke it per-ticket for robustness and closing work — but
  fixing the ticket wording is the change that actually matters.

Distinguish the guard from the harness that tests the guard. A `static_assert` that
makes a build fail is product and ships at any stage; a probe matrix proving that
`static_assert` fires is scaffolding and waits.

**Project-wide renames are classified, never blanket-substituted.** A term that
names two different things (`protoArtoo` is both the project and, historically,
a PlatformIO env) cannot be renamed with one substitution. Classify every hit as
*identifier* (rename), *project or product name* (keep), *history* (keep), or
*intentional fixture* (keep); then sweep the whole tree — `tools/`, `.claude/`,
`.github/`, `.gitignore` carry identifiers and operator-facing strings just as
`docs/` does. An audit whose path list omits them reports clean over the damage.

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

**Worker slice gate:** after committing a slice, run
`python3 tools/slice_verify.py --base <base-ref>` with the `--fenced` pathspecs,
`--mutations` patches and any waiver flag the coordinator's brief names, and paste
its full block verbatim — provenance lines included — into the issue status
comment. The coordinator re-runs the same command and compares blocks; divergence
marks the slice unverified. Waiver flags are coordinator-granted only. Contract,
evidence rules and the mutation stage: `docs/agents/slice-gate.md`.

**CI gate:** `verification` workflow runs on every PR to `main` — do not bypass it.

**Build-size budget rule (operator decision 2026-08-26):** the per-env flash
budget in `tools/build_budgets.json` is a tripwire, not a ceiling. When a change
trips it, the PR that trips it raises the budget with a written reason in the
budget's rationale field — a conscious decision, never a silent bump — and a
separate fixed hard ceiling below the partition size never moves. The artoo-esp32
image sits within ~31 KB of its budget, so an ordinary artoo feature can trip it,
not only spill from another target. The RAM budget moves by the same
explicit-decision rule.

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
- `full-hardware-required` — the remaining exposure is droid-only; recorded, not scheduled

**These labels describe evidence. They are not a gate** (operator decision,
2026-09-01). protoArtoo is a small open-source hobby project, and integrated-droid
confirmation is a desirable outcome, not a precondition for closing work.

- A ticket, a PR, or an epic **closes on the strongest evidence the available
  hardware can produce**. `full-hardware-required` names what is still unproven so
  the next person knows where to look; it never blocks closure on its own.
- **Do not write a droid-hardware verification ticket as a gate.** Whatever real
  droid operation turns up is ordinary follow-up work, filed when it appears. A
  fault in drive, dome, servos, sound, SBUS or battery sense shows itself in the
  first minutes of driving; a gate does not find it earlier, it only delays the
  close. #193 was exactly this ticket and was dropped `not planned`.
- **Where a risk is real but unmeasurable, document it, do not schedule it.** Put
  it in the file that owns the hardware truth — the `GPIO48-52` LDO watch item
  lives in `docs/pin_map.md`, "Known Issue: GPIO 48-52 LDO Rails (Unmeasured)" —
  with how the fault would present and how to diagnose it. A durable note beats a
  ticket nobody can action.
- **This does not relax the safety invariants.** The contracts in "Safety-Critical
  Rules" are still proven to the maximum the available hardware allows, and a gap
  is named explicitly rather than assumed away. Not being able to reach the droid
  is a reason to record a limit, never a reason to skip a check the bench can run.

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
- **Orchestration bookkeeping never ships in source.** A comment may cite a durable public
  reference — an issue (`#189`), an ADR, a spec sheet, a `file:line` — and CONTRIBUTING.md's
  "Code standards summary" asks for one where it helps. It may **not** carry the workflow step
  that produced the code: no slice numbers (`#189 slice 3`), no wave, attempt, or worker
  identifiers. A slice decomposition lives in one coordinator's dispatch plan, is not
  recoverable from the repo, and means nothing to whoever holds the file later — the same reason
  the phase-era `T<NN>` token was dropped from commit scopes, and why per-slice tracking belongs
  in the issue checklist comment. Write what the code does and why it is that way, then cite the
  issue. This binds commit subjects too: `Refs #189` yes, "slice 3" no.

### Branch model

Post-`v1.0.0`: short-lived branches off `main`, PR back into `main`, delete after
merge; independent branches coexist.

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

**Release cadence:** small and frequent — `main` is tagged per semver as merged
work accrues; an epic merge typically warrants a minor release.

**Merge strategy:** "Rebase and merge" for feature-branch PRs.

**Approval:** Mattias approves every PR merge to `main`, unconditionally — no
agent self-merge under any circumstance.

The retired phase-branch model and the one-time `phase/v1.0.0` merge-commit
exception are history, recorded in `CONTRIBUTING.md` "Branch strategy".

### Push and remote policy

The gate binds on **what receives the push**, not on the act of pushing.

| Action | Gate |
|---|---|
| Push a `feature/`, `fix/`, `refactor/`, `chore/`, `docs/`, `test/`, or `exp/` branch you own | Free — no approval |
| `--force-with-lease` onto that same branch after a rebase | Free — no approval |
| `gh issue develop` branch creation | Free — no approval |
| Push to any shared integration branch (`phase/*` is retired history) | Explicit operator approval |
| Open **or** merge a PR | Explicit operator approval |
| Push a tag | Explicit operator approval |
| Push to `main`, or self-merge any PR | Never, unconditionally |

Pushing worker branches is **encouraged**: commits that exist only in one local
worktree have no backup, and origin is the backup. The repo is public, so branch
work is world-visible before review — an accepted trade against losing it.
Rationale for the free tier: `CONTRIBUTING.md` "Pushing branches".

### Commit scope format (required)

Plain [Conventional Commits](https://www.conventionalcommits.org):

```
type(scope): summary
```

- `type`: `feat`, `fix`, `docs`, `refactor`, `chore`, `test`, `style`, `perf`
- `scope`: from CONTRIBUTING.md's domain-scope table

The phase-era `type(phase:vX.Y.Z/T<NN>)` token is history (`CONTRIBUTING.md`
"Commit scope"). Per-slice tracking lives in the issue checklist comment
(below), never in the scope.

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

`data/fw-version.json` and `data/fs-version.json` are generated, never
hand-edited, on any branch — a PreToolUse hook
(`.claude/hooks/version_json_edit_guard.py`) blocks Edit/Write against them.
Local builds regenerate both on every `pio run` (git-describe based; non-`main`
branches carry a `+<branch>` suffix) — build normally, stage nothing. `main` is
the single source of truth: CI regenerates and bot-commits both files on every
push to `main`. Mechanism and fallbacks: the docstring in
`tools/extract_version.py`.

### Invariants

- Never commit directly to `main`
- Ad-hoc incidental improvements are permitted commits without plan amendment; formal scope additions require PM approval
- Mattias approves every PR merge to `main`, unconditionally — no agent self-merge regardless of change size or risk
- Merge method for ongoing feature-branch PRs is "Rebase and merge" (the one-time `phase/v1.0.0` exception is history in CONTRIBUTING.md)
- Pushing your own work branch is free; pushing to a shared branch, opening or merging a PR, and pushing tags each need explicit operator approval (see "Push and remote policy")

## Disclosed references

- Issue tracker (GitHub, `gh` conventions): `docs/agents/issue-tracker.md`
- Triage labels: `docs/agents/triage-labels.md`
- Domain docs (`CONTEXT.md`, `docs/adr/`): `docs/agents/domain.md`
- MemPalace protocol, for when it is re-enabled: `docs/agents/mempalace.md`
- Wrap-up procedure: `docs/agents/wrap-up.md`
- Worker slice gate contract and evidence rules: `docs/agents/slice-gate.md`
