#!/usr/bin/env python3
"""PreToolUse hook: block hand-edits to the generated version JSON files.

data/fw-version.json and data/fs-version.json are 100% generated —
tools/extract_version.py regenerates them on every local `pio run`, and CI
regenerates them authoritatively on push to main. They are never legitimately
hand-edited via Edit/Write/MultiEdit, on any branch.
"""

import json
import os
import sys

GUARDED_BASENAMES = ("fw-version.json", "fs-version.json")


def _deny(reason: str) -> None:
    payload = {
        "hookSpecificOutput": {
            "hookEventName": "PreToolUse",
            "permissionDecision": "deny",
            "permissionDecisionReason": reason,
        }
    }
    print(json.dumps(payload))


def main() -> int:
    try:
        data = json.load(sys.stdin)
    except json.JSONDecodeError:
        return 0

    if data.get("tool_name") not in ("Edit", "Write", "MultiEdit"):
        return 0

    file_path = str(data.get("tool_input", {}).get("file_path", ""))
    basename = os.path.basename(file_path)

    if basename in GUARDED_BASENAMES:
        _deny(
            f"Blocked: {basename} is a generated file (tools/extract_version.py / "
            "CI regeneration on main). Never hand-edit it via Edit/Write/MultiEdit — "
            "run 'pio run' locally to regenerate, or let CI regenerate it on merge "
            "to main. See AGENTS.md's Version JSON workflow section."
        )

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
