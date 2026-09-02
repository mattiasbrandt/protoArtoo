"""Pinned behaviour for the soak harness's status schemas (#197).

`tools/soak.py` reads three different firmware images over HTTP, and a field
mapped to the wrong name stays invisible until a bench day -- bench days on
this project are expensive and operator-scheduled. Two kinds of test here,
both cheap:

1. **Decision logic**, pinned directly: which reset reasons are crash-shaped,
   what counts as a restart on each image, what a wrong `--image` does, and
   which report keys each image may emit.
2. **Drift guards** that read the firmware sources the schemas were derived
   from (`src/web/web_server.cpp`, `src/reset_reason.cpp`, `include/config.h`,
   `bringup/p4_hosted_bench.cpp`). If a payload field is renamed on any image,
   or a board capability stops gating what the schemas assume it gates, this
   suite goes red instead of the harness silently reading a field that is no
   longer there.

The harness's own `--self-test` covers the wire: real SSE framing through the
real parser and every driver end to end against local fixtures. It is not
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
CONFIG_H = (REPO_ROOT / "include" / "config.h").read_text()

BENCH = soak.SCHEMAS["bench"]
SHIPPING = soak.SCHEMAS["shipping"]
ARTOO = soak.SCHEMAS["artoo"]


def bench_status_body(**overrides) -> dict:
    body = dict(soak.FIXTURE_STATUS_BODY)
    body.update(overrides)
    return body


def shipping_status_body(**overrides) -> dict:
    body = dict(soak.FIXTURE_SHIPPING_STATUS_BODY)
    body["hostedLink"] = dict(body["hostedLink"])
    body.update(overrides)
    return body


def artoo_status_body(**overrides) -> dict:
    body = dict(soak.FIXTURE_ARTOO_STATUS_BODY)
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


class ArtooIsTheSharedStatusBuilderMinusOneCapability(unittest.TestCase):
    """The artoo schema's whole premise: `buildStatusJson()` is one function
    serving every board, and `PA_CAP_HOSTED_WIFI` is the only thing that
    changes its shape. These read the firmware rather than trusting the
    premise -- if the hostedLink block ever becomes unconditional, or the
    capability stops being 0 on artoo-esp32, the schema is wrong and this
    suite is where that surfaces."""

    def test_the_hostedlink_object_is_emitted_only_under_the_capability_guard(self):
        guarded = re.findall(r"#if PA_CAP_HOSTED_WIFI\n(.*?)\n#endif", WEB_SERVER_CPP,
                             re.DOTALL)
        emit = f'\\"{soak.HOSTED_LINK_CONTAINER}\\":'
        total = WEB_SERVER_CPP.count(emit)
        inside = sum(block.count(emit) for block in guarded)
        self.assertGreater(total, 0, "buildStatusJson() no longer emits hostedLink at all")
        self.assertEqual(
            total, inside,
            "hostedLink is emitted outside `#if PA_CAP_HOSTED_WIFI`; the artoo schema "
            "requires its ABSENCE and would start refusing a valid artoo payload")

    def test_the_capability_is_zero_on_artoo_esp32(self):
        # config.h has several `#if PA_BOARD == PA_BOARD_ARTOO_ESP32` arms
        # (chip target, pin map, capabilities); pick the one that declares the
        # capability rather than assuming it is the first.
        arms = re.findall(r"#if PA_BOARD == PA_BOARD_ARTOO_ESP32\n(.*?)\n#elif",
                          CONFIG_H, re.DOTALL)
        declaring = [arm for arm in arms if "PA_CAP_HOSTED_WIFI" in arm]
        self.assertEqual(len(declaring), 1,
                         "expected exactly one artoo-esp32 arm to declare "
                         f"PA_CAP_HOSTED_WIFI, found {len(declaring)}")
        self.assertRegex(declaring[0], r"#define\s+PA_CAP_HOSTED_WIFI\s+0\b")

    def test_the_event_stream_task_carries_no_capability_guard(self):
        # The artoo schema reuses the shipping heartbeat continuity model on
        # the strength of this: same task, same three event names, same 1 Hz
        # tick, no board branch anywhere in it.
        start = WEB_SERVER_CPP.index("void eventStreamTask(void*) {")
        body = WEB_SERVER_CPP[start:WEB_SERVER_CPP.index("\n}\n", start)]
        self.assertIsNone(
            re.search(r"^\s*#\s*(if|ifdef|ifndef|else|elif|endif)\b", body, re.M),
            "eventStreamTask() grew a preprocessor branch; the artoo and shipping "
            "images may no longer put the same frames on the wire")
        self.assertEqual(ARTOO.new_continuity_tracker().model, "heartbeat")

    def test_artoo_reads_the_same_shared_fields_as_shipping(self):
        # They come out of the same unconditional snprintf, so a divergence
        # here would mean one of the two schemas is reading a name the
        # firmware does not emit for it.
        for attribute in ("heap_field", "sse_clients_field", "restart_field",
                          "restart_verb", "reset_reason_kind"):
            with self.subTest(attribute=attribute):
                self.assertEqual(getattr(ARTOO, attribute), getattr(SHIPPING, attribute))
        for field in (ARTOO.heap_field, ARTOO.sse_clients_field, ARTOO.restart_field,
                      "resetReason", "wifiConnected"):
            with self.subTest(field=field):
                self.assertIn(f'\\"{field}\\":', WEB_SERVER_CPP,
                              f"{field!r} is no longer emitted by buildStatusJson()")


class ArtooPublishesNoRecoveryLadder(unittest.TestCase):
    def test_the_absence_is_a_property_not_an_empty_reading(self):
        self.assertFalse(ARTOO.publishes_recovery_ladder)
        self.assertIsNone(ARTOO.ladder_container)
        self.assertEqual(ARTOO.ladder_fields, {})
        self.assertIsNone(ARTOO.ladder(artoo_status_body(), "t"))
        anomalies = []
        self.assertIsNone(ARTOO.collect_ladder([artoo_status_body()], anomalies))
        # None, not an empty LadderSamples, and not an anomaly either: nothing
        # was malformed, there is simply no block on this image.
        self.assertEqual(anomalies, [])

    def test_the_field_map_says_so_in_words(self):
        published = ARTOO.fields_read()["recoveryLadder"]
        self.assertEqual(published, ARTOO.ladder_absence_note)
        self.assertIn("PA_CAP_HOSTED_WIFI", published)
        # The images that do have one still publish a field map, unchanged.
        self.assertEqual(SHIPPING.fields_read()["recoveryLadder"]["state"],
                         "hostedLink.phase")
        self.assertEqual(BENCH.fields_read()["recoveryLadder"]["state"],
                         "recoveryLadderState")

    def test_the_shipping_container_name_is_shared_with_the_absence_check(self):
        # One constant, so a rename cannot reach the reader without also
        # reaching the schema that requires the block to be missing.
        self.assertEqual(SHIPPING.ladder_container, soak.HOSTED_LINK_CONTAINER)


class ArtooDeclaredImageIsChecked(unittest.TestCase):
    def test_the_artoo_fixture_satisfies_its_own_schema(self):
        self.assertEqual(ARTOO.structural_mismatches(artoo_status_body()), [])

    def test_a_shipping_payload_is_refused_by_the_artoo_schema(self):
        mismatches = ARTOO.structural_mismatches(shipping_status_body())
        self.assertTrue(mismatches)
        self.assertTrue(any("'hostedLink' is present" in m for m in mismatches), mismatches)

    def test_an_artoo_payload_is_refused_by_the_shipping_schema(self):
        mismatches = SHIPPING.structural_mismatches(artoo_status_body())
        self.assertTrue(mismatches)
        self.assertTrue(any("no 'hostedLink' object" in m for m in mismatches), mismatches)

    def test_a_bootcount_is_refused_positively_the_same_way_shipping_refuses_one(self):
        mismatches = ARTOO.structural_mismatches(artoo_status_body(bootCount=1))
        self.assertTrue(any("bootCount is present" in m for m in mismatches), mismatches)
        self.assertTrue(any("not an artoo image" in m for m in mismatches), mismatches)

    def test_a_bench_payload_is_refused_by_the_artoo_schema(self):
        self.assertTrue(ARTOO.structural_mismatches(bench_status_body()))

    def test_identify_schema_separates_all_three(self):
        self.assertIs(soak.identify_schema(artoo_status_body()), ARTOO)
        self.assertIs(soak.identify_schema(shipping_status_body()), SHIPPING)
        self.assertIs(soak.identify_schema(bench_status_body()), BENCH)


class ArtooLinkReadiness(unittest.TestCase):
    def test_it_reads_wificonnected_and_nothing_else(self):
        ready, why_not = ARTOO.link_readiness(artoo_status_body())
        self.assertTrue(ready)
        self.assertEqual(why_not, "")
        # A phase would be meaningless on this board; a stray one must not
        # change the answer in either direction.
        with_phase = artoo_status_body()
        with_phase[soak.HOSTED_LINK_CONTAINER] = {"phase": "degraded"}
        self.assertTrue(ARTOO.link_readiness(with_phase)[0])

    def test_a_missing_field_is_absent_evidence_not_a_failed_link(self):
        body = artoo_status_body()
        body.pop("wifiConnected")
        ready, why_not = ARTOO.link_readiness(body)
        self.assertFalse(ready)
        self.assertIn("not that the link failed", why_not)

    def test_the_diagnostic_states_that_this_is_weaker_than_the_shipping_check(self):
        _, why_not = ARTOO.link_readiness(artoo_status_body(wifiConnected=False))
        self.assertIn("WEAKER evidence", why_not)
        self.assertIn("hostedLink.phase", why_not)
        self.assertIn("#184", why_not)
        # And it names what wifiConnected actually is, which is not
        # WiFi.status() on its own.
        self.assertIn("apEnabled || staConnected", why_not)
        self.assertIn("fields.wifiConnected = apEnabled || staConnected;",
                      (REPO_ROOT / "src" / "web" / "api_status_serializers.cpp").read_text())


class ArtooHasNoResetRoute(unittest.TestCase):
    def test_the_driver_refuses_before_building_a_request(self):
        # The client is deliberately unusable: reaching the network at all
        # would raise here, which is the assertion.
        result = soak.run_c6_reset_recovery(
            client=None, schema=ARTOO, recovery_timeout_s=1.0, poll_interval_s=0.1,
            heap_tolerance_pct=20.0, sse_resume_timeout_s=1.0,
        )
        self.assertEqual(result["verdict"], "UNAVAILABLE")
        self.assertEqual(result["image"], "artoo")
        reasons = " ".join(result["reasons"])
        self.assertIn("no companion radio", reasons)
        self.assertIn("PA_CAP_HOSTED_WIFI is 0", reasons)

    def test_each_image_gives_its_own_reason_for_having_no_route(self):
        # #243 is a missing route on a board that has a C6; artoo-esp32 has no
        # C6. Telling an operator the wrong one of those sends them to the
        # wrong ticket.
        self.assertIn("#243", SHIPPING.reset_unavailable_reason)
        self.assertNotIn("#243", ARTOO.reset_unavailable_reason.split("This is not")[0])
        self.assertIsNone(ARTOO.reset_path)
        self.assertIsNone(SHIPPING.reset_path)
        self.assertEqual(BENCH.reset_path, soak.DEFAULT_RESET_PATH)

    def test_a_reset_route_implies_a_ladder_to_watch_it_with(self):
        # run_c6_reset_recovery() reads schema.ladder() straight after the
        # reset_path refusal and dereferences it, so a schema with a route and
        # no ladder would deref None there. The coupling is pinned rather than
        # left to the comment at that call site.
        for name, schema in soak.SCHEMAS.items():
            with self.subTest(image=name):
                if schema.reset_path is not None:
                    self.assertTrue(schema.publishes_recovery_ladder)


class ArtooReportKeys(unittest.TestCase):
    def test_restart_evidence_is_uptime_with_no_bootcount_key(self):
        self.assertFalse(ARTOO.publishes_boot_count)
        report = ARTOO.restart_report(3_600_000, 3_601_000, False)
        self.assertEqual(report, {"baselineUptimeMs": 3_600_000,
                                  "finalUptimeMs": 3_601_000,
                                  "uptimeMsWentBackwards": False})
        self.assertNotIn("bootCount", str(report))

    def test_the_same_two_uptime_limits_hold_as_on_shipping(self):
        self.assertFalse(ARTOO.restart_detected(3_600_000, [3_601_000, 3_602_000]))
        self.assertTrue(ARTOO.restart_detected(3_600_000, [3_601_000, 1_200]))
        self.assertTrue(ARTOO.restart_detected(1_000, [500, 900_000]))

    def test_reset_reason_classification_is_the_shipping_one(self):
        # Same resetReasonName() on both boards (src/reset_reason.cpp has no
        # board branch), so the tri-state must be identical.
        self.assertIs(ARTOO.reset_reason(artoo_status_body(resetReason="TASK_WDT"),
                                         "t").crash_shaped, True)
        self.assertIs(ARTOO.reset_reason(artoo_status_body(resetReason="POWERON"),
                                         "t").crash_shaped, False)
        ambiguous = ARTOO.reset_reason(artoo_status_body(resetReason="OTHER"), "t")
        self.assertIsNone(ambiguous.crash_shaped)
        self.assertTrue(ambiguous.caveat)


if __name__ == "__main__":
    unittest.main()
