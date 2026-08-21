#!/usr/bin/env python3
"""PreToolUse hook: block backend commits until baseline firmware build passed."""

import json
import re
import time
from pathlib import Path

STATE_FILE = Path("/tmp/protoartoo_backend_verify.json")
MAX_AGE_SECONDS = 6 * 60 * 60
REQUIRED = (
    "firmware_build",
)


def _print_deny(reason: str) -> None:
    payload = {
        "hookSpecificOutput": {
            "hookEventName": "PreToolUse",
            "permissionDecision": "deny",
            "permissionDecisionReason": reason,
        }
    }
    print(json.dumps(payload))


def _load_state() -> dict:
    try:
        return json.loads(STATE_FILE.read_text())
    except Exception:
        return {}


def _is_git_commit_command(cmd: str) -> bool:
    return bool(re.search(r"(^|\s)git\s+commit(\s|$)", cmd))


def _missing_checks(state: dict, now: int) -> list:
    missing = []
    for key in REQUIRED:
        entry = state.get(key)
        if not isinstance(entry, dict):
            missing.append(key)
            continue
        ts = int(entry.get("timestamp", 0))
        if ts <= 0 or (now - ts) > MAX_AGE_SECONDS:
            missing.append(key)
    return missing


def main() -> int:
    try:
        data = json.load(__import__("sys").stdin)
    except Exception:
        return 0

    if data.get("tool_name") != "Bash":
        return 0

    cmd = str(data.get("tool_input", {}).get("command", "")).strip()
    if not _is_git_commit_command(cmd):
        return 0

    state = _load_state()
    missing = _missing_checks(state, int(time.time()))
    if not missing:
        return 0

    label_map = {
        "firmware_build": "pio run -e protoArtoo",
    }
    missing_cmds = [label_map[m] for m in missing]
    _print_deny(
        "Commit blocked: run and pass the baseline firmware build first: "
        + "; ".join(missing_cmds)
        + ". Add native tests, action-drift checks, static analysis, or upload/runtime verification "
          "when the change risk justifies them; otherwise report why the chosen evidence is sufficient."
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
