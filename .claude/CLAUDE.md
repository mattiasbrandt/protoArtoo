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

### Hardware and Tooling Reminders

- Use [tools/serial_monitor.py](../tools/serial_monitor.py) for serial capture; avoid ad-hoc pyserial snippets.
- Do not guess GPIO values. If a pin is unresolved, keep it as `TBD` and surface the blocker.
- `initAsyncWeb()` must be initialized from the WiFi event callback path, not directly in `setup()`.
- Core 1 real-time loops must avoid heap allocation. Core 0 web handlers may use bounded per-request `JsonDocument` allocations.

### Verification and Reporting

- Use the [AGENTS.md](../AGENTS.md) verification sequence as applicable.
- Explicitly classify validation status: `bench-tested`, `partial`, or `full-hardware-required`.
- For hardware-touching changes, state what was and was not proven.

## References

- [AGENTS.md](../AGENTS.md)
- [tasks/dev-workflow-change-spec.md](../tasks/dev-workflow-change-spec.md)
- [tasks/lessons.md](../tasks/lessons.md)
- [docs/goal.md](../docs/goal.md)
