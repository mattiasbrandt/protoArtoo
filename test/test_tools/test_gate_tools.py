"""Pinned behavior for the verification tooling itself.

The slice gate and the mutation verifier are load-bearing evidence producers;
these tests pin the pure decision logic so a regression in either shows up as
a red check instead of as silently wrong evidence. Run via `make test-tools`
or `python3 -m unittest discover -s test/test_tools`; the slice gate runs this
suite as its first check.
"""

import json
import os
import subprocess
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


class PerFileOutcome(unittest.TestCase):
    """The per-file runner's definition of a kill (#405): unchanged from the
    suite-level one, applied to one file's process instead of the suite's."""

    KILLED_COUNTS = {"tests": 12, "pass": 11, "fail": 1, "cancelled": 0}

    def test_clean_assertion_kill(self):
        self.assertEqual(
            mutation_verify.file_outcome(1, self.KILLED_COUNTS, True, False),
            mutation_verify.KILL,
        )

    def test_cancelled_test_is_a_hang(self):
        counts = {"tests": 12, "pass": 11, "fail": 0, "cancelled": 1}
        self.assertEqual(
            mutation_verify.file_outcome(1, counts, True, False), mutation_verify.HANG
        )

    def test_per_test_timeout_failure_is_a_hang(self):
        self.assertEqual(
            mutation_verify.file_outcome(1, self.KILLED_COUNTS, True, True),
            mutation_verify.HANG,
        )

    def test_runner_timeout_is_a_hang_even_with_a_not_ok(self):
        self.assertEqual(
            mutation_verify.file_outcome(124, self.KILLED_COUNTS, True, False),
            mutation_verify.HANG,
        )

    def test_nonzero_exit_without_a_not_ok_is_not_a_kill(self):
        self.assertEqual(
            mutation_verify.file_outcome(1, {}, False, False), mutation_verify.HANG
        )

    def test_exit_zero_is_green(self):
        counts = {"tests": 12, "pass": 12, "fail": 0, "cancelled": 0}
        self.assertEqual(
            mutation_verify.file_outcome(0, counts, False, False), mutation_verify.GREEN
        )


class PatchVerdictFromFiles(unittest.TestCase):
    def test_every_file_green_survives(self):
        green = mutation_verify.GREEN
        self.assertEqual(mutation_verify.patch_verdict([green, green]), "SURVIVED")

    def test_hang_without_a_kill_is_a_hang_kill(self):
        outcomes = [mutation_verify.GREEN, mutation_verify.HANG]
        self.assertEqual(mutation_verify.patch_verdict(outcomes), "KILLED-BY-HANG")

    def test_assertion_kill_after_a_hang_is_killed(self):
        # The documented loosening: stopping early at a kill never sees a hang
        # in a later file, so a hang elsewhere no longer rejects the patch.
        outcomes = [mutation_verify.HANG, mutation_verify.KILL]
        self.assertEqual(mutation_verify.patch_verdict(outcomes), "KILLED")

    def test_runner_stops_at_the_first_clean_kill(self):
        calls = []

        def fake_run_node(files, timeout):
            calls.append(files[0])
            if files[0] == "b.js":
                return 1, "not ok 1 - b\n# tests 1\n# fail 1\n# cancelled 0\n"
            return 0, "ok 1 - a\n# tests 1\n# pass 1\n# fail 0\n# cancelled 0\n"

        class NoCache:
            def record_duration(self, path, wall_ms):
                pass

        original = mutation_verify.run_node
        mutation_verify.run_node = fake_run_node
        try:
            runs, verdict = mutation_verify.run_likely_set(["a.js", "b.js", "c.js"], NoCache())
        finally:
            mutation_verify.run_node = original
        self.assertEqual(verdict, "KILLED")
        self.assertEqual(calls, ["a.js", "b.js"])
        self.assertEqual(len(runs), 2)


class LikelySet(unittest.TestCase):
    ALL = [
        "test/test_web/test_a.js",
        "test/test_web/test_b.js",
        "test/test_web/test_c.js",
    ]
    MAP = {
        "test/test_web/test_a.js": ["data/page_bootstrap.js", "data/shell.js"],
        "test/test_web/test_b.js": ["data/wiring.js"],
        "test/test_web/test_c.js": [],
    }

    def test_narrows_to_the_files_that_opened_the_patched_file(self):
        selected, widened = mutation_verify.likely_set(["data/wiring.js"], self.MAP, self.ALL)
        self.assertEqual(selected, ["test/test_web/test_b.js"])
        self.assertIsNone(widened)

    def test_union_across_patched_files(self):
        selected, _ = mutation_verify.likely_set(
            ["data/wiring.js", "data/shell.js"], self.MAP, self.ALL
        )
        self.assertEqual(selected, ["test/test_web/test_a.js", "test/test_web/test_b.js"])

    def test_a_file_no_test_opens_widens_to_the_whole_suite(self):
        # diagnostics.js at HEAD: no web test opens it. The empty likely-set
        # is an unknown, and unknowns widen.
        selected, widened = mutation_verify.likely_set(["data/diagnostics.js"], self.MAP, self.ALL)
        self.assertEqual(selected, self.ALL)
        self.assertIsNotNone(widened)

    def test_one_unmapped_file_widens_even_beside_a_mapped_one(self):
        selected, _ = mutation_verify.likely_set(
            ["data/wiring.js", "data/diagnostics.js"], self.MAP, self.ALL
        )
        self.assertEqual(selected, self.ALL)

    def test_a_patched_file_the_tracer_cannot_see_widens(self):
        selected, _ = mutation_verify.likely_set(["data/index.html"], self.MAP, self.ALL)
        self.assertEqual(selected, self.ALL)

    def test_no_map_widens(self):
        selected, _ = mutation_verify.likely_set(["data/wiring.js"], None, self.ALL)
        self.assertEqual(selected, self.ALL)

    def test_a_map_that_missed_a_test_file_widens(self):
        partial = {k: v for k, v in self.MAP.items() if not k.endswith("test_c.js")}
        selected, _ = mutation_verify.likely_set(["data/wiring.js"], partial, self.ALL)
        self.assertEqual(selected, self.ALL)


class ShortestFirst(unittest.TestCase):
    SLOW = "test/test_web/test_status_plate_346.js"

    def test_unknown_duration_runs_before_the_seeded_slow_file(self):
        # Named to sort after the slow file, so only the seed can put it first.
        ordered = mutation_verify.shortest_first(
            [self.SLOW, "test/test_web/test_zz_new.js"], {}
        )
        self.assertEqual(ordered, ["test/test_web/test_zz_new.js", self.SLOW])

    def test_cached_durations_order_the_rest(self):
        ordered = mutation_verify.shortest_first(
            ["test/test_web/test_b.js", "test/test_web/test_a.js"],
            {"test/test_web/test_b.js": 40, "test/test_web/test_a.js": 900},
        )
        self.assertEqual(ordered, ["test/test_web/test_b.js", "test/test_web/test_a.js"])

    def test_a_measured_time_replaces_the_seed(self):
        ordered = mutation_verify.shortest_first(
            [self.SLOW, "test/test_web/test_a.js"],
            {self.SLOW: 10, "test/test_web/test_a.js": 900},
        )
        self.assertEqual(ordered[0], self.SLOW)


class WebOnlyAllowList(unittest.TestCase):
    def test_a_data_js_only_diff_is_web_only(self):
        self.assertTrue(slice_verify.is_web_only(["data/app.js"]))

    def test_web_tests_docs_css_and_html_stay_web_only(self):
        self.assertTrue(slice_verify.is_web_only([
            "data/app.js", "data/style.css", "data/index.html",
            "test/test_web/test_app.js", "test/test_web/helpers/mini_dom.js",
            "docs/agents/slice-gate.md",
        ]))

    def test_a_diff_that_also_has_src_is_not(self):
        self.assertFalse(slice_verify.is_web_only(["data/app.js", "src/x.cpp"]))

    def test_console_help_is_not_web_only(self):
        # A native test reads data/console_help.txt.
        self.assertFalse(slice_verify.is_web_only(["data/console_help.txt"]))

    def test_data_subdirectories_are_not_web_only(self):
        self.assertFalse(slice_verify.is_web_only(["data/asset-sets/r2/set.json"]))

    def test_an_empty_diff_is_not_web_only(self):
        self.assertFalse(slice_verify.is_web_only([]))


class MutationTableInTheBlock(unittest.TestCase):
    def test_table_keeps_rows_and_drops_the_runner_summary(self):
        stdout = "runner: per file\nmutation  ran  verdict\nm01.patch  3/21  KILLED\n\nmutation gate: PASS\n"
        self.assertEqual(
            slice_verify.mutation_table(stdout),
            ["runner: per file", "mutation  ran  verdict", "m01.patch  3/21  KILLED"],
        )

    def test_the_tracer_is_a_fenced_verifier(self):
        self.assertIn("tools/web_load_trace.cjs", slice_verify.VERIFIER_SCRIPTS)


class SeparateLocks(unittest.TestCase):
    def test_the_web_lock_never_reads_as_holding_the_pio_lock(self):
        pio_lock = sys.modules["pio_lock"]
        self.assertEqual(pio_lock.held_env_for(pio_lock.lock_path()), pio_lock.HELD_ENV)
        self.assertNotEqual(
            pio_lock.held_env_for(slice_verify.WEBTEST_LOCK_PATH), pio_lock.HELD_ENV
        )


class LoadTraceCanary(unittest.TestCase):
    """Pins the Node behaviour the load map rests on.

    tools/web_load_trace.cjs sees a data/ file only because both require() and
    import() read it with fs.readFileSync (Node 26.8.1). If a Node upgrade
    changes that, this goes red here instead of every likely-set silently
    widening - or worse, narrowing.
    """

    TRACER = Path(__file__).resolve().parents[2] / "tools" / "web_load_trace.cjs"

    def test_require_and_import_are_both_recorded(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            (root / "data").mkdir()
            (root / "test" / "test_web").mkdir(parents=True)
            (root / "data" / "required.js").write_text("module.exports = 1;\n")
            (root / "data" / "imported.js").write_text("module.exports = 2;\n")
            (root / "data" / "unopened.js").write_text("module.exports = 3;\n")
            (root / "test" / "test_web" / "test_canary.js").write_text(
                'const test = require("node:test");\n'
                'require("../../data/required.js");\n'
                'test("imports", async () => { await import("../../data/imported.js"); });\n'
            )
            (root / "test" / "test_web" / "test_quiet.js").write_text(
                'require("node:test")("opens nothing", () => {});\n'
            )
            out = root / "map.json"
            env = {**os.environ, slice_verify.LOAD_TRACE_ENV: str(out)}
            proc = subprocess.run(
                ["node", "--require", str(self.TRACER), *slice_verify.WEB_TEST_FLAGS,
                 "test/test_web/test_canary.js", "test/test_web/test_quiet.js"],
                cwd=root, env=env, capture_output=True, text=True, timeout=60,
            )
            self.assertEqual(proc.returncode, 0, proc.stdout + proc.stderr)
            self.assertEqual(slice_verify.parse_tap_counts(proc.stdout)["tests"], 2)
            self.assertEqual(json.loads(out.read_text()), {
                "test/test_web/test_canary.js": ["data/imported.js", "data/required.js"],
                "test/test_web/test_quiet.js": [],
            })
            self.assertFalse((root / "map.json.parts").exists())


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
