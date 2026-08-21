---
name: playwright
description: Browser verification and UX behavior auditing for protoArtoo web UI using Playwright MCP. Use for interaction checks, regressions, and screenshot-backed findings.
---

Use this skill when validating operator-facing web flows in data/ pages.

The Playwright MCP server is registered in `.mcp.json` under the key `playwright`.
Tool names exposed by this server vary by runtime (for example `mcp__playwright__browser_navigate`
in Claude Code, or a different prefix in other runtimes). Use whatever browser tools the current
runtime exposes from that server — do not hardcode the namespace prefix.

Local server:
- The local HTTP server on port 4173 (serving `data/`) is managed automatically by a project hook — do not start it manually with `python3 -m http.server` or similar commands.
- If the server is already up, the hook is a no-op. If not, it starts automatically before any playwright test script runs.
- Navigate to `http://127.0.0.1:4173/<page>.html` for local validation.

Startup protocol (required):
1. Call `tool_search` with query "playwright browser navigate screenshot" to load the Playwright MCP tools into the deferred tool registry before attempting any browser tool call. This is required in VS Code Copilot — skipping it causes the tools to be missing and triggers CLI fallback.
2. Navigate to the target page using the Playwright browser navigate tool.
3. Continue with browser snapshot, click, and screenshot tools from the same server.
4. If MCP browser tools are unavailable after tool_search, report the blocker and continue with fallback remediation.
5. Do not run CLI/runtime probes for Playwright (`npm`, `npx`, `node`, `find`, temporary JS scripts) unless explicitly requested.
6. If the Playwright MCP server is not exposed, report to the operator: the server is registered in `.mcp.json` — check that the current runtime loads that file.
7. Do not call Playwright resize/viewport tools in MCP validation flows. Validate using the runtime default viewport.

Failure protocol (required):
1. If Playwright MCP tools are unavailable, report the blocker. The server is registered in `.mcp.json` as `playwright` using `npx @playwright/mcp@latest` — check that the runtime loads that file.
2. Use URL-first fallback (reachable running host preferred). If needed, start local server on port 4173.
3. If Bash is permitted, run existing repo scripts under `test/playwright/` against that URL to preserve audit progress.
4. If Bash is denied for local server start, request/update permission for this exact safe command and retry once: `python3 -m http.server 4173 --directory data`.
   (Some runtimes use absolute paths — if the allow rule is path-sensitive, use `python3 -m http.server 4173 *` as the wildcard form.)
5. If no reachable URL and no permission update is possible, ask the operator for one explicit action: provide URL or allow one server-start command.
6. Escalate only after the above attempts, including exact failed step and full error text.

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
