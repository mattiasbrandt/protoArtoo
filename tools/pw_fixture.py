#!/usr/bin/env python3
"""Run one folder of fixture Playwright scripts with the env they need.

    make pw-fixture DIR=seq

Starts tools/serve_editor_fixture.py on a free port and runs every script in
test/playwright/<DIR>/ whose `// bench-auto: fixture <page>.html` line names a
page. Each run gets FIXTURE=1, BASE_URL and TARGET_URL at that page, and
HEADLESS=true. A route override installed after the page has loaded does not
reach loadRehearsalFacts(); install it before goto, or page.reload() after.

The server is stopped when the folder finishes. Exit status is the first
script that did not exit 0.
"""

from __future__ import annotations

import argparse
import os
import socket
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
PLAYWRIGHT = ROOT / "test" / "playwright"
# The path test/playwright/README.md gives. NODE_PATH wins when set.
NODE_PATH_DEFAULT = Path.home() / ".npm" / "_npx" / "e41f203b7505f1fb" / "node_modules"


def _free_port() -> int:
    with socket.socket() as sock:
        sock.bind(("127.0.0.1", 0))
        return sock.getsockname()[1]


def _fixture_page(script: Path) -> str | None:
    for line in script.read_text(encoding="utf-8").splitlines():
        text = line.strip()
        if text.startswith("// bench-auto: fixture "):
            page = text.split()[-1]
            if page.endswith(".html"):
                return page
    return None


def _node_env(base: str, page: str) -> dict[str, str]:
    env = dict(os.environ)
    env["FIXTURE"] = "1"
    env["HEADLESS"] = "true"
    env["BASE_URL"] = base
    env["TARGET_URL"] = f"{base}/{page}"
    if "NODE_PATH" not in env and NODE_PATH_DEFAULT.is_dir():
        env["NODE_PATH"] = str(NODE_PATH_DEFAULT)
    return env


def main(argv: list[str] | None = None) -> int:
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--dir", required=True, help="folder under test/playwright, for example seq")
    args = p.parse_args(argv)
    folder = PLAYWRIGHT / args.dir
    if not folder.is_dir():
        print(f"no such folder: {folder}", file=sys.stderr)
        return 2
    scripts = []
    for script in sorted(folder.glob("*.js")):
        page = _fixture_page(script)
        if page:
            scripts.append((script, page))
    if not scripts:
        print(f"no fixture scripts in {folder}", file=sys.stderr)
        return 2

    port = _free_port()
    base = f"http://127.0.0.1:{port}"
    server = subprocess.Popen(
        [sys.executable, str(ROOT / "tools" / "serve_editor_fixture.py")],
        env={**os.environ, "PA_FIXTURE_PORT": str(port)},
        cwd=ROOT,
    )
    try:
        deadline = time.monotonic() + 10
        while time.monotonic() < deadline:
            if server.poll() is not None:
                print("fixture server exited before it listened", file=sys.stderr)
                return 1
            try:
                with socket.create_connection(("127.0.0.1", port), timeout=0.2):
                    break
            except OSError:
                time.sleep(0.1)
        else:
            print("fixture server did not listen", file=sys.stderr)
            return 1
        failed = 0
        for script, page in scripts:
            print(f"== {script.relative_to(ROOT)}  {page}")
            r = subprocess.run(["node", str(script)], cwd=ROOT, env=_node_env(base, page))
            if r.returncode != 0:
                failed += 1
                print(f"FAIL {script.name} exit {r.returncode}", file=sys.stderr)
                return r.returncode
        print(f"ok {len(scripts)} {args.dir}")
        return 0 if failed == 0 else 1
    finally:
        server.terminate()
        try:
            server.wait(timeout=5)
        except subprocess.TimeoutExpired:
            server.kill()


if __name__ == "__main__":
    raise SystemExit(main())
