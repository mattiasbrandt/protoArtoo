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


# `make flash`, `make ota`, `make uploadfs` and their `-<variant>` targets
# (`flash-monitor`, `ota-chirp`, ...), with optional VAR=value words first.
_MAKE_UPLOAD = re.compile(r"\bmake\b(?:\s+\S+=\S*)*\s+(flash|ota|uploadfs)(?:-[\w-]+)?(?=\s|$)")


def _make_upload_target(cmd: str) -> str:
    match = _MAKE_UPLOAD.search(cmd)
    return match.group(1) if match else ""


def is_upload_command(cmd: str) -> bool:
    return bool(_make_upload_target(cmd)) or (
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
        print_decision("deny", "Blocked: /dev/ttyS0 is not the board; use its USB serial port (/dev/ttyUSB* or /dev/ttyACM*).")
        return 0

    make_target = _make_upload_target(cmd)
    is_uploadfs = ("-t uploadfs" in cmd) or ("--target uploadfs" in cmd) or make_target == "uploadfs"
    has_ota_env = bool(re.search(r"-e\s+\S*_ota\b", cmd)) or make_target == "ota"
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
        "(Yes: continue, No: cancel and run software-only verification).",
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
