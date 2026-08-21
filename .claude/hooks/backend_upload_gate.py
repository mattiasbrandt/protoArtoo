#!/usr/bin/env python3
"""Subagent-scoped gate for backend upload commands."""

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
                "Backend verification gate: ask using a structured picker - "
                "'Is target hardware available right now?' (Yes: continue upload, "
                "No: skip upload and finish software checks with status partial or full-hardware-required)."
            ),
        }
    }
    print(json.dumps(payload))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
