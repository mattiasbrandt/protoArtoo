"""Pinned behavior for tools/pane_run.py's log reading and argument checks.

The Herdr round trip is not driven here (it needs a live Herdr session); what
is pinned is the part that decides when the wait ends and what gets printed,
because a stale or misread GATE_EXIT line ends the wait on the wrong run.
"""

import os
import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "tools"))

import pane_run  # noqa: E402


class GateExit(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.log = Path(self.tmp.name) / "run.log"

    def tearDown(self):
        self.tmp.cleanup()

    def test_no_log_yet_keeps_waiting(self):
        self.assertIsNone(pane_run.gate_exit(self.log))

    def test_a_running_command_keeps_waiting(self):
        self.log.write_text("building...\nGATE_EXIT=0 appears in a message\n")
        self.assertIsNone(pane_run.gate_exit(self.log))

    def test_the_last_line_is_the_exit_code(self):
        self.log.write_text("error: x\nGATE_EXIT=2\n\n")
        self.assertEqual(pane_run.gate_exit(self.log), 2)

    def test_another_runs_finished_log_is_not_this_run_ending(self):
        self.log.write_text("PANE_RUN_ID=theirs\nbuilt\nGATE_EXIT=0\n")
        self.assertIsNone(pane_run.gate_exit(self.log, "mine"))
        self.log.write_text("PANE_RUN_ID=mine\nbuilt\nGATE_EXIT=3\n")
        self.assertEqual(pane_run.gate_exit(self.log, "mine"), 3)

    def test_a_second_claim_on_the_same_log_is_refused(self):
        first = pane_run.claim_log(self.log)
        self.addCleanup(os.close, first)
        # A second open file description, as a second process would have.
        self.assertIsNone(pane_run.claim_log(self.log))

    def test_a_log_still_being_written_is_refused(self):
        self.log.write_text("PANE_RUN_ID=theirs\nbuilding...\n")
        with mock.patch.dict("os.environ", {"HERDR_ENV": "1"}):
            self.assertEqual(pane_run.main([str(self.log), "--", "true"]), pane_run.EXIT_USAGE)
        self.assertTrue(self.log.exists())

    def test_summary_filters_then_tails_and_drops_the_exit_line(self):
        self.log.write_text("PANE_RUN_ID=x\na\nERR 1\nb\nERR 2\nERR 3\nGATE_EXIT=1\n")
        self.assertEqual(pane_run.summary(self.log, 2, "ERR"), ["ERR 2", "ERR 3"])
        self.assertEqual(pane_run.summary(self.log, 10, None), ["a", "ERR 1", "b", "ERR 2", "ERR 3"])


class Arguments(unittest.TestCase):
    def test_a_command_and_a_script_together_are_refused(self):
        self.assertEqual(pane_run.main(["/tmp/x.log", "--script", "/tmp/s.sh", "--", "true"]), 2)

    def test_neither_is_refused(self):
        self.assertEqual(pane_run.main(["/tmp/x.log"]), 2)

    def test_outside_herdr_it_says_so_instead_of_running_headless(self):
        with mock.patch.dict("os.environ", {"HERDR_ENV": "0"}):
            self.assertEqual(pane_run.main(["/tmp/x.log", "--", "true"]), pane_run.EXIT_NO_HERDR)

    def test_the_pane_runs_the_command_under_gate_in_pane(self):
        line = pane_run.pane_command(Path("/tmp/x.log"), ["make", "build", "BUILD_ENV=a b"], "id1")
        self.assertIn("gate_in_pane.sh /tmp/x.log -- sh -c", line)
        self.assertTrue(line.endswith("id1 make build 'BUILD_ENV=a b'"))


if __name__ == "__main__":
    unittest.main()
