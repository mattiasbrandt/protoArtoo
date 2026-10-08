#!/usr/bin/env python3
"""PreToolUse hook: gate firmware/filesystem upload commands with contextual asks."""

import json
import sys

from upload_command import find_upload


def print_decision(decision: str, reason: str) -> None:
    payload = {
        "hookSpecificOutput": {
            "hookEventName": "PreToolUse",
            "permissionDecision": decision,
            "permissionDecisionReason": reason,
        }
    }
    print(json.dumps(payload))


def main() -> int:
    try:
        data = json.load(sys.stdin)
    except json.JSONDecodeError:
        return 0

    if data.get("tool_name") != "Bash":
        return 0

    cmd = str(data.get("tool_input", {}).get("command", ""))
    upload = find_upload(cmd) if cmd else None
    if upload is None:
        return 0

    # The upload's own words, so a port named elsewhere in the command (an
    # echo, a comment) does not decide what this upload does.
    words = upload.words
    if any("/dev/ttyS0" in word for word in words):
        print_decision("deny", "Blocked: /dev/ttyS0 is not the board; use its USB serial port (/dev/ttyUSB* or /dev/ttyACM*).")
        return 0

    has_explicit_upload_port = any(
        word.startswith("--upload-port=") or (word == "--upload-port" and index + 1 < len(words))
        for index, word in enumerate(words)
    )

    if upload.kind == "uploadfs":
        print_decision(
            "ask",
            "UploadFS detected. Ask with a structured picker: 'Is target hardware available right now?' "
            "(Yes: continue, No: cancel and run local verification).",
        )
        return 0

    if upload.ota or has_explicit_upload_port:
        print_decision(
            "ask",
            "OTA firmware upload detected. Ask with a structured picker: 'Is target hardware available right now?' "
            "(Yes: continue, No: cancel and run software-only verification).",
        )
        return 0

    print_decision(
        "ask",
        "USB firmware upload detected. Ask with a structured picker: 'Is target hardware available right now?' "
        "(Yes: continue, No: cancel and run software-only verification).",
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
