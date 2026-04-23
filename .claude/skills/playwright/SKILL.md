---
name: playwright
description: Browser verification and UX behavior auditing for protoArtoo web UI using Playwright MCP. Use for interaction checks, regressions, and screenshot-backed findings.
allowed-tools:
  - mcp__plugin_playwright_playwright__browser_navigate
  - mcp__plugin_playwright_playwright__browser_snapshot
  - mcp__plugin_playwright_playwright__browser_click
  - mcp__plugin_playwright_playwright__browser_take_screenshot
---

Use this skill when validating operator-facing web flows in data/ pages.

Startup protocol (required):
1. Use `mcp__plugin_playwright_playwright__browser_navigate` first.
2. Continue with MCP browser tools (`browser_snapshot`, `browser_click`, `browser_take_screenshot`).
3. If MCP browser tools are unavailable, report the blocker and continue with fallback remediation.
4. Do not run CLI/runtime probes for Playwright (`npm`, `npx`, `node`, `find`, temporary JS scripts) unless explicitly requested.
5. If MCP is unavailable, provide this operator command and continue with allowed fallback paths: `claude mcp add playwright npx @playwright/mcp@latest`.

Failure protocol (required):
1. If navigation/tool execution fails with schema/tooling errors (for example `Failed to compile JSON schema`), stop and report a tooling blocker with exact error text.
2. Retry once using a fresh session flow (`about:blank` then target URL).
3. If still failing, continue with URL-first fallback (use reachable running host; avoid local server boot dependency).
4. If Bash is permitted, run existing repo scripts under `test/playwright/` against that reachable URL to preserve audit progress.
5. If Bash is denied for local server start, request/update permission for this exact safe command and retry once: `python3 -m http.server 4173 --directory data`.
6. If no reachable URL and no permission update is possible, ask the operator for one explicit action: provide URL or allow one server-start command.
7. Escalate only after the above attempts, including exact failed step and full error text.

Blocked-run report format (required):
1. Failed tool call: exact tool name (for example `mcp__plugin_playwright_playwright__browser_navigate`).
2. Attempted input: exact URL/command/arguments used for that failing call.
3. Runtime error: exact returned text, unchanged.
4. Permission source: `local`, `project`, `managed`, or `UNKNOWN`.
5. Remediation attempted now: exact update/request/alternative path attempted.
6. Retry result: success/fail with exact error text if fail.
7. Operator next step: one concrete action only.

Invalid blocker reports (forbidden):
- "Bash access was blocked"
- "Playwright validation was blocked"
- Any blocker statement without exact tool input and exact runtime error text.

Execution pattern (required):
1. Navigate.
2. Snapshot.
3. Interact using element refs from the snapshot.
4. Re-snapshot and verify state change.

Defaults:
- Headed mode is required by default so interactions remain visible.
- Use headless mode only when explicitly requested.
- Use desktop viewport validation by default (for example 1440x900).
- Do not run tablet/mobile viewport checks unless explicitly requested.

Execution checklist:
1. Navigate to the target page and collect an initial snapshot.
2. Perform the requested interactions with realistic operator behavior.
3. Capture screenshots for key states before and after interactions.
4. Report findings in plain language suitable for non-developers.
5. Include concrete selector and page-state evidence for each finding.

Quality rules:
- Prioritize clarity, legibility, and predictable interaction behavior.
- Flag ambiguous labels, unclear feedback, and hidden state transitions.
- Treat desktop PC layout as the primary validation target.
- When no issue is found, explicitly state the tested flow and evidence.
