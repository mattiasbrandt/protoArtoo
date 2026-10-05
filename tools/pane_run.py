#!/usr/bin/env python3
"""Run one command in a fresh Herdr pane, wait for it, and say how it ended.

WHY THIS EXISTS
---------------
A device or gate session used to be four hand-typed steps, every time: split a
pane, `herdr pane run` the command under tools/gate_in_pane.sh, poll the log in
a `for i in $(seq ...)` loop for GATE_EXIT=, then grep the lines that mattered
out of the log. One epic day wrote that sequence about 40 times. This is the
sequence, once:

    python3 tools/pane_run.py /tmp/build.log -- make build BUILD_ENV=artoo_esp32
    python3 tools/pane_run.py /tmp/flash.log --script /tmp/flash.sh --grep 'error|GATE'

It splits a sibling pane next to the caller's, runs the command there under
gate_in_pane.sh (the operator can watch the pane; the log is what is
verified), waits for the log's GATE_EXIT=<n> line, prints the tail, closes the
pane it made, and exits <n>. It never closes any other pane.

--script exists for the upload guard: it matches "make flash" or "make ota"
anywhere in a Bash command, so an upload has to live in a file (written with
the Write tool) and reach the pane by path. `--script FILE` runs `bash FILE`.

The pane's working directory is --cwd, default the caller's: a tool that writes
a run directory relative to its cwd writes it where you ran this, not wherever
a reused pane happened to be.

Herdr facts this relies on, read 2026-10-05: `herdr pane read` can return
nothing while a command is running, and `pane wait-output` matches the command
text itself when a sentinel appears in it - so the log is polled, never the
pane.

Exit codes: the command's own; 2 for usage; 3 when Herdr is not available; 124
when --timeout passes (the pane is left running and named).
"""

from __future__ import annotations

import argparse
import json
import os
import re
import shlex
import subprocess
import sys
import time
from pathlib import Path

GATE_IN_PANE = Path(__file__).resolve().parent / "gate_in_pane.sh"
EXIT_RE = re.compile(r"^GATE_EXIT=(\d+)$")
EXIT_NO_HERDR = 3
EXIT_TIMEOUT = 124


def note(message: str) -> None:
    print(f"[pane-run] {message}", file=sys.stderr, flush=True)


def herdr(*args: str) -> dict:
    proc = subprocess.run(["herdr", *args], capture_output=True, text=True)
    if proc.returncode != 0:
        raise RuntimeError(f"herdr {' '.join(args)} exited {proc.returncode}: "
                           f"{(proc.stdout + proc.stderr).strip()}")
    return json.loads(proc.stdout) if proc.stdout.strip() else {}


def gate_exit(log: Path) -> int | None:
    """The GATE_EXIT code once gate_in_pane.sh has written its last line, else None."""
    try:
        lines = log.read_text(errors="replace").splitlines()
    except OSError:
        return None
    for line in reversed(lines):
        if line.strip():
            match = EXIT_RE.match(line.strip())
            return int(match.group(1)) if match else None
    return None


def summary(log: Path, tail: int, pattern: str | None) -> list[str]:
    """The last `tail` lines of the log, or of the lines matching `pattern`."""
    lines = [l for l in log.read_text(errors="replace").splitlines() if not EXIT_RE.match(l.strip())]
    if pattern:
        rx = re.compile(pattern)
        lines = [l for l in lines if rx.search(l)]
    return lines[-tail:] if tail > 0 else []


def pane_command(log: Path, command: list[str]) -> str:
    return shlex.join([str(GATE_IN_PANE), str(log), "--", *command])


def build_parser() -> argparse.ArgumentParser:
    p = argparse.ArgumentParser(
        description="Run a command in a new Herdr pane, wait for GATE_EXIT, close the pane.",
        usage="pane_run.py LOG [options] (--script FILE | -- command ...)",
    )
    p.add_argument("log", type=Path, help="log file; its last line becomes GATE_EXIT=<n>")
    p.add_argument("--script", type=Path, help="run `bash FILE` instead of a -- command")
    p.add_argument("--cwd", type=Path, default=Path.cwd(), help="pane working directory (default: here)")
    p.add_argument("--direction", choices=("right", "down"), default="right")
    p.add_argument("--tail", type=int, default=40, help="lines of the log to print (default 40)")
    p.add_argument("--grep", help="print only log lines matching this regex (then --tail)")
    p.add_argument("--timeout", type=float, default=7200, help="seconds to wait (default 7200)")
    p.add_argument("--interval", type=float, default=2.0, help="log poll interval in seconds")
    p.add_argument("--keep", action="store_true", help="leave the pane open afterwards")
    return p


def main(argv: list[str] | None = None) -> int:
    argv = list(sys.argv[1:] if argv is None else argv)
    command: list[str] = []
    if "--" in argv:
        split = argv.index("--")
        argv, command = argv[:split], argv[split + 1:]
    args = build_parser().parse_args(argv)
    if bool(command) == bool(args.script):
        note("give exactly one of --script FILE or -- command ...")
        return 2
    if args.script:
        script = args.script.resolve()
        if not script.is_file():
            note(f"no script at {script}")
            return 2
        command = ["bash", str(script)]
    if os.environ.get("HERDR_ENV") != "1":
        note("not inside Herdr (HERDR_ENV is not 1); run the command in a terminal yourself")
        return EXIT_NO_HERDR

    log = args.log.resolve()
    if log.exists():
        # Its old GATE_EXIT line would end the wait before the command starts.
        note(f"replacing the existing log {log}")
        log.unlink()

    try:
        # Herdr sets HERDR_PANE_ID in every pane it starts: next to the pane
        # this runs in, not whichever pane has focus right now.
        own = os.environ.get("HERDR_PANE_ID")
        target = ["--pane", own] if own else ["--current"]
        split = herdr("pane", "split", *target, "--direction", args.direction,
                      "--cwd", str(args.cwd.resolve()), "--no-focus")
        pane = split["result"]["pane"]["pane_id"]
        herdr("pane", "run", pane, pane_command(log, command))
    except (RuntimeError, KeyError, ValueError) as err:
        note(f"could not start the pane: {err}")
        return EXIT_NO_HERDR
    note(f"pane {pane}: {shlex.join(command)}")
    note(f"log {log}")

    deadline = time.monotonic() + args.timeout
    code = gate_exit(log)
    while code is None:
        if time.monotonic() >= deadline:
            note(f"no GATE_EXIT after {args.timeout:.0f}s; pane {pane} left running")
            return EXIT_TIMEOUT
        time.sleep(args.interval)
        code = gate_exit(log)

    for line in summary(log, args.tail, args.grep):
        print(line)
    print(f"GATE_EXIT={code}")
    print(f"log: {log}")
    if not args.keep:
        try:
            herdr("pane", "close", pane)
        except RuntimeError as err:
            note(f"could not close pane {pane}: {err}")
    return code


if __name__ == "__main__":
    sys.exit(main())
