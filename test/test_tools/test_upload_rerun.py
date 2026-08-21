"""Pin the decision logic in tools/upload_rerun.py (issue #80).

The harness drives a live controller, so most of it cannot be tested off the
device. Its *decisions* can, and they are the part worth pinning: each one
answered a question that decided whether a hardware run counted, and two of them
were wrong the first time in ways that would have reported a passing run as a
failure, or measured the wrong firmware without noticing.
"""

import importlib.util
import sys
import tempfile
import unittest
from pathlib import Path

TOOLS_DIR = Path(__file__).parents[2] / "tools"
MODULE_PATH = TOOLS_DIR / "upload_rerun.py"
sys.path.insert(0, str(TOOLS_DIR))
SPEC = importlib.util.spec_from_file_location("upload_rerun", MODULE_PATH)
RERUN = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = RERUN
SPEC.loader.exec_module(RERUN)


class FakeClock:
    """Wall clock the test advances explicitly; sleep just moves it forward."""

    def __init__(self, start: float = 1000.0) -> None:
        self.now = start

    def monotonic(self) -> float:
        return self.now

    def sleep(self, seconds: float) -> None:
        self.now += seconds


class ProbeClassificationTest(unittest.TestCase):
    """The oversize probe's answer decides whether a round-trip is worth running."""

    def classify(self, status, body):
        return RERUN.classify_probe({"httpStatus": status, "body": body})[0]

    def test_oversize_rejection_proves_a_part_callback_ran(self):
        # The guard that produces this lives in the chunk handler at index == 0,
        # so reaching it at all means the multipart parser delivered a chunk.
        self.assertEqual(
            "parser-emits-parts",
            self.classify(
                413,
                {"ok": False, "error": "firmware image is larger than the app partition"},
            ),
        )

    def test_unreadable_body_is_recognised_as_the_issue_96_failure(self):
        self.assertEqual(
            "issue96-regressed",
            self.classify(
                503,
                {
                    "ok": False,
                    "error": "the controller could not read the upload body; no image "
                    "data reached the updater. retry",
                },
            ),
        )

    def test_no_image_is_not_confused_with_an_unreadable_body(self):
        # Before 047da8a these were the same answer. Keeping them apart is the
        # whole reason #96 was diagnosable at all.
        self.assertEqual(
            "empty-post-reported",
            self.classify(400, {"ok": False, "error": "no image received"}),
        )

    def test_a_non_json_answer_is_reported_as_a_broken_contract(self):
        # PsychicHttp's own error paths send text/html, which data/firmware.js
        # cannot read a message out of.
        self.assertEqual("non-json-answer", self.classify(400, None))

    def test_an_unknown_outcome_is_inconclusive_rather_than_a_pass(self):
        self.assertEqual(
            "unexpected", self.classify(500, {"ok": False, "error": "update failed"})
        )


class RebootDetectionTest(unittest.TestCase):
    """A reboot is uptime falling behind wall clock, not uptime decreasing."""

    def wait(self, uptime_before, uptime_readings, upload_seconds):
        clock = FakeClock()
        original_time = RERUN.time
        RERUN.time = clock
        reference = clock.monotonic()
        clock.now += upload_seconds
        readings = iter(uptime_readings)
        original_read = RERUN.read_status
        RERUN.read_status = lambda host, port: {
            "uptimeMs": next(readings),
            "firmwareVersion": "v",
        }
        try:
            return RERUN.wait_for_reboot("h", 80, uptime_before, reference, "v")
        finally:
            RERUN.time = original_time
            RERUN.read_status = original_read

    def test_a_reboot_is_caught_even_when_uptime_reads_higher_than_before(self):
        # The regression this exists for. Round 2 of the #80 rerun began at
        # uptime 2667 ms; had its post-reboot poll landed a few hundred ms later
        # it would have read *above* 2667 and the old gate would have called a
        # genuine reboot a failed one.
        result = self.wait(2667, [3200], upload_seconds=13.5)
        self.assertTrue(result["rebooted"])
        self.assertEqual(3200, result["uptimeAfterMs"])
        self.assertGreater(result["projectedUptimeWithoutRebootMs"], 15000)

    def test_a_reboot_from_a_long_running_device_is_caught(self):
        result = self.wait(311828, [2443], upload_seconds=12.2)
        self.assertTrue(result["rebooted"])

    def test_uptime_tracking_wall_clock_is_not_a_reboot(self):
        # The controller never went down: uptime advances by at least the time
        # that passed, so nothing here may read as a reboot.
        readings = [int(2667 + (13.5 + 2 * i) * 1000) for i in range(1, 80)]
        result = self.wait(2667, readings, upload_seconds=13.5)
        self.assertFalse(result["rebooted"])


class ContentionDetectionTest(unittest.TestCase):
    """A measurement taken while another session drives the board is not evidence."""

    def detect(self, served_readings, socket_readings=None):
        if socket_readings is None:
            socket_readings = served_readings
        clock = FakeClock()
        served = iter(served_readings)
        sockets = iter(socket_readings)
        original_time, original_read = RERUN.time, RERUN.read_status
        RERUN.time = clock
        RERUN.read_status = lambda host, port: {
            "httpRequestsServed": next(served),
            "httpSocketsAccepted": next(sockets),
        }
        try:
            return RERUN.detect_contention("h", 80)
        finally:
            RERUN.time, RERUN.read_status = original_time, original_read

    def test_an_idle_board_counts_only_our_own_reads(self):
        # Each sample is itself one served request, so consecutive reads on an
        # idle controller differ by exactly one.
        result = self.detect([100, 101, 102])
        self.assertTrue(result["checked"])
        self.assertEqual(0, result["foreignRequests"])
        self.assertFalse(result["busy"])

    def test_foreign_traffic_is_reported_as_busy(self):
        result = self.detect([100, 110, 122])
        self.assertEqual(20, result["foreignRequests"])
        self.assertTrue(result["busy"])

    def test_keep_alive_sharing_one_socket_is_not_mistaken_for_traffic(self):
        # Our three samples may ride one connection, so the socket counter
        # barely moves. That must not read as negative foreign activity.
        result = self.detect([100, 101, 102], socket_readings=[50, 50, 50])
        self.assertEqual(0, result["foreignSocketsAtLeast"])
        self.assertFalse(result["busy"])

    def test_an_unreadable_counter_is_unknown_rather_than_idle(self):
        clock = FakeClock()
        original_time, original_read = RERUN.time, RERUN.read_status
        RERUN.time = clock
        RERUN.read_status = lambda host, port: {"uptimeMs": 5}
        try:
            result = RERUN.detect_contention("h", 80)
        finally:
            RERUN.time, RERUN.read_status = original_time, original_read
        self.assertFalse(result["checked"])
        self.assertNotIn("busy", result)


class ImageIdentityTest(unittest.TestCase):
    """An image from another build directory must not be flashed silently."""

    def test_an_image_carrying_the_stamp_is_accepted(self):
        with tempfile.TemporaryDirectory() as tmp:
            image = Path(tmp) / "firmware.bin"
            image.write_bytes(b"\x00\xff" * 64 + b"v1.0.0-243-gabc123" + b"\x00" * 64)
            self.assertTrue(RERUN.image_carries_version(image, "v1.0.0-243-gabc123"))

    def test_a_stale_image_is_rejected(self):
        # pio run -e protoArtoo and make ota write to different directories, so
        # the default path can hold a build from before the last merge.
        with tempfile.TemporaryDirectory() as tmp:
            image = Path(tmp) / "firmware.bin"
            image.write_bytes(b"\x00\xff" * 64 + b"v1.0.0-198-gold999" + b"\x00" * 64)
            self.assertFalse(RERUN.image_carries_version(image, "v1.0.0-243-gabc123"))


class CounterDeltaTest(unittest.TestCase):
    def test_only_numeric_counters_produce_deltas(self):
        before = {
            "httpRequestsServed": 10,
            "estop": False,
            "firmwareVersion": "a",
            "heapFree": 100,
        }
        after = {
            "httpRequestsServed": 14,
            "estop": True,
            "firmwareVersion": "b",
            "heapFree": 90,
        }
        deltas = RERUN.counter_deltas(before, after)
        # Booleans are ints in Python; letting them through would report an
        # estop latching as "estop: 1".
        self.assertEqual({"httpRequestsServed": 4, "heapFree": -10}, deltas)


class MultipartFramingTest(unittest.TestCase):
    def test_framing_stays_well_inside_the_overhead_allowance(self):
        # littlefs.bin is exactly the 655,360-byte partition, so the guard
        # compares Content-Length against partition + 4096. Framing that grew
        # past the allowance would reject the project's own filesystem image.
        preamble, epilogue = RERUN.multipart_frame("filesystem", "littlefs.bin")
        self.assertLess(len(preamble) + len(epilogue), 4096)

    def test_the_payload_generator_yields_exactly_the_requested_total(self):
        total = sum(len(chunk) for chunk in RERUN.zero_chunks(RERUN.PROBE_BODY_BYTES))
        self.assertEqual(RERUN.PROBE_BODY_BYTES, total)


if __name__ == "__main__":
    unittest.main()
