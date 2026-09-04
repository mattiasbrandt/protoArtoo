#!/usr/bin/env python3
"""
Lightweight fixture server for offline editor development and Playwright tests.

Serves the protoArtoo editor (data/) + fixture APIs locally on :4173.

Usage:
  python3 tools/serve_editor_fixture.py
  # Serves on localhost:4173
  # GET / -> data/index.html
  # GET /api/dome/layout -> tests/fixtures/dome_layout_mk4.json

Ports:
  - :4173 (dev server, matches Vite convention)

Routes:
  - /api/dome/layout -> tests/fixtures/dome_layout_mk4.json
  - /api/logs -> a synthetic log ring, text/plain, like the device sends
  - POST /api/console -> Console Records for the command, in the device's
    envelope; `system.status.logs` comes back cut, with "truncated": true
  - /api/* (anything else) -> 404, the way the device answers an unknown route
  - /* -> data/* (static files, fallback to index.html for SPA routing)

The /api/* 404 is deliberate and load-bearing. Without it, SimpleHTTPRequestHandler
fell through to the SPA fallback below and answered every unimplemented API path
with index.html at "200 text/html" -- so the dashboard rendered its own markup
into the Live Logs panel and nothing said the route was missing (#261). A 404
matches src/web/web_request_psychic.cpp's onNotFound() and makes the next
missing fixture route announce itself instead of hiding.
"""

import json
import os
import re
import sys
from http.server import HTTPServer, SimpleHTTPRequestHandler
from pathlib import Path
from urllib.parse import parse_qs

REPO_ROOT = Path(__file__).parent.parent
DATA_DIR = REPO_ROOT / "data"
FIXTURE_FILE = REPO_ROOT / "tests" / "fixtures" / "dome_layout_mk4.json"
PORT = 4173
HOST = "localhost"

# Shaped like what GET /api/logs actually returns: the log ring copied out
# newline-separated (src/web/api_logs.cpp), one line per entry in the
# "[<millis>][<level>][<tag>] message" form include/logging.h emits.
FIXTURE_LOG_LINES = (
    "[312][I][boot] protoArtoo starting, reset reason POWERON",
    "[418][I][config] loaded from NVS, log level 3",
    "[1204][I][wifi] connecting to bench-ap",
    "[2530][I][wifi] connected, ip 192.168.1.42",
    "[2544][I][web] http server listening on port 80",
    "[2610][I][ota] ArduinoOTA ready on port 3232",
    "[3001][W][rc] no SBUS frames yet, failsafe held",
    "[4120][I][drive] zero frames streaming at 50 Hz",
    "[5330][D][console] catalog built, 148 operations",
    "[9042][E][battery] sense read failed, retrying",
    "[10118][I][dome] link up, protoR2link answering",
    "[11400][I][sound] CHIRP catalog loaded, 62 tracks",
    "[12960][D][servo] holding 6 channels at rest",
    "[14002][W][heap] largest free block 42 KB",
    "[15330][I][web] client connected, heapFree=173152",
    "[16720][D][rc] SBUS frames steady at 71 Hz",
    "[18004][I][drive] estop cleared, drive armed",
    "[19550][W][web] accept rejected: heap floor",
    "[21010][I][console] operator typed system.status.health",
    "[22480][D][dome] panel 3 closed, holding",
)
FIXTURE_LOG_BODY = "\n".join(FIXTURE_LOG_LINES).encode("utf-8")

# ---------------------------------------------------------------------------
# POST /api/console fixture - the Controller Console's browser adapter
# ---------------------------------------------------------------------------
# Without this route the dashboard's Live Logs command box cannot be driven
# offline at all: SimpleHTTPRequestHandler answers any POST with 501, so every
# command typed against this server came back as a failed request, and the
# panel's own states - a reply that fit, a reply the controller had to cut -
# were reachable only against a live board (#240).
#
# Answers follow docs/console-protocol.md's record model and docs/api.md's
# envelope: {"records": [...]}, plus "truncated": true on an answer the bounded
# adapter path could not carry whole.

# A slice of the real catalog, shaped like the `operations` listing's item
# values ("name (type)", docs/console-protocol.md s.2). Several system.* rows
# on purpose: Tab on "sys" is the completion case that has to extend to the
# common prefix and then list.
FIXTURE_CONSOLE_OPERATIONS = (
    ("dome.action.marcduino-sequence", "action"),
    ("dome.status.link", "status"),
    ("drive.action.move", "action"),
    ("drive.config.speed-limit", "config"),
    ("sound.action.random-humming", "action"),
    ("system.config.log-level", "config"),
    ("system.event.boot-complete", "event"),
    ("system.status.health", "status"),
    ("system.status.logs", "status"),
    ("system.status.wifi", "status"),
    ("wifi.config.settings", "config"),
)

# Parameter descriptors per operation, in the "name:type:disposition" form
# console_module.cpp joins into the `params` field of `help <op>` - the shape
# data/app.js parses for argument-key completion and for #227's
# write-exclusion rule. wifi.config.settings carries the write-excluded key
# that rule exists for, so that path is drivable here too.
FIXTURE_CONSOLE_PARAMS = {
    "dome.action.marcduino-sequence": "value:int:required",
    "drive.action.move": "speed:int:required,steer:int:optional",
    "drive.config.speed-limit": "value:int:required",
    "system.config.log-level": "value:int:required",
    "wifi.config.settings": "ssid:string:required,password:string:write-excluded",
}

# How many log lines the bounded response path keeps for system.status.logs
# before it starts discarding the OLDEST: CONSOLE_RECORD_VALUE_ARENA (2048)
# less one CONSOLE_ITEM_VALUE_RESERVE_BYTES (LOG_LINE_MAX, 128) held back for
# the begin record's own value, over that same per-item reserve
# (src/web/api_console.cpp, handleConsolePost()'s system.status.logs branch).
# That is 15, and it binds well before the 30 item slots left in the 32-entry
# record array. FIXTURE_LOG_LINES is deliberately longer than this, so
# `system.status.logs` reaches the truncated state here exactly as it does on
# a board with a full ring, and every other command stays complete.
CONSOLE_LOG_ITEM_CAP = (2048 - 128) // 128


def _console_command_from_body(content_type, body):
    """The command line out of a form-encoded or JSON POST body.

    Both are what POST /api/console accepts (docs/api.md); the dashboard sends
    the form-encoded one.
    """
    text = body.decode("utf-8", errors="replace")
    if "application/json" in (content_type or ""):
        try:
            parsed = json.loads(text)
        except ValueError:
            return None
        command = parsed.get("command") if isinstance(parsed, dict) else None
        return command if isinstance(command, str) else None
    values = parse_qs(text).get("command")
    return values[0] if values else None


def _console_answer(command, request_id):
    """(records, truncated) for one command line, in the wire's record shape."""
    parts = command.split()
    name = parts[0] if parts else ""
    args = parts[1:]

    def begin(operation):
        return {"id": request_id, "type": "begin", "operation": operation}

    def end():
        return {"id": request_id, "type": "end", "status": "ok", "outcome": "completed"}

    def unknown():
        return [{
            "id": request_id,
            "type": "result",
            "status": "err",
            "outcome": "invalid",
            "reason": "unknown-operation",
        }], False

    if name == "operations":
        wanted = None
        for arg in args:
            if arg.startswith("type="):
                wanted = arg[len("type="):]
        records = [begin("operations")]
        for op_name, op_type in FIXTURE_CONSOLE_OPERATIONS:
            if wanted is not None and op_type != wanted:
                continue
            records.append({"id": request_id, "type": "item", "value": f"{op_name} ({op_type})"})
        records.append(end())
        return records, False

    if name == "help":
        if not args:
            return [
                begin("help"),
                {"id": request_id, "type": "field", "name": "usage",
                 "value": "<operation> [key=value ...]"},
                end(),
            ], False
        target = args[0]
        known = dict(FIXTURE_CONSOLE_OPERATIONS)
        if target not in known:
            return unknown()
        records = [
            begin(target),
            {"id": request_id, "type": "field", "name": "type", "value": known[target]},
            {"id": request_id, "type": "field", "name": "description",
             "value": f"Fixture description for {target}"},
        ]
        params = FIXTURE_CONSOLE_PARAMS.get(target)
        if params is not None:
            records.append({"id": request_id, "type": "field", "name": "params", "value": params})
        records.append(end())
        return records, False

    if name == "system.status.logs":
        # The adapter discards the OLDEST lines first, so the answer carries
        # the newest ones - the same order and the same choice
        # webOnRecordItem_impl makes via itemsToSkip.
        kept = FIXTURE_LOG_LINES[-CONSOLE_LOG_ITEM_CAP:]
        records = [begin("system.status.logs")]
        records.extend(
            {"id": request_id, "type": "item", "value": line} for line in kept
        )
        records.append(end())
        return records, len(kept) < len(FIXTURE_LOG_LINES)

    known = dict(FIXTURE_CONSOLE_OPERATIONS)
    if name not in known:
        return unknown()

    if known[name] == "status":
        return [
            begin(name),
            {"id": request_id, "type": "field", "name": "estop", "value": "false"},
            {"id": request_id, "type": "field", "name": "heapLargestBlock", "value": "262144"},
            end(),
        ], False

    return [{
        "id": request_id,
        "type": "result",
        "status": "ok",
        "outcome": "queued",
    }], False


# PA:INCLUDE pattern matching, same as gzip_fsdata.py
INCLUDE_RE = re.compile(r"[ \t]*<!--\s*PA:INCLUDE\s+([A-Za-z0-9_.\-/]+)\s*-->[ \t]*\n?")


def _expand_includes(content, src_root):
    """Expand PA:INCLUDE directives in HTML content.

    Replaces <!-- PA:INCLUDE filename --> with the contents of that file.
    Single-pass, non-recursive like gzip_fsdata.py.
    """
    def _replace(match):
        target = src_root / match.group(1)
        if not target.is_file():
            raise ValueError(f"PA:INCLUDE target not found: {target}")
        return target.read_text(encoding="utf-8")

    return INCLUDE_RE.sub(_replace, content)


class FixtureHandler(SimpleHTTPRequestHandler):
    """HTTP request handler that maps fixture APIs and serves static files."""

    # Request ids run across the session the way consoleGetNextRequestId()
    # does on the controller, so a transcript here reads like a board's.
    console_request_id = 0

    def _send_json(self, status, payload):
        content = json.dumps(payload).encode("utf-8")
        self.send_response(status)
        self.send_header("Content-type", "application/json")
        self.send_header("Access-Control-Allow-Origin", "*")
        self.send_header("Content-Length", len(content))
        self.end_headers()
        self.wfile.write(content)

    def do_POST(self):
        """Handle the POST fixture routes; everything else is a missing route."""
        route = self.path.split("?", 1)[0]

        if route == "/api/console":
            length = int(self.headers.get("Content-Length") or 0)
            body = self.rfile.read(length) if length > 0 else b""
            command = _console_command_from_body(self.headers.get("Content-Type"), body)
            if command is None:
                # Same refusals the device gives for a body it cannot read a
                # command out of (docs/api.md, POST /api/console).
                self._send_json(400, {"ok": False, "error": "missing command"})
                return
            command = command.strip()
            if not command:
                self._send_json(400, {"ok": False, "error": "empty command"})
                return

            FixtureHandler.console_request_id += 1
            records, truncated = _console_answer(command, FixtureHandler.console_request_id)
            payload = {"records": records}
            if truncated:
                # Present only when the answer was cut, matching the device:
                # the dashboard reads its absence as "this reply is whole".
                payload["truncated"] = True
            self._send_json(200, payload)
            return

        # A POST to anything else is a route this fixture does not have. Same
        # reasoning as the /api/* 404 in do_GET: announce the gap rather than
        # letting it look like something else. The 501 the base handler would
        # send instead reads to the page as "Not supported by device".
        self.send_error(404, f"No fixture route for POST {self.path}")

    def do_GET(self):
        """Handle GET requests with fixture interception."""
        # Route on the path alone: a cache-busting query string must not make a
        # known fixture route fall through to the /api/* 404 below.
        route = self.path.split("?", 1)[0]

        # Intercept API routes
        if route == "/api/dome/layout":
            if not FIXTURE_FILE.exists():
                self.send_error(404, "Fixture file not found")
                return

            self.send_response(200)
            self.send_header("Content-type", "application/json")
            self.send_header("Access-Control-Allow-Origin", "*")
            with open(FIXTURE_FILE, "rb") as f:
                content = f.read()
            self.send_header("Content-Length", len(content))
            self.end_headers()
            self.wfile.write(content)
            return

        if route == "/api/logs":
            self.send_response(200)
            # text/plain is the contract both of handleLogsGet()'s exits use,
            # and since #261 the dashboard refuses a log body that arrives as
            # anything else -- so serving this as text/html would reproduce the
            # very defect this route exists to keep away.
            self.send_header("Content-type", "text/plain; charset=utf-8")
            self.send_header("Access-Control-Allow-Origin", "*")
            self.send_header("Content-Length", len(FIXTURE_LOG_BODY))
            self.end_headers()
            self.wfile.write(FIXTURE_LOG_BODY)
            return

        # An API path with no fixture is a missing route, not a page. Answering
        # it from the SPA fallback below is what let index.html reach the Live
        # Logs panel (#261); a 404 is both what the device does and what makes
        # the gap visible the moment a page starts calling a new endpoint.
        if route.startswith("/api/"):
            self.send_error(404, f"No fixture route for {self.path}")
            return

        # SPA routing: fallback to index.html for unknown paths
        if self.path == "/" or self.path.startswith("/?"):
            self.path = "/index.html"

        # Serve static files from data/
        self.directory = str(DATA_DIR)
        try:
            # Check if file exists in data/
            requested_file = DATA_DIR / self.path.lstrip("/")
            if not requested_file.is_file():
                # Fallback to index.html for SPA
                self.path = "/index.html"
                requested_file = DATA_DIR / "index.html"

            # Expand PA:INCLUDE directives for HTML files
            if requested_file.is_file() and requested_file.suffix.lower() in {".html", ".htm"}:
                try:
                    content = requested_file.read_text(encoding="utf-8")
                    expanded = _expand_includes(content, DATA_DIR)
                    payload = expanded.encode("utf-8")

                    self.send_response(200)
                    self.send_header("Content-type", "text/html; charset=utf-8")
                    self.send_header("Content-Length", len(payload))
                    self.end_headers()
                    self.wfile.write(payload)
                    return
                except Exception as e:
                    self.send_error(500, f"Error expanding includes: {e}")
                    return

            super().do_GET()
        except Exception as e:
            self.send_error(500, f"Server error: {e}")

    def log_message(self, format, *args):
        """Log to stderr for cleaner output."""
        sys.stderr.write(f"[{self.client_address[0]}] {format % args}\n")


def main():
    """Start the fixture server."""
    if not DATA_DIR.exists():
        print(f"Error: {DATA_DIR} not found", file=sys.stderr)
        sys.exit(1)

    if not FIXTURE_FILE.exists():
        print(f"Warning: {FIXTURE_FILE} not found; /api/dome/layout will 404", file=sys.stderr)

    server = HTTPServer((HOST, PORT), FixtureHandler)
    url = f"http://{HOST}:{PORT}"
    print(f"Serving on {url}")
    print(f"  Static files: {DATA_DIR}")
    print(f"  Fixture: {FIXTURE_FILE}")
    print(f"  Press Ctrl+C to stop")

    try:
        server.serve_forever()
    except KeyboardInterrupt:
        print("\nShutdown.", file=sys.stderr)
        sys.exit(0)


if __name__ == "__main__":
    main()
