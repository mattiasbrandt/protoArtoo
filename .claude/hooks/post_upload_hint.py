#!/usr/bin/env python3
"""PostToolUse hook: add concise verification hints after upload commands."""

import json
import os
import re
import sys


def extract_target_host(cmd: str) -> str:
    match = re.search(r"--upload-port\s+(\S+)", cmd)
    if match:
        return match.group(1)

    ota_host = os.environ.get("OTA_HOST", "").strip()
    if ota_host:
        return ota_host

    ota_ip = os.environ.get("OTA_IP", "").strip()
    if ota_ip:
        return ota_ip

    return "artoo.local"


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

    is_upload = ("-t upload" in cmd) or ("--target upload" in cmd)
    is_uploadfs = ("-t uploadfs" in cmd) or ("--target uploadfs" in cmd)
    if not is_upload and not is_uploadfs:
        return 0

    if is_uploadfs:
        context = "UploadFS completed. Suggested check: reload setup/status pages and verify UI assets are current."
    else:
        target_host = extract_target_host(cmd)
        context = (
            f"Firmware upload completed. Suggested check: curl -s http://{target_host}/api/status and verify firmwareVersion."
        )

    payload = {
        "hookSpecificOutput": {
            "hookEventName": "PostToolUse",
            "additionalContext": context,
        }
    }
    print(json.dumps(payload))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
