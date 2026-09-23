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
- Inside the `frontend-designer` agent, a project hook starts the local HTTP server on port 4173 (serving `data/`) before any `test/playwright/` script runs - do not start a second one there.
- Anywhere else, if nothing answers on port 4173, start it once: `python3 -m http.server 4173 --directory data`.
- Navigate to `http://127.0.0.1:4173/<page>.html` for local validation.

Startup protocol (required):
1. If the Playwright browser tools are deferred in this runtime, load them first with its tool-search tool (`ToolSearch` in Claude Code, `tool_search` in VS Code Copilot). An unloaded tool is missing, not broken, and is no reason to fall back to the CLI.
2. Navigate to the target page using the Playwright browser navigate tool.
3. Continue with browser snapshot, click, and screenshot tools from the same server.
4. If MCP browser tools are unavailable after that search, report the blocker and continue with fallback remediation.
5. Do not run CLI/runtime probes for Playwright (`npm`, `npx`, `node`, `find`, temporary JS scripts) unless explicitly requested.
6. If the Playwright MCP server is not exposed, report to the operator: the server is registered in `.mcp.json` — check that the current runtime loads that file.
7. Do not call Playwright resize/viewport tools in MCP validation flows. Validate using the runtime default viewport.

Failure protocol (required):
1. If Playwright MCP tools are unavailable, report the blocker. The server is registered in `.mcp.json` as `playwright` using `npx @playwright/mcp@latest` — check that the runtime loads that file.
2. Use URL-first fallback (reachable running host preferred). If needed, start local server on port 4173.
3. If Bash is permitted, run existing repo scripts under `test/playwright/` against that URL to preserve audit progress.
4. If Bash is denied for local server start, do not edit permission settings to lift it. With no reachable URL, ask the operator for one explicit action: provide a URL or allow `python3 -m http.server 4173 --directory data`.
5. Report what was attempted with the blocked-run format below.
6. Escalate only after the above attempts, including exact failed step and full error text.

Shutdown protocol (required):
1. Close the browser with the runtime's browser-close tool (`mcp__playwright__browser_close` in
   Claude Code) as the LAST step of every run -- including a run that found nothing, a run that
   ended in a blocker, and a run you are abandoning. Headed mode is the default here, so every
   browser you open is a window left on the operator's desktop until you close it.
2. Close it before you write your report, not after. A report is the point at which a run is
   over, and a browser still open at that point is one nobody will come back for.
3. One browser at a time. If a check needs a fresh page, navigate -- do not open a second browser
   and leave the first behind.
4. If the close call fails, say so in the report with the exact error, and name the leftover so the
   operator can close it. A silently abandoned window is the failure this protocol exists to prevent.
5. Closing the browser does NOT stop the Playwright MCP server, and it should not: the server is
   cheap, idle and reused by the rest of the session. The window is what costs the operator
   something.

Why this is a required step and not a courtesy: on 2026-09-12 the operator reported being left
with open Playwright Chromium windows from sessions that had long finished. The startup protocol
above was followed every time; there was no shutdown protocol to follow.

Blocked-run report format (required):
1. Failed tool call: exact tool name as reported by the runtime.
2. Attempted input: exact URL/command/arguments used for that failing call.
3. Runtime error: exact returned text, unchanged.
4. Permission source: `local`, `project`, `managed`, or `UNKNOWN`.
5. Fallback taken: the alternative path attempted, if any.
6. Fallback result: success/fail with exact error text if fail.
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
- Headed mode is required by default so interactions remain visible. That is exactly why the
  Shutdown protocol above is mandatory: a headed run that is not closed leaves a real window on
  the operator's desktop.
- Use headless mode only when explicitly requested.
- Use desktop-first validation expectations with runtime default viewport.
- Do not run tablet/mobile viewport checks unless explicitly requested.

Execution checklist:
1. Navigate to the target page and collect an initial snapshot.
2. Perform the requested interactions with realistic operator behavior.
3. Capture screenshots for key states before and after interactions.
4. Close the browser (Shutdown protocol above).
5. Report findings in plain language suitable for non-developers.
6. Include concrete selector and page-state evidence for each finding.

Quality rules:
- Prioritize clarity, legibility, and predictable interaction behavior.
- Flag ambiguous labels, unclear feedback, and hidden state transitions.
- Treat desktop PC layout as the primary validation target.
- When no issue is found, explicitly state the tested flow and evidence.
