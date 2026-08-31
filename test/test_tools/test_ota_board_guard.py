"""Pin the wrong-board OTA guard's decision logic (#252 Finding 1).

No device, no network: ``fetch_reported_board``'s only network call
(``urllib.request.urlopen``) is monkeypatched. What is under test is the
guard's *decision* -- match proceeds, mismatch refuses, and every fail-closed
path (unreachable, non-200, malformed JSON, non-object JSON, missing/bad
``board`` field) refuses rather than falling through to a push.
"""

import importlib.util
import json
import sys
import tempfile
import unittest
import urllib.error
from pathlib import Path

TOOLS_DIR = Path(__file__).parents[2] / "tools"
MODULE_PATH = TOOLS_DIR / "ota_board_guard.py"
sys.path.insert(0, str(TOOLS_DIR))
SPEC = importlib.util.spec_from_file_location("ota_board_guard", MODULE_PATH)
GUARD = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = GUARD
SPEC.loader.exec_module(GUARD)


class FakeResponse:
    """Stands in for the object ``urllib.request.urlopen`` returns."""

    def __init__(self, status: int, body: bytes):
        self.status = status
        self._body = body

    def __enter__(self):
        return self

    def __exit__(self, *exc_info):
        return False

    def read(self) -> bytes:
        return self._body


def fake_urlopen_returning(status: int, body: bytes):
    def _fake(request, timeout):
        return FakeResponse(status, body)

    return _fake


def fake_urlopen_raising(exc: Exception):
    def _fake(request, timeout):
        raise exc

    return _fake


class EnvToBoardMappingTest(unittest.TestCase):
    """expected_board_for_env is a positive membership test for P4, never a prefix match."""

    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.budgets_path = Path(self.tmp.name) / "build_budgets.json"
        self.budgets_path.write_text(
            json.dumps(
                {
                    "platforms": {
                        "esp32p4": {"envs": ["firebeetle2_ota", "firebeetle2"]},
                        "esp32": {},
                    }
                }
            ),
            encoding="utf-8",
        )

    def test_an_env_in_the_p4_registry_maps_to_firebeetle2(self):
        self.assertEqual(
            GUARD.BOARD_FIREBEETLE2,
            GUARD.expected_board_for_env("firebeetle2_ota", self.budgets_path),
        )

    def test_an_env_absent_from_the_p4_registry_falls_to_the_artoo_else_branch(self):
        # Not a prefix match: this env shares no prefix with any firebeetle2
        # entry, and still correctly falls to the else-branch.
        self.assertEqual(
            GUARD.BOARD_ARTOO_ESP32,
            GUARD.expected_board_for_env("artoo_esp32_ota", self.budgets_path),
        )

    def test_prefix_matching_would_have_been_wrong_here(self):
        # A prefix-match implementation keyed on "firebeetle2" would also
        # reject this correctly, so it alone would not prove much. This name
        # is deliberately chosen to fail a prefix scheme that checks
        # startswith("artoo_esp32") and treats anything else as P4 by
        # default; membership-against-the-registry must not do that either.
        self.assertEqual(
            GUARD.BOARD_ARTOO_ESP32,
            GUARD.expected_board_for_env("artoo_esp32_chirp_ota", self.budgets_path),
        )

    def test_the_real_registry_classifies_both_boards_this_repo_ships(self):
        # Same file the Makefile reads (P4_ENVS, Makefile:46) -- no stub, but
        # no network either. Pins the actual integration point, not a fixture.
        self.assertEqual(
            GUARD.BOARD_FIREBEETLE2,
            GUARD.expected_board_for_env("firebeetle2_ota"),
        )
        self.assertEqual(
            GUARD.BOARD_ARTOO_ESP32,
            GUARD.expected_board_for_env("artoo_esp32_ota"),
        )


class FetchReportedBoardTest(unittest.TestCase):
    """Every non-answer refuses; only a clean 200 with a string board proceeds."""

    def fetch(self, urlopen):
        original = GUARD.urllib.request.urlopen
        GUARD.urllib.request.urlopen = urlopen
        try:
            return GUARD.fetch_reported_board("10.0.0.51", port=80, timeout_seconds=1.0)
        finally:
            GUARD.urllib.request.urlopen = original

    def test_a_clean_200_with_a_board_field_is_read_through(self):
        body = json.dumps({"droidName": "r2", "board": "firebeetle2"}).encode("utf-8")
        self.assertEqual("firebeetle2", self.fetch(fake_urlopen_returning(200, body)))

    def test_an_unreachable_host_refuses(self):
        with self.assertRaises(GUARD.OtaBoardGuardError):
            self.fetch(fake_urlopen_raising(urllib.error.URLError("connection refused")))

    def test_a_timeout_refuses(self):
        with self.assertRaises(GUARD.OtaBoardGuardError):
            self.fetch(fake_urlopen_raising(TimeoutError("timed out")))

    def test_a_non_200_status_refuses(self):
        with self.assertRaises(GUARD.OtaBoardGuardError):
            self.fetch(fake_urlopen_returning(500, b"{}"))

    def test_a_non_json_body_refuses(self):
        with self.assertRaises(GUARD.OtaBoardGuardError):
            self.fetch(fake_urlopen_returning(200, b"not json"))

    def test_a_json_array_instead_of_an_object_refuses(self):
        with self.assertRaises(GUARD.OtaBoardGuardError):
            self.fetch(fake_urlopen_returning(200, b"[1, 2, 3]"))

    def test_a_missing_board_field_refuses(self):
        body = json.dumps({"droidName": "r2"}).encode("utf-8")
        with self.assertRaises(GUARD.OtaBoardGuardError):
            self.fetch(fake_urlopen_returning(200, body))

    def test_a_non_string_board_field_refuses(self):
        # A response shaped like the contract but with the wrong type must not
        # be coerced into a string and compared anyway.
        body = json.dumps({"board": 123}).encode("utf-8")
        with self.assertRaises(GUARD.OtaBoardGuardError):
            self.fetch(fake_urlopen_returning(200, body))

    def test_an_empty_string_board_field_refuses(self):
        body = json.dumps({"board": ""}).encode("utf-8")
        with self.assertRaises(GUARD.OtaBoardGuardError):
            self.fetch(fake_urlopen_returning(200, body))


class EnforceBoardMatchTest(unittest.TestCase):
    """The composed decision: expected board vs. reported board."""

    def enforce(self, env_name, reported_board, budgets_path):
        original = GUARD.fetch_reported_board
        GUARD.fetch_reported_board = lambda host, port, timeout_seconds: reported_board
        try:
            return GUARD.enforce_board_match(
                env_name, "10.0.0.51", budgets_path=budgets_path
            )
        finally:
            GUARD.fetch_reported_board = original

    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.budgets_path = Path(self.tmp.name) / "build_budgets.json"
        self.budgets_path.write_text(
            json.dumps({"platforms": {"esp32p4": {"envs": ["firebeetle2_ota"]}}}),
            encoding="utf-8",
        )

    def test_a_matching_board_proceeds_and_returns_the_confirmed_board(self):
        result = self.enforce("firebeetle2_ota", "firebeetle2", self.budgets_path)
        self.assertEqual("firebeetle2", result)

    def test_a_mismatched_board_refuses(self):
        with self.assertRaises(GUARD.OtaBoardGuardError):
            self.enforce("firebeetle2_ota", "artoo_esp32", self.budgets_path)

    def test_the_mismatch_message_names_both_boards(self):
        # The pin requires the refusal to name both boards explicitly -- what
        # the env builds for and what actually answered -- not just "mismatch".
        with self.assertRaises(GUARD.OtaBoardGuardError) as ctx:
            self.enforce("firebeetle2_ota", "artoo_esp32", self.budgets_path)
        message = str(ctx.exception)
        self.assertIn("firebeetle2", message)
        self.assertIn("artoo_esp32", message)

    def test_artoo_env_against_a_p4_host_also_refuses(self):
        # The reverse direction of the exact hazard in the issue: an
        # artoo-esp32 env pushed at a host that turns out to be the P4 board.
        with self.assertRaises(GUARD.OtaBoardGuardError):
            self.enforce("artoo_esp32_ota", "firebeetle2", self.budgets_path)


if __name__ == "__main__":
    unittest.main()
