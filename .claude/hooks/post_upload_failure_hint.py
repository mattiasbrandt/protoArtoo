#!/usr/bin/env python3
"""PostToolUseFailure hook: add focused recovery hints for upload failures."""

import json
import sys

from upload_command import is_upload_command


def main() -> int:
    try:
        data = json.load(sys.stdin)
    except json.JSONDecodeError:
        return 0

    if data.get("tool_name") != "Bash":
        return 0

    cmd = str(data.get("tool_input", {}).get("command", ""))
    if not is_upload_command(cmd):
        return 0

    hint = (
        "Upload failed. Check: target env (_ota vs USB), upload-port value, device reachability, and native test gate status."
    )
    payload = {
        "hookSpecificOutput": {
            "hookEventName": "PostToolUseFailure",
            "additionalContext": hint,
        }
    }
    print(json.dumps(payload))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
