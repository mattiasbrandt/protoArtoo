#!/usr/bin/env python3
"""Stop hook: emit lightweight session audit reminders.

Non-blocking and intentionally low-noise.
"""

import json
import os
import sys

HOOK_PROFILE_ENV = "PROTOARTOO_HOOK_PROFILE"


def _is_minimal_profile() -> bool:
    return os.environ.get(HOOK_PROFILE_ENV, "standard").strip().lower() == "minimal"


def main() -> int:
    if _is_minimal_profile():
        return 0

    try:
        data = json.load(sys.stdin)
    except json.JSONDecodeError:
        return 0

    if data.get("hook_event_name") != "Stop":
        return 0

    context = (
        "Session audit: include explicit verification status label in completion notes "
        "(usb-standalone-verified | partial | full-hardware-required), and call out any pending hardware-only checks."
    )

    payload = {
        "hookSpecificOutput": {
            "hookEventName": "Stop",
            "additionalContext": context,
        }
    }
    print(json.dumps(payload))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
