"""Pinned behaviour for the soak harness's status schemas and its verdict.

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
WEB_ADMISSION_CPP = (REPO_ROOT / "src" / "web" / "web_admission.cpp").read_text()
WEB_ADMISSION_PSYCHIC_CPP = (REPO_ROOT / "src" / "web" / "web_admission_psychic.cpp").read_text()
WEB_EVENT_STREAM_H = (REPO_ROOT / "include" / "web_event_stream.h").read_text()
API_EVENTS_CPP = (REPO_ROOT / "src" / "web" / "api_events.cpp").read_text()
PLATFORMIO_INI_TEXT = (REPO_ROOT / "platformio.ini").read_text()
SOAK_PY = REPO_ROOT / "tools" / "soak.py"

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


def declared_macro_values(macro: str) -> list[str]:
    """Every `-D <macro>=<value>` platformio.ini writes, read straight out of
    the text.

    Deliberately a second, independent read of the same file the harness
    resolves through: the point of these tests is that the harness's numbers
    ARE the file's, and a check that went through the harness's own resolver
    would agree with it however wrong it had become. Comment lines are excluded
    for the reason test_env_flag_declarations.py excludes them -- platformio.ini
    discusses these macros in prose right above declaring them.
    """
    body = "\n".join(
        line for line in PLATFORMIO_INI_TEXT.splitlines() if not line.strip().startswith(";")
    )
    return re.findall(r"-D\s*%s=(\S+)" % re.escape(macro), body)


def pio_section(name: str) -> str:
    """One platformio.ini section's raw text, comments and all."""
    match = re.search(r"(?ms)^\[%s\]\n(.*?)(?=^\[|\Z)" % re.escape(name), PLATFORMIO_INI_TEXT)
    assert match, f"platformio.ini declares no [{name}]"
    return match.group(1)


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
    """The Run Verdict, in the words its Soak Drivers use (CONTEXT.md).

    The strings are asserted as literals rather than through soak.VERDICT_*:
    a test that compared the constant with itself would pass whatever it was
    reworded to, and the point here is that the run and its drivers speak ONE
    vocabulary. The exit-code VALUES are asserted as bare integers for the same
    reason -- they are the contract (ADR 0035) and must not be free to move.
    """

    def test_an_unavailable_driver_can_never_reach_a_passing_exit_code(self):
        verdict, code = soak._compose_overall_verdict({
            "sse_soak": {"verdict": "PASS"},
            "c6_reset_recovery": {"verdict": "UNAVAILABLE"},
        })
        self.assertEqual(verdict, "INVALID")
        self.assertEqual(code, 3)

    def test_a_real_failure_outranks_a_coverage_gap(self):
        verdict, code = soak._compose_overall_verdict({
            "sse_soak": {"verdict": "FAIL"},
            "c6_reset_recovery": {"verdict": "UNAVAILABLE"},
        })
        self.assertEqual(verdict, "FAIL")
        self.assertEqual(code, 2)

    def test_all_passing_is_a_pass(self):
        verdict, code = soak._compose_overall_verdict({
            "sse_soak": {"verdict": "PASS"},
            "reconnect_storm": {"verdict": "PASS"},
        })
        self.assertEqual(verdict, "PASS")
        self.assertEqual(code, 0)

    def test_the_run_speaks_only_its_drivers_words(self):
        # The retired go/no-go vocabulary named a gate that closed with one
        # epic, on an instrument meant to outlive it (ADR 0035). A rewording is
        # sanctioned; leaving the old synonym behind in one output path is not.
        source = SOAK_PY.read_text(encoding="utf-8")
        for retired in ("NO IMMEDIATE BLOCKER", "NO-GO", "INVALID / UNKNOWN"):
            self.assertNotIn(retired, source, f"{retired!r} is retired vocabulary")
        for driver_verdict in (soak.VERDICT_PASS, soak.VERDICT_FAIL, soak.VERDICT_INVALID):
            self.assertEqual(
                soak._compose_overall_verdict({"only": {"verdict": driver_verdict}})[0],
                driver_verdict,
                "one driver's verdict IS the run's -- no translation layer",
            )

    def test_the_exit_codes_are_the_ones_a_wrapper_may_rely_on(self):
        # ADR 0035: these VALUES are contract. Written out rather than derived,
        # so a change to any of them fails here and has to be argued for.
        self.assertEqual(soak.EXIT_PASS, 0)
        self.assertEqual(soak.EXIT_SELF_TEST_FAILURE, 1)
        self.assertEqual(soak.EXIT_FAIL, 2)
        self.assertEqual(soak.EXIT_INVALID, 3)
        self.assertEqual(
            soak._compose_overall_verdict(
                {"only": {"verdict": soak.VERDICT_INTERRUPTED_DRIVER}}),
            (soak.VERDICT_INTERRUPTED_RUN, 3),
            "a truncated run is missing evidence, which is exit 3 -- not a failure",
        )

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


class AdmissionFloorIsReadFromPlatformioIni(unittest.TestCase):
    """The heap verdict's yardstick.

    #194's first graded artoo soak failed on "heapLargest8bit fell to 11764
    from baseline 40948 (beyond 20.0% tolerance)" while heapFree held
    ~80 000, heapMin was unmoved, every refusal counter read 0 and the block
    recovered fully the moment the clients left. A percentage of an arbitrary
    baseline sample is not evidence about service; the floor the firmware
    refuses at is. These tests exist so the harness's floor stays the file's
    floor -- they read platformio.ini themselves rather than asking the
    resolver, which would agree with itself however wrong it had become.
    """

    def test_the_resolved_floors_are_the_values_platformio_ini_declares(self):
        declared_ordinary = declared_macro_values(soak.ADMISSION_FLOOR_MACRO)
        declared_diag = declared_macro_values(soak.ADMISSION_FLOOR_DIAG_MACRO)
        self.assertEqual(len(declared_ordinary), 1, declared_ordinary)
        self.assertEqual(len(declared_diag), 1, declared_diag)
        for env in ("artoo_esp32", "firebeetle2"):
            with self.subTest(env=env):
                floor = soak.resolve_admission_floor(env)
                self.assertEqual(floor.ordinary_bytes, int(declared_ordinary[0]))
                self.assertEqual(floor.diagnostic_bytes, int(declared_diag[0]))

    def test_the_floors_are_reached_through_flags_base_and_nothing_else(self):
        floor = soak.resolve_admission_floor("artoo_esp32")
        self.assertEqual(floor.sources[soak.ADMISSION_FLOOR_MACRO], "[flags_base].build_flags")
        self.assertEqual(floor.sources[soak.ADMISSION_FLOOR_DIAG_MACRO],
                         "[flags_base].build_flags")
        # [env:artoo_esp32] reaches it only by naming the interpolation, which
        # is what makes "not every env inherits the floor" true rather than a
        # defensive hypothetical.
        self.assertIn("${flags_base.build_flags}", pio_section("env:artoo_esp32"))

    def test_an_env_that_never_references_flags_base_resolves_no_floor(self):
        # [env:native] declares its own build_flags and names no interpolation,
        # so no floor reaches it. Per #194's pin this is INVALID, never a
        # default -- and never the `#ifndef` fallback in
        # src/web/web_admission_psychic.cpp, which is a compile-time safety net
        # rather than a calibration for any board.
        self.assertNotIn("${flags_base.build_flags}", pio_section("env:native"))
        with self.assertRaises(soak.BuildConstantUnresolved) as caught:
            soak.resolve_admission_floor("native")
        self.assertIn(soak.ADMISSION_FLOOR_MACRO, str(caught.exception))
        self.assertIn("INVALID", str(caught.exception))

    def test_an_undeclared_environment_is_refused_and_lists_the_real_ones(self):
        with self.assertRaises(soak.BuildConstantUnresolved) as caught:
            soak.resolve_admission_floor("no_such_env")
        self.assertIn("no_such_env", str(caught.exception))
        self.assertIn("artoo_esp32", str(caught.exception))

    def test_a_child_env_inherits_its_parents_flags_when_it_declares_none(self):
        # [env:firebeetle2] declares no build_flags of its own; it extends
        # [env:firebeetle2_bringup], which does. PlatformIO resolves it that
        # way and so must this -- a resolver that only looked at the env's own
        # section would report the shipping image as having no floor.
        self.assertNotRegex(pio_section("env:firebeetle2"), r"(?m)^build_flags\s*=")
        self.assertRegex(pio_section("env:firebeetle2"), r"(?m)^extends\s*=\s*env:firebeetle2_bringup")
        self.assertEqual(soak.resolve_admission_floor("firebeetle2").ordinary_bytes,
                         soak.resolve_admission_floor("firebeetle2_bringup").ordinary_bytes)

    def test_a_bench_override_replaces_the_ordinary_floor_only(self):
        declared_override = declared_macro_values(soak.ADMISSION_FLOOR_OVERRIDE_MACRO)
        self.assertEqual(len(declared_override), 1, declared_override)
        product = soak.resolve_admission_floor("artoo_esp32")
        induced = soak.resolve_admission_floor("artoo_esp32_recovery_bench")
        self.assertEqual(induced.override_bytes, int(declared_override[0]))
        self.assertEqual(induced.ordinary_bytes, int(declared_override[0]))
        # The displaced value is still reported, so a reader can tell an
        # override from a calibration.
        self.assertEqual(induced.declared_ordinary_bytes, product.ordinary_bytes)
        self.assertEqual(induced.diagnostic_bytes, product.diagnostic_bytes)

    def test_two_conflicting_definitions_are_raised_on_rather_than_picked_between(self):
        ini = REPO_ROOT / "test" / "test_tools" / "__pycache__" / "conflicting_platformio.ini"
        ini.parent.mkdir(parents=True, exist_ok=True)
        ini.write_text(
            "[flags_base]\n"
            f"build_flags =\n\t-D {soak.ADMISSION_FLOOR_MACRO}=9000\n"
            f"\t-D {soak.ADMISSION_FLOOR_DIAG_MACRO}=7500\n"
            "\n[env:clash]\n"
            "build_flags =\n\t${flags_base.build_flags}\n"
            f"\t-D {soak.ADMISSION_FLOOR_MACRO}=11500\n",
            encoding="utf-8",
        )
        try:
            with self.assertRaises(soak.BuildConstantUnresolved) as caught:
                soak.resolve_admission_floor("clash", ini_path=ini)
            self.assertIn("two different values", str(caught.exception))
        finally:
            ini.unlink()

    def test_a_floor_of_zero_is_no_floor_and_is_refused(self):
        # webRequestAdmissionDecide()'s comparison is `largestFreeBlock < floor`,
        # which can never be true at 0 -- so a soak judged against it would pass
        # whatever the heap did. That vacuous pass is the class of proof this
        # rule exists to remove, so it is INVALID rather than a floor.
        ini = REPO_ROOT / "test" / "test_tools" / "__pycache__" / "zero_floor_platformio.ini"
        ini.parent.mkdir(parents=True, exist_ok=True)
        ini.write_text(
            "[flags_base]\n"
            f"build_flags =\n\t-D {soak.ADMISSION_FLOOR_MACRO}=0\n"
            f"\t-D {soak.ADMISSION_FLOOR_DIAG_MACRO}=7500\n"
            "\n[env:nofloor]\nbuild_flags =\n\t${flags_base.build_flags}\n",
            encoding="utf-8",
        )
        try:
            with self.assertRaises(soak.BuildConstantUnresolved) as caught:
                soak.resolve_admission_floor("nofloor", ini_path=ini)
            self.assertIn("can never refuse at", str(caught.exception))
        finally:
            ini.unlink()

    def test_the_report_names_the_environment_and_where_the_number_came_from(self):
        report = soak.resolve_admission_floor("artoo_esp32").report()
        self.assertEqual(report["env"], "artoo_esp32")
        self.assertEqual(report["readFrom"], "platformio.ini")
        self.assertIn("flags_base", report["macros"][soak.ADMISSION_FLOOR_MACRO])
        # The two floors are not interchangeable and the report says which one
        # the verdict used.
        self.assertIn("ordinary floor", report["note"])
        self.assertLess(report["diagnosticFloorBytes"], report["ordinaryFloorBytes"])


class SseClientCapIsReadFromTheHeader(unittest.TestCase):
    """The concurrency yardstick.

    `--num-clients 3` used to mean "at production's cap" because the harness
    carried the literal 3. A literal cannot go wrong loudly: if a build ever
    displaced PA_ADMISSION_MAX_SSE_CLIENTS, the same flag would quietly have
    started meaning "one short of the cap" or "one over it", and a run would
    have kept reporting a verdict for a concurrency nobody was testing at. The
    number is now read from include/web_event_stream.h, and these tests read
    that header themselves rather than asking the resolver, which would agree
    with itself however wrong it had become.
    """

    def test_the_resolved_cap_is_the_number_the_header_declares(self):
        declared = re.findall(
            r"(?m)^\s*#\s*define\s+%s\s+(\d+)\s*$" % re.escape(soak.SSE_CLIENT_CAP_MACRO),
            WEB_EVENT_STREAM_H,
        )
        self.assertEqual(len(declared), 1, declared)
        for name, schema in soak.SCHEMAS.items():
            with self.subTest(image=name):
                cap = soak.resolve_sse_client_cap(schema.build_env)
                self.assertEqual(cap.value, int(declared[0]))
                self.assertEqual(cap.header_default, int(declared[0]))
                self.assertEqual(cap.source, "include/web_event_stream.h")

    def test_the_header_default_is_a_default_a_build_can_displace(self):
        # An #ifndef guard is what makes "honour a per-environment override"
        # meaningful. Without it a -D would be a redefinition error, and the
        # resolver's override branch would be resolving something unreachable.
        self.assertIn(f"#ifndef {soak.SSE_CLIENT_CAP_MACRO}", WEB_EVENT_STREAM_H)
        self.assertNotIn(
            f"-D{soak.SSE_CLIENT_CAP_MACRO}", PLATFORMIO_INI_TEXT,
            "no environment overrides the cap today; if one starts to, the resolver "
            "reports it and this expectation is the thing to update",
        )
        self.assertEqual(declared_macro_values(soak.SSE_CLIENT_CAP_MACRO), [])

    def test_an_environment_definition_wins_over_the_header_default(self):
        ini = REPO_ROOT / "test" / "test_tools" / "__pycache__" / "cap_override_platformio.ini"
        ini.parent.mkdir(parents=True, exist_ok=True)
        header_default, _ = soak.read_header_sse_client_cap()
        ini.write_text(
            PLATFORMIO_INI_TEXT
            + soak._cap_override_env_section("cap_override", header_default + 4),
            encoding="utf-8",
        )
        try:
            cap = soak.resolve_sse_client_cap("cap_override", ini_path=ini)
            self.assertEqual(cap.value, header_default + 4)
            self.assertEqual(cap.header_default, header_default)
            self.assertTrue(cap.source.startswith("[env:cap_override]"), cap.source)
            # Local to that environment, not global to the file.
            self.assertEqual(
                soak.resolve_sse_client_cap("artoo_esp32", ini_path=ini).value,
                header_default,
            )
        finally:
            ini.unlink()

    def test_a_header_with_no_cap_is_refused_rather_than_defaulted(self):
        header = REPO_ROOT / "test" / "test_tools" / "__pycache__" / "cap_free_header.h"
        header.parent.mkdir(parents=True, exist_ok=True)
        header.write_text("#pragma once\n", encoding="utf-8")
        try:
            with self.assertRaises(soak.BuildConstantUnresolved) as caught:
                soak.read_header_sse_client_cap(header)
            self.assertIn(soak.SSE_CLIENT_CAP_MACRO, str(caught.exception))
        finally:
            header.unlink()

    def test_the_firmware_still_refuses_at_the_macro_this_cap_describes(self):
        # The cap is only a yardstick because api_events.cpp acts on it. If
        # that handler stopped comparing against this macro, the harness would
        # be judging concurrency against a number the firmware ignores.
        self.assertIn(f">= {soak.SSE_CLIENT_CAP_MACRO}", API_EVENTS_CPP)
        self.assertIn("webEventStreamClientCount()", API_EVENTS_CPP)

    def test_the_cap_decides_which_runs_carry_a_verdict(self):
        cap = soak.resolve_sse_client_cap(ARTOO.build_env)
        self.assertGreaterEqual(cap.value, 1)
        report = cap.report()
        self.assertEqual(report["clients"], cap.value)
        self.assertEqual(report["macro"], soak.SSE_CLIENT_CAP_MACRO)
        self.assertEqual(report["readFrom"], cap.source)
        self.assertIn("observation", report["note"])

    def test_num_clients_defaults_to_the_cap_rather_than_to_a_literal(self):
        # The default is resolved in run(), not baked into the parser: at parse
        # time --build-env has not selected an environment yet, and a literal
        # default is exactly the rot this derivation removes.
        self.assertIsNone(soak.build_parser().parse_args([]).num_clients)


class TheArtefactBelongsToTheInstrumentNotToATicket(unittest.TestCase):
    """`tools/soak.py` outlives the epic that produced it (ADR 0035).

    Its JSON keys are contract, so removing one is a version bump rather than a
    tidy-up -- and `schemaVersion` is what a consumer switches on to know which
    shape it is holding.
    """

    def test_the_report_carries_a_schema_version_and_no_ticket_number(self):
        # Asserted on a real report rather than on the source text: the report
        # is the artefact a consumer holds. Driven through run()'s
        # unresolvable-floor path, which composes the whole header and returns
        # before any request goes out, so this costs no fixture and no network.
        args = soak.build_parser().parse_args([
            "--device", "127.0.0.1", "--port", "1", "--image", "artoo",
            "--driver", "sse_soak", "--build-env", "native",
        ])
        report, exit_code = soak.run(args)
        self.assertEqual(exit_code, 3)
        self.assertEqual(report["schemaVersion"], soak.REPORT_SCHEMA_VERSION)
        self.assertEqual(soak.REPORT_SCHEMA_VERSION, 4)
        self.assertNotIn("issue", report,
                         "the artefact does not belong to one ticket")

    def test_the_version_history_names_every_version_it_claims(self):
        # A bare number is not worth having; the constant is documented with
        # what each version meant, so a consumer holding an old artefact can
        # tell what changed under it.
        source = SOAK_PY.read_text(encoding="utf-8")
        for version in range(1, soak.REPORT_SCHEMA_VERSION + 1):
            self.assertRegex(
                source, r"(?m)^#\s+%d\s+\S" % version,
                f"schemaVersion {version} is claimed but not described",
            )


class AdmissionFloorResolutionOrderMatchesTheFirmware(unittest.TestCase):
    """The harness applies the firmware's own precedence, read from it."""

    def test_the_override_displaces_the_ordinary_floor_and_only_when_non_zero(self):
        self.assertIn(
            "in.minLargestFreeBlockOverride != 0 ? in.minLargestFreeBlockOverride",
            WEB_ADMISSION_CPP,
            "webRequestAdmissionDecide() no longer resolves the override the way "
            "resolve_admission_floor() reproduces it",
        )
        self.assertIn(
            "in.diagnostic ? in.minLargestFreeBlockDiagnostic : ordinaryFloor",
            WEB_ADMISSION_CPP,
            "diagnostics no longer keep their own floor; the harness reports two levels "
            "that would then be one",
        )

    def test_the_device_hookup_still_passes_the_macros_the_harness_reads(self):
        for macro in (soak.ADMISSION_FLOOR_MACRO, soak.ADMISSION_FLOOR_DIAG_MACRO,
                      soak.ADMISSION_FLOOR_OVERRIDE_MACRO):
            with self.subTest(macro=macro):
                self.assertIn(macro, WEB_ADMISSION_PSYCHIC_CPP,
                              f"{macro} is no longer read by the request-admission hookup")

    def test_status_and_events_are_the_diagnostic_class_the_lower_floor_covers(self):
        # Why a soak can watch a controller that is already shedding page loads:
        # its own two paths keep the lower floor. If either stopped being
        # diagnostic, the harness would start losing the device exactly when
        # the evidence mattered, and the floor note would be wrong.
        self.assertIn('strcmp(path, "/api/status") == 0', WEB_ADMISSION_CPP)
        self.assertIn('strcmp(path, "/api/events") == 0', WEB_ADMISSION_CPP)


class AdmissionCountersMatchTheFirmware(unittest.TestCase):
    def test_every_counter_the_product_schemas_read_is_still_emitted(self):
        for schema in (SHIPPING, ARTOO):
            for field in (schema.refused_heap_floor_field,
                          schema.refused_heap_floor_diag_field,
                          schema.accept_min_largest_block_field,
                          schema.heap_free_field, schema.heap_min_field):
                with self.subTest(image=schema.name, field=field):
                    self.assertIn(f'\\"{field}\\":', WEB_SERVER_CPP,
                                  f"{field!r} is no longer emitted by buildStatusJson()")

    def test_the_never_sampled_sentinel_is_the_one_the_firmware_publishes(self):
        # g_webAcceptMinLargestBlockSeen starts at UINT32_MAX and is published
        # as -1 until the guard has sampled once. Reading that as a byte count
        # would report the deepest possible breach on an idle controller.
        self.assertIn("g_webAcceptMinLargestBlockSeen == UINT32_MAX", WEB_SERVER_CPP)
        self.assertIn("? -1L", WEB_SERVER_CPP)
        self.assertEqual(soak.ACCEPT_MIN_LARGEST_BLOCK_NEVER_SAMPLED, -1)

    def test_a_never_sampled_reading_is_none_and_never_zero(self):
        body = artoo_status_body(acceptMinLargestBlockSeen=-1)
        reading = ARTOO.admission(body, "t")
        self.assertIsNone(reading.accept_min_largest_block_seen)
        real = ARTOO.admission(artoo_status_body(acceptMinLargestBlockSeen=11764), "t")
        self.assertEqual(real.accept_min_largest_block_seen, 11764)

    def test_the_counters_are_structural_markers_so_a_wrong_image_fails_preflight(self):
        for field in ("refusedHeapFloor", "refusedHeapFloorDiag", "acceptMinLargestBlockSeen"):
            with self.subTest(field=field):
                body = artoo_status_body()
                body.pop(field)
                mismatches = ARTOO.structural_mismatches(body)
                self.assertTrue(any(field in m for m in mismatches), mismatches)

    def test_recorded_only_heap_fields_are_not_structural_markers(self):
        # heapFree and heapMin are recorded, not judged: losing one degrades the
        # evidence without invalidating the verdict, so it surfaces as a series
        # anomaly rather than refusing the whole run at preflight.
        for field in ("heapFree", "heapMin"):
            with self.subTest(field=field):
                body = artoo_status_body()
                body.pop(field)
                self.assertEqual(ARTOO.structural_mismatches(body), [])


class BenchCompilesNoAdmissionGuard(unittest.TestCase):
    """The bench image's floor is inapplicable, not unresolvable.

    [env:firebeetle2_hosted_bench] does inherit the floor flags -- it extends
    [env:firebeetle2] -- but compiles none of src/, so nothing in the binary
    reads them. Reporting 9000 for that board would describe a gate the image
    does not contain.
    """

    def test_the_bench_env_compiles_none_of_src(self):
        section = pio_section("env:firebeetle2_hosted_bench")
        self.assertIn("-<*>", section)
        self.assertIn("+<../bringup/p4_hosted_bench.cpp>", section)
        self.assertNotIn("+<*>", section)

    def test_the_bench_env_would_otherwise_resolve_a_floor(self):
        # Stated as a fact rather than assumed: the reason the bench has no
        # floor is the source filter, not a missing flag, and a future reader
        # must not "fix" it by adding one to the env.
        self.assertEqual(soak.resolve_admission_floor("firebeetle2_hosted_bench").ordinary_bytes,
                         soak.resolve_admission_floor("firebeetle2").ordinary_bytes)
        self.assertFalse(BENCH.enforces_admission_floor)

    def test_the_bench_handler_publishes_none_of_the_refusal_counters(self):
        for field in ("refusedHeapFloor", "refusedHeapFloorDiag", "acceptMinLargestBlockSeen"):
            with self.subTest(field=field):
                self.assertNotIn(field, BENCH_CPP)

    def test_the_absence_reads_as_absent_never_as_zeroed_counters(self):
        self.assertIsNone(BENCH.admission(bench_status_body(), "t"))
        anomalies = []
        self.assertIsNone(BENCH.collect_admission([bench_status_body()], anomalies))
        self.assertEqual(anomalies, [])
        published = BENCH.fields_read()["admissionRefusals"]
        self.assertEqual(published, BENCH.admission_absence_note)
        self.assertIn("build_src_filter", published)

    def test_the_driver_refuses_a_floor_that_describes_a_gate_the_image_lacks(self):
        # A floor handed to an image with no guard, and an image with a guard
        # left without a floor, are both harness bugs. Neither is a device
        # contract violation, so both raise past _run_driver_safely() rather
        # than being absorbed into an INVALID nobody investigates.
        floor = soak.resolve_admission_floor("firebeetle2")
        with self.assertRaises(ValueError):
            soak.run_sse_soak(
                client=None, schema=BENCH, num_clients=1, duration_s=0.0,
                status_poll_interval_s=1.0, admission_floor=floor,
                sse_client_cap=soak.resolve_sse_client_cap(BENCH.build_env),
                early_stall_check_s=0.0, max_silence_s=5.0,
            )
        with self.assertRaises(ValueError):
            soak.run_sse_soak(
                client=None, schema=ARTOO, num_clients=1, duration_s=0.0,
                status_poll_interval_s=1.0, admission_floor=None,
                sse_client_cap=soak.resolve_sse_client_cap(ARTOO.build_env),
                early_stall_check_s=0.0, max_silence_s=5.0,
            )


class EachImageNamesItsBuildEnvironment(unittest.TestCase):
    def test_every_schema_names_an_environment_platformio_ini_declares(self):
        for name, schema in soak.SCHEMAS.items():
            with self.subTest(image=name):
                self.assertTrue(schema.build_env, f"{name} names no build environment")
                soak.require_declared_environment(schema.build_env)

    def test_an_image_that_judges_a_floor_can_resolve_one(self):
        # The coupling run_sse_soak() raises on, pinned here rather than left
        # to that call site: a product schema whose env resolved nothing would
        # make every run of that image INVALID.
        for name, schema in soak.SCHEMAS.items():
            with self.subTest(image=name):
                if schema.enforces_admission_floor:
                    self.assertGreater(
                        soak.resolve_admission_floor(schema.build_env).ordinary_bytes, 0)
                else:
                    self.assertTrue(schema.admission_absence_note)


class HeapSeriesRows(unittest.TestCase):
    """The per-poll record. #194's graded run could say the largest free block
    reached 11 764 and could not say whether it touched that once or sat near
    it for twenty minutes -- different findings, and the shape is what tells
    them apart."""

    def poll(self, elapsed_s, body):
        return soak.StatusPollSample(elapsed_s=elapsed_s, body=body)

    def test_a_product_row_carries_all_four_readings_plus_its_timestamp(self):
        anomalies = []
        rows = ARTOO.collect_heap_series([self.poll(1.25, artoo_status_body())], anomalies)
        self.assertEqual(anomalies, [])
        self.assertEqual(rows, [{
            soak.SERIES_KEY_ELAPSED_S: 1.25,
            soak.SERIES_KEY_LARGEST_FREE_8BIT: 123456,
            soak.SERIES_KEY_HEAP_FREE: 260000,
            soak.SERIES_KEY_HEAP_MIN: 240000,
            soak.SERIES_KEY_SSE_CLIENTS: 1,
        }])

    def test_a_field_the_image_does_not_publish_is_absent_and_not_an_anomaly(self):
        anomalies = []
        rows = BENCH.collect_heap_series([self.poll(0.5, bench_status_body())], anomalies)
        self.assertEqual(anomalies, [])
        self.assertNotIn(soak.SERIES_KEY_HEAP_MIN, rows[0])
        self.assertEqual(rows[0][soak.SERIES_KEY_HEAP_FREE], 260000)
        self.assertIsNone(BENCH.heap_min_field)

    def test_a_field_the_image_does_publish_but_this_sample_got_wrong_is_an_anomaly(self):
        anomalies = []
        body = artoo_status_body(heapFree=True)  # bool is a subclass of int
        body.pop("heapMin")
        rows = ARTOO.collect_heap_series([self.poll(0.0, body)], anomalies)
        self.assertEqual(len(anomalies), 2, anomalies)
        self.assertNotIn(soak.SERIES_KEY_HEAP_FREE, rows[0])
        self.assertNotIn(soak.SERIES_KEY_HEAP_MIN, rows[0])
        # The readings that were fine are still recorded: one bad field does
        # not cost the row.
        self.assertEqual(rows[0][soak.SERIES_KEY_LARGEST_FREE_8BIT], 123456)

    def test_the_aggregates_skip_a_hole_rather_than_defaulting_it_to_zero(self):
        rows = [
            {soak.SERIES_KEY_LARGEST_FREE_8BIT: 40948},
            {},  # the sample whose reading was malformed
            {soak.SERIES_KEY_LARGEST_FREE_8BIT: 11764},
        ]
        values = soak.series_values(rows, soak.SERIES_KEY_LARGEST_FREE_8BIT)
        self.assertEqual(values, [40948, 11764])
        self.assertEqual(min(values), 11764,
                         "a zero substituted for a missing reading would drag the minimum "
                         "straight through any floor")


if __name__ == "__main__":
    unittest.main()
