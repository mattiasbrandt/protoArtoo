#!/usr/bin/env python3
"""Subagent-scoped gate for frontend upload commands.

When frontend-designer tries upload/uploadfs, require an explicit confirmation so
hardware availability is checked first.
"""

import json
import sys


def main() -> int:
    try:
        data = json.load(sys.stdin)
    except json.JSONDecodeError:
        return 0

    if data.get("tool_name") != "Bash":
        return 0

    cmd = str(data.get("tool_input", {}).get("command", ""))
    if "pio" not in cmd:
        return 0

    if not any(token in cmd for token in ("-t upload", "--target upload", "-t uploadfs", "--target uploadfs")):
        return 0

    payload = {
        "hookSpecificOutput": {
            "hookEventName": "PreToolUse",
            "permissionDecision": "ask",
            "permissionDecisionReason": (
                "Frontend verification gate: ask using a structured picker - "
                "'Is target hardware available right now?' (Yes: continue upload, "
                "No: cancel upload and run local web verification on :4173 + Playwright)."
            ),
        }
    }
    print(json.dumps(payload))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
