# AI Coding Agent Guidelines - protoArtoo (Claude Adapter)

This is a thin adapter. Canonical cross-agent rules live in [AGENTS.md](../AGENTS.md).
If any rule conflicts, follow [AGENTS.md](../AGENTS.md) unless the user explicitly overrides.

## Purpose

- Keep Claude behavior aligned with repo safety and workflow contracts.
- Avoid duplication that increases token usage and instruction conflicts.
- Retain Claude-specific operating reminders not fully covered elsewhere.

## Required Canonical Sources

1. [AGENTS.md](../AGENTS.md) (authoritative policy)
2. [docs/goal.md](../docs/goal.md) and [docs/status.md](../docs/status.md) (public planning baseline)
3. [tasks/rc_diagnostics_contract.md](../tasks/rc_diagnostics_contract.md) when working RC diagnostics/mapping
4. [include/config.h](../include/config.h) and [docs/pin_map.md](../docs/pin_map.md) for hardware truth
5. [docs/action-registry.yaml](../docs/action-registry.yaml) — canonical action/event registry; naming convention and audio/sound boundary

## Safety-Critical Rules (Must Always Hold)

Follow [AGENTS.md](../AGENTS.md) for the full canonical list. Do not violate:

- Drive zero-frame continuity at 50 Hz
- DriveTask speed cap enforcement
- SBUS-safe boot default
- Latching estop behavior
- TWDT reset -> estop on boot
- No blocking real-time control loops

## Claude-Specific Additions

### Token-Efficient and Safe Context Use

- Avoid redundant re-reads of unchanged files.
- Re-read when freshness risk exists: after edits, formatters/codegen, failed patch apply, user edits, or before line-specific patches/explanations that depend on exact current text.
- Prefer targeted reads over broad scans.
- Read the target file before modifying it; never edit blind.
- Prefer simple, maintainable solutions by default; add complexity when it clearly improves correctness, safety, or maintainability.
- Prefer structured outputs (bullets/tables/JSON when requested) over narrative-heavy prose.
- Lead with outcome/findings, then supporting details.

### Output, Review, and Debugging Discipline

- Return code/results first; keep explanations brief and only for non-obvious logic.
- Avoid speculative features and out-of-scope suggestions unless explicitly requested.
- For reviews, state the issue, where it is, and the fix; avoid extra commentary.
- For debugging, do not speculate before reading relevant code.
- If root cause is unclear, state uncertainty and the next verification step; do not guess.
- Keep prose concise, but include brief rationale and comments where they materially improve readability or safe maintenance.
- Use ASCII-only punctuation in generated content unless non-ASCII is required by existing file content.
- For quantitative analysis, include units and source/derivation context; do not present unsupported numbers.
- Distinguish observed facts from inference, and label low confidence explicitly.

### Regression Troubleshooting Mode (T19-style)

When debugging parser/protocol regressions, default to iterate-commit-fail-fast:

- Use one explicit hypothesis per loop.
- Make one minimal code change to test that hypothesis.
- Run fast verification immediately after the change.
- If verification passes, commit the slice immediately.
- If verification fails, stop quickly, record evidence, and move to the next bounded hypothesis.
- Do not spend long cycles re-checking the same reasoning without new telemetry.
- After two failed loops without new signal, gather fresh runtime evidence before further edits.
- Do not re-test mechanisms already rejected in the active regression notes unless new telemetry directly contradicts that rejection.
- Keep a short rejected-approaches list in task notes and treat it as out-of-scope during micro-iteration loops.

Mandatory test-effort policy in this mode:
- Precedence: in Regression Troubleshooting Mode, this policy overrides generic expectations to create/update tests for every small change.
- Do not create/update tests for every small troubleshooting iteration.
- Every iteration still requires a fast verification step (targeted existing test/build/runtime probe).
- Add/update tests at commit boundaries for confirmed fixes and when a larger feature/task slice is completed.
- For larger feature implementations, keeping tests up to date is required before task completion.
- If two loops fail without new telemetry, stop test churn and collect fresh device/runtime evidence.

### Hallucination and Pipeline Safety (Compatible Subset)

- Never invent file paths, symbols, API endpoints, or field names.
- If a value is unknown, state `UNKNOWN` and include the minimal verification step.
- Do not claim contents of a file/resource that was not read.
- When a step fails, report: failed step, likely cause, and what was attempted.
- Keep pipeline-oriented strictness where compatible, but do not override interactive repo workflow requirements from AGENTS.

### Interaction and Clarification Style

- Ask concise multi-choice clarification questions only when ambiguity materially affects safety, correctness, architecture, or acceptance criteria.
- For minor details, state assumptions and proceed.
- Use non-blocking progress updates instead of repeated planning chatter.
- **Use the tool's structured ask mechanism** (`ask_followup_question` or equivalent MCP
  tool) for any question with discrete options — never emit lettered/numbered plain-text
  option lists ("A:", "B:", "1.", "2.") that require the user to type a reply manually.
  Structured questions render as native UI pickers; plain-text lists break the interaction.

### Subagent Delegation Mode

- Default to planner-orchestrator behavior for non-trivial tasks:
	- main model performs deep analysis and creates a detailed TODO packet,
	- subagents execute scoped tasks from that packet.
- Do not fall back to main-model solo execution after subagent timeout/cancel/usage-cap unless the user explicitly requests solo execution.
- On interruption, checkpoint completed results and continue with a new delegated wave.

### MemPalace MCP (Claude-Specific Operational Notes)

The canonical MemPalace protocol is defined in `AGENTS.md` under
`## MemPalace Memory Protocol`. Follow that. Claude-specific additions:

- Auto-save is handled by the user-level MemPalace daemon (`mempalace-daemon.service`,
  `hooks.auto_save: true` in `~/.mempalace/config.json`), not by Stop/PreCompact hooks in
  `.claude/settings.json` — this project defines none. Writes persist continuously through
  the daemon regardless of session end or context compaction; you do not need to manually
  trigger saves.
- `mempalace_status` is your first MCP call every session, before any tool use.
  The response self-teaches the AAAK dialect and reveals the palace structure:
  use the wing name it returns for all subsequent scoped searches.
- Prefer `mempalace_search` over re-reading large files when looking for a past
  decision or rationale. Only fall back to file reads when you need the
  exact current source of truth (code, config, task spec).
- `hall_facts` = confirmed design decisions; `hall_discoveries` = notable findings;
  `hall_events` = session milestones; `hall_preferences` = operator preferences.
- After `mempalace_status`, call `mempalace_list_agents` to discover available
  specialist agents. If one exists for the domain being worked on, read its
  recent diary (`mempalace_diary_read("<agent>", last_n=10)`) before starting
  and write a AAAK diary entry (`mempalace_diary_write`) after significant
  domain work completes. Agent definitions live in `~/.mempalace/agents/` —
  not in `CLAUDE.md`.

### Hardware and Tooling Reminders

- Use [tools/serial_monitor.py](../tools/serial_monitor.py) for serial capture; avoid ad-hoc pyserial snippets.
- After editing action registry metadata, RC action tokens, `ACTION_REGISTRY[]`, or the RC page fallback list, run `make check-action-drift`. The checker reports mismatches only; it does not generate or rewrite files.
- Do not guess GPIO values. If a pin is unresolved, keep it as `TBD` and surface the blocker.
- `initAsyncWeb()` must be initialized from the WiFi event callback path, not directly in `setup()`.
- Core 1 real-time loops must avoid heap allocation. Core 0 web handlers may use bounded per-request `JsonDocument` allocations.

### Verification and Reporting

- Use the [AGENTS.md](../AGENTS.md) verification sequence as applicable.
- Explicitly classify validation status using AGENTS labels:
  `software-verified`, `controller-upload-verified`, `full-hardware-verified`,
  `partial`, or `full-hardware-required`.
- Do not use "bench verified" or "bench-tested" as a status. Public docs should
  use plain evidence phrases instead of internal labels.
- For hardware-touching changes, state what was and was not proven.

## References

- [AGENTS.md](../AGENTS.md)
- [tasks/dev-workflow-change-spec.md](../tasks/dev-workflow-change-spec.md)
- [tasks/lessons.md](../tasks/lessons.md)
- [docs/goal.md](../docs/goal.md)
