#!/usr/bin/env python3
"""PreToolUse hook: ensure local HTTP server is running on port 4173 before playwright test scripts.

If the server is already up, exits silently (no-op).
If not, starts it from the repo's data/ directory and waits up to 3 seconds for readiness.
"""

import json
import os
import signal
import subprocess
import sys
import time
import urllib.request
from pathlib import Path

PORT = 4173
SERVE_DIR = "data"
LOG_FILE = "/tmp/protoartoo-server.log"
PID_FILE = "/tmp/protoartoo-server.pid"
TIMEOUT_S = 3.0


def _is_playwright_command(cmd: str) -> bool:
    return "test/playwright" in cmd


def _server_ready() -> bool:
    try:
        with urllib.request.urlopen(
            f"http://127.0.0.1:{PORT}/index.html", timeout=1
        ) as resp:
            return resp.status == 200
    except Exception:
        return False


def _start_server(project_dir: str) -> int:
    serve_path = str(Path(project_dir) / SERVE_DIR)
    with open(LOG_FILE, "a") as log:
        proc = subprocess.Popen(
            ["python3", "-m", "http.server", str(PORT), "--directory", serve_path],
            stdout=log,
            stderr=log,
            start_new_session=True,
        )
    Path(PID_FILE).write_text(str(proc.pid))
    return proc.pid


def main() -> int:
    try:
        data = json.load(sys.stdin)
    except json.JSONDecodeError:
        return 0

    if data.get("tool_name") != "Bash":
        return 0

    cmd = str(data.get("tool_input", {}).get("command", ""))
    if not _is_playwright_command(cmd):
        return 0

    if _server_ready():
        return 0

    project_dir = os.environ.get("CLAUDE_PROJECT_DIR", os.getcwd())
    pid = _start_server(project_dir)

    deadline = time.monotonic() + TIMEOUT_S
    while time.monotonic() < deadline:
        if _server_ready():
            payload = {
                "hookSpecificOutput": {
                    "hookEventName": "PreToolUse",
                    "additionalContext": (
                        f"Local HTTP server started (PID {pid}) on http://127.0.0.1:{PORT}/ "
                        f"serving data/ — log at {LOG_FILE}."
                    ),
                }
            }
            print(json.dumps(payload))
            return 0
        time.sleep(0.25)

    sys.stderr.write(
        f"[ensure_local_server] Server did not become ready on port {PORT} within {TIMEOUT_S}s. "
        f"Check {LOG_FILE} for errors.\n"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
