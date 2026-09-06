"""Pinned behavior for the verification tooling itself.

The slice gate and the mutation verifier are load-bearing evidence producers;
these tests pin the pure decision logic so a regression in either shows up as
a red check instead of as silently wrong evidence. Run via `make test-tools`
or `python3 -m unittest discover -s test/test_tools`; the slice gate runs this
suite as its first check.
"""

import sys
import tempfile
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


class ProductionSplit(unittest.TestCase):
    def test_trees_map_to_their_suites(self):
        names = [
            "data/page_bootstrap.js",
            "data/index.html",
            "src/main.cpp",
            "include/config.h",
            "tools/slice_verify.py",
            "test/test_web/test_footer.js",
        ]
        split = slice_verify.production_changes(names)
        self.assertEqual(split["web"], ["data/page_bootstrap.js", "data/index.html"])
        self.assertEqual(split["native"], ["src/main.cpp", "include/config.h"])

    def test_version_stamps_are_not_production(self):
        split = slice_verify.production_changes(
            ["data/fw-version.json", "data/fs-version.json"]
        )
        self.assertEqual(split["web"], [])


class ZeroDeltaGate(unittest.TestCase):
    def test_flat_delta_with_production_changes_fails(self):
        ok, notes = slice_verify.zero_delta_ok(0, ["data/app.js"], False, "web")
        self.assertFalse(ok)
        self.assertTrue(notes)

    def test_flat_delta_without_production_changes_passes(self):
        self.assertEqual(slice_verify.zero_delta_ok(0, [], False, "web"), (True, []))

    def test_growing_delta_passes(self):
        self.assertEqual(
            slice_verify.zero_delta_ok(3, ["data/app.js"], False, "web"), (True, [])
        )

    def test_waiver_passes_with_visible_ack(self):
        ok, notes = slice_verify.zero_delta_ok(0, ["src/main.cpp"], True, "native")
        self.assertTrue(ok)
        self.assertIn("--expect-no-new-tests", notes[0])

    def test_shrinking_delta_is_not_this_checks_business(self):
        # delta < 0 already fails via the shrink rule; zero_delta_ok stays out.
        self.assertEqual(
            slice_verify.zero_delta_ok(-2, ["data/app.js"], False, "web"), (True, [])
        )


class MutationRequirement(unittest.TestCase):
    def test_no_production_js_means_not_required(self):
        result = slice_verify.check_mutations(["data/index.html"], [], False)
        self.assertTrue(result.passed)
        self.assertEqual(result.detail, "not required")

    def test_production_js_without_patches_fails(self):
        result = slice_verify.check_mutations(["data/app.js"], [], False)
        self.assertFalse(result.passed)
        self.assertIn("data/app.js", " ".join(result.notes))

    def test_waiver_passes_with_visible_ack(self):
        result = slice_verify.check_mutations(["data/app.js"], [], True)
        self.assertTrue(result.passed)
        self.assertEqual(result.detail, "ACK (expect-no-mutations)")

    def test_uncovered_files_are_named(self):
        uncovered = slice_verify.uncovered_production_files(
            ["data/app.js", "data/footer.js"],
            {"m1.patch": ["data/app.js"], "m2.patch": []},
        )
        self.assertEqual(uncovered, ["data/footer.js"])

    def test_full_coverage_leaves_nothing_uncovered(self):
        self.assertEqual(
            slice_verify.uncovered_production_files(
                ["data/app.js"], {"m1.patch": ["data/app.js", "data/other.js"]}
            ),
            [],
        )


class MutationEntryExpansion(unittest.TestCase):
    def test_directory_expands_to_sorted_patches(self):
        with tempfile.TemporaryDirectory() as tmp:
            for name in ("b.patch", "a.patch", "notes.md"):
                (Path(tmp) / name).write_text("")
            expanded = slice_verify.expand_mutation_entries([tmp, "single.patch"])
            self.assertEqual(
                expanded,
                [str(Path(tmp) / "a.patch"), str(Path(tmp) / "b.patch"), "single.patch"],
            )


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


class GeneratedDataExclusion(unittest.TestCase):
    """data/ holds two unrelated things and the web suite can only answer for one.

    The suite's model is vm.runInNewContext(readFileSync("data/<module>.js"))
    plus tests that read .html; data/console_help.txt is generated text the
    FIRMWARE reads from LittleFS, which no JS-behaviour test can turn red on.
    Before this exclusion, regenerating the help file produced a FAIL that no
    honest test could clear, and #221 had to be waived for it.
    """

    def test_generated_help_text_is_not_web_production(self):
        out = slice_verify.production_changes(["data/console_help.txt"])
        self.assertEqual(out["web"], [])

    def test_browser_js_is_still_web_production(self):
        out = slice_verify.production_changes(["data/app.js"])
        self.assertEqual(out["web"], ["data/app.js"])

    def test_html_is_still_web_production(self):
        # The web suite does read .html - narrowing this rule to *.js would
        # have dropped a real coverage requirement to fix a nuisance.
        out = slice_verify.production_changes(["data/index.html"])
        self.assertEqual(out["web"], ["data/index.html"])

    def test_version_stamps_stay_excluded(self):
        out = slice_verify.production_changes(["data/fw-version.json"])
        self.assertEqual(out["web"], [])


class WaiverIsVisibleInTheBlock(unittest.TestCase):
    """A consumed waiver must leave a note the renderer prints.

    AGENTS.md: an unsanctioned waiver ACK in a worker's block is an automatic
    reject. That rule needs something to detect. zero_delta_ok() has always
    produced the ACK note, but the renderer printed notes only for FAILING
    checks, so a waived run rendered identically to one that never needed a
    waiver.
    """

    def test_waived_zero_delta_passes_with_an_ack_note(self):
        ok, notes = slice_verify.zero_delta_ok(0, ["data/app.js"], True, "web")
        self.assertTrue(ok)
        self.assertTrue(notes, "a consumed waiver must carry an ACK note")
        self.assertIn("ACK", notes[0])

    def test_unwaived_zero_delta_fails(self):
        ok, notes = slice_verify.zero_delta_ok(0, ["data/app.js"], False, "web")
        self.assertFalse(ok)
        self.assertTrue(notes)

    def test_no_production_change_needs_no_waiver_and_no_note(self):
        ok, notes = slice_verify.zero_delta_ok(0, [], False, "web")
        self.assertTrue(ok)
        self.assertEqual(notes, [])


if __name__ == "__main__":
    unittest.main()
