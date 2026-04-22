#!/usr/bin/env python3
"""TaskCompleted hook: require an explicit verification status label in task descriptions when present."""

import json
import sys


VALID_LABELS = (
    "usb-standalone-verified",
    "partial",
    "full-hardware-required",
)


def main() -> int:
    try:
        data = json.load(sys.stdin)
    except json.JSONDecodeError:
        return 0

    if data.get("hook_event_name") != "TaskCompleted":
        return 0

    task_description = str(data.get("task_description") or "").lower()
    if not task_description:
        # No description provided: do not block, keep hook low-friction.
        return 0

    if any(label in task_description for label in VALID_LABELS):
        return 0

    sys.stderr.write(
        "Task completion blocked: include verification status in task_description "
        "(usb-standalone-verified | partial | full-hardware-required).\n"
    )
    return 2


if __name__ == "__main__":
    raise SystemExit(main())
