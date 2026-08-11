#!/usr/bin/env python3
"""Mechanical mutation-coverage check for the web suite.

Takes one or more mutation patches (unified diffs against HEAD), and for each:
applies it, proves from the working tree that it changed something, runs the
suite, restores, and prints one verdict row. The table is the evidence — paste
it verbatim; a reviewer re-runs the same command with the same patches.

Verdicts:
  KILLED          suite exited non-zero with at least one TAP `not ok` failure
                  and no cancellations — the only acceptable outcome
  KILLED-BY-HANG  suite exited non-zero but the kill was a timeout or a
                  cancelled test, not an assertion — rejected: a test that can
                  only fail by hanging vanishes from `# fail` and proves the
                  behaviour is unasserted
  SURVIVED        suite stayed green — the mutation is uncovered; rejected
  APPLY-FAIL      the patch did not apply cleanly to HEAD
  NO-TREE-CHANGE  the patch applied but left the working tree unchanged — the
                  silent-no-op case that motivated this tool

Requirements: a working tree clean beyond `data/*version.json` (restore uses
`git apply -R`; uncommitted work would be at risk), and patches produced
against HEAD (`git diff > mX.patch`, then revert the hand edit).

Exit code 0 only when every mutation is KILLED; 1 when any verdict is not
KILLED; 2 on operational errors (dirty tree, failed restore). A failed restore
aborts immediately and names the files left mutated.
"""

from __future__ import annotations

import argparse
import shlex
import sys

from slice_verify import (
    ROOT,
    VERSION_JSON_RE,
    WEB_TEST_TIMEOUT,
    info,
    parse_tap_counts,
    run,
)

LABEL_WIDTH = 34


def tracked_changes_present() -> bool:
    """Tracked modifications beyond data/*version.json put restore at risk.

    Untracked files are ignored: `git apply -R` only ever touches the files
    named in a patch, which are tracked by definition.
    """
    for line in run(["git", "status", "--porcelain", "-uno"]).stdout.splitlines():
        path = line[3:]
        if " -> " in path:
            path = path.split(" -> ", 1)[1]
        if not VERSION_JSON_RE.match(path):
            return True
    return False


def patch_files(patch_path: str) -> list[str]:
    proc = run(["git", "apply", "--numstat", patch_path])
    if proc.returncode != 0:
        return []
    return [line.split("\t")[2] for line in proc.stdout.splitlines() if line]


def tree_changed(files: list[str]) -> bool:
    proc = run(["git", "diff", "--quiet", "--", *files])
    return proc.returncode != 0


def restore(patch_path: str, files: list[str]) -> bool:
    proc = run(["git", "apply", "-R", patch_path])
    return proc.returncode == 0 and not tree_changed(files)


def run_suite(cmd: list[str], timeout: int) -> tuple[int, dict[str, int], bool]:
    proc = run(cmd, cwd=ROOT, timeout=timeout)
    output = proc.stdout + proc.stderr
    counts = parse_tap_counts(output) or {}
    has_not_ok = any(
        line.lstrip().startswith("not ok") for line in output.splitlines()
    )
    return proc.returncode, counts, has_not_ok


def verdict_for(
    code: int, counts: dict[str, int], has_not_ok: bool
) -> str:
    if code == 0:
        return "SURVIVED"
    timed_out = code == 124
    cancelled = counts.get("cancelled", 0)
    failed = counts.get("fail", 0)
    if failed >= 1 and cancelled == 0 and not timed_out and has_not_ok:
        return "KILLED"
    return "KILLED-BY-HANG"


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Apply mutation patches one at a time and prove each turns"
        " the suite red by assertion."
    )
    parser.add_argument("patches", nargs="+", help="unified diff files against HEAD")
    parser.add_argument(
        "--suite",
        default="make test-web",
        help="suite command (default: %(default)s)",
    )
    parser.add_argument(
        "--timeout",
        type=int,
        default=WEB_TEST_TIMEOUT,
        help="suite timeout in seconds (default: %(default)s)",
    )
    args = parser.parse_args()
    suite_cmd = shlex.split(args.suite)

    if tracked_changes_present():
        print(
            "error: tracked files changed beyond data/*version.json —"
            " commit or stash before running mutations (restore would risk"
            " uncommitted work)",
            file=sys.stderr,
        )
        return 2

    header = (
        f"{'mutation':<{LABEL_WIDTH}}{'files':<7}{'exit':<6}{'fail':<6}"
        f"{'cancelled':<11}verdict"
    )
    print(f"suite: {args.suite}")
    print(header)
    rows_ok = True
    for patch_path in args.patches:
        label = patch_path if len(patch_path) <= LABEL_WIDTH - 1 else (
            "..." + patch_path[-(LABEL_WIDTH - 4):]
        )
        files = patch_files(patch_path)
        apply_proc = run(["git", "apply", patch_path])
        if apply_proc.returncode != 0 or not files:
            print(f"{label:<{LABEL_WIDTH}}{'-':<7}{'-':<6}{'-':<6}{'-':<11}APPLY-FAIL")
            info(apply_proc.stderr.strip() or "patch reported no files")
            rows_ok = False
            continue
        if not tree_changed(files):
            print(
                f"{label:<{LABEL_WIDTH}}{len(files):<7}{'-':<6}{'-':<6}{'-':<11}"
                "NO-TREE-CHANGE"
            )
            rows_ok = False
            continue
        info(f"mutation applied ({', '.join(files)}); running suite...")
        code, counts, has_not_ok = run_suite(suite_cmd, args.timeout)
        verdict = verdict_for(code, counts, has_not_ok)
        print(
            f"{label:<{LABEL_WIDTH}}{len(files):<7}{code:<6}"
            f"{counts.get('fail', '-'):<6}{counts.get('cancelled', '-'):<11}{verdict}"
        )
        if verdict != "KILLED":
            rows_ok = False
        if not restore(patch_path, files):
            print(
                f"error: restore failed after {patch_path}; working tree still"
                f" mutated in: {', '.join(files)}",
                file=sys.stderr,
            )
            return 2

    print()
    print("mutation gate:", "PASS" if rows_ok else "FAIL")
    return 0 if rows_ok else 1


if __name__ == "__main__":
    sys.exit(main())
