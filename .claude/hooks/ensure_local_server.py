#!/usr/bin/env python3
"""PreToolUse / SessionStart hook: ensure local HTTP server and Playwright MCP server are running."""

import json
import os
import subprocess
import sys
import time
import urllib.request
from pathlib import Path

PORT = 4173
SERVE_DIR = "data"
LOG_FILE = "/tmp/protoartoo-server.log"
PID_FILE = "/tmp/protoartoo-server.pid"

PW_MCP_PORT = 8931
PW_MCP_LOG = "/tmp/protoartoo-pw-mcp.log"
PW_MCP_PID_FILE = "/tmp/protoartoo-pw-mcp.pid"

TIMEOUT_S = 15.0


def _is_playwright_command(cmd: str) -> bool:
    return "test/playwright" in cmd


def _http_ready(url: str) -> bool:
    try:
        with urllib.request.urlopen(url, timeout=1) as resp:
            return resp.status < 500
    except Exception:
        return False


def _start_static_server(project_dir: str) -> int:
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


def _start_pw_mcp_server() -> int:
    with open(PW_MCP_LOG, "a") as log:
        proc = subprocess.Popen(
            ["npx", "@playwright/mcp@latest", f"--port={PW_MCP_PORT}"],
            stdout=log,
            stderr=log,
            start_new_session=True,
        )
    Path(PW_MCP_PID_FILE).write_text(str(proc.pid))
    return proc.pid


def _ensure_servers(project_dir: str, include_static: bool) -> list:
    """Start any servers that aren't yet up. Returns list of newly started entries."""
    started = []

    if include_static:
        static_url = f"http://127.0.0.1:{PORT}/index.html"
        if not _http_ready(static_url):
            pid = _start_static_server(project_dir)
            started.append(("static-server", pid, static_url, LOG_FILE))

    pw_mcp_url = f"http://127.0.0.1:{PW_MCP_PORT}/mcp"
    if not _http_ready(pw_mcp_url):
        pid = _start_pw_mcp_server()
        started.append(("pw-mcp-server", pid, pw_mcp_url, PW_MCP_LOG))

    return started


def _wait_and_report(started: list, event_name: str) -> int:
    if not started:
        return 0

    deadline = time.monotonic() + TIMEOUT_S
    pending = list(started)
    while time.monotonic() < deadline and pending:
        pending = [(n, p, u, l) for n, p, u, l in pending if not _http_ready(u)]
        if pending:
            time.sleep(0.25)

    for name, pid, url, log in pending:
        sys.stderr.write(
            f"[ensure_local_server] {name} (PID {pid}) did not become ready at {url} "
            f"within {TIMEOUT_S}s. Check {log} for errors.\n"
        )

    ready = [e for e in started if e not in pending]
    if ready:
        lines = [f"{n} started (PID {p}) at {u}" for n, p, u, _ in ready]
        payload = {
            "hookSpecificOutput": {
                "hookEventName": event_name,
                "additionalContext": "Services ready: " + "; ".join(lines),
            }
        }
        print(json.dumps(payload))
    return 0


def main() -> int:
    try:
        data = json.load(sys.stdin)
    except (json.JSONDecodeError, ValueError):
        data = {}

    project_dir = os.environ.get("CLAUDE_PROJECT_DIR", os.getcwd())
    hook_event = data.get("hook_event_name", "")

    # SessionStart: bring PW MCP server up before Claude Code attempts MCP injection.
    if hook_event == "SessionStart" or data.get("session_id"):
        started = _ensure_servers(project_dir, include_static=False)
        return _wait_and_report(started, "SessionStart")

    # PreToolUse: only act on playwright test commands.
    if data.get("tool_name") != "Bash":
        return 0
    cmd = str(data.get("tool_input", {}).get("command", ""))
    if not _is_playwright_command(cmd):
        return 0

    started = _ensure_servers(project_dir, include_static=True)
    return _wait_and_report(started, "PreToolUse")


if __name__ == "__main__":
    raise SystemExit(main())
