#!/usr/bin/env python3
"""PostToolUse hook: track successful backend verification commands.

Stores timestamps for required software checks so commit gating can enforce
"verify before commit" behavior for the backend-coder agent.
"""

import json
import time
from pathlib import Path

STATE_FILE = Path("/tmp/protoartoo_backend_verify.json")


def _classify_command(cmd: str) -> str:
    text = cmd.strip()
    if "pio test -e native" in text:
        return "native_tests"
    if "pio check" in text:
        return "static_check"
    # Accept compile-only firmware build; ignore upload/uploadfs commands.
    if "pio run -e protoArtoo" in text and "upload" not in text:
        return "firmware_build"
    return ""


def _load_state() -> dict:
    try:
        return json.loads(STATE_FILE.read_text())
    except Exception:
        return {}


def _save_state(state: dict) -> None:
    STATE_FILE.write_text(json.dumps(state, ensure_ascii=True, sort_keys=True, indent=2) + "\n")


def main() -> int:
    try:
        data = json.load(__import__("sys").stdin)
    except Exception:
        return 0

    if data.get("hook_event_name") != "PostToolUse":
        return 0
    if data.get("tool_name") != "Bash":
        return 0

    cmd = str(data.get("tool_input", {}).get("command", ""))
    key = _classify_command(cmd)
    if not key:
        return 0

    # Only record when the command succeeded (exit 0).  A failed build or test
    # run must not satisfy the commit gate.
    tool_result = data.get("tool_result", {})
    if isinstance(tool_result, dict) and tool_result.get("is_error", False):
        return 0

    state = _load_state()
    state[key] = {
        "timestamp": int(time.time()),
        "command": cmd,
    }
    _save_state(state)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
