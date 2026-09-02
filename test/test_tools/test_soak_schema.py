"""Pinned behaviour for the soak harness's status schemas (#197).

`tools/soak.py` reads two different firmware images over HTTP, and a field
mapped to the wrong name stays invisible until a bench day -- bench days on
this project are expensive and operator-scheduled. Two kinds of test here,
both cheap:

1. **Decision logic**, pinned directly: which reset reasons are crash-shaped,
   what counts as a restart on each image, what a wrong `--image` does, and
   which report keys each image may emit.
2. **Drift guards** that read the firmware sources the schemas were derived
   from (`src/web/web_server.cpp`, `src/reset_reason.cpp`,
   `bringup/p4_hosted_bench.cpp`). If a payload field is renamed on either
   image, this suite goes red instead of the harness silently reading a field
   that is no longer there.

The harness's own `--self-test` covers the wire: real SSE framing through the
real parser and both drivers end to end against local fixtures. It is not
duplicated here. Run via `python3 -m unittest discover -s test/test_tools`;
the slice gate runs this suite as its first check.
"""

import re
import sys
import unittest
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPO_ROOT / "tools"))

import soak  # noqa: E402

WEB_SERVER_CPP = (REPO_ROOT / "src" / "web" / "web_server.cpp").read_text()
RESET_REASON_CPP = (REPO_ROOT / "src" / "reset_reason.cpp").read_text()
BENCH_CPP = (REPO_ROOT / "bringup" / "p4_hosted_bench.cpp").read_text()

BENCH = soak.SCHEMAS["bench"]
SHIPPING = soak.SCHEMAS["shipping"]


def bench_status_body(**overrides) -> dict:
    body = dict(soak.FIXTURE_STATUS_BODY)
    body.update(overrides)
    return body


def shipping_status_body(**overrides) -> dict:
    body = dict(soak.FIXTURE_SHIPPING_STATUS_BODY)
    body["hostedLink"] = dict(body["hostedLink"])
    body.update(overrides)
    return body


class ShippingFieldNamesMatchTheFirmware(unittest.TestCase):
    """Every name the shipping schema reads must still be emitted by
    buildStatusJson(). These are the reads that a bench day would otherwise
    discover."""

    def test_flat_fields_are_emitted_as_json_keys(self):
        for field in (SHIPPING.heap_field, SHIPPING.sse_clients_field,
                      SHIPPING.restart_field, "resetReason"):
            with self.subTest(field=field):
                self.assertIn(f'\\"{field}\\":', WEB_SERVER_CPP,
                              f"{field!r} is no longer emitted by buildStatusJson()")

    def test_reset_reason_is_emitted_as_a_string(self):
        # The harness treats it as resetReasonName()'s string. An int round
        # trip would re-create the guessed-ESP_RST_* defect this ticket
        # already recorded once.
        self.assertIn('\\"resetReason\\":\\"%s\\"', WEB_SERVER_CPP)
        self.assertIn("resetReasonName(esp_reset_reason())", WEB_SERVER_CPP)

    def test_ladder_fields_are_emitted_inside_the_hostedlink_object(self):
        self.assertIn(f'\\"{SHIPPING.ladder_container}\\":{{', WEB_SERVER_CPP)
        for name in SHIPPING.ladder_fields.values():
            with self.subTest(field=name):
                self.assertIn(f'\\"{name}\\":', WEB_SERVER_CPP)

    def test_the_shipping_payload_still_publishes_no_bootcount(self):
        # The whole shipping restart model rests on this: no bootCount means
        # uptimeMs regression is the only reboot evidence available. If one
        # ever appears, the schema should read it rather than keep inferring.
        self.assertNotIn("bootCount", WEB_SERVER_CPP)
        self.assertFalse(SHIPPING.publishes_boot_count)

    def test_no_c6_reset_route_exists_in_the_shipping_sources(self):
        route_table = (REPO_ROOT / "src" / "web" / "web_seam_routes.cpp").read_text()
        self.assertNotIn("c6/reset", route_table)
        self.assertIsNone(SHIPPING.reset_path)


class BenchFieldNamesMatchTheFirmware(unittest.TestCase):
    def test_flat_fields_are_emitted_by_the_bench_handler(self):
        for field in (BENCH.heap_field, BENCH.sse_clients_field, BENCH.restart_field,
                      "resetReason", *BENCH.ladder_fields.values()):
            with self.subTest(field=field):
                self.assertIn(f'doc["{field}"]', BENCH_CPP,
                              f"{field!r} is no longer emitted by the bench handleStatus()")

    def test_bench_reset_reason_is_emitted_as_an_int(self):
        self.assertIn('doc["resetReason"] = static_cast<int>(esp_reset_reason());', BENCH_CPP)

    def test_the_bench_publishes_the_reset_route_the_schema_posts_to(self):
        self.assertEqual(BENCH.reset_path, "/api/c6/reset")
        self.assertIn("/api/c6/reset", BENCH_CPP)


class ResetReasonClassification(unittest.TestCase):
    def test_the_three_shipping_name_sets_cover_resetreasonname_exactly(self):
        # Read the switch rather than trusting the harness's copy of it. A new
        # case (say ESP_RST_CPU_LOCKUP getting its own name) narrows the
        # ambiguity and must be reflected, not silently ignored.
        emitted = set(re.findall(r'return "([A-Z_]+)";', RESET_REASON_CPP))
        classified = (soak.SHIPPING_CRASH_SHAPED_RESET_NAMES
                      | soak.SHIPPING_CLEAN_RESET_NAMES
                      | soak.SHIPPING_UNKNOWN_RESET_NAMES)
        self.assertEqual(emitted, classified)

    def test_shipping_crash_shaped_names(self):
        for name in ("PANIC", "INT_WDT", "TASK_WDT", "WDT"):
            with self.subTest(name=name):
                assessment = SHIPPING.reset_reason(shipping_status_body(resetReason=name), "t")
                self.assertIs(assessment.crash_shaped, True)

    def test_shipping_clean_names(self):
        for name in ("POWERON", "EXTERNAL", "SOFTWARE", "DEEPSLEEP", "BROWNOUT", "SDIO"):
            with self.subTest(name=name):
                assessment = SHIPPING.reset_reason(shipping_status_body(resetReason=name), "t")
                self.assertIs(assessment.crash_shaped, False)

    def test_collapsed_and_undetermined_names_read_as_unknown_never_as_clean(self):
        for name in ("OTHER", "UNKNOWN", "SOMETHING_ELSE"):
            with self.subTest(name=name):
                assessment = SHIPPING.reset_reason(shipping_status_body(resetReason=name), "t")
                self.assertIsNone(assessment.crash_shaped)
                self.assertTrue(assessment.caveat)

    def test_shipping_reset_reason_must_be_a_string(self):
        with self.assertRaises(TypeError):
            SHIPPING.reset_reason(shipping_status_body(resetReason=4), "t")

    def test_bench_reset_reason_uses_the_enum_read_from_esp_system_h(self):
        self.assertEqual(soak.ESP_RESET_REASON_NAMES[6], "ESP_RST_TASK_WDT")
        self.assertIn(6, soak.BAD_RESET_REASONS)
        self.assertNotIn(1, soak.BAD_RESET_REASONS)  # a normal power-on is not a fault
        crash = BENCH.reset_reason(bench_status_body(resetReason=6), "t")
        self.assertIs(crash.crash_shaped, True)
        self.assertEqual(crash.display, "ESP_RST_TASK_WDT")
        clean = BENCH.reset_reason(bench_status_body(resetReason=1), "t")
        self.assertIs(clean.crash_shaped, False)


class RestartEvidence(unittest.TestCase):
    def test_bench_treats_any_bootcount_change_as_a_restart(self):
        self.assertFalse(BENCH.restart_detected(4, [4, 4, 4]))
        self.assertTrue(BENCH.restart_detected(4, [4, 5]))
        # A power cycle resets the RTC counter downward.
        self.assertTrue(BENCH.restart_detected(4, [1]))

    def test_shipping_reads_a_restart_as_uptime_stepping_backwards(self):
        self.assertFalse(SHIPPING.restart_detected(3_600_000, [3_601_000, 3_602_000]))
        self.assertTrue(SHIPPING.restart_detected(3_600_000, [3_601_000, 1_200]))

    def test_shipping_compares_against_the_previous_sample_not_the_baseline(self):
        # A device that reboots and then runs past its old uptime still shows
        # the step down; comparing only against the baseline would miss it.
        self.assertTrue(SHIPPING.restart_detected(1_000, [500, 900_000]))

    def test_report_keys_are_each_image_s_own(self):
        bench_report = BENCH.restart_report(4, 5, True)
        self.assertEqual(bench_report,
                         {"baselineBootCount": 4, "finalBootCount": 5, "bootCountAdvanced": True})
        shipping_report = SHIPPING.restart_report(10, 20, False)
        self.assertEqual(shipping_report,
                         {"baselineUptimeMs": 10, "finalUptimeMs": 20,
                          "uptimeMsWentBackwards": False})
        # The point of the split: no bootCount key on a shipping report, so a
        # consumer looking for one finds nothing rather than an uptime reading
        # wearing bootCount's name.
        self.assertNotIn("baselineBootCount", shipping_report)


class DeclaredImageIsChecked(unittest.TestCase):
    def test_each_fixture_satisfies_its_own_schema(self):
        self.assertEqual(SHIPPING.structural_mismatches(shipping_status_body()), [])
        self.assertEqual(BENCH.structural_mismatches(bench_status_body()), [])

    def test_a_bench_payload_is_refused_by_the_shipping_schema(self):
        mismatches = SHIPPING.structural_mismatches(bench_status_body())
        self.assertTrue(mismatches)
        self.assertTrue(any("bootCount is present" in m for m in mismatches), mismatches)

    def test_a_shipping_payload_is_refused_by_the_bench_schema(self):
        mismatches = BENCH.structural_mismatches(shipping_status_body())
        self.assertTrue(mismatches)
        self.assertTrue(any("bootCount" in m for m in mismatches), mismatches)

    def test_a_missing_hostedlink_block_is_a_mismatch_not_an_empty_ladder(self):
        body = shipping_status_body()
        body.pop("hostedLink")
        mismatches = SHIPPING.structural_mismatches(body)
        self.assertTrue(any("hostedLink" in m for m in mismatches), mismatches)

    def test_identify_schema_names_the_image_but_only_when_unambiguous(self):
        self.assertIs(soak.identify_schema(shipping_status_body()), SHIPPING)
        self.assertIs(soak.identify_schema(bench_status_body()), BENCH)
        self.assertIsNone(soak.identify_schema({"nothing": "recognisable"}))

    def test_link_readiness_needs_transport_evidence_not_just_wificonnected(self):
        # wifiConnected is WiFi.status(), which #184 proved stays WL_CONNECTED
        # through a dead SDIO transport.
        ready, _ = SHIPPING.link_readiness(shipping_status_body())
        self.assertTrue(ready)
        for phase in ("armed", "attempting", "degraded"):
            with self.subTest(phase=phase):
                body = shipping_status_body()
                body["hostedLink"]["phase"] = phase
                ready, why_not = SHIPPING.link_readiness(body)
                self.assertFalse(ready)
                self.assertIn(phase, why_not)
        body = shipping_status_body(wifiConnected=None)
        ready, why_not = SHIPPING.link_readiness(body)
        self.assertFalse(ready)


class FieldReads(unittest.TestCase):
    def test_shipping_reads_the_nested_ladder(self):
        ladder = SHIPPING.ladder(shipping_status_body(), "t")
        self.assertEqual(ladder.state, "idle")
        self.assertEqual(ladder.transport_up_event_count, 1)

    def test_a_missing_container_is_reported_as_a_missing_container(self):
        body = shipping_status_body()
        body.pop("hostedLink")
        with self.assertRaises(KeyError) as caught:
            SHIPPING.ladder(body, "t")
        self.assertIn("missing required object 'hostedLink'", str(caught.exception))

    def test_a_json_true_is_not_read_as_the_integer_one(self):
        # bool is a subclass of int in Python; a payload sending `true` for a
        # counter is a type error, not the value 1.
        self.assertTrue(soak._type_mismatch(True, int))
        with self.assertRaises(TypeError):
            SHIPPING.heap_largest_free(shipping_status_body(heapLargest8bit=True), "t")

    def test_a_soft_sample_records_an_anomaly_instead_of_ending_the_run(self):
        anomalies = []
        values = soak._collect_field(
            [shipping_status_body(), {"hostedLink": {}}], "phase", str, anomalies,
            container="hostedLink")
        self.assertEqual(values, ["idle"])
        self.assertEqual(len(anomalies), 1)
        self.assertIn("hostedLink.phase", anomalies[0])


class ContinuityModels(unittest.TestCase):
    def frame(self, event=None, data="{}", frame_id=None):
        return soak.SseFrame(id=frame_id, event=event, retry=None, data=data)

    def test_each_image_gets_its_own_model(self):
        self.assertEqual(BENCH.new_continuity_tracker().model, "counter")
        self.assertEqual(SHIPPING.new_continuity_tracker().model, "heartbeat")

    def test_the_counter_model_still_counts_gaps(self):
        tracker = BENCH.new_continuity_tracker()
        for counter in (0, 1, 3):
            tracker.observe(self.frame(data=str(counter)), 0.0)
        fields, reasons = BENCH.summarize_continuity([tracker], 5.0)
        self.assertEqual(fields["totalFrameGaps"], 1)
        self.assertTrue(reasons)

    def test_the_heartbeat_model_measures_silence_from_the_connection(self):
        tracker = SHIPPING.new_continuity_tracker()
        tracker.stream_started(0.0)
        tracker.observe(self.frame(event="rc", frame_id=1000), 1.0)
        tracker.observe(self.frame(event="rc", frame_id=2000), 9.0)
        tracker.stream_ended(9.0, stopped_by_harness=True)
        fields, reasons = SHIPPING.summarize_continuity([tracker], 5.0)
        self.assertEqual(fields["maxSilenceSObserved"], 8.0)
        self.assertTrue(any("no SSE frame" in r for r in reasons), reasons)

    def test_a_healthy_heartbeat_stream_produces_no_reason(self):
        tracker = SHIPPING.new_continuity_tracker()
        tracker.stream_started(0.0)
        for tick in range(1, 5):
            tracker.observe(self.frame(event="rc", frame_id=tick * 1000), float(tick))
        tracker.stream_ended(4.0, stopped_by_harness=True)
        fields, reasons = SHIPPING.summarize_continuity([tracker], 5.0)
        self.assertEqual(reasons, [])
        self.assertEqual(fields["totalHeartbeatFrames"], 4)

    def test_a_stream_the_peer_ends_early_is_reported(self):
        tracker = SHIPPING.new_continuity_tracker()
        tracker.stream_started(0.0)
        tracker.observe(self.frame(event="rc", frame_id=1000), 1.0)
        tracker.stream_ended(1.1, stopped_by_harness=False)
        _, reasons = SHIPPING.summarize_continuity([tracker], 5.0)
        self.assertTrue(any("ended before the harness stopped it" in r for r in reasons), reasons)

    def test_a_nameless_frame_is_reported_rather_than_counted_as_an_event(self):
        # This is what a bench stream looks like to a shipping reader.
        tracker = SHIPPING.new_continuity_tracker()
        tracker.stream_started(0.0)
        tracker.observe(self.frame(data="7"), 0.5)
        tracker.stream_ended(0.6, stopped_by_harness=True)
        fields, reasons = SHIPPING.summarize_continuity([tracker], 5.0)
        self.assertEqual(fields["totalFramesWithoutEventName"], 1)
        self.assertTrue(any("no event: name" in r for r in reasons), reasons)

    def test_an_id_going_backwards_is_reported(self):
        tracker = SHIPPING.new_continuity_tracker()
        tracker.stream_started(0.0)
        tracker.observe(self.frame(event="rc", frame_id=5000), 1.0)
        tracker.observe(self.frame(event="rc", frame_id=4000), 2.0)
        tracker.stream_ended(2.0, stopped_by_harness=True)
        fields, reasons = SHIPPING.summarize_continuity([tracker], 5.0)
        self.assertEqual(fields["totalIdRegressions"], 1)
        self.assertTrue(any("go backwards" in r for r in reasons), reasons)

    def test_the_shipping_event_names_match_what_the_firmware_broadcasts(self):
        broadcast = set(re.findall(r'webEventStreamBroadcast\("(\w+)"', WEB_SERVER_CPP))
        self.assertEqual(broadcast, set(soak.SHIPPING_SSE_EVENT_NAMES))
        self.assertIn(soak.SHIPPING_SSE_HEARTBEAT_EVENT, broadcast)


class VerdictComposition(unittest.TestCase):
    def test_an_unavailable_driver_can_never_reach_a_passing_exit_code(self):
        verdict, code = soak._compose_overall_verdict({
            "sse_soak": {"verdict": "PASS"},
            "c6_reset_recovery": {"verdict": "UNAVAILABLE"},
        })
        self.assertEqual(verdict, "INVALID / UNKNOWN")
        self.assertEqual(code, soak.EXIT_INVALID_UNKNOWN)

    def test_a_real_failure_outranks_a_coverage_gap(self):
        verdict, code = soak._compose_overall_verdict({
            "sse_soak": {"verdict": "FAIL"},
            "c6_reset_recovery": {"verdict": "UNAVAILABLE"},
        })
        self.assertEqual(verdict, "NO-GO")
        self.assertEqual(code, soak.EXIT_NO_GO)

    def test_all_passing_is_no_immediate_blocker(self):
        verdict, code = soak._compose_overall_verdict({
            "sse_soak": {"verdict": "PASS"},
            "reconnect_storm": {"verdict": "PASS"},
        })
        self.assertEqual(verdict, "NO IMMEDIATE BLOCKER")
        self.assertEqual(code, soak.EXIT_NO_IMMEDIATE_BLOCKER)

    def test_a_non_json_body_is_a_contract_violation_not_a_traceback(self):
        # A device answering with an HTML error page must leave a verdict on a
        # multi-hour run, not a stack trace.
        def explode(*_args):
            raise __import__("json").JSONDecodeError("Expecting value", "<html>", 0)

        result = soak._run_driver_safely("sse_soak", explode)
        self.assertEqual(result["verdict"], "INVALID")


class ResetDriverRefusesOnShipping(unittest.TestCase):
    def test_it_refuses_before_building_a_request(self):
        # The client is deliberately unusable: reaching the network at all
        # would raise here, which is the assertion.
        result = soak.run_c6_reset_recovery(
            client=None, schema=SHIPPING, recovery_timeout_s=1.0, poll_interval_s=0.1,
            heap_tolerance_pct=20.0, sse_resume_timeout_s=1.0,
        )
        self.assertEqual(result["verdict"], "UNAVAILABLE")
        self.assertTrue(any("#243" in reason for reason in result["reasons"]), result["reasons"])

    def test_the_bench_schema_still_has_a_route_to_post_to(self):
        self.assertEqual(BENCH.reset_path, soak.DEFAULT_RESET_PATH)


if __name__ == "__main__":
    unittest.main()
