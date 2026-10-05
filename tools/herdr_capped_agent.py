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
     (e.g. `-- --resume <session-id>`); Herdr returns only once the agent is
     ready for input,
  4. with --brief PATH, submits "Read PATH in full, then carry it out end to
     end." as the first prompt (the brief itself is multi-KB and stays in its
     file, as worker dispatch always did), and returns once
     Herdr sees the agent working (or blocked on a question), not when the
     turn ends: `agent prompt --wait --until working --until blocked`. Herdr
     reports agent_prompt_stalled when the agent does not start within five
     seconds of the submission - the miss that, on 2026-09-13, left a worker
     idle with an empty input line after an exit-0 prompt.

Usage:
  python3 tools/herdr_capped_agent.py --pane w8:p82 --name w411w --brief /tmp/brief-411.md
  python3 tools/herdr_capped_agent.py --pane w8:p82 --name w411w -- --resume <id>

The pane must be at an interactive shell prompt. Nothing here edits a file.
"""

from __future__ import annotations

import argparse
import json
import os
import subprocess
import sys
import time

PROMPT_MARK = "❯"  # the zsh prompt glyph this machine's shells print


def run(cmd: list[str]) -> str:
    proc = subprocess.run(cmd, capture_output=True, text=True)
    if proc.returncode != 0:
        # Herdr prints its JSON error on stderr (agent_not_ready, agent_not_found,
        # ...); a bare CalledProcessError traceback threw it away.
        raise SystemExit(f"[capped-agent] {' '.join(cmd[:4])} exited {proc.returncode}: "
                         f"{(proc.stderr or proc.stdout).strip()}")
    return proc.stdout


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


def submit_prompt(name: str, text: str, timeout_ms: int) -> int:
    """Send the first prompt and confirm the agent took it. 0 when it did.

    A stall or timeout does not prove nothing was sent (Herdr's agent
    automation docs), so this never re-sends: it shows the pane and stops.
    """
    proc = subprocess.run(
        ["herdr", "agent", "prompt", name, text, "--wait",
         "--until", "working", "--until", "blocked", "--timeout", str(timeout_ms)],
        capture_output=True, text=True,
    )
    if proc.returncode == 0:
        status = json.loads(proc.stdout).get("result", {}).get("agent", {}).get("agent_status")
        print(f"[capped-agent] {name} took the prompt: {status}")
        return 0
    print(f"[capped-agent] {name} did not confirm the prompt: {(proc.stderr or proc.stdout).strip()}",
          file=sys.stderr)
    print("[capped-agent] Read the pane before re-sending; the prompt may have landed:", file=sys.stderr)
    print(run(["herdr", "agent", "read", name, "--source", "recent-unwrapped", "--lines", "30"]),
          file=sys.stderr)
    return 1


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
    parser.add_argument("--brief", help="prompt the agent to read this file and carry it out; confirm it started")
    parser.add_argument("--prompt-timeout-ms", type=int, default=60000)
    parser.add_argument("agent_args", nargs="*", help="passed to the agent after --")
    args = parser.parse_args(argv)
    prompt = None
    if args.brief:
        # Checked before anything starts: a bad path should not leave an idle agent.
        brief = os.path.abspath(args.brief)
        if not os.path.isfile(brief) or os.path.getsize(brief) == 0:
            raise SystemExit(f"[capped-agent] no brief at {brief}, or it is empty")
        prompt = f"Read {brief} in full, then carry it out end to end."

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
    if prompt is not None:
        return submit_prompt(args.name, prompt, args.prompt_timeout_ms)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
