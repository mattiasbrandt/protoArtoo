#!/usr/bin/env python3
"""Mechanical mutation-coverage check for the web suite.

Takes one or more mutation patches (unified diffs against HEAD), and for each:
applies it, proves from the working tree that it changed something, runs the
tests that can see it, restores, and prints one verdict row. The canonical
entry point is the slice gate: `tools/slice_verify.py --mutations <patches>`
runs this tool and prints its table in the gate block, so passing the gate
implies killed mutations. Standalone runs remain useful while authoring patches.

How a patch is run (#405). This tool owns the node processes, one per test
file, concurrency 1:

  1. the likely-set: every test file whose load-map entry lists a data/*.js
     file the patch touches. The load map comes from the load-traced HEAD web
     run (tools/web_load_trace.cjs), cached in /tmp/protoartoo-web-load-map.json
     and keyed by the data/ and test/test_web/ trees and the Node version.
     Every unknown widens to the whole suite, never narrows: no map, a map
     that does not cover exactly the test files on disk, a patched file no
     test opens, a patched file that is not data/*.js
  2. shortest-first, by the cached wall time of each file's last run here.
     An unknown time sorts first; test_status_plate_346.js is seeded at
     16.1 s so it starts at the back on a cold cache
  3. one `node --test` child per file, 60 s each, under the same memory
     ceiling as the gate's web run; stop at the first clean assertion kill

`--whole-suite` runs today's single invocation of every test file instead:
no likely-set, no stop-early. The gate never passes it; it is for debugging
and for replaying the patch corpus.

A file is a CLEAN ASSERTION KILL when its exit code is non-zero and not 124
(this tool's own timeout), its TAP carries a `not ok`, its `# cancelled` is 0,
and `testTimeoutFailure` is not in its output. Any other non-zero file - a
cancelled test, a per-test timeout, a file that ran out of time, a crash that
reported no assertion - is not a kill.

Verdicts:
  KILLED          at least one file was a clean assertion kill; the files
                  after it were not run
  KILLED-BY-HANG  no file killed cleanly, and at least one exited non-zero
                  without an assertion (a hang, a timeout, a crash) - rejected:
                  neither proves the behaviour is asserted
  SURVIVED        every file in the likely-set exited 0 - no test that loads
                  this file killed it; rejected
  APPLY-FAIL      the patch did not apply cleanly to HEAD
  NO-TREE-CHANGE  the patch applied but left the working tree unchanged — the
                  silent-no-op case that motivated this tool

The `ran` column is files run / likely-set size. It depends on the cached
durations, so it can differ between two runs of one patch; the verdict cannot.

Requirements: a working tree clean beyond `data/*version.json` (restore uses
`git apply -R`; uncommitted work would be at risk), and patches produced
against HEAD (`git diff > mX.patch`, then revert the hand edit).

Exit code 0 only when every mutation is KILLED; 1 when any verdict is not
KILLED; 2 on operational errors (dirty tree, failed restore). A failed restore
aborts immediately and names the files left mutated.
"""

from __future__ import annotations

import argparse
import signal
import sys
import time
from dataclasses import dataclass

import pio_lock
from slice_verify import (
    LOAD_MAP_CACHE_PATH,
    MUTATION_FILE_TIMEOUT,
    ROOT,
    TEST_MEMORY_LIMIT_BYTES,
    WEB_JS_RE,
    WEB_TEST_FLAGS,
    WEB_TEST_TIMEOUT,
    WEBTEST_LOCK_PATH,
    info,
    load_map_key,
    parse_tap_counts,
    patch_files,
    porcelain_nonversion_paths,
    read_json,
    run,
    run_web_tests,
    web_test_files,
    write_json,
)

LABEL_WIDTH = 34
# The one duration this tool is told rather than measures: the slowest file at
# HEAD on 2026-09-18 (#405). Without it a cold cache runs the 16 s file first,
# which is the whole cost the ordering exists to avoid. Do not add the other
# slow files here; their times arrive with their first run.
SEED_DURATIONS_MS = {"test/test_web/test_status_plate_346.js": 16100}

# Per-file outcomes.
KILL = "kill"
GREEN = "green"
HANG = "hang"


@dataclass
class FileRun:
    path: str
    code: int
    counts: dict[str, int]
    outcome: str


def tracked_changes_present() -> bool:
    """Tracked modifications beyond data/*version.json put restore at risk.

    Untracked files are ignored: `git apply -R` only ever touches the files
    named in a patch, which are tracked by definition.
    """
    porcelain = run(["git", "status", "--porcelain", "-uno"]).stdout
    return bool(porcelain_nonversion_paths(porcelain))


def tree_changed(files: list[str]) -> bool:
    proc = run(["git", "diff", "--quiet", "--", *files])
    return proc.returncode != 0


def restore(patch_path: str, files: list[str]) -> bool:
    proc = run(["git", "apply", "-R", patch_path])
    return proc.returncode == 0 and not tree_changed(files)


def output_signals(output: str) -> tuple[dict[str, int], bool, bool]:
    counts = parse_tap_counts(output) or {}
    has_not_ok = any(
        line.lstrip().startswith("not ok") for line in output.splitlines()
    )
    has_timeout_failure = "testTimeoutFailure" in output
    return counts, has_not_ok, has_timeout_failure


def run_node(files: list[str], timeout: int) -> tuple[int, str]:
    # Capped as well as timed. A mutation is deliberately broken code, which is
    # exactly the state an unbounded loop lives in (see TEST_MEMORY_LIMIT_BYTES
    # in slice_verify for the measurement this is sized from).
    proc = run(
        ["node", *WEB_TEST_FLAGS, *files], cwd=ROOT, timeout=timeout,
        memory_limit=TEST_MEMORY_LIMIT_BYTES,
    )
    return proc.returncode, proc.stdout + proc.stderr


def file_outcome(
    code: int, counts: dict[str, int], has_not_ok: bool, has_timeout_failure: bool
) -> str:
    """One test file's result under a mutation: KILL, GREEN or HANG."""
    if code == 0:
        return GREEN
    if (
        code != 124
        and has_not_ok
        and counts.get("cancelled", 0) == 0
        and not has_timeout_failure
    ):
        return KILL
    return HANG


def patch_verdict(outcomes: list[str]) -> str:
    """The patch's verdict from the files it ran, in the order they ran."""
    if KILL in outcomes:
        return "KILLED"
    if HANG in outcomes:
        return "KILLED-BY-HANG"
    return "SURVIVED"


def verdict_for(
    code: int, counts: dict[str, int], has_not_ok: bool, has_timeout_failure: bool
) -> str:
    """The --whole-suite verdict: one invocation of every file, as before #405."""
    if code == 0:
        return "SURVIVED"
    timed_out = code == 124
    cancelled = counts.get("cancelled", 0)
    failed = counts.get("fail", 0)
    if (
        failed >= 1
        and cancelled == 0
        and not timed_out
        and not has_timeout_failure
        and has_not_ok
    ):
        return "KILLED"
    return "KILLED-BY-HANG"


def load_map_valid(load_map: dict, all_files: list[str]) -> bool:
    """A map is usable only if it traced exactly the test files on disk.

    A file the map never saw could open anything; a file the map has that is
    gone means the map is of another tree. Either way the tracer is unsure.
    """
    return isinstance(load_map, dict) and sorted(load_map) == sorted(all_files)


def likely_set(
    patched: list[str], load_map: dict[str, list[str]] | None, all_files: list[str]
) -> tuple[list[str], str | None]:
    """The test files that can see a patch, and why it widened if it did.

    Every unknown widens to all_files; nothing here ever narrows on a guess.
    """
    if load_map is None or not load_map_valid(load_map, all_files):
        return list(all_files), "no usable load map"
    selected: set[str] = set()
    for name in patched:
        if not WEB_JS_RE.match(name):
            return list(all_files), f"{name} is not data/*.js, which the tracer cannot see"
        loaders = {test for test, opened in load_map.items() if name in opened}
        if not loaders:
            return list(all_files), f"no traced test opens {name}"
        selected |= loaders
    if not selected:
        return list(all_files), "empty likely-set"
    return sorted(selected), None


def shortest_first(files: list[str], durations_ms: dict[str, int]) -> list[str]:
    """Order by cached wall time; unknown is 0, so an unmeasured file runs first."""
    known = {**SEED_DURATIONS_MS, **durations_ms}
    return sorted(files, key=lambda path: (known.get(path, 0), path))


class MapCache:
    """The shared load-map cache: the map for one key, and unkeyed durations."""

    def __init__(self) -> None:
        self.data = read_json(LOAD_MAP_CACHE_PATH)

    def map_for(self, key: dict[str, str] | None) -> dict[str, list[str]] | None:
        if key is None or self.data.get("key") != key:
            return None
        return self.data.get("map")

    def durations(self) -> dict[str, int]:
        durations = self.data.get("durations_ms")
        return durations if isinstance(durations, dict) else {}

    def record_duration(self, path: str, wall_ms: int) -> None:
        # Re-read before writing: the gate's HEAD run may have stored a map.
        self.data = read_json(LOAD_MAP_CACHE_PATH)
        durations = self.durations()
        durations[path] = wall_ms
        self.data["durations_ms"] = durations
        write_json(LOAD_MAP_CACHE_PATH, self.data)


def current_load_map(all_files: list[str]) -> dict[str, list[str]] | None:
    """The cached map for HEAD, rebuilt once from a traced HEAD run if stale."""
    key = load_map_key()
    load_map = MapCache().map_for(key)
    if load_map is not None:
        return load_map
    info(
        "no load map for this HEAD in"
        f" {LOAD_MAP_CACHE_PATH}; running the HEAD web suite once, load-traced..."
    )
    code, counts, _ = run_web_tests(ROOT, trace=True)
    load_map = MapCache().map_for(key)
    if load_map is None:
        info(f"HEAD web run exit {code}, counts {counts}: no map; every patch runs every file")
    return load_map


def run_likely_set(
    ordered: list[str], cache: MapCache
) -> tuple[list[FileRun], str]:
    """Run files one at a time until the first clean assertion kill."""
    runs: list[FileRun] = []
    for path in ordered:
        started = time.monotonic()
        code, output = run_node([path], MUTATION_FILE_TIMEOUT)
        wall_ms = int((time.monotonic() - started) * 1000)
        cache.record_duration(path, wall_ms)
        counts, has_not_ok, has_timeout_failure = output_signals(output)
        outcome = file_outcome(code, counts, has_not_ok, has_timeout_failure)
        runs.append(FileRun(path, code, counts, outcome))
        if outcome != GREEN:
            info(f"  {path}: {outcome} (exit {code}, {wall_ms} ms)")
        if outcome == KILL:
            break
    return runs, patch_verdict([r.outcome for r in runs])


def row_numbers(runs: list[FileRun]) -> tuple[int, int, int]:
    """exit, fail, cancelled for the table: the deciding file's exit, summed counts."""
    deciding = next((r for r in runs if r.outcome == KILL), None) or next(
        (r for r in runs if r.outcome == HANG), None
    )
    code = deciding.code if deciding else 0
    failed = sum(r.counts.get("fail", 0) for r in runs)
    cancelled = sum(r.counts.get("cancelled", 0) for r in runs)
    return code, failed, cancelled


def row(label: str, files: object, code: object, failed: object,
        cancelled: object, ran: object, verdict: str) -> str:
    return (
        f"{label:<{LABEL_WIDTH}}{files!s:<7}{code!s:<6}{failed!s:<6}"
        f"{cancelled!s:<11}{ran!s:<8}{verdict}"
    )


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Apply mutation patches one at a time and prove each turns"
        " a test file red by assertion."
    )
    parser.add_argument("patches", nargs="+", help="unified diff files against HEAD")
    parser.add_argument(
        "--whole-suite",
        action="store_true",
        help="run every test file in one node invocation per patch, with no"
        " likely-set and no stop-early (the pre-#405 runner; for debugging and"
        " corpus replay - the gate never passes it)",
    )
    parser.add_argument(
        "--timeout",
        type=int,
        default=WEB_TEST_TIMEOUT,
        help="--whole-suite timeout in seconds (default: %(default)s); the"
        f" per-file runner uses {MUTATION_FILE_TIMEOUT} s per file",
    )
    args = parser.parse_args()

    if tracked_changes_present():
        print(
            "error: tracked files changed beyond data/*version.json —"
            " commit or stash before running mutations (restore would risk"
            " uncommitted work)",
            file=sys.stderr,
        )
        return 2

    # SIGTERM unwinds like Ctrl-C, so the finally below restores the patch in
    # hand instead of leaving the tree mutated.
    signal.signal(signal.SIGTERM, lambda signum, _frame: sys.exit(128 + signum))

    # A no-op under the gate, which already holds this lock around us.
    with pio_lock.build_lock(
        ["python3", "tools/mutation_verify.py", *args.patches],
        lock_path=WEBTEST_LOCK_PATH,
    ):
        return run_patches(args)


def run_patches(args: argparse.Namespace) -> int:
    all_files = web_test_files(ROOT)
    load_map = None if args.whole_suite else current_load_map(all_files)
    cache = MapCache()

    if args.whole_suite:
        print("runner: whole suite, one node invocation per patch")
    else:
        print("runner: per file, likely-set, shortest-first, stop at first clean kill")
    print(row("mutation", "files", "exit", "fail", "cancelled", "ran", "verdict"))
    rows_ok = True
    for patch_path in args.patches:
        label = patch_path if len(patch_path) <= LABEL_WIDTH - 1 else (
            "..." + patch_path[-(LABEL_WIDTH - 4):]
        )
        files = patch_files(patch_path)
        apply_proc = run(["git", "apply", patch_path])
        if apply_proc.returncode != 0 or not files:
            print(row(label, "-", "-", "-", "-", "-", "APPLY-FAIL"))
            info(apply_proc.stderr.strip() or "patch reported no files")
            rows_ok = False
            continue
        if not tree_changed(files):
            # Nothing to reverse; as before #405, no restore is attempted.
            print(row(label, len(files), "-", "-", "-", "-", "NO-TREE-CHANGE"))
            rows_ok = False
            continue
        try:
            if args.whole_suite:
                info(f"mutation applied ({', '.join(files)}); running whole suite...")
                code, output = run_node(all_files, args.timeout)
                counts, has_not_ok, has_timeout_failure = output_signals(output)
                verdict = verdict_for(code, counts, has_not_ok, has_timeout_failure)
                ran = f"{len(all_files)}/{len(all_files)}"
                print(row(label, len(files), code, counts.get("fail", "-"),
                          counts.get("cancelled", "-"), ran, verdict))
            else:
                selected, widened = likely_set(files, load_map, all_files)
                if widened:
                    info(f"{patch_path}: whole suite ({widened})")
                ordered = shortest_first(selected, cache.durations())
                info(
                    f"mutation applied ({', '.join(files)}); running"
                    f" {len(ordered)} files, shortest first..."
                )
                runs, verdict = run_likely_set(ordered, cache)
                code, failed, cancelled = row_numbers(runs)
                ran = f"{len(runs)}/{len(selected)}"
                print(row(label, len(files), code, failed, cancelled, ran, verdict))
            sys.stdout.flush()
            if verdict != "KILLED":
                rows_ok = False
        finally:
            # Runs on an interrupt too; the exception then carries on unwinding,
            # and the message below is what says the tree is still mutated.
            restored = restore(patch_path, files)
            if not restored:
                print(
                    f"error: restore failed after {patch_path}; working tree still"
                    f" mutated in: {', '.join(files)}",
                    file=sys.stderr,
                )
        if not restored:
            return 2

    print()
    print("mutation gate:", "PASS" if rows_ok else "FAIL")
    return 0 if rows_ok else 1


if __name__ == "__main__":
    sys.exit(main())
