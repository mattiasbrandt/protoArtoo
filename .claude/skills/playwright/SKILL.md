---
name: playwright
description: Browser verification and UX behavior auditing for protoArtoo web UI using Playwright MCP. Use for interaction checks, regressions, and screenshot-backed findings.
---

Use this skill when validating operator-facing web flows in data/ pages.

The Playwright MCP server is registered in `.mcp.json` under the key `playwright`.
Tool names exposed by this server vary by runtime (for example `mcp__playwright__browser_navigate`
in Claude Code, or a different prefix in other runtimes). Use whatever browser tools the current
runtime exposes from that server — do not hardcode the namespace prefix.

Startup protocol (required):
1. Navigate to the target page using the Playwright browser navigate tool.
2. Continue with browser snapshot, click, and screenshot tools from the same server.
3. If MCP browser tools are unavailable in this runtime, report the blocker and continue with fallback remediation.
4. Do not run CLI/runtime probes for Playwright (`npm`, `npx`, `node`, `find`, temporary JS scripts) unless explicitly requested.
5. If the Playwright MCP server is not exposed, report to the operator: the server is registered in `.mcp.json` — check that the current runtime loads that file.
6. Do not call Playwright resize/viewport tools in MCP validation flows. Validate using the runtime default viewport.

Failure protocol (required):
1. If navigation/tool execution fails with schema/tooling errors (for example `Failed to compile JSON schema`), stop and report a tooling blocker with exact error text. Do NOT retry with `about:blank` — a crashed MCP tool will fail again and may produce a permission denial. Switch immediately to script-based fallback.
2. If only the resize/viewport tool fails with a schema error, skip resize and continue with navigate/snapshot/click/screenshot. Treat this as degraded tooling, not a hard blocker.
3. If still failing, continue with URL-first fallback (use reachable running host; avoid local server boot dependency).
4. If Bash is permitted, run existing repo scripts under `test/playwright/` against that reachable URL to preserve audit progress.
5. If Bash is denied for local server start, request/update permission for this exact safe command and retry once: `python3 -m http.server 4173 --directory data`.
   (Some runtimes use absolute paths — if the allow rule is path-sensitive, use `python3 -m http.server 4173 *` as the wildcard form.)
6. If no reachable URL and no permission update is possible, ask the operator for one explicit action: provide URL or allow one server-start command.
7. Escalate only after the above attempts, including exact failed step and full error text.

Blocked-run report format (required):
1. Failed tool call: exact tool name as reported by the runtime.
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
- Use desktop-first validation expectations with runtime default viewport.
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
