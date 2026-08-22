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
   fails the gate (coverage reduction needs explicit coordinator acceptance),
   and a flat total fails when the diff touches src/ or include/ — production
   changes must grow the suite that covers them unless the run carries a
   coordinator-sanctioned --expect-no-new-tests (the ACK is visible)
2. web suite (node:test) total at HEAD vs base, gated on process exit code and
   `# cancelled` — never on `# fail`, which is unreliable in both directions
   (a hung test vanishes from it; a broken invocation inflates it); a flat
   total fails when the diff touches data/ (beyond the version stamps), with
   the same --expect-no-new-tests waiver
3. mutation gate: a diff that touches web production JS (data/*.js) must
   carry mutation patches (--mutations, files or directories of *.patch);
   the gate runs tools/mutation_verify.py itself and fails unless every
   mutation is KILLED and every changed JS file is hit by at least one
   patch, so a passing block *implies* killed mutations — waivable only via
   a visible, coordinator-sanctioned --expect-no-mutations ACK
4. no test file deleted between base and HEAD (native or web)
5. `pio run -e artoo_esp32` exit code (never bare `pio run`)
6. `tools/check_action_registry_drift.py` exit code
7. `data/*version.json` must not appear in the diff
8. no new `extern` declarations added inside `.cpp` files (`extern "C"` allowed)
9. no new `#ifdef ARDUINO` / `#ifndef ARDUINO` blocks added under `include/`
10. neither `tools/slice_verify.py` nor `tools/mutation_verify.py` may be in
    the diff unless the run acknowledges it with --expect-gate-edit (the ACK
    is visible in the block) — both scripts produce evidence, so both are
    fenced
11. no fenced pathspec (--fenced, comma-separated, repeatable) in the diff
12. the tooling test suite (test/test_tools/, includes the gate's own unit
    tests) passes — the evidence producer is not exempt from prove-it-works

The block opens with provenance lines — blob hashes of both verifier
scripts, HEAD sha, a
DIRTY marker when the working tree differs beyond `data/*version.json`,
base/merge-base, diff size, and toolchain versions — so a pasted block can be
checked against the commit and gate version it claims to describe. Every
subprocess runs under a timeout so a hung build or test fails the gate loudly
instead of hanging it.

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
WEB_PRODUCTION_RE = re.compile(r"^data/")
NATIVE_PRODUCTION_RE = re.compile(r"^(?:src|include)/")
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

GIT_TIMEOUT = 120
BUILD_TIMEOUT = 1800
NATIVE_TEST_TIMEOUT = 1800
WEB_TEST_TIMEOUT = 300
DEFAULT_TIMEOUT = 600
GATE_SCRIPT = "tools/slice_verify.py"
MUTATION_SCRIPT = "tools/mutation_verify.py"
WEB_JS_RE = re.compile(r"^data/.*\.js$")


@dataclass
class CheckResult:
    label: str
    detail: str
    passed: bool
    notes: list[str]


def info(message: str) -> None:
    print(f"[slice_verify] {message}", file=sys.stderr, flush=True)


def _text(data: str | bytes | None) -> str:
    if data is None:
        return ""
    if isinstance(data, bytes):
        return data.decode(errors="replace")
    return data


def run(
    cmd: list[str], cwd: Path = ROOT, timeout: int = DEFAULT_TIMEOUT
) -> subprocess.CompletedProcess:
    try:
        return subprocess.run(
            cmd, cwd=cwd, capture_output=True, text=True, timeout=timeout
        )
    except subprocess.TimeoutExpired as exc:
        stderr = _text(exc.stderr) + f"\n[slice_verify] timed out after {timeout}s"
        return subprocess.CompletedProcess(cmd, 124, _text(exc.stdout), stderr)


def git(args: list[str], cwd: Path = ROOT) -> str:
    proc = run(["git", *args], cwd=cwd, timeout=GIT_TIMEOUT)
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
    proc = run(["pio", "test", "-e", "native"], cwd=cwd, timeout=NATIVE_TEST_TIMEOUT)
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
    proc = run(["node", *WEB_TEST_FLAGS, *files], cwd=cwd, timeout=WEB_TEST_TIMEOUT)
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


def production_changes(diff_names: list[str]) -> dict[str, list[str]]:
    """Split diff paths into the production trees each suite is answerable for.

    Only the native suite can cover src/ and include/; only the web suite can
    cover data/. The version stamps are build artifacts, not production code.
    """
    return {
        "web": [
            name
            for name in diff_names
            if WEB_PRODUCTION_RE.match(name) and not VERSION_JSON_RE.match(name)
        ],
        "native": [name for name in diff_names if NATIVE_PRODUCTION_RE.match(name)],
    }


def zero_delta_ok(
    delta: int, production: list[str], waived: bool, suite: str
) -> tuple[bool, list[str]]:
    """Production changes must grow the owning suite; a flat delta is only
    acceptable under an explicit, visible waiver. delta == 0 is precisely the
    state in which no mutation can ever be killed."""
    if delta != 0 or not production:
        return True, []
    if waived:
        return True, [
            f"{suite} delta +0 with {len(production)} production files changed;"
            " ACK (--expect-no-new-tests); needs coordinator sanction"
        ]
    return False, [
        f"{len(production)} production files changed but the {suite} test count"
        " did not grow; add coverage or rerun with coordinator-sanctioned"
        " --expect-no-new-tests"
    ]


def check_native_tests(
    base_total: int | None,
    same_commit: bool,
    base_notes: list[str],
    production: list[str],
    expect_no_new_tests: bool,
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
        delta_ok, delta_notes = zero_delta_ok(
            delta, production, expect_no_new_tests, "native"
        )
        notes.extend(delta_notes)
        passed = passed and delta_ok
    if code != 0 or succeeded != total:
        notes.append(f"native suite: {succeeded}/{total} succeeded, exit {code}; tail:")
        notes.extend(output.splitlines()[-10:])
        passed = False
    return CheckResult("native tests", detail, passed, notes)


def check_web_tests(
    base_total: int | None,
    same_commit: bool,
    base_notes: list[str],
    production: list[str],
    expect_no_new_tests: bool,
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
        delta_ok, delta_notes = zero_delta_ok(
            delta, production, expect_no_new_tests, "web"
        )
        notes.extend(delta_notes)
        passed = passed and delta_ok
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


def patch_files(patch_path: str) -> list[str]:
    """Files a patch touches, via git apply --numstat; empty on a bad patch.

    Shared with mutation_verify so both tools agree on what a patch targets.
    """
    proc = run(["git", "apply", "--numstat", patch_path], timeout=GIT_TIMEOUT)
    if proc.returncode != 0:
        return []
    return [line.split("\t")[2] for line in proc.stdout.splitlines() if line]


def expand_mutation_entries(entries: list[str]) -> list[str]:
    """--mutations entries: a directory contributes its *.patch files, sorted."""
    patches: list[str] = []
    for entry in entries:
        path = Path(entry)
        if path.is_dir():
            patches.extend(sorted(str(child) for child in path.glob("*.patch")))
        else:
            patches.append(entry)
    return patches


def uncovered_production_files(
    production_js: list[str], patch_lists: dict[str, list[str]]
) -> list[str]:
    """Changed web production JS files no mutation patch touches.

    One weak patch must not stand in for a multi-file sweep: every changed
    production module needs at least one mutation aimed at it."""
    covered = {name for files in patch_lists.values() for name in files}
    return [name for name in production_js if name not in covered]


def check_mutations(
    production_web: list[str], patches: list[str], expect_no_mutations: bool
) -> CheckResult:
    """Mutation coverage as a gate row: a passing block implies killed
    mutations, so there is no separate evidence left for a report to
    substitute or omit."""
    label = "mutation gate"
    production_js = [name for name in production_web if WEB_JS_RE.match(name)]
    if not patches:
        if not production_js:
            return CheckResult(label, "not required", True, [])
        if expect_no_mutations:
            return CheckResult(
                label,
                "ACK (expect-no-mutations)",
                True,
                [
                    f"{len(production_js)} web production JS files changed with"
                    " no mutation evidence; needs coordinator sanction"
                ],
            )
        notes = [f"web production JS changed: {name}" for name in production_js]
        notes.append(
            "supply mutation patches via --mutations (files or a directory of"
            " *.patch), or rerun with coordinator-sanctioned"
            " --expect-no-mutations"
        )
        return CheckResult(label, "patches missing", False, notes)
    passed = True
    notes = []
    patch_lists = {patch: patch_files(patch) for patch in patches}
    uncovered = uncovered_production_files(production_js, patch_lists)
    if uncovered:
        passed = False
        notes.extend(f"no mutation patch touches: {name}" for name in uncovered)
        notes.append(
            "every changed web production JS file needs at least one mutation"
            " patch aimed at it"
        )
    info(f"running {MUTATION_SCRIPT} on {len(patches)} patches...")
    timeout = 120 + (WEB_TEST_TIMEOUT + 30) * len(patches)
    proc = run(["python3", MUTATION_SCRIPT, *patches], timeout=timeout)
    for line in proc.stdout.splitlines():
        info(f"  {line}")
    if proc.returncode != 0:
        passed = False
        notes.append(f"{MUTATION_SCRIPT} exit {proc.returncode}; table:")
        notes.extend(proc.stdout.splitlines()[-(len(patches) + 4) :])
        stderr_tail = proc.stderr.strip()
        if stderr_tail:
            notes.extend(stderr_tail.splitlines()[-5:])
    return CheckResult(label, f"{len(patches)} patches", passed, notes)


def check_deleted_tests(base_sha: str) -> CheckResult:
    names = git(
        ["diff", "--diff-filter=D", "--name-only", base_sha, "HEAD", "--", "test/"]
    ).splitlines()
    notes = [f"deleted: {name}" for name in names]
    if names:
        notes.append("deleting a test file needs explicit coordinator acceptance")
    return CheckResult("deleted test files", str(len(names)), not names, notes)


def check_command_exit(
    label: str, cmd: list[str], timeout: int = DEFAULT_TIMEOUT
) -> CheckResult:
    info(f"running {' '.join(cmd)}...")
    proc = run(cmd, timeout=timeout)
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


def check_gate_script(base_sha: str, expect_gate_edit: bool) -> CheckResult:
    names = git(
        ["diff", "--name-only", base_sha, "HEAD", "--", GATE_SCRIPT, MUTATION_SCRIPT]
    ).splitlines()
    if not names:
        return CheckResult("gate script in diff", "0 files", True, [])
    if expect_gate_edit:
        notes = ["gate edit acknowledged via --expect-gate-edit; needs coordinator sanction"]
        return CheckResult("gate script in diff", "ACK (expect-gate-edit)", True, notes)
    notes = [f"in diff: {name}" for name in names]
    notes.append(
        "neither verifier script may be edited by the slice it verifies; rerun"
        " with --expect-gate-edit only for coordinator-sanctioned gate work"
    )
    return CheckResult("gate script in diff", f"{len(names)} files", False, notes)


def check_fenced_paths(base_sha: str, fences: list[str]) -> CheckResult:
    if not fences:
        return CheckResult("fenced paths", "none declared", True, [])
    names = git(["diff", "--name-only", base_sha, "HEAD", "--", *fences]).splitlines()
    notes = [f"fenced path touched: {name}" for name in names]
    if names:
        notes.append("fenced paths are out of scope for this slice by coordinator order")
    return CheckResult("fenced paths", f"{len(names)} touched", not names, notes)


def script_blob_hash(script: str = GATE_SCRIPT) -> str:
    proc = run(["git", "hash-object", script], timeout=GIT_TIMEOUT)
    return proc.stdout.strip()[:12] if proc.returncode == 0 else "unknown"


def porcelain_nonversion_paths(porcelain: str) -> list[str]:
    """Paths from `git status --porcelain` output, minus data/*version.json.

    Renames report their destination path. Shared with mutation_verify so both
    tools agree on what counts as a dirty tree.
    """
    paths: list[str] = []
    for line in porcelain.splitlines():
        path = line[3:]
        if " -> " in path:
            path = path.split(" -> ", 1)[1]
        if not VERSION_JSON_RE.match(path):
            paths.append(path)
    return paths


def working_tree_dirty() -> bool:
    """True when the working tree differs from HEAD beyond data/*version.json."""
    return bool(porcelain_nonversion_paths(git(["status", "--porcelain"])))


def env_fingerprint() -> str:
    def first_line(cmd: list[str]) -> str:
        proc = run(cmd, timeout=60)
        text = (proc.stdout or proc.stderr).strip()
        return text.splitlines()[0] if proc.returncode == 0 and text else "unknown"

    return " | ".join(
        (
            first_line(["pio", "--version"]),
            first_line(["python3", "--version"]),
            first_line(["node", "--version"]),
        )
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
    parser.add_argument(
        "--fenced",
        action="append",
        default=[],
        metavar="PATHSPECS",
        help="comma-separated pathspecs the slice must not touch; repeatable",
    )
    parser.add_argument(
        "--expect-gate-edit",
        action="store_true",
        help="acknowledge a coordinator-sanctioned edit to this script;"
        " the ACK is visible in the printed block",
    )
    parser.add_argument(
        "--expect-no-new-tests",
        action="store_true",
        help="acknowledge, with coordinator sanction, that this slice changes"
        " production code without growing the owning test suite;"
        " the ACK is visible in the printed block",
    )
    parser.add_argument(
        "--mutations",
        action="append",
        default=[],
        metavar="PATHS",
        help="comma-separated mutation patch files or directories of *.patch;"
        " repeatable; required when the diff touches web production JS",
    )
    parser.add_argument(
        "--expect-no-mutations",
        action="store_true",
        help="acknowledge, with coordinator sanction, that this slice changes"
        " web production JS without mutation evidence;"
        " the ACK is visible in the printed block",
    )
    parser.add_argument(
        "--json", metavar="PATH", help="also write the results as JSON to PATH"
    )
    args = parser.parse_args()
    fences = [spec for chunk in args.fenced for spec in chunk.split(",") if spec]
    mutations = expand_mutation_entries(
        [spec for chunk in args.mutations for spec in chunk.split(",") if spec]
    )

    try:
        base_sha = git(["merge-base", args.base, "HEAD"]).strip()
        head_sha = git(["rev-parse", "HEAD"]).strip()
    except RuntimeError as err:
        print(f"error: {err}", file=sys.stderr)
        return 2

    gate_hash = script_blob_hash()
    mutation_hash = script_blob_hash(MUTATION_SCRIPT)
    dirty = working_tree_dirty()
    env = env_fingerprint()
    diff_names = git(["diff", "--name-only", base_sha, "HEAD"]).splitlines()
    diff_files = len(diff_names)
    production = production_changes(diff_names)
    print(
        f"gate {gate_hash}  mut {mutation_hash}"
        f"  head {head_sha[:12]}{'  DIRTY' if dirty else ''}"
    )
    print(f"base {args.base}  merge-base {base_sha[:12]}  files {diff_files}")
    print(f"env  {env}")
    print()

    same_commit = base_sha == head_sha
    if same_commit:
        base: dict[str, int | None] = {"native": None, "web": None}
        base_notes: dict[str, list[str]] = {"native": [], "web": []}
    else:
        base, base_notes = base_totals(base_sha)

    results = [
        check_command_exit(
            "gate self-tests",
            ["python3", "-m", "unittest", "discover", "-s", "test/test_tools", "-q"],
            timeout=120,
        ),
        check_native_tests(
            base["native"],
            same_commit,
            base_notes["native"],
            production["native"],
            args.expect_no_new_tests,
        ),
        check_web_tests(
            base["web"],
            same_commit,
            base_notes["web"],
            production["web"],
            args.expect_no_new_tests,
        ),
        check_mutations(production["web"], mutations, args.expect_no_mutations),
        check_deleted_tests(base_sha),
        check_command_exit("pio run -e artoo_esp32", ["pio", "run", "-e", "artoo_esp32"]),
        check_command_exit(
            "check-action-drift", ["python3", "tools/check_action_registry_drift.py"]
        ),
        check_version_json(base_sha),
        check_added_pattern("new extern in .cpp", base_sha, ["*.cpp"], EXTERN_RE),
        check_added_pattern(
            "new #ifndef ARDUINO in inc/", base_sha, ["include/"], ARDUINO_GUARD_RE
        ),
        check_gate_script(base_sha, args.expect_gate_edit),
        check_fenced_paths(base_sha, fences),
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

    if args.json:
        payload = {
            "gate": {
                "script_hash": gate_hash,
                "mutation_script_hash": mutation_hash,
                "head": head_sha,
                "dirty": dirty,
                "base_ref": args.base,
                "merge_base": base_sha,
                "files_in_diff": diff_files,
                "env": env,
                "fenced": fences,
                "expect_gate_edit": args.expect_gate_edit,
                "expect_no_new_tests": args.expect_no_new_tests,
                "expect_no_mutations": args.expect_no_mutations,
                "mutations": mutations,
                "production_files": production,
            },
            "checks": [
                {
                    "label": result.label,
                    "detail": result.detail,
                    "passed": result.passed,
                    "notes": result.notes,
                }
                for result in results
            ],
            "ok": not failures,
        }
        Path(args.json).write_text(json.dumps(payload, indent=2) + "\n")

    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
