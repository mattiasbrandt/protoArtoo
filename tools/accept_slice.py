#!/usr/bin/env python3
"""Check a worker's gate block against the branch it claims to describe.

    python3 tools/accept_slice.py --json /tmp/slice-<n>.json --worktree <path> --base <branch>

The coordinate-epic critic protocol, step 1, as one command. Each row prints
PASS or FAIL, and a FAIL row says what to do next:

  gate result      the block's `ok` is true and its tree was not dirty
  head             the block's head is the worktree's HEAD
  base tip         the block's merge-base is the base's CURRENT tip
                   (`git rev-parse <base>`, not `git merge-base`). When the
                   base moved, the files changed on both sides are listed; no
                   overlap passes with a note, overlap fails
  verifier hashes  the block's gate, mutation and trace hashes equal those
                   files' blobs at the base tip
  worktree clean   `git status --porcelain` is empty but for
                   data/fs-version.json and data/fw-version.json

Exit 0 only when every row passes, 1 when a row fails, 2 when the block or
the base cannot be read.

Why: a block naming a head two commits behind the branch, and a gate script
changed mid-session under a block, were both caught by hand and by luck on
epic #175 (#469).
"""

from __future__ import annotations

import argparse
import json
import subprocess
import sys
from dataclasses import dataclass, field
from pathlib import Path

# The gate records each verifier's `git hash-object` cut to 12 characters, or
# "unknown" when the file could not be hashed. A gate older than the trace
# script writes no trace_script_hash at all.
VERIFIERS = (
    ("gate", "script_hash", "tools/slice_verify.py"),
    ("mut", "mutation_script_hash", "tools/mutation_verify.py"),
    ("trace", "trace_script_hash", "tools/web_load_trace.cjs"),
)
HASH_LEN = 12
UNKNOWN = "unknown"
# The device build stamps these two; nothing else may be dirty. Deliberately
# the two exact paths, not the gate's wider data/*version.json pattern.
STAMP_PATHS = {"data/fs-version.json", "data/fw-version.json"}


class Unreadable(Exception):
    """The block or the repository cannot be read; exit 2."""


@dataclass
class Row:
    label: str
    passed: bool
    detail: str
    next_step: str = ""
    notes: list[str] = field(default_factory=list)


def git(worktree: Path, *args: str, check: bool = True) -> subprocess.CompletedProcess:
    proc = subprocess.run(["git", "-C", str(worktree), *args],
                          capture_output=True, text=True, timeout=60)
    if check and proc.returncode != 0:
        raise Unreadable(f"git {' '.join(args)}: {proc.stderr.strip()}")
    return proc


def names(worktree: Path, *diff_args: str) -> set[str]:
    return set(git(worktree, "diff", "--name-only", *diff_args).stdout.split())


def check_result(gate: dict, ok) -> Row:
    dirty = gate.get("dirty")
    passed = ok is True and dirty is False
    return Row("gate result", passed, f"ok {ok}  dirty {dirty}",
               "" if passed else "read the block's FAIL rows; fix, commit, and re-run the gate on a clean tree")


def check_head(worktree: Path, gate: dict) -> Row:
    head = git(worktree, "rev-parse", "HEAD").stdout.strip()
    block = str(gate.get("head", ""))
    passed = block == head
    return Row("head", passed, f"block {block[:12] or '-'}  HEAD {head[:12]}",
               "" if passed else "the block is not of this HEAD: re-run the gate on HEAD")


def check_base_tip(worktree: Path, gate: dict, base: str, tip: str) -> Row:
    block = str(gate.get("merge_base", ""))
    detail = f"block {block[:12] or '-'}  {base} {tip[:12]}"
    if block == tip:
        return Row("base tip", True, detail)
    if not block or git(worktree, "cat-file", "-e", f"{block}^{{commit}}", check=False).returncode:
        return Row("base tip", False, detail,
                   "the block's merge-base is not a commit here: merge the base and re-run the gate")
    if git(worktree, "merge-base", "--is-ancestor", block, tip, check=False).returncode:
        return Row("base tip", False, detail,
                   f"{base} no longer contains the block's merge-base: merge {base} and re-run the gate")
    # The protocol's comm -12: files changed on the base since the block's
    # merge-base that this branch also changes.
    overlap = sorted(names(worktree, f"{block}..{tip}") & names(worktree, f"{block}...HEAD"))
    if not overlap:
        return Row("base tip", True, detail + "  moved, no overlap",
                   notes=[f"{base} moved since the block; no file overlap. Merge it and let the"
                          " wave's gate run be the proof, unless the merged work is this slice's subject"])
    return Row("base tip", False, detail + f"  moved, {len(overlap)} overlapping",
               f"the worker merges {base} and re-runs the gate; accept that block",
               notes=[f"overlap: {path}" for path in overlap])


def check_hashes(worktree: Path, gate: dict, tip: str) -> Row:
    shown, notes, drift = [], [], []
    for short, key, path in VERIFIERS:
        proc = git(worktree, "rev-parse", "--verify", "-q", f"{tip}:{path}", check=False)
        at_tip = proc.stdout.strip()[:HASH_LEN] if proc.returncode == 0 else None
        block = gate.get(key)
        block = None if block in (None, UNKNOWN) else str(block)
        shown.append(f"{short} {block or '-'}")
        if at_tip == block:
            if at_tip is None:
                notes.append(f"{path}: not at the base tip and not in the block")
            continue
        drift.append(f"{path}: block {block or 'none'}, base tip {at_tip or 'absent'}")
    passed = not drift
    return Row("verifier hashes", passed, "  ".join(shown),
               "" if passed else "the verifiers changed under the block: merge the base and re-run the gate",
               notes + drift)


def porcelain_paths(porcelain: str) -> list[str]:
    paths = []
    for line in porcelain.splitlines():
        path = line[3:]
        if " -> " in path:
            path = path.split(" -> ", 1)[1]
        paths.append(path)
    return paths


def check_clean(worktree: Path) -> Row:
    porcelain = git(worktree, "status", "--porcelain").stdout
    dirty = [path for path in porcelain_paths(porcelain) if path not in STAMP_PATHS]
    passed = not dirty
    return Row("worktree clean", passed, f"{len(dirty)} path(s) beyond the version stamps",
               "" if passed else "commit or discard these, then re-run the gate",
               [f"dirty: {path}" for path in dirty])


def load_block(path: Path) -> dict:
    try:
        block = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as err:
        raise Unreadable(f"{path}: {err}") from err
    if not isinstance(block, dict) or not isinstance(block.get("gate"), dict):
        raise Unreadable(f"{path}: no 'gate' object; is this a slice_verify --json block?")
    return block


def evaluate(block: dict, worktree: Path, base: str) -> list[Row]:
    gate = block["gate"]
    proc = git(worktree, "rev-parse", "--verify", "-q", f"{base}^{{commit}}", check=False)
    if proc.returncode:
        raise Unreadable(f"--base {base!r} does not name a commit in {worktree}")
    tip = proc.stdout.strip()
    return [
        check_result(gate, block.get("ok")),
        check_head(worktree, gate),
        check_base_tip(worktree, gate, base, tip),
        check_hashes(worktree, gate, tip),
        check_clean(worktree),
    ]


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[1])
    parser.add_argument("--json", type=Path, required=True, help="the gate's --json block")
    parser.add_argument("--worktree", type=Path, required=True, help="the worker's worktree")
    parser.add_argument("--base", required=True, help="the epic's integration branch")
    args = parser.parse_args(argv)

    if not args.worktree.is_dir():
        print(f"accept_slice: no worktree at {args.worktree}", file=sys.stderr)
        return 2
    try:
        rows = evaluate(load_block(args.json), args.worktree, args.base)
    except Unreadable as err:
        print(f"accept_slice: {err}", file=sys.stderr)
        return 2

    for row in rows:
        print(f"{'PASS' if row.passed else 'FAIL'}  {row.label:<16}{row.detail}")
        for note in row.notes:
            print(f"      {note}")
        if row.next_step:
            print(f"      next: {row.next_step}")
    failed = [row for row in rows if not row.passed]
    print(f"{'ACCEPT' if not failed else 'REJECT'}: {len(rows) - len(failed)}/{len(rows)} rows pass")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
