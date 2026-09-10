#!/usr/bin/env python3
"""Set up a worker's branch and worktree for an epic sub-issue, at the RIGHT commit.

WHY THIS EXISTS
---------------
`gh issue develop <n> --base <branch>` creates the branch **server-side**, from
the **remote** ref: "the new development branch will be created from the
specified remote branch" (gh's own help). An epic's integration branch, however,
advances **locally** - the coordinator merges each accepted slice into it and
AGENTS.md "Push and remote policy" makes pushing a shared integration branch an
operator-approved act. So `origin/<base>` sits N merges behind `<base>`, and
every worktree made this way starts life at the wrong commit.

The answer used to be a sentence in the coordinate-epic skill telling the
coordinator to remember a `reset --hard` after every single worktree. It was
remembered - and it is still the wrong shape, because a rule that must be
remembered on every repetition is a defect waiting for the repetition where it
is not. This script does it instead, and refuses to hand over a worktree it
could not put on the base sha.

It also fixes the *linked* branch rather than leaving it lying. After the reset,
the local branch is ahead of the remote branch `gh` just created, and the remote
one still points at the stale base - so the branch shown under the issue's
Development section describes a base the work is not on. Pushing the branch
fast-forwards it onto the true base (no force: the stale base is an ancestor),
and AGENTS.md prices that push at "Free - no approval" for a branch you own.

WHAT IT DOES NOT DO
-------------------
It does not push `<base>` itself. That is the one act that would remove the
divergence at its source, and AGENTS.md reserves it for the operator. The script
reports the gap instead, every run, so the decision stays visible rather than
becoming background noise.

USAGE
    python3 tools/epic_worktree.py <issue> --base <branch> --name <branch-name>
                                   [--path ../wt-<issue>] [--no-push] [--dry-run]
    python3 tools/epic_worktree.py --check <path> --base <branch>

Exit codes: 0 = the worktree is on the base tip; 1 = it is not, or a step failed.
"""

from __future__ import annotations

import argparse
import subprocess
import sys
from pathlib import Path


class StepFailed(RuntimeError):
    """A git or gh step exited non-zero; the message carries its output."""


def run(cmd: list[str], cwd: Path | None = None, check: bool = True) -> str:
    proc = subprocess.run(
        cmd, cwd=str(cwd) if cwd else None, capture_output=True, text=True
    )
    if check and proc.returncode != 0:
        raise StepFailed(
            f"{' '.join(cmd)} exited {proc.returncode}\n"
            f"{(proc.stdout + proc.stderr).strip()}"
        )
    return proc.stdout.strip()


def rev_parse(ref: str, cwd: Path | None = None) -> str:
    return run(["git", "rev-parse", ref], cwd=cwd)


def count_ahead(base: str, remote_ref: str, cwd: Path | None = None) -> int | None:
    """How many commits <base> has that <remote_ref> does not.

    None when the remote ref does not exist, which is the ordinary state for a
    branch that has never been pushed - not an error.
    """
    try:
        out = run(["git", "rev-list", "--count", f"{remote_ref}..{base}"], cwd=cwd)
    except StepFailed:
        return None
    return int(out)


def divergence_note(base: str, ahead: int | None) -> str:
    """The line that keeps the unpushed gap visible on every single run."""
    if ahead is None:
        return f"origin has no {base}; nothing to compare"
    if ahead == 0:
        return f"origin/{base} is level with {base}; gh would have branched correctly"
    commits = "commit" if ahead == 1 else "commits"
    return (
        f"origin/{base} is {ahead} {commits} behind {base} - "
        f"which is exactly why the worktree needed moving"
    )


def verdict(head: str, base_sha: str) -> tuple[bool, str]:
    """Whether a worktree sits on the base tip, and the line that says so."""
    if head == base_sha:
        return True, f"OK   HEAD {head[:8]} == {base_sha[:8]}"
    return False, f"FAIL HEAD {head[:8]} != base tip {base_sha[:8]}"


def default_path(issue: int) -> str:
    return f"../wt-{issue}"


def create(args: argparse.Namespace) -> int:
    root = Path(run(["git", "rev-parse", "--show-toplevel"]))
    base_sha = rev_parse(args.base)
    ahead = count_ahead(args.base, f"origin/{args.base}")
    path = Path(args.path or default_path(args.issue))

    print(f"[epic-worktree] base {args.base} @ {base_sha[:8]}")
    print(f"[epic-worktree] {divergence_note(args.base, ahead)}")

    if args.dry_run:
        print(f"[epic-worktree] dry run: would create {args.name} and {path}")
        return 0

    # gh creates this on the remote, from origin/<base> - stale by construction.
    print(f"[epic-worktree] gh issue develop {args.issue} --name {args.name}")
    run(
        [
            "gh", "issue", "develop", str(args.issue),
            "--base", args.base, "--name", args.name,
        ],
        cwd=root,
    )
    run(["git", "worktree", "add", str(path), args.name], cwd=root)

    # The whole point: put it where the coordinator's base actually is.
    wt = (root / path).resolve()
    run(["git", "reset", "--hard", base_sha], cwd=wt)
    ok, line = verdict(rev_parse("HEAD", cwd=wt), base_sha)
    print(f"[epic-worktree] {line}")
    if not ok:
        return 1

    if args.push:
        # Fast-forward, never a force: origin/<name> is at the stale base, which
        # is an ancestor of base_sha. AGENTS.md prices this push as free.
        run(["git", "push", "origin", f"HEAD:{args.name}"], cwd=wt)
        print(f"[epic-worktree] pushed {args.name} -> the linked branch now names the real base")
    else:
        print(
            f"[epic-worktree] --no-push: the linked branch on origin still points at "
            f"the stale base, so the issue's Development section will misdescribe it"
        )

    print(f"[epic-worktree] worktree ready: {wt}")
    return 0


def check(args: argparse.Namespace) -> int:
    wt = Path(args.check).resolve()
    base_sha = rev_parse(args.base)
    ok, line = verdict(rev_parse("HEAD", cwd=wt), base_sha)
    print(f"[epic-worktree] {wt}")
    print(f"[epic-worktree] {line}")
    return 0 if ok else 1


def build_parser() -> argparse.ArgumentParser:
    p = argparse.ArgumentParser(
        description="Create an epic sub-issue's linked branch and worktree at the local base tip.",
    )
    p.add_argument("issue", nargs="?", type=int, help="sub-issue number")
    p.add_argument("--base", required=True, help="the epic's integration branch")
    p.add_argument("--name", help="branch name to create, e.g. feat/338-addressed-rows")
    p.add_argument("--path", help="worktree path (default ../wt-<issue>)")
    p.add_argument(
        "--check", metavar="PATH",
        help="verify an existing worktree sits on the base tip, and do nothing else",
    )
    p.add_argument(
        "--no-push", dest="push", action="store_false",
        help="leave the linked branch on origin pointing at the stale base",
    )
    p.add_argument("--dry-run", action="store_true", help="report, create nothing")
    return p


def main(argv: list[str] | None = None) -> int:
    args = build_parser().parse_args(argv)
    try:
        if args.check:
            return check(args)
        if args.issue is None or not args.name:
            print("epic_worktree: <issue> and --name are required to create", file=sys.stderr)
            return 1
        return create(args)
    except StepFailed as exc:
        print(f"[epic-worktree] {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
