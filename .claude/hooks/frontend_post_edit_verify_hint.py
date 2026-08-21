#!/usr/bin/env python3
"""PostToolUse reminder for frontend edits under data/ to run local verification."""

import json
import sys


FRONTEND_SUFFIXES = (".html", ".js", ".css")


def extract_path(data: dict) -> str:
    tool_input = data.get("tool_input", {}) or {}
    for key in ("file_path", "filePath"):
        value = tool_input.get(key)
        if isinstance(value, str):
            return value
    return ""


def main() -> int:
    try:
        data = json.load(sys.stdin)
    except json.JSONDecodeError:
        return 0

    if data.get("tool_name") not in ("Edit", "Write"):
        return 0

    file_path = extract_path(data)
    if not file_path:
        return 0

    normalized = file_path.replace("\\", "/")
    if "/data/" not in normalized:
        return 0
    if not normalized.endswith(FRONTEND_SUFFIXES):
        return 0

    payload = {
        "hookSpecificOutput": {
            "hookEventName": "PostToolUse",
            "additionalContext": (
                "Frontend file changed under data/. Run local web verification: serve data/ on :4173 and run "
                "relevant test/playwright/<page>/ scripts with screenshot evidence."
            ),
        }
    }
    print(json.dumps(payload))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
