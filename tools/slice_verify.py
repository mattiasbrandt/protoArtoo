#!/usr/bin/env python3
"""Mechanical PASS/FAIL gate for a working branch against a base ref.

Runs the checks required by AGENTS.md's verification section and prints a
stable, copy-pasteable block. Workers paste the block verbatim into their
status comment; a reviewer re-runs the same command and compares.

Diff checks compare merge-base(<base>, HEAD) against HEAD — committed work
only, so commit the slice before running the gate. Working-tree changes are
excluded deliberately: the device build itself stamps data/*version.json, so a
working-tree diff would always self-flag. Pre-existing instances in untouched
files are never reported. Checks:

1. native test total at HEAD vs base, plus suite pass/fail
2. `pio run -e protoArtoo` exit code (never bare `pio run`)
3. `tools/check_action_registry_drift.py` exit code
4. `data/*version.json` must not appear in the diff
5. no new `extern` declarations added inside `.cpp` files (`extern "C"` allowed)
6. no new `#ifdef ARDUINO` / `#ifndef ARDUINO` blocks added under `include/`

Exit code 0 only when every check passes. Dependency-free: stdlib + git + pio.
"""

from __future__ import annotations

import argparse
import json
import re
import subprocess
import sys
import tempfile
from dataclasses import dataclass
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
CACHE_PATH = ROOT / ".pio" / "slice-verify-cache.json"
VERSION_JSON_RE = re.compile(r"^data/.*version\.json$")
EXTERN_RE = re.compile(r'\bextern\b(?!\s*"C")')
ARDUINO_GUARD_RE = re.compile(
    r"#\s*(?:ifndef|ifdef)\s+ARDUINO\b|#\s*(?:if|elif)\b.*defined\s*\(\s*ARDUINO\s*\)"
)
TEST_TOTAL_RE = re.compile(r"(\d+) test cases:")
TEST_SUCCEEDED_RE = re.compile(r"(\d+) succeeded")
TEST_FAILED_RE = re.compile(r"(\d+) failed")

LABEL_WIDTH = 29
DETAIL_WIDTH = 26


@dataclass
class CheckResult:
    label: str
    detail: str
    passed: bool
    notes: list[str]


def info(message: str) -> None:
    print(f"[slice_verify] {message}", file=sys.stderr, flush=True)


def run(cmd: list[str], cwd: Path = ROOT) -> subprocess.CompletedProcess:
    return subprocess.run(cmd, cwd=cwd, capture_output=True, text=True)


def git(args: list[str], cwd: Path = ROOT) -> str:
    proc = run(["git", *args], cwd=cwd)
    if proc.returncode != 0:
        raise RuntimeError(f"git {' '.join(args)} failed: {proc.stderr.strip()}")
    return proc.stdout


def parse_native_summary(output: str) -> tuple[int, int] | None:
    total_match = TEST_TOTAL_RE.search(output)
    if not total_match:
        return None
    total = int(total_match.group(1))
    succeeded_match = TEST_SUCCEEDED_RE.search(output)
    if succeeded_match:
        return total, int(succeeded_match.group(1))
    failed_match = TEST_FAILED_RE.search(output)
    if failed_match:
        return total, total - int(failed_match.group(1))
    return None


def run_native_tests(cwd: Path) -> tuple[int, tuple[int, int] | None, str]:
    proc = run(["pio", "test", "-e", "native"], cwd=cwd)
    output = proc.stdout + proc.stderr
    return proc.returncode, parse_native_summary(output), output


def load_cache() -> dict:
    try:
        return json.loads(CACHE_PATH.read_text())
    except (OSError, ValueError):
        return {}


def save_cache(cache: dict) -> None:
    try:
        CACHE_PATH.parent.mkdir(exist_ok=True)
        CACHE_PATH.write_text(json.dumps(cache))
    except OSError:
        pass


def base_native_total(base_sha: str) -> tuple[int | None, list[str]]:
    """Native test total at the base commit, via a throwaway worktree.

    Cached per commit SHA so repeated gate runs do not rebuild the base suite.
    """
    cache = load_cache()
    if base_sha in cache:
        return cache[base_sha], []
    info(f"running native tests at base {base_sha[:12]} (temporary worktree)...")
    worktree = Path(tempfile.mkdtemp(prefix="slice-verify-base-"))
    notes: list[str] = []
    total: int | None = None
    try:
        git(["worktree", "add", "--detach", str(worktree), base_sha])
        code, summary, output = run_native_tests(worktree)
        if code != 0 or summary is None:
            notes.append(f"base native run failed (exit {code}); tail:")
            notes.extend(output.splitlines()[-10:])
        else:
            total = summary[0]
            cache[base_sha] = total
            save_cache(cache)
    finally:
        run(["git", "worktree", "remove", "--force", str(worktree)])
        run(["git", "worktree", "prune"])
    return total, notes


def check_native_tests(base_sha: str, head_sha: str) -> CheckResult:
    info("running native tests at HEAD...")
    code, summary, output = run_native_tests(ROOT)
    notes: list[str] = []
    if summary is None:
        notes.append(f"could not parse native test summary (exit {code}); tail:")
        notes.extend(output.splitlines()[-10:])
        return CheckResult("native tests", "unparseable", False, notes)
    total, succeeded = summary
    if base_sha == head_sha:
        base_total: int | None = total
    else:
        base_total, base_notes = base_native_total(base_sha)
        notes.extend(base_notes)
    if base_total is None:
        detail = f"? -> {total}"
        passed = False
    else:
        delta = total - base_total
        detail = f"{base_total} -> {total}  (delta {delta:+d})"
        passed = code == 0 and succeeded == total
    if code != 0 or succeeded != total:
        notes.append(f"native suite: {succeeded}/{total} succeeded, exit {code}; tail:")
        notes.extend(output.splitlines()[-10:])
        passed = False
    return CheckResult("native tests", detail, passed, notes)


def check_command_exit(label: str, cmd: list[str]) -> CheckResult:
    info(f"running {' '.join(cmd)}...")
    proc = run(cmd)
    notes: list[str] = []
    if proc.returncode != 0:
        notes.append(f"{' '.join(cmd)} failed; tail:")
        notes.extend((proc.stdout + proc.stderr).splitlines()[-10:])
    return CheckResult(label, f"exit {proc.returncode}", proc.returncode == 0, notes)


def added_lines(base_sha: str, pathspec: list[str]) -> list[tuple[str, str]]:
    """(file, line) pairs added between base and HEAD."""
    diff = git(["diff", base_sha, "HEAD", "--", *pathspec])
    current = ""
    added: list[tuple[str, str]] = []
    for line in diff.splitlines():
        if line.startswith("+++ b/"):
            current = line[6:]
        elif line.startswith("+") and not line.startswith("+++"):
            added.append((current, line[1:]))
    return added


def check_version_json(base_sha: str) -> CheckResult:
    names = git(["diff", "--name-only", base_sha, "HEAD"]).splitlines()
    hits = [name for name in names if VERSION_JSON_RE.match(name)]
    notes = [f"in diff: {name}" for name in hits]
    return CheckResult(
        "data/*version.json in diff",
        f"{len(hits)} files",
        not hits,
        notes,
    )


def check_added_pattern(
    label: str, base_sha: str, pathspec: list[str], pattern: re.Pattern
) -> CheckResult:
    hits = [
        (path, line)
        for path, line in added_lines(base_sha, pathspec)
        if pattern.search(line)
    ]
    notes = [f"{path}: {line.strip()}" for path, line in hits]
    return CheckResult(label, str(len(hits)), not hits, notes)


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Mechanical PASS/FAIL gate for a branch against a base ref."
    )
    parser.add_argument("--base", required=True, help="base ref, e.g. phase/v1.0.0")
    args = parser.parse_args()

    try:
        base_sha = git(["merge-base", args.base, "HEAD"]).strip()
        head_sha = git(["rev-parse", "HEAD"]).strip()
    except RuntimeError as err:
        print(f"error: {err}", file=sys.stderr)
        return 2

    results = [
        check_native_tests(base_sha, head_sha),
        check_command_exit("pio run -e protoArtoo", ["pio", "run", "-e", "protoArtoo"]),
        check_command_exit(
            "check-action-drift", ["python3", "tools/check_action_registry_drift.py"]
        ),
        check_version_json(base_sha),
        check_added_pattern("new extern in .cpp", base_sha, ["*.cpp"], EXTERN_RE),
        check_added_pattern(
            "new #ifndef ARDUINO in inc/", base_sha, ["include/"], ARDUINO_GUARD_RE
        ),
    ]

    for result in results:
        status = "PASS" if result.passed else "FAIL"
        print(f"{result.label:<{LABEL_WIDTH}}{result.detail:<{DETAIL_WIDTH}}{status}")

    failures = [result for result in results if not result.passed]
    if failures:
        print()
        for result in failures:
            for note in result.notes:
                print(f"FAIL {result.label}: {note}")
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
