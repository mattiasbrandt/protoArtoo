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

1. native test total at HEAD vs base, plus suite pass/fail; a shrinking total
   fails the gate (coverage reduction needs explicit coordinator acceptance)
2. web suite (node:test) total at HEAD vs base, gated on process exit code and
   `# cancelled` — never on `# fail`, which is unreliable in both directions
   (a hung test vanishes from it; a broken invocation inflates it)
3. no test file deleted between base and HEAD (native or web)
4. `pio run -e protoArtoo` exit code (never bare `pio run`)
5. `tools/check_action_registry_drift.py` exit code
6. `data/*version.json` must not appear in the diff
7. no new `extern` declarations added inside `.cpp` files (`extern "C"` allowed)
8. no new `#ifdef ARDUINO` / `#ifndef ARDUINO` blocks added under `include/`

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

WEB_TEST_DIR = Path("test") / "test_web"
WEB_TEST_GLOB = "test_*.js"
# Keep flags in sync with `make test-web`. The gate invokes node directly so
# base-commit worktrees that predate the make target verify identically.
WEB_TEST_FLAGS = ["--test", "--test-timeout=10000"]
TAP_COUNT_RE = re.compile(r"^# (tests|pass|fail|cancelled) (\d+)$", re.MULTILINE)

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


def web_test_files(cwd: Path) -> list[str]:
    return sorted(
        str(path.relative_to(cwd)) for path in (cwd / WEB_TEST_DIR).glob(WEB_TEST_GLOB)
    )


def parse_tap_counts(output: str) -> dict[str, int] | None:
    counts = {key: int(value) for key, value in TAP_COUNT_RE.findall(output)}
    return counts if "tests" in counts else None


def run_web_tests(cwd: Path) -> tuple[int, dict[str, int] | None, str]:
    files = web_test_files(cwd)
    if not files:
        return 0, {"tests": 0, "pass": 0, "fail": 0, "cancelled": 0}, ""
    proc = run(["node", *WEB_TEST_FLAGS, *files], cwd=cwd)
    output = proc.stdout + proc.stderr
    return proc.returncode, parse_tap_counts(output), output


def base_totals(base_sha: str) -> tuple[dict[str, int | None], dict[str, list[str]]]:
    """Native and web test totals at the base commit, via a throwaway worktree.

    Cached per commit SHA so repeated gate runs do not rebuild the base
    suites. Cache entries written by older gate versions were a bare native
    total; those are treated as native-only and the web total is filled in.
    """
    cache = load_cache()
    entry = cache.get(base_sha)
    if isinstance(entry, int):
        entry = {"native": entry}
    totals: dict[str, int | None] = dict(entry) if isinstance(entry, dict) else {}
    notes: dict[str, list[str]] = {"native": [], "web": []}
    if "native" in totals and "web" in totals:
        return totals, notes
    info(f"running base suites at {base_sha[:12]} (temporary worktree)...")
    worktree = Path(tempfile.mkdtemp(prefix="slice-verify-base-"))
    try:
        git(["worktree", "add", "--detach", str(worktree), base_sha])
        if "native" not in totals:
            code, summary, output = run_native_tests(worktree)
            if code != 0 or summary is None:
                totals["native"] = None
                notes["native"].append(f"base native run failed (exit {code}); tail:")
                notes["native"].extend(output.splitlines()[-10:])
            else:
                totals["native"] = summary[0]
        if "web" not in totals:
            code, counts, output = run_web_tests(worktree)
            if code != 0 or counts is None or counts.get("cancelled", 0):
                totals["web"] = None
                notes["web"].append(f"base web run failed (exit {code}); tail:")
                notes["web"].extend(output.splitlines()[-10:])
            else:
                totals["web"] = counts["tests"]
    finally:
        run(["git", "worktree", "remove", "--force", str(worktree)])
        run(["git", "worktree", "prune"])
    cached = {key: value for key, value in totals.items() if value is not None}
    if cached:
        cache[base_sha] = cached
        save_cache(cache)
    return totals, notes


def check_native_tests(
    base_total: int | None, same_commit: bool, base_notes: list[str]
) -> CheckResult:
    info("running native tests at HEAD...")
    code, summary, output = run_native_tests(ROOT)
    notes: list[str] = list(base_notes)
    if summary is None:
        notes.append(f"could not parse native test summary (exit {code}); tail:")
        notes.extend(output.splitlines()[-10:])
        return CheckResult("native tests", "unparseable", False, notes)
    total, succeeded = summary
    if same_commit:
        base_total = total
    if base_total is None:
        detail = f"? -> {total}"
        passed = False
    else:
        delta = total - base_total
        detail = f"{base_total} -> {total}  (delta {delta:+d})"
        passed = code == 0 and succeeded == total and delta >= 0
        if delta < 0:
            notes.append(
                f"native test count shrank by {-delta}; shrinking coverage"
                " needs explicit coordinator acceptance"
            )
    if code != 0 or succeeded != total:
        notes.append(f"native suite: {succeeded}/{total} succeeded, exit {code}; tail:")
        notes.extend(output.splitlines()[-10:])
        passed = False
    return CheckResult("native tests", detail, passed, notes)


def check_web_tests(
    base_total: int | None, same_commit: bool, base_notes: list[str]
) -> CheckResult:
    info("running web tests at HEAD...")
    code, counts, output = run_web_tests(ROOT)
    notes: list[str] = list(base_notes)
    if counts is None:
        notes.append(f"could not parse web TAP summary (exit {code}); tail:")
        notes.extend(output.splitlines()[-10:])
        return CheckResult("web tests", "unparseable", False, notes)
    total = counts["tests"]
    cancelled = counts.get("cancelled", 0)
    if same_commit:
        base_total = total
    if base_total is None:
        detail = f"? -> {total}"
        passed = False
    else:
        delta = total - base_total
        detail = f"{base_total} -> {total}  (delta {delta:+d})"
        passed = code == 0 and cancelled == 0 and delta >= 0
        if delta < 0:
            notes.append(
                f"web test count shrank by {-delta}; shrinking coverage"
                " needs explicit coordinator acceptance"
            )
    if code != 0 or cancelled:
        # Gate on exit code and cancellations, never on `# fail`: a hung test
        # is reported cancelledByParent and vanishes from the fail count while
        # the process still exits non-zero.
        notes.append(
            f"web suite exit {code}, fail {counts.get('fail', 0)},"
            f" cancelled {cancelled}; failing tests:"
        )
        not_ok = [
            line for line in output.splitlines() if line.lstrip().startswith("not ok")
        ]
        notes.extend(not_ok[:20] or output.splitlines()[-10:])
        passed = False
    return CheckResult("web tests", detail, passed, notes)


def check_deleted_tests(base_sha: str) -> CheckResult:
    names = git(
        ["diff", "--diff-filter=D", "--name-only", base_sha, "HEAD", "--", "test/"]
    ).splitlines()
    notes = [f"deleted: {name}" for name in names]
    if names:
        notes.append("deleting a test file needs explicit coordinator acceptance")
    return CheckResult("deleted test files", str(len(names)), not names, notes)


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

    same_commit = base_sha == head_sha
    if same_commit:
        base: dict[str, int | None] = {"native": None, "web": None}
        base_notes: dict[str, list[str]] = {"native": [], "web": []}
    else:
        base, base_notes = base_totals(base_sha)

    results = [
        check_native_tests(base["native"], same_commit, base_notes["native"]),
        check_web_tests(base["web"], same_commit, base_notes["web"]),
        check_deleted_tests(base_sha),
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
