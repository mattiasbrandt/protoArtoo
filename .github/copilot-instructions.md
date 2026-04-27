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

## Espressif MCP Usage

Use Espressif MCP servers for external ESP-IDF/component research when repo docs do
not already answer the question.

- `espressif-documentation`: ESP-IDF API/driver behavior, version notes, and hardware capability checks.
- `esp-component-registry`: component discovery, metadata, and example lookup.

Usage rules:
- Query MCP before coding when behavior/component choices are unresolved in repo docs.
- Prefer concise, targeted queries (API/peripheral/chip/IDF version).
- Mark unresolved values as `UNKNOWN`; do not guess.
- For SBUS/RMT and project contracts, repo docs stay authoritative (`docs/spec-sheets/sbus-protocol.md`, `docs/spec-sheets/rmt-esp32-idf5.md`, `docs/spec-sheets/hotrc-sbus-spec.md`).
- RainMaker MCP is out of scope unless explicitly requested.

## Verification and Reporting

Use AGENTS verification flow and classify status as one of:

- `usb-standalone-verified`
- `partial`
- `full-hardware-required`

`usb-standalone-verified` means validation on an ESP32 connected over USB only,
without additional droid hardware/serial peripherals attached.

When hardware validation is deferred, explicitly state blockers and remaining checks.

Regression troubleshooting test policy:
- Precedence: while Regression Debug Mode is active, this policy takes priority over generic per-change test-update expectations.
- During small iterate/fix loops, do not require new test authoring on every micro-change.
- Still run a fast relevant verification on each loop (targeted existing tests/build/runtime probe).
- Add/update tests when a fix is confirmed for commit, when safety-critical behavior changes, or when a larger feature/task slice is completed.
- For larger feature implementations, tests must be kept up to date before marking complete.

## Interaction and Clarification

- Use `vscode_askQuestions` (or equivalent structured ask tool) for any question with
  discrete options — never emit lettered/numbered plain-text option lists ("A:", "B:",
  "1.", "2.") that require the user to type a reply manually. Structured questions
  render as native VS Code UI pickers.
- For minor/inferable details, state the assumption and proceed rather than asking.

## Subagent Orchestration

- For non-trivial tasks, use planner-orchestrator mode:
	- main model handles deep reasoning and detailed TODO packet design,
	- subagents execute scoped tasks.
- Do not switch to main-model solo execution after subagent timeout/cancel/usage-cap unless the user explicitly asks.
- Preserve partial results and continue in a new delegated wave when interruptions occur.

Delegation cues (auto-routing hints):
- `backend-coder`: firmware/ESP32/PlatformIO/API/safety/failsafe/upload-debug prompts.
- `frontend-designer`: UI/UX/layout/copy/operator flow/Playwright verification prompts.
- `Explore`: read-only discovery/search/pattern-finding prompts.

## MemPalace Memory

This project uses MemPalace for long-term memory (`mempalace` MCP server).
For agents/runtimes with MCP access: use MCP-first (`mempalace_status` then targeted search).
For GitHub Copilot runtime: use CLI fallback commands (`mempalace status`, `mempalace search ...`) when memory lookup is needed.

- Past design decisions, architectural rationale, and session history for this
  project are stored in the MemPalace palace. When another agent (Claude, Cursor)
  references prior context, it is retrieving from MemPalace — treat those results
  as authoritative session history.
- Specialist agents with per-domain diaries may exist for this project
  (`~/.mempalace/agents/`). When another agent retrieves diary context via
  `mempalace_diary_read`, treat those entries as authoritative domain history.
  Agent role definitions belong in the palace, not in this file.
- The canonical memory protocol is in `AGENTS.md` under `## MemPalace Memory Protocol`.
- Do not store memory content in this file — the palace is the memory layer.

## Verification Status Labels

Use AGENTS verification flow and classify status as one of:

- `usb-standalone-verified`
- `partial`
- `full-hardware-required`

`usb-standalone-verified` means validation on an ESP32 connected over USB only,
without additional droid hardware/serial peripherals attached.
