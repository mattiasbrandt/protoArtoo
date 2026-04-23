#!/usr/bin/env python3
"""PostToolUseFailure hook: emit concrete diagnostics for permission-denied failures."""

import json
import sys
from typing import Any, Dict


def _detect_permission_source(error_text: str) -> str:
    text = error_text.lower()
    if "managed" in text:
        return "managed"
    if "project" in text:
        return "project"
    if "local" in text:
        return "local"
    return "UNKNOWN"


_PLAYWRIGHT_PREFIX = "mcp__playwright__"  # canonical prefix from .mcp.json key; may differ per runtime


def _is_playwright_tool(tool_name: str) -> bool:
    """Match any Playwright MCP tool regardless of runtime-specific namespace prefix."""
    return "playwright" in tool_name.lower() and tool_name.startswith("mcp__")
_SCHEMA_PATTERNS = ("compile json schema", "no schema with key or ref")


def _classify_error(error_text: str) -> str:
    low = error_text.lower()
    if "denied" in low or "permission" in low:
        return "permission_denied"
    if any(p in low for p in _SCHEMA_PATTERNS):
        return "mcp_tooling_crash"
    return "tool_failure"


def _suggest_remediation(tool_name: str, tool_input: Dict[str, Any], error_kind: str) -> str:
    if error_kind == "mcp_tooling_crash" and _is_playwright_tool(tool_name):
        return (
            "MCP server crashed on tool call (JSON schema validation bug in @playwright/mcp). "
            "Do NOT retry MCP navigation — switch immediately to script-based fallback: "
            "run existing scripts in test/playwright/ against a reachable URL."
        )

    if error_kind == "permission_denied":
        if tool_name == "Bash":
            command = str(tool_input.get("command", "")).strip()
            if command:
                return (
                    f"Add explicit allow rule in .claude/settings.local.json: "
                    f"permissions.allow Bash({command})"
                )
            return "Add an exact Bash allow rule in .claude/settings.local.json."
        if _is_playwright_tool(tool_name):
            return (
                f"Add allow rule for '{tool_name}' in .claude/settings.json permissions.allow, then retry once. "
                "If already present, check that the project settings file is loaded in this runtime scope."
            )

    return "Check tool error; confirm applicable permission rule at local/project scope, then retry once."


def main() -> int:
    try:
        data = json.load(sys.stdin)
    except json.JSONDecodeError:
        return 0

    if data.get("hook_event_name") != "PostToolUseFailure":
        return 0

    tool_name = str(data.get("tool_name", ""))
    if tool_name != "Bash" and not _is_playwright_tool(tool_name):
        return 0

    error_text = str(data.get("error", ""))
    if not error_text:
        return 0

    tool_input = data.get("tool_input") or {}
    error_kind = _classify_error(error_text)
    permission_source = _detect_permission_source(error_text) if error_kind == "permission_denied" else "N/A"
    remediation = _suggest_remediation(tool_name, tool_input, error_kind)

    packet_lines = [
        f"Tool failure diagnostics [{error_kind}]:",
        f"1. Failed tool call: {tool_name}",
        f"2. Attempted input: {json.dumps(tool_input, ensure_ascii=True, sort_keys=True)}",
        f"3. Exact runtime error text: {error_text}",
        f"4. Permission source: {permission_source}",
        f"5. Remediation: {remediation}",
        "6. Retry result: PENDING",
        "7. Operator next step: follow remediation in field 5 before retrying.",
    ]

    payload = {
        "hookSpecificOutput": {
            "hookEventName": "PostToolUseFailure",
            "additionalContext": "\n".join(packet_lines),
        }
    }
    print(json.dumps(payload))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
