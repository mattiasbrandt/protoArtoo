# Copilot Instructions - protoArtoo

Adapter file for GitHub Copilot.
Canonical cross-agent instructions live in `AGENTS.md` at repository root.
If a rule here conflicts with `AGENTS.md`, follow `AGENTS.md` unless the user explicitly overrides.

## Purpose

- Keep Copilot behavior aligned with the canonical repo policy.
- Avoid duplicated policy blocks that increase token cost.
- Preserve concise code-generation guidance specific to this repo.

## Canonical Policy Source

For safety, architecture, workflow, verification, and git rules, follow:

- `AGENTS.md`
- `docs/goal.md` and `docs/status.md`
- `tasks/dev-workflow-change-spec.md`

Do not duplicate large policy sections from those files in this adapter.

## Code Generation Style (Concise)

- Favor minimal, explicit, readable code over abstraction-heavy patterns.
- Use `snake_case` for functions/variables, `PascalCase` for types, `UPPER_SNAKE` for constants.
- Use 4-space indentation and keep lines near the project style limit.
- Avoid unnecessary dependencies and broad refactors in targeted fixes.
- Preserve useful comments by default; only remove comments when stale or incorrect.

## Embedded Guardrails (Copilot-Specific Quick Reminders)

- Never block real-time loops (Core 1 tasks).
- Never use `delay()` in tasks; use `vTaskDelay(pdMS_TO_TICKS(ms))`.
- Do not add dynamic allocation in Core 1 task loops after `setup()`.
- Web handlers validate/constrain input and route through queues/state; no direct hardware writes.
- Do not guess GPIO pins. If unresolved, keep `TBD` and surface the blocker.
- Keep drive safety invariants intact (50 Hz zero-frame continuity and DriveTask speed cap).

## Prompt-Efficiency and Freshness Rules

- Avoid redundant full-file rereads.
- Re-read files when freshness risk exists: after edits, formatter/codegen actions, user edits, failed patch apply, or before line-specific edits/explanations.
- Prefer targeted file reads over repo-wide scans.
- Read target files before modifying; never edit blind.

## Token-Efficient Response Discipline

- Return code/results first; provide brief explanation only when non-obvious.
- Prefer simple, maintainable changes by default; add complexity when it clearly improves correctness, safety, or maintainability.
- Do not add speculative features or out-of-scope suggestions unless asked.
- For reviews, state issue, location, and fix without extra commentary.
- For debugging, inspect relevant code first; if uncertain, say so and propose a verification step.
- Keep prose concise, but include brief rationale and comments where they materially improve readability or safe maintenance.
- Keep generated content ASCII unless file context requires non-ASCII.
- Prefer structured outputs (bullets/tables/JSON when requested) over long narrative text.
- Lead with the primary result, then concise supporting details.
- For quantitative claims, include units and source/derivation context; if unknown, mark as UNKNOWN.
- Distinguish observed facts from inferences and label low confidence explicitly.

## Hallucination and Output Safety (Compatible Subset)

- Never invent file paths, symbols, API endpoints, or field names.
- If a value is unknown, explicitly mark it as `UNKNOWN` and provide a minimal verification step.
- Do not reference file/resource contents that were not read.
- On failure, report only: failed step, likely cause, and what was attempted.
- Keep pipeline-oriented strictness where compatible, but do not override AGENTS interactive workflow requirements.

## Domain-Specific Notes

- Marcduino RX routing and action naming conventions are canonicalized in `docs/action-registry.yaml` and `AGENTS.md`.
- Use `tools/serial_monitor.py` for serial capture during verification workflows.
- Keep operator-facing web copy free of internal phase/process language.

## Verification and Reporting

Use AGENTS verification flow and classify status as one of:

- `bench-tested`
- `partial`
- `full-hardware-required`

When hardware validation is deferred, explicitly state blockers and remaining checks.

## Subagent Orchestration

- For non-trivial tasks, use planner-orchestrator mode:
	- main model handles deep reasoning and detailed TODO packet design,
	- subagents execute scoped tasks.
- Do not switch to main-model solo execution after subagent timeout/cancel/usage-cap unless the user explicitly asks.
- Preserve partial results and continue in a new delegated wave when interruptions occur.

## MemPalace Memory

This project uses MemPalace for long-term memory (`mempalace` MCP server, 19 tools).
GitHub Copilot does not currently support MCP tool calls, so this is advisory:

- Past design decisions, architectural rationale, and session history for this
  project are stored in the MemPalace palace. When another agent (Claude, Cursor)
  references prior context, it is retrieving from MemPalace — treat those results
  as authoritative session history.
- The canonical memory protocol is in `AGENTS.md` under `## MemPalace Memory Protocol`.
- Do not store memory content in this file — the palace is the memory layer.
