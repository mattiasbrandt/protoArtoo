"""Pinned behaviour for the soak harness's progress, transcript and interrupt
handling (`tools/soak.py`).

The harness is a permanent instrument: it is pointed at a board for hours at a
time, and two of its properties are worth a test each time it is touched.

1. **A truncated run can never look like a completed one.** Neither the
   overall verdict nor any driver's verdict may read PASS after a SIGINT or a
   SIGTERM, the exit code must follow, and the report must carry the duration
   actually observed next to the duration requested. This is the same hazard
   the whole harness exists to avoid -- a measurement that reports success
   while measuring less than it claims -- applied to a partial result.
2. **Absent stays absent on the progress surface too.** A reading the image
   does not publish is left off a progress line entirely; a reading it does
   publish but that a given sample did not carry prints as `?`. Neither ever
   becomes a zero, because a zero on a progress line is what an operator
   watches for hours and then quotes.

The harness's own `--self-test` covers the wire and the drivers end to end
against local fixtures and is not duplicated here. Run via
`python3 -m unittest discover -s test/test_tools`; the slice gate runs this
suite directly.
"""

import io
import json
import os
import signal
import sys
import tempfile
import threading
import time
import unittest
from pathlib import Path
from unittest import mock

REPO_ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPO_ROOT / "tools"))

import soak  # noqa: E402

WEB_SERVER_CPP = (REPO_ROOT / "src" / "web" / "web_server.cpp").read_text()
BENCH_CPP = (REPO_ROOT / "bringup" / "p4_hosted_bench.cpp").read_text()

BENCH = soak.SCHEMAS["bench"]
SHIPPING = soak.SCHEMAS["shipping"]
ARTOO = soak.SCHEMAS["artoo"]


def interrupted_monitor(signal_name="SIGTERM", **kwargs):
    """A monitor already carrying an interrupt, without sending a real signal
    to the test runner."""
    event = threading.Event()
    event.set()
    monitor = soak.RunMonitor(interrupt=event, **kwargs)
    monitor._signal_name = signal_name
    return monitor


class ProgressFieldsAreHonestAboutAbsence(unittest.TestCase):
    def test_a_counter_the_image_does_not_publish_is_left_off_the_line(self):
        fields = soak.progress_status_fields(BENCH, dict(soak.FIXTURE_STATUS_BODY))
        # The bench sketch compiles no admission guard and no evicting stream
        # registry, so there is nothing to count -- and a zero here would
        # claim this run watched a gate the binary does not contain.
        for absent in ("refusedHeapFloor", "refusedHeapFloorDiag", "refusedSseCap",
                       "sseEvicted"):
            self.assertNotIn(absent, fields)
        # It does publish a flat recovery ladder, so those keys are present.
        self.assertEqual(fields["recoveryLadder"], "idle")
        self.assertEqual(fields["recoveryLadderUpEvents"], 0)

    def test_an_image_with_no_recovery_ladder_shows_no_ladder_keys(self):
        fields = soak.progress_status_fields(ARTOO, dict(soak.FIXTURE_ARTOO_STATUS_BODY))
        for absent in ("recoveryLadder", "recoveryLadderFailures",
                       "recoveryLadderUpEvents", "recoveryLadderAttempts",
                       "recoveryLadderRecovered"):
            self.assertNotIn(absent, fields, "an artoo progress line that carried a ladder "
                                             "would claim to watch one that is compiled out")
        self.assertEqual(fields["refusedSseCap"], 0)
        self.assertEqual(fields["sseEvicted"], 0)

    def test_the_shipping_ladder_is_read_out_of_its_nested_object(self):
        fields = soak.progress_status_fields(
            SHIPPING, json.loads(json.dumps(soak.FIXTURE_SHIPPING_STATUS_BODY)))
        self.assertEqual(fields["recoveryLadder"], "idle")
        self.assertEqual(fields["recoveryLadderUpEvents"], 1)

    def test_a_sample_missing_a_published_field_reads_absent_not_zero(self):
        body = json.loads(json.dumps(soak.FIXTURE_SHIPPING_STATUS_BODY))
        del body["refusedHeapFloor"]
        del body["hostedLink"]["transportFailureCount"]
        fields = soak.progress_status_fields(SHIPPING, body)
        self.assertIsNone(fields["refusedHeapFloor"])
        self.assertIsNone(fields["recoveryLadderFailures"])
        self.assertEqual(soak.render_progress_value(fields["refusedHeapFloor"]),
                         soak.PROGRESS_ABSENT)
        self.assertNotEqual(soak.render_progress_value(fields["refusedHeapFloor"]), "0")

    def test_a_bool_where_an_int_belongs_reads_absent_not_one(self):
        body = dict(soak.FIXTURE_ARTOO_STATUS_BODY, refusedHeapFloor=True)
        fields = soak.progress_status_fields(ARTOO, body)
        self.assertIsNone(fields["refusedHeapFloor"])

    def test_an_unreachable_poll_carries_no_numbers_at_all(self):
        fields = soak.progress_status_fields(ARTOO, {"_pollError": "connection refused"})
        self.assertIn("connection refused", fields["statusPoll"])
        self.assertEqual(len(fields), 1,
                         "a stale reading carried over from an older sample would show an "
                         "operator a number the controller is no longer reporting")

    def test_the_floor_margin_is_absent_when_the_heap_reading_is(self):
        floor = soak.resolve_admission_floor("artoo_esp32")
        body = dict(soak.FIXTURE_ARTOO_STATUS_BODY)
        del body["heapLargest8bit"]
        fields = soak.progress_status_fields(ARTOO, body, floor)
        self.assertIsNone(fields["floorMargin"])

    def test_the_progress_only_fields_name_what_the_firmware_actually_emits(self):
        """Drift guard. These names are read off buildStatusJson()'s
        unconditional snprintf; a rename there must go red here rather than
        turning every product progress line into a row of '?'."""
        for field in (SHIPPING.sse_refused_cap_field, SHIPPING.sse_evicted_field,
                      SHIPPING.sse_clients_peak_field):
            self.assertIn(f'\\"{field}\\":', WEB_SERVER_CPP,
                          f"{field} is not in buildStatusJson()'s payload")
        self.assertEqual(ARTOO.sse_refused_cap_field, SHIPPING.sse_refused_cap_field,
                         "both product images share one unconditional snprintf")
        # And the bench genuinely has none of them, which is why it declares None.
        for field in ("refusedSseCap", "sseEvicted", "sseClientsPeak"):
            self.assertNotIn(field, BENCH_CPP)
        self.assertIsNone(BENCH.sse_refused_cap_field)
        self.assertIsNone(BENCH.sse_evicted_field)


class ProgressLinesCarryClocks(unittest.TestCase):
    def snapshot(self, **kwargs):
        base = dict(
            driver="sse_soak", driver_index=1, driver_count=3, run_elapsed_s=90.0,
            elapsed_s=45.0, total_s=180.0, headline={"frames": 12}, fields={},
        )
        base.update(kwargs)
        return soak.ProgressSnapshot(**base)

    def test_a_planned_driver_shows_elapsed_total_percent_and_remaining(self):
        line = self.snapshot().render()
        self.assertIn("[1/3 sse_soak]", line)
        self.assertIn("run 0:01:30", line)
        self.assertIn("drv 0:00:45/0:03:00", line)
        self.assertIn("(25.0%)", line)
        self.assertIn("left 0:02:15", line)

    def test_a_timeout_driver_is_never_shown_as_a_percentage_of_progress(self):
        line = self.snapshot(
            driver="c6_reset_recovery", total_kind="timeout", total_s=30.0, elapsed_s=12.0,
        ).render()
        self.assertIn("timeout 0:00:30", line)
        self.assertIn("0:00:18 left", line)
        self.assertNotIn("%", line, "finishing early is the good outcome for a recovery "
                                    "budget, so a completion percentage would be a lie")

    def test_an_unbounded_wait_names_what_it_is_waiting_for_and_guesses_no_eta(self):
        line = self.snapshot(
            total_s=None, total_kind="unbounded", waiting_for="the link to come back",
        ).render()
        self.assertIn("waiting for the link to come back", line)
        self.assertNotIn("left", line)
        self.assertNotIn("%", line)

    def test_the_status_line_omits_the_device_fields_the_heartbeat_carries(self):
        snapshot = self.snapshot(fields={"refusedSseCap": 0})
        self.assertIn("refusedSseCap=0", snapshot.render())
        self.assertNotIn("refusedSseCap", snapshot.status_line())
        self.assertIn("frames=12", snapshot.status_line())

    def test_the_checkpoint_dict_says_the_same_thing_the_line_does(self):
        snapshot = self.snapshot(fields={"refusedSseCap": 0})
        as_dict = snapshot.as_dict()
        self.assertEqual(as_dict["fields"], {"frames": 12, "refusedSseCap": 0})
        self.assertEqual(as_dict["elapsedS"], 45.0)
        self.assertEqual(as_dict["remainingS"], 135.0)

    def test_a_none_renders_as_absent_and_never_as_zero(self):
        line = self.snapshot(fields={"sseEvicted": None}).render()
        self.assertIn(f"sseEvicted={soak.PROGRESS_ABSENT}", line)
        self.assertNotIn("sseEvicted=0", line)


class TheConsoleDegradesAndKeepsTheTranscriptPlain(unittest.TestCase):
    def test_a_redirected_stream_carries_no_escape_bytes(self):
        stream = io.StringIO()
        console = soak.RunConsole(stream=stream, log_path=None)
        console.line("everything fine", kind="ok")
        console.line("something is wrong", kind="fail")
        console.close()
        self.assertNotIn("\x1b", stream.getvalue())
        self.assertIn("[OK] everything fine", stream.getvalue())
        self.assertIn("[FAIL] something is wrong", stream.getvalue())

    def test_a_terminal_gets_colour_and_the_same_ascii_tokens(self):
        # rich honours NO_COLOR and TERM=dumb even with force_terminal=True;
        # RunConsole does not second-guess that (tools/soak.py). This test
        # still has to prove colour on a terminal, so it supplies its own
        # env rather than assuming a human tty.
        env = {
            key: value
            for key, value in os.environ.items()
            if key not in ("NO_COLOR", "FORCE_COLOR")
        }
        env["TERM"] = "xterm-256color"
        stream = io.StringIO()
        with mock.patch.dict(os.environ, env, clear=True):
            console = soak.RunConsole(stream=stream, log_path=None, force_terminal=True)
            console.line("everything fine", kind="ok")
            console.close()
        self.assertIn("\x1b[", stream.getvalue(), "a terminal should get colour")
        self.assertIn("[OK]", stream.getvalue(), "the token stays ASCII everywhere")

    def test_the_transcript_has_no_ansi_even_when_the_terminal_does(self):
        with tempfile.TemporaryDirectory() as tmp:
            log = Path(tmp) / "run.log"
            stream = io.StringIO()
            console = soak.RunConsole(stream=stream, log_path=log, force_terminal=True)
            console.line("everything fine", kind="ok")
            console.detail("a poll failed, in full")
            console.close()
            text = log.read_text()
        self.assertNotIn("\x1b", text)
        self.assertIn("[OK] everything fine", text)
        self.assertIn("a poll failed, in full", text)

    def test_quiet_silences_the_stream_and_still_writes_the_transcript(self):
        with tempfile.TemporaryDirectory() as tmp:
            log = Path(tmp) / "run.log"
            stream = io.StringIO()
            console = soak.RunConsole(stream=stream, quiet=True, log_path=log)
            console.line("heartbeat", kind=None)
            console.close()
            self.assertEqual(stream.getvalue(), "")
            self.assertIn("heartbeat", log.read_text())

    def test_a_long_line_is_not_wrapped_off_a_terminal(self):
        stream = io.StringIO()
        console = soak.RunConsole(stream=stream, log_path=None)
        long_line = "field=" + "x" * 400
        console.line(long_line)
        console.close()
        self.assertIn(long_line, stream.getvalue(),
                      "a wrapped heartbeat is a heartbeat nobody can grep")


class TheMonitorCadenceAndWaits(unittest.TestCase):
    def test_a_disabled_monitor_neither_prints_nor_builds_a_snapshot(self):
        monitor = soak.RunMonitor.disabled()
        built = []

        def source(full):
            built.append(full)
            return monitor.snapshot("d", 0.0, None, {})

        monitor.tick(source, force=True)
        monitor.status_tick(source)
        self.assertEqual(built, [], "a driver called without a monitor must put nothing on "
                                    "the wire that it would not have put there itself")

    def test_wait_returns_true_when_the_full_time_elapsed(self):
        monitor = soak.RunMonitor.disabled()
        started = time.monotonic()
        self.assertTrue(monitor.wait(0.05))
        self.assertGreaterEqual(time.monotonic() - started, 0.04)

    def test_wait_returns_false_immediately_once_interrupted(self):
        monitor = interrupted_monitor(quiet=True)
        started = time.monotonic()
        self.assertFalse(monitor.wait(30.0))
        self.assertLess(time.monotonic() - started, 1.0,
                        "an interrupt must not have to wait out the current sleep")

    def test_the_heartbeat_cadence_holds_and_the_status_line_is_faster(self):
        stream = io.StringIO()
        console = soak.RunConsole(stream=stream, log_path=None)
        monitor = soak.RunMonitor(interval_s=10.0, console=console)
        monitor.start_run(1)
        monitor.begin_driver(1, "sse_soak")
        source = lambda full: monitor.snapshot(  # noqa: E731 - one-line test stub
            "sse_soak", 0.0, 10.0, {"frames": 1})
        monitor.tick(source, force=True)
        monitor.tick(source)   # cadence not up yet
        monitor.tick(source)
        console.close()
        heartbeats = [line for line in stream.getvalue().splitlines()
                      if line.startswith("[1/1 sse_soak]")]
        self.assertEqual(len(heartbeats), 1, stream.getvalue())

    def test_the_checkpoint_is_written_even_when_the_console_is_quiet(self):
        written = []
        monitor = soak.RunMonitor(
            interval_s=10.0, quiet=True,
            checkpoint_writer=written.append,
        )
        monitor.bind_checkpoint_source(lambda snapshot: {"snapshot": snapshot is not None})
        monitor.tick(lambda full: monitor.snapshot("d", 0.0, 1.0, {}), force=True)
        self.assertEqual(written, [{"snapshot": True}])


class AnInterruptedRunCanNeverReportAPass(unittest.TestCase):
    """The founding hazard of this harness, applied to a partial result."""

    def setUp(self):
        self.server, self.thread = soak._start_fixture_server(
            status_body=soak.FIXTURE_ARTOO_STATUS_BODY, sse_mode="shipping_live")
        self.addCleanup(soak._stop_fixture_server, self.server, self.thread)
        self.port = self.server.server_address[1]
        self.client = soak.BenchClient("127.0.0.1", self.port, connect_timeout_s=5.0)
        self.floor = soak.resolve_admission_floor(ARTOO.build_env)
        self.cap = soak.resolve_sse_client_cap(ARTOO.build_env)

    def soak_report(self, monitor):
        return soak.run_sse_soak(
            self.client, ARTOO, num_clients=1, duration_s=30.0,
            status_poll_interval_s=0.2, admission_floor=self.floor,
            sse_client_cap=self.cap, early_stall_check_s=1.0, max_silence_s=5.0,
            monitor=monitor,
        )

    def test_a_clean_short_run_still_passes(self):
        report = soak.run_sse_soak(
            self.client, ARTOO, num_clients=1, duration_s=1.0,
            status_poll_interval_s=0.2, admission_floor=self.floor,
            sse_client_cap=self.cap, early_stall_check_s=1.0, max_silence_s=5.0,
        )
        self.assertEqual(report["verdict"], "PASS", report["reasons"])
        self.assertNotIn("interrupted", report)
        self.assertNotIn("durationSObserved", report)

    def test_an_interrupted_soak_is_INTERRUPTED_and_records_both_durations(self):
        monitor = interrupted_monitor(quiet=True)
        report = self.soak_report(monitor)
        self.assertEqual(report["verdict"], soak.VERDICT_INTERRUPTED_DRIVER)
        self.assertNotEqual(report["verdict"], "PASS")
        self.assertTrue(report["interrupted"])
        self.assertEqual(report["interruptSignal"], "SIGTERM")
        self.assertLess(report["durationSObserved"], report["durationSRequested"])
        self.assertIn("interrupted", report["reasons"][0])

    def test_an_interrupt_inside_the_early_window_is_not_called_a_stall(self):
        monitor = interrupted_monitor(quiet=True)
        report = self.soak_report(monitor)
        self.assertFalse(report["immediateStall"],
                         "the run was stopped before the stall window elapsed; blaming the "
                         "controller for that is the same error as claiming a pass")

    def test_the_overall_verdict_and_exit_code_follow_the_truncation(self):
        verdict, code = soak._compose_overall_verdict(
            {"sse_soak": {"verdict": soak.VERDICT_INTERRUPTED_DRIVER}})
        self.assertEqual(verdict, soak.VERDICT_INTERRUPTED_RUN)
        self.assertEqual(code, soak.EXIT_INVALID)

    def test_a_truncated_driver_outranks_a_passing_one(self):
        verdict, code = soak._compose_overall_verdict({
            "sse_soak": {"verdict": "PASS"},
            "reconnect_storm": {"verdict": soak.VERDICT_INTERRUPTED_DRIVER},
        })
        self.assertEqual(verdict, soak.VERDICT_INTERRUPTED_RUN)
        self.assertNotEqual(code, soak.EXIT_PASS)

    def test_a_completed_run_still_reaches_the_exit_codes_it_always_did(self):
        # The verdict WORDING was reworded when #184's go/no-go vocabulary was
        # retired (ADR 0035); the exit-code VALUES are contract and did not
        # move. Both halves are written out, so a future rewording has to come
        # back through here and a changed exit code cannot slip past.
        for drivers, expected in (
            ({"a": {"verdict": "PASS"}}, ("PASS", 0)),
            ({"a": {"verdict": "OBSERVATION_ONLY"}}, ("PASS", 0)),
            ({"a": {"verdict": "FAIL"}}, ("FAIL", 2)),
            ({"a": {"verdict": "INVALID"}}, ("INVALID", 3)),
            ({"a": {"verdict": "UNAVAILABLE"}}, ("INVALID", 3)),
        ):
            self.assertEqual(soak._compose_overall_verdict(drivers), expected, drivers)

    def test_run_marks_a_driver_that_never_started_rather_than_omitting_it(self):
        monitor = interrupted_monitor(quiet=True)
        args = soak.build_parser().parse_args([
            "--device", "127.0.0.1", "--port", str(self.port), "--image", "artoo",
            "--driver", "all", "--duration", "1.0", "--num-clients", "1",
            "--status-poll-interval-s", "0.2", "--early-stall-check-s", "1.0",
        ])
        report, code = soak.run(args, monitor)
        self.assertEqual(code, soak.EXIT_INVALID)
        self.assertEqual(report["verdict"], soak.VERDICT_INTERRUPTED_RUN)
        self.assertEqual(set(report["drivers"]),
                         {"sse_soak", "reconnect_storm", "c6_reset_recovery"})
        for name, result in report["drivers"].items():
            self.assertEqual(result["verdict"], soak.VERDICT_INTERRUPTED_DRIVER, name)
            self.assertIs(result["started"], False, name)
        self.assertEqual(report["phases"], [],
                         "a driver that never ran must leave no phase; a zero-second phase "
                         "would claim it ran")


class TheReportEchoesTheClocksAndTheTranscriptPath(unittest.TestCase):
    def setUp(self):
        self.server, self.thread = soak._start_fixture_server(
            status_body=soak.FIXTURE_ARTOO_STATUS_BODY, sse_mode="shipping_live")
        self.addCleanup(soak._stop_fixture_server, self.server, self.thread)
        self.port = self.server.server_address[1]

    def run_report(self, monitor=None, extra=()):
        args = soak.build_parser().parse_args([
            "--device", "127.0.0.1", "--port", str(self.port), "--image", "artoo",
            "--driver", "sse_soak", "--duration", "1.0", "--num-clients", "1",
            "--status-poll-interval-s", "0.2", "--early-stall-check-s", "1.0", *extra,
        ])
        return soak.run(args, monitor)

    def test_the_clocks_are_in_the_json_so_nothing_has_to_parse_the_terminal(self):
        report, _ = self.run_report()
        self.assertRegex(report["startedAt"], r"^\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}[+-]\d{4}$")
        self.assertRegex(report["endedAt"], r"^\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}[+-]\d{4}$")
        self.assertGreater(report["durationS"], 0.0)
        self.assertEqual(report["plannedDurationS"], 1.0)
        self.assertEqual([phase["driver"] for phase in report["phases"]], ["sse_soak"])
        self.assertEqual(report["phases"][0]["plannedDurationS"], 1.0)

    def test_a_run_with_no_transcript_reports_no_transcript(self):
        report, _ = self.run_report()
        self.assertIsNone(report["logPath"],
                          "a fabricated path would send the next tool looking for a file "
                          "that was never written")

    def test_the_transcript_path_is_recorded_when_there_is_one(self):
        with tempfile.TemporaryDirectory() as tmp:
            log = Path(tmp) / "run.log"
            console = soak.RunConsole(stream=io.StringIO(), log_path=log)
            monitor = soak.RunMonitor(interval_s=0.2, console=console)
            report, _ = self.run_report(monitor)
            console.close()
            self.assertEqual(report["logPath"], str(log))
            self.assertIn("sse_soak", log.read_text())

    def test_a_refusal_before_the_device_is_touched_still_carries_clocks(self):
        report, code = self.run_report(extra=["--build-env", "native"])
        self.assertEqual(code, soak.EXIT_INVALID)
        self.assertIn("startedAt", report)
        self.assertIn("endedAt", report)
        self.assertEqual(report["phases"], [])

    def test_the_planned_duration_comes_from_the_arguments_not_a_constant(self):
        args = soak.build_parser().parse_args([
            "--device", "d", "--duration", "60", "--storm-duration", "10",
            "--storm-settle-s", "2", "--reset-recovery-timeout-s", "7",
            "--sse-resume-timeout-s", "3",
        ])
        self.assertEqual(soak.planned_driver_duration_s("sse_soak", args), 60.0)
        self.assertEqual(soak.planned_driver_duration_s("reconnect_storm", args), 12.0)
        self.assertEqual(soak.planned_driver_duration_s("c6_reset_recovery", args), 10.0)
        self.assertIsNone(soak.planned_driver_duration_s("a_driver_added_later", args))


class TheCheckpointArtifact(unittest.TestCase):
    def setUp(self):
        self.server, self.thread = soak._start_fixture_server(
            status_body=soak.FIXTURE_ARTOO_STATUS_BODY, sse_mode="shipping_live")
        self.addCleanup(soak._stop_fixture_server, self.server, self.thread)
        self.port = self.server.server_address[1]

    def test_a_checkpoint_can_never_be_read_as_a_finished_run(self):
        with tempfile.TemporaryDirectory() as tmp:
            artifact = Path(tmp) / "run.json"
            monitor = soak.RunMonitor(
                interval_s=0.2, quiet=True,
                checkpoint_writer=lambda report, path=artifact: soak.write_json_artifact(
                    path, json.dumps(report, indent=2, default=str) + "\n"),
            )
            args = soak.build_parser().parse_args([
                "--device", "127.0.0.1", "--port", str(self.port), "--image", "artoo",
                "--driver", "sse_soak", "--duration", "1.0", "--num-clients", "1",
                "--status-poll-interval-s", "0.2", "--early-stall-check-s", "1.0",
            ])
            soak.run(args, monitor)
            # The last checkpoint on disk is the driver-boundary one, written
            # before main() would overwrite it with the final report.
            checkpoint = json.loads(artifact.read_text())
        self.assertIs(checkpoint["checkpoint"], True)
        self.assertEqual(checkpoint["verdict"], soak.VERDICT_CHECKPOINT)
        self.assertNotIn(
            checkpoint["verdict"],
            (soak.VERDICT_PASS, soak.VERDICT_FAIL, soak.VERDICT_INVALID),
            "a checkpoint must not carry any verdict a finished run can carry",
        )
        self.assertEqual(checkpoint["drivers"]["sse_soak"]["verdict"], "PASS")

    def test_the_artifact_is_replaced_atomically_and_leaves_no_partial_behind(self):
        with tempfile.TemporaryDirectory() as tmp:
            artifact = Path(tmp) / "run.json"
            soak.write_json_artifact(artifact, '{"first": true}\n')
            soak.write_json_artifact(artifact, '{"second": true}\n')
            self.assertEqual(json.loads(artifact.read_text()), {"second": True})
            self.assertEqual(
                sorted(path.name for path in Path(tmp).iterdir()), ["run.json"],
                "the temporary must not survive a successful write")


class TheStormSeparatesPollGranularityFromLiveness(unittest.TestCase):
    """The classification that used to fail every healthy board.

    `run_reconnect_storm()` passed its 0.25s abort granularity to
    `stream_sse()` as the read window, and an expired read window was reported
    as a stalled stream -- so any stream slower than 4 Hz read as broken, which
    is every stream this harness drives (a device run failed with 218 stalls
    out of 218 cycles against a board that was fine). The classifier
    below now judges what a cycle actually observed, and these pin both sides
    of that line: what may fail, and what must still fail.
    """

    @staticmethod
    def stream(elapsed_s, connect_ok=True, status_code=200):
        return soak.SseStreamResult(
            frames=[], truncated=False, truncated_detail=None, error=None,
            connect_ok=connect_ok, status_line="200 OK", elapsed_s=elapsed_s,
            status_code=status_code,
        )

    def test_a_cycle_that_saw_a_frame_is_alive_however_short_it_was(self):
        self.assertEqual(
            soak._storm_cycle_liveness(self.stream(0.1), frame_count=1, max_silence_s=5.0),
            soak.LIVENESS_DELIVERED)

    def test_a_short_silent_look_is_not_assessed_and_never_fine(self):
        verdict = soak._storm_cycle_liveness(
            self.stream(0.7), frame_count=0, max_silence_s=5.0)
        self.assertEqual(verdict, soak.LIVENESS_NOT_ASSESSED)
        self.assertNotEqual(verdict, soak.LIVENESS_DELIVERED,
                            "0.7s of silence says nothing about a 1 Hz stream")

    def test_a_full_budget_of_silence_on_an_open_stream_is_a_stall(self):
        self.assertEqual(
            soak._storm_cycle_liveness(self.stream(5.4), frame_count=0, max_silence_s=5.0),
            soak.LIVENESS_SILENT)

    def test_a_cycle_that_never_opened_a_stream_judges_no_liveness(self):
        self.assertEqual(
            soak._storm_cycle_liveness(
                self.stream(9.0, connect_ok=False, status_code=None),
                frame_count=0, max_silence_s=5.0),
            soak.LIVENESS_NOT_ASSESSED)
        self.assertEqual(
            soak._storm_cycle_liveness(
                self.stream(9.0, status_code=503), frame_count=0, max_silence_s=5.0),
            soak.LIVENESS_NOT_ASSESSED,
            "a stream the cap refused was never open to go silent")

    def test_the_liveness_hold_is_longer_than_the_budget_and_derived_from_the_arguments(self):
        hold = soak._storm_hold_seconds(
            0, cycle_min_s=0.5, cycle_max_s=3.0, max_silence_s=5.0, liveness_cycle_every=4)
        self.assertEqual(hold, 8.0)
        self.assertGreater(hold, 5.0,
                           "a liveness cycle must be able to observe a full budget of "
                           "silence, or its silence proves nothing")
        for index in (1, 2, 3):
            ordinary = soak._storm_hold_seconds(
                index, cycle_min_s=0.5, cycle_max_s=3.0, max_silence_s=5.0,
                liveness_cycle_every=4)
            self.assertLessEqual(ordinary, 3.0)
            self.assertGreaterEqual(ordinary, 0.5)

    def test_liveness_cycles_can_be_switched_off_entirely(self):
        for index in range(5):
            self.assertLessEqual(
                soak._storm_hold_seconds(
                    index, cycle_min_s=0.5, cycle_max_s=3.0, max_silence_s=5.0,
                    liveness_cycle_every=0),
                3.0)

    def test_the_read_window_expiry_is_reported_apart_from_the_error(self):
        """A timeout is a fact about the window, not a verdict about the
        stream. Both fields exist so the storm can tell them apart."""
        result = soak.SseStreamResult(
            frames=[], truncated=False, truncated_detail=None, error=None,
            connect_ok=True, status_line="200 OK", elapsed_s=1.0, status_code=200)
        self.assertIs(result.read_timed_out, False)
        self.assertIsNone(result.silence_before_end_s)


class TheSignalHandlerIsSafeToUse(unittest.TestCase):
    def test_a_signal_sets_the_flag_and_restores_the_default_disposition(self):
        monitor = soak.RunMonitor(interval_s=0.0, quiet=True)
        previous = {sig: signal.getsignal(sig) for sig in (signal.SIGINT, signal.SIGTERM)}
        self.addCleanup(
            lambda: [signal.signal(sig, handler) for sig, handler in previous.items()])
        monitor.install_signal_handlers()
        self.assertIsNot(signal.getsignal(signal.SIGTERM), signal.SIG_DFL)
        monitor._handle_signal(signal.SIGTERM, None)
        self.assertTrue(monitor.interrupted)
        self.assertEqual(monitor.signal_name, "SIGTERM")
        self.assertIs(
            signal.getsignal(signal.SIGTERM), signal.SIG_DFL,
            "a second signal must kill the process: an interrupt path that cannot itself "
            "be interrupted is a hang")


if __name__ == "__main__":
    unittest.main()
