#!/usr/bin/env python3
"""PreToolUse hook: gate firmware/filesystem upload commands with contextual asks."""

import json
import re
import sys


def print_decision(decision: str, reason: str) -> None:
    payload = {
        "hookSpecificOutput": {
            "hookEventName": "PreToolUse",
            "permissionDecision": decision,
            "permissionDecisionReason": reason,
        }
    }
    print(json.dumps(payload))


def is_upload_command(cmd: str) -> bool:
    return (
        "pio" in cmd
        and (
            "-t upload" in cmd
            or "--target upload" in cmd
            or "-t uploadfs" in cmd
            or "--target uploadfs" in cmd
        )
    )


def main() -> int:
    try:
        data = json.load(sys.stdin)
    except json.JSONDecodeError:
        return 0

    if data.get("tool_name") != "Bash":
        return 0

    cmd = str(data.get("tool_input", {}).get("command", ""))
    if not cmd or not is_upload_command(cmd):
        return 0

    if "/dev/ttyS0" in cmd:
        print_decision("deny", "Blocked: use /dev/ttyUSB0 for USB uploads, not /dev/ttyS0.")
        return 0

    is_uploadfs = ("-t uploadfs" in cmd) or ("--target uploadfs" in cmd)
    has_ota_env = bool(re.search(r"-e\s+\S*_ota\b", cmd))
    has_explicit_upload_port = bool(re.search(r"--upload-port\s+\S+", cmd))

    if is_uploadfs:
        print_decision(
            "ask",
            "UploadFS detected. Ask with a structured picker: 'Is target hardware available right now?' "
            "(Yes: continue, No: cancel and run local verification).",
        )
        return 0

    if has_ota_env or has_explicit_upload_port:
        print_decision(
            "ask",
            "OTA firmware upload detected. Ask with a structured picker: 'Is target hardware available right now?' "
            "(Yes: continue, No: cancel and run software-only verification).",
        )
        return 0

    print_decision(
        "ask",
        "USB firmware upload detected. Ask with a structured picker: 'Is target hardware available right now?' "
        "(Yes: continue with /dev/ttyUSB0, No: cancel and run software-only verification).",
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
