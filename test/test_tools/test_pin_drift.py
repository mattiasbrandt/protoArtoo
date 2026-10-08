"""Unit coverage for tools/check_pin_drift.py (#367).

The fixtures are synthetic on purpose: two made-up boards, made-up PIN_FIXTURE_*
names and GPIO numbers no supported chip routes. Copying rows out of the real
include/config.h or docs/pin_map.md would write their pin numbers down a third
time, which is the thing the checker exists to stop.

The `RealTree` case keeps the slice gate covering the repository: the gate runs
this directory and does not run `make check-pin-drift`.
"""

import hashlib
import io
import sys
import tempfile
import unittest
from contextlib import redirect_stderr, redirect_stdout
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "tools"))

import check_pin_drift as drift  # noqa: E402

BOARD_A = "PA_BOARD_FIXTURE_A"
BOARD_B = "PA_BOARD_FIXTURE_B"
SECTIONS = {"Fixture A wiring": BOARD_A, "Fixture B wiring": BOARD_B}

# Board A ranks the document first, board B ranks config.h first: the
# per-board inversion the real contract carries, in miniature.
CONTRACT = """## Source of Truth Contract

**For fixture A (`PA_BOARD_FIXTURE_A`):**
1. `docs/pin_map.md` (authoritative: fixture A was traced)
2. `include/config.h` (must match this document)

**For fixture B (`PA_BOARD_FIXTURE_B`):**
1. `include/config.h` (fixture B was allocated in code)
2. `docs/pin_map.md` (derived from the above)
"""


def config_h(a_tx="91", b_tx="93", extra_a="") -> str:
    return f"""#if PA_BOARD == PA_BOARD_FIXTURE_A
constexpr uint8_t PIN_FIXTURE_TX = {a_tx};
constexpr uint8_t PIN_FIXTURE_RX = 92;
constexpr uint8_t PIN_FIXTURE_ALIAS = PIN_FIXTURE_RX;
{extra_a}
#elif PA_BOARD == PA_BOARD_FIXTURE_B
constexpr uint8_t PIN_FIXTURE_TX = {b_tx};
constexpr uint8_t PIN_FIXTURE_RX = 94;
#else
#error
#endif
"""


def pin_map(a_rows=None, b_rows=None) -> str:
    a_rows = a_rows if a_rows is not None else ["| 91 | `PIN_FIXTURE_TX` |", "| 92 | `PIN_FIXTURE_RX` |"]
    b_rows = b_rows if b_rows is not None else ["| 93 | `PIN_FIXTURE_TX` |", "| 94 | `PIN_FIXTURE_RX` |"]
    table = "| GPIO | Signal |\n|------|--------|\n"
    return (
        f"# Fixture\n\n{CONTRACT}\n"
        f"## Fixture A wiring\n\n{table}" + "\n".join(a_rows) + "\n\n"
        f"## Fixture B wiring\n\n{table}" + "\n".join(b_rows) + "\n"
    )


LANES = "PA_BOARD_LANE(fixture, UART_PORT_FIXTURE, PIN_FIXTURE_TX, PIN_FIXTURE_RX)\n"


class Fixture:
    def __init__(self, tmp: str, config: str, doc: str, lanes: str = LANES):
        self.config = Path(tmp) / "config.h"
        self.doc = Path(tmp) / "pin_map.md"
        self.lanes = Path(tmp) / "board_lanes.inc"
        self.config.write_text(config, encoding="utf-8")
        self.doc.write_text(doc, encoding="utf-8")
        self.lanes.write_text(lanes, encoding="utf-8")

    def check(self, row_names=None) -> drift.Report:
        return drift.check(self.config, self.doc, self.lanes, section_boards=SECTIONS,
                           row_names=row_names or {})

    def main(self) -> tuple[int, str, str]:
        out, err = io.StringIO(), io.StringIO()
        with redirect_stdout(out), redirect_stderr(err):
            code = drift.main(config=self.config, doc=self.doc, lanes_path=self.lanes,
                              section_boards=SECTIONS, row_names={})
        return code, out.getvalue(), err.getvalue()

    def digest(self) -> str:
        return hashlib.sha256(
            b"".join(p.read_bytes() for p in (self.config, self.doc, self.lanes))
        ).hexdigest()


class PinDrift(unittest.TestCase):
    def test_agreeing_files_pass_and_say_what_they_compared(self):
        with tempfile.TemporaryDirectory() as tmp:
            code, out, err = Fixture(tmp, config_h(), pin_map()).main()
        self.assertEqual((0, ""), (code, err))
        self.assertIn("fixture_a: 2 pins, 1 lanes", out)

    def test_a_mismatch_fails_every_run_and_rewrites_nothing(self):
        with tempfile.TemporaryDirectory() as tmp:
            fixture = Fixture(tmp, config_h(a_tx="99"), pin_map())
            before = fixture.digest()
            first = fixture.main()
            second = fixture.main()
            after = fixture.digest()
        self.assertEqual(1, first[0])
        self.assertEqual(first, second)
        self.assertEqual(before, after)
        self.assertIn("PIN_FIXTURE_TX", first[2])
        self.assertIn("says GPIO 99", first[2])
        self.assertIn("says GPIO 91", first[2])

    def test_the_next_move_follows_each_boards_contract(self):
        # The same one-pin mismatch on both boards: A's document wins, B's
        # config.h wins, and each finding sends the reader to the other file.
        with tempfile.TemporaryDirectory() as tmp:
            errors = Fixture(tmp, config_h(a_tx="99", b_tx="98"), pin_map()).check().errors
        a = next(e for e in errors if e.startswith("fixture_a "))
        b = next(e for e in errors if e.startswith("fixture_b "))
        self.assertIn("correct include/config.h to match docs/pin_map.md", a)
        self.assertIn("correct docs/pin_map.md to match include/config.h", b)

    def test_an_aux_led_slot_index_is_not_read_as_a_pin(self):
        # robotState.auxLed.pin is an AUX slot 0..3, not a GPIO. Were these
        # read as pins, each would be reported as missing from the document.
        extra = "constexpr uint8_t AUX_LED_PIN_AUX1 = 1;\nconstexpr uint8_t AUX_LED_PIN_MAX = AUX_LED_PIN_AUX1;"
        with tempfile.TemporaryDirectory() as tmp:
            report = Fixture(tmp, config_h(extra_a=extra), pin_map()).check()
        self.assertEqual([], report.errors)

    def test_a_board_with_no_lanes_is_not_a_pass(self):
        with tempfile.TemporaryDirectory() as tmp:
            errors = Fixture(tmp, config_h(), pin_map(), lanes="// no lanes yet\n").check().errors
        self.assertTrue(any("fixture_b: no lanes reported for this board" in e for e in errors), errors)

    def test_a_board_whose_document_rows_are_gone_is_not_a_pass(self):
        with tempfile.TemporaryDirectory() as tmp:
            errors = Fixture(tmp, config_h(), pin_map(b_rows=[])).check().errors
        self.assertTrue(any("fixture_b: no pins compared" in e for e in errors), errors)

    def test_a_pin_missing_from_either_side_is_reported(self):
        with tempfile.TemporaryDirectory() as tmp:
            errors = Fixture(tmp, config_h(), pin_map(
                a_rows=["| 91 | `PIN_FIXTURE_TX` |", "| 95 | an unmapped row |"],
            )).check().errors
        self.assertTrue(any("PIN_FIXTURE_RX" in e and "no row of" in e for e in errors), errors)
        self.assertTrue(any("'an unmapped row'" in e and "cannot join" in e for e in errors), errors)

    def test_row_names_join_a_row_without_a_pin_name(self):
        rows = {BOARD_A: {"S9 TX": "PIN_FIXTURE_TX", "S9 RX": "PIN_FIXTURE_ALIAS", "S8": None}}
        doc = pin_map(a_rows=["| 91 | S9 TX |", "| 92 | S9 RX |", "| 96 | S8 |"])
        with tempfile.TemporaryDirectory() as tmp:
            self.assertEqual([], Fixture(tmp, config_h(), doc).check(row_names=rows).errors)


class RealTree(unittest.TestCase):
    def test_config_h_and_pin_map_agree(self):
        report = drift.check()
        self.assertEqual([], report.errors)
        self.assertEqual({drift.ARTOO, drift.FIREBEETLE}, set(report.compared))


if __name__ == "__main__":
    unittest.main()
