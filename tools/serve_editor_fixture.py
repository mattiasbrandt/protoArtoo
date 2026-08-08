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
  - /* -> data/* (static files, fallback to index.html for SPA routing)
"""

import json
import os
import re
import sys
from http.server import HTTPServer, SimpleHTTPRequestHandler
from pathlib import Path

REPO_ROOT = Path(__file__).parent.parent
DATA_DIR = REPO_ROOT / "data"
FIXTURE_FILE = REPO_ROOT / "tests" / "fixtures" / "dome_layout_mk4.json"
PORT = 4173
HOST = "localhost"

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

    def do_GET(self):
        """Handle GET requests with fixture interception."""
        # Intercept API routes
        if self.path == "/api/dome/layout":
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
