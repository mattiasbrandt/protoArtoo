#!/usr/bin/env python3
"""Start an agent in a Herdr pane, inside its own memory-capped systemd scope.

Why this exists (2026-09-29): a worker ran one web test whose failing
assertion printed an entire mini_dom page, growing memory at about 1 GB/s.
Nothing bounded it, so the machine swapped until systemd-oomd killed the
terminal's whole cgroup, which held the Herdr server and every agent session,
twice. A per-agent scope turns that into one process killed inside its own
limit: measured the same evening, the runaway hit a 6 GB cap and the kernel
killed only `node`, while the pane's shell, the agent and the rest of the
machine carried on.

What it does, in order:
  1. starts `zsh` in the pane under `systemd-run --user --scope` with
     MemoryMax, MemorySwapMax=0 and OOMPolicy=continue (continue: the kernel
     kills the offending process and the scope - the agent - lives on),
  2. checks the scope is active with that MemoryMax, and refuses otherwise,
  3. runs `herdr agent start` in that shell, passing any agent arguments
     (e.g. `-- --resume <session-id>`).

Usage:
  python3 tools/herdr_capped_agent.py --pane w8:p82 --name w411w
  python3 tools/herdr_capped_agent.py --pane w8:p82 --name w411w -- --resume <id>

The pane must be at an interactive shell prompt. Nothing here edits a file.
"""

from __future__ import annotations

import argparse
import json
import subprocess
import sys
import time

PROMPT_MARK = "❯"  # the zsh prompt glyph this machine's shells print


def run(cmd: list[str]) -> str:
    return subprocess.run(cmd, check=True, capture_output=True, text=True).stdout


def pane_text(pane: str) -> str:
    return run(["herdr", "pane", "read", pane])


def wait_for_prompt(pane: str, timeout_s: float) -> None:
    deadline = time.monotonic() + timeout_s
    while time.monotonic() < deadline:
        tail = pane_text(pane).rstrip().splitlines()[-3:]
        if any(PROMPT_MARK in line for line in tail):
            return
        time.sleep(0.5)
    raise SystemExit(f"[capped-agent] pane {pane} showed no shell prompt within {timeout_s:.0f}s")


def scope_props(unit: str) -> dict[str, str]:
    out = subprocess.run(
        ["systemctl", "--user", "show", unit, "-p", "ActiveState", "-p", "MemoryMax", "-p", "MemorySwapMax"],
        capture_output=True, text=True,
    ).stdout
    return dict(line.split("=", 1) for line in out.splitlines() if "=" in line)


def to_bytes(size: str) -> int:
    units = {"K": 1 << 10, "M": 1 << 20, "G": 1 << 30}
    return int(float(size[:-1]) * units[size[-1].upper()]) if size[-1].upper() in units else int(size)


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("--pane", required=True, help="Herdr pane id, exactly as printed (ids can end in a letter)")
    parser.add_argument("--name", required=True, help="agent name for herdr, also names the scope")
    parser.add_argument("--kind", default="claude")
    parser.add_argument("--mem", default="10G", help="MemoryMax for the agent and everything it runs (default 10G)")
    parser.add_argument("agent_args", nargs="*", help="passed to the agent after --")
    args = parser.parse_args(argv)

    unit = f"pa-agent-{args.name}-{int(time.time())}.scope"
    wait_for_prompt(args.pane, 30)
    # `exec`: herdr starts an agent only in the pane's own shell process, and
    # a nested shell is "not an available shell" (measured 2026-09-29). If
    # systemd-run fails the pane's shell is gone and the check below refuses,
    # so an agent is never started uncapped.
    run(["herdr", "pane", "run", args.pane,
         f"exec systemd-run --user --scope --quiet --unit={unit} -p MemoryMax={args.mem} "
         f"-p MemorySwapMax=0 -p OOMPolicy=continue zsh"])
    time.sleep(1.5)
    wait_for_prompt(args.pane, 30)

    props = scope_props(unit)
    if props.get("ActiveState") != "active" or props.get("MemoryMax") != str(to_bytes(args.mem)):
        raise SystemExit(f"[capped-agent] scope {unit} is not active with MemoryMax={args.mem}: {props}. "
                         "Not starting the agent uncapped.")
    print(f"[capped-agent] {unit}: MemoryMax={props['MemoryMax']} MemorySwapMax={props.get('MemorySwapMax')}")

    start = ["herdr", "agent", "start", args.name, "--kind", args.kind, "--pane", args.pane]
    if args.agent_args:
        start += ["--", *args.agent_args]
    result = json.loads(run(start))
    status = result.get("result", {}).get("agent", {}).get("agent_status")
    print(f"[capped-agent] {args.name} started in {args.pane}: {status}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
