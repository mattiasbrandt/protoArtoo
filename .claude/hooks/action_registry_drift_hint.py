#!/usr/bin/env python3
"""PostToolUse hint: remind agents to run the action registry drift checker."""

import json
import sys


WATCHED_SUFFIXES = (
    "/docs/action-registry.yaml",
    "/include/rc_mapping.h",
    "/include/action_registry.h",
    "/src/web/action_registry.cpp",
    "/data/rc.js",
)


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

    if data.get("tool_name") not in ("Edit", "Write", "MultiEdit"):
        return 0

    normalized = extract_path(data).replace("\\", "/")
    if not normalized:
        return 0
    if not any(normalized.endswith(suffix) for suffix in WATCHED_SUFFIXES):
        return 0

    payload = {
        "hookSpecificOutput": {
            "hookEventName": "PostToolUse",
            "additionalContext": (
                "Action registry-related file changed. Run `make check-action-drift` "
                "before completion to verify docs/action-registry.yaml, C++ registry/token "
                "mapping, and data/rc.js fallback metadata still align."
            ),
        }
    }
    print(json.dumps(payload))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
