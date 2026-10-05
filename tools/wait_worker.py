#!/usr/bin/env python3
"""Wait until a worker says it is done.

The idle pane is not that signal: the gate runs in a sibling pane and the
worker pane looks idle while it does. The worker writes one of these, and
this waits on it:

    WORKER_DONE: ok
    WORKER_DONE: blocked

as the last non-empty line of its status comment (the comment whose body
starts with --marker). A trailing "//" signature is not that line. A later
line that is not the token means the worker is not done. Or a JSON file
--file with a boolean "ok".

Given both, both must agree: ok only when the comment AND the file say ok,
blocked as soon as either says blocked. The file alone is the gate's output,
and a gate run made mid-slice writes it while the worker is still writing its
report - on 2026-10-05 a --file watcher fired WORKER_DONE: ok on exactly
that. The comment's token never misfired. Pass both when you have both.

    python3 tools/wait_worker.py --issue 441 --marker '<!-- worker-status-441-slug -->'
    python3 tools/wait_worker.py --issue 441 --marker '<!-- ... -->' --file /tmp/slice-441.json
    python3 tools/wait_worker.py --file /tmp/slice-441.json

Exit 0 when the worker says ok, 1 when it says blocked or the wait ends.
"""

from __future__ import annotations

import argparse
import json
import re
import subprocess
import sys
import time
from pathlib import Path

_SIGNATURE = re.compile(r"^//\S")


def _comment_bodies(issue: int) -> list[str]:
    raw = subprocess.run(
        [
            "gh", "api", "--paginate",
            f"repos/{{owner}}/{{repo}}/issues/{issue}/comments",
            "--jq", ".[] | .body | @base64",
        ],
        check=True, capture_output=True, text=True,
    )
    import base64
    bodies = []
    for line in raw.stdout.splitlines():
        if line.strip():
            bodies.append(base64.b64decode(line.strip()).decode("utf-8"))
    return bodies


def _verdict_from_text(body: str) -> str | None:
    """The token only counts as the last line. A trailing // signature is ignored."""
    lines = [line.strip() for line in body.splitlines()]
    while lines and lines[-1] == "":
        lines.pop()
    if lines and _SIGNATURE.match(lines[-1]):
        lines.pop()
        while lines and lines[-1] == "":
            lines.pop()
    if not lines:
        return None
    if lines[-1] == "WORKER_DONE: ok":
        return "ok"
    if lines[-1] == "WORKER_DONE: blocked":
        return "blocked"
    return None


def _verdict_from_comment(issue: int, marker: str) -> str | None:
    for body in _comment_bodies(issue):
        if not body.startswith(marker):
            continue
        return _verdict_from_text(body)
    return None


def _verdict_from_file(path: Path) -> str | None:
    if not path.is_file():
        return None
    try:
        data = json.loads(path.read_text(encoding="utf-8"))
    except json.JSONDecodeError:
        return None
    if not isinstance(data, dict) or "ok" not in data:
        return None
    return "ok" if data["ok"] is True else "blocked" if data["ok"] is False else None


def _combined(verdicts: list[str | None]) -> str | None:
    """Blocked if any signal says blocked, ok only if every signal says ok."""
    if "blocked" in verdicts:
        return "blocked"
    if verdicts and all(v == "ok" for v in verdicts):
        return "ok"
    return None


def main(argv: list[str] | None = None) -> int:
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--issue", type=int)
    p.add_argument("--marker", help="the status comment's first line")
    p.add_argument("--file", type=Path, help='JSON file with a boolean "ok"')
    p.add_argument("--timeout", type=float, default=7200, help="seconds (default 7200)")
    p.add_argument("--interval", type=float, default=15)
    args = p.parse_args(argv)
    use_comment = args.issue is not None or bool(args.marker)
    if use_comment and (args.issue is None or not args.marker):
        p.error("--issue and --marker go together")
    if args.file is None and not use_comment:
        p.error("pass --issue and --marker, --file, or all three")
    deadline = time.monotonic() + args.timeout
    while True:
        verdicts = []
        if use_comment:
            verdicts.append(_verdict_from_comment(args.issue, args.marker))
        if args.file is not None:
            verdicts.append(_verdict_from_file(args.file))
        verdict = _combined(verdicts)
        if verdict == "ok":
            print("WORKER_DONE: ok")
            return 0
        if verdict == "blocked":
            print("WORKER_DONE: blocked")
            return 1
        if time.monotonic() >= deadline:
            print("WORKER_DONE: timeout", file=sys.stderr)
            return 1
        time.sleep(args.interval)


if __name__ == "__main__":
    raise SystemExit(main())
