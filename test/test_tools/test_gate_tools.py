"""Pinned behavior for the verification tooling itself.

The slice gate and the mutation verifier are load-bearing evidence producers;
these tests pin the pure decision logic so a regression in either shows up as
a red check instead of as silently wrong evidence. Run via `make test-tools`
or `python3 -m unittest discover -s test/test_tools`; the slice gate runs this
suite as its first check.
"""

import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "tools"))

import mutation_verify  # noqa: E402
import slice_verify  # noqa: E402


class TapCountParsing(unittest.TestCase):
    def test_full_summary(self):
        out = "# tests 202\n# suites 0\n# pass 200\n# fail 1\n# cancelled 1\n"
        self.assertEqual(
            slice_verify.parse_tap_counts(out),
            {"tests": 202, "pass": 200, "fail": 1, "cancelled": 1},
        )

    def test_missing_tests_line_is_unparseable(self):
        self.assertIsNone(slice_verify.parse_tap_counts("# pass 5\n# fail 0\n"))

    def test_indented_lines_do_not_match(self):
        # Subtest summaries are indented; only the top-level summary counts.
        self.assertIsNone(slice_verify.parse_tap_counts("  # tests 3\n"))


class NativeSummaryParsing(unittest.TestCase):
    def test_succeeded_form(self):
        out = "1614 test cases: 1614 succeeded"
        self.assertEqual(slice_verify.parse_native_summary(out), (1614, 1614))

    def test_failed_form(self):
        out = "10 test cases: 3 failed"
        self.assertEqual(slice_verify.parse_native_summary(out), (10, 7))

    def test_unparseable(self):
        self.assertIsNone(slice_verify.parse_native_summary("no summary here"))


class VersionJsonPattern(unittest.TestCase):
    def test_matches_stamp_files(self):
        for path in ("data/fw-version.json", "data/fs-version.json"):
            self.assertIsNotNone(slice_verify.VERSION_JSON_RE.match(path))

    def test_ignores_other_files(self):
        for path in ("data/web_api.js", "test/version.json", "src/version.json"):
            self.assertIsNone(slice_verify.VERSION_JSON_RE.match(path))


class PorcelainDirtyPaths(unittest.TestCase):
    def test_version_stamps_are_not_dirty(self):
        text = " M data/fw-version.json\n M data/fs-version.json\n"
        self.assertEqual(slice_verify.porcelain_nonversion_paths(text), [])

    def test_tracked_and_untracked_changes_are_dirty(self):
        text = " M src/main.cpp\n?? tools/new_tool.py\n"
        self.assertEqual(
            slice_verify.porcelain_nonversion_paths(text),
            ["src/main.cpp", "tools/new_tool.py"],
        )

    def test_rename_uses_destination_path(self):
        text = "R  data/old.js -> data/fw-version.json\n"
        self.assertEqual(slice_verify.porcelain_nonversion_paths(text), [])


class TimeoutRunner(unittest.TestCase):
    def test_timeout_returns_124_with_note(self):
        proc = slice_verify.run(["sleep", "5"], timeout=1)
        self.assertEqual(proc.returncode, 124)
        self.assertIn("timed out", proc.stderr)


class MutationVerdicts(unittest.TestCase):
    GREEN = {"tests": 202, "pass": 202, "fail": 0, "cancelled": 0}

    def test_green_suite_means_survived(self):
        self.assertEqual(
            mutation_verify.verdict_for(0, self.GREEN, False, False), "SURVIVED"
        )

    def test_assertion_kill(self):
        counts = {"tests": 202, "pass": 197, "fail": 5, "cancelled": 0}
        self.assertEqual(
            mutation_verify.verdict_for(1, counts, True, False), "KILLED"
        )

    def test_cancelled_test_is_a_hang_kill(self):
        counts = {"tests": 202, "pass": 201, "fail": 0, "cancelled": 1}
        self.assertEqual(
            mutation_verify.verdict_for(1, counts, True, False), "KILLED-BY-HANG"
        )

    def test_suite_timeout_is_a_hang_kill(self):
        self.assertEqual(
            mutation_verify.verdict_for(124, {}, False, False), "KILLED-BY-HANG"
        )

    def test_per_test_timeout_failure_is_a_hang_kill(self):
        # node --test-timeout converts a hang into a counted failure whose TAP
        # diagnostic says testTimeoutFailure; that is still not an assertion.
        counts = {"tests": 202, "pass": 201, "fail": 1, "cancelled": 0}
        self.assertEqual(
            mutation_verify.verdict_for(1, counts, True, True), "KILLED-BY-HANG"
        )


if __name__ == "__main__":
    unittest.main()
