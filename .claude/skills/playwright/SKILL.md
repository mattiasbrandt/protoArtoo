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

Execution checklist:
1. Navigate to the target page and collect an initial snapshot.
2. Perform the requested interactions with realistic operator behavior.
3. Capture screenshots for key states before and after interactions.
4. Report findings in plain language suitable for non-developers.
5. Include concrete selector and page-state evidence for each finding.

Quality rules:
- Prioritize clarity, legibility, and predictable interaction behavior.
- Flag ambiguous labels, unclear feedback, and hidden state transitions.
- Treat desktop and tablet layouts as first-class targets.
- When no issue is found, explicitly state the tested flow and evidence.
