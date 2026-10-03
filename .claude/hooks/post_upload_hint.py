#!/usr/bin/env python3
"""PostToolUse hook: add concise verification hints after upload commands."""

import json
import os
import sys

from upload_command import find_upload


def extract_target_host(words: list[str]) -> str:
    for index, word in enumerate(words):
        if word == "--upload-port" and index + 1 < len(words):
            return words[index + 1]
        if word.startswith("--upload-port="):
            return word.split("=", 1)[1]

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
    upload = find_upload(cmd)
    if upload is None:
        return 0

    if upload.kind == "uploadfs":
        context = "UploadFS completed. Suggested check: reload setup/status pages and verify UI assets are current."
    else:
        target_host = extract_target_host(upload.words)
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
