"""Compiler proof for the chip-target-sized shared constants (#256).

Several numbers in shared code were sized against artoo-esp32's heap and flash
and the ESP32-P4 inherited them verbatim. They are now per chip target. Native
Unity tests (test/test_native/) always build PA_BOARD_ARTOO_ESP32 (platformio.ini
env:native), so a single binary can only ever pin one board's values -- and the
defect is by definition a cross-board fact. It is proven the way
test_board_uart_allocation.py proves the UART allocation: compile the real
headers once under each PA_BOARD value and compare what each actually produces.

Three things are asserted, and the third is the one that matters:

1. Each board's values, written here independently of the headers so a
   copy-paste that converged the two boards cannot also update the expectation.
2. That the ESP32-P4 arm satisfies the ceilings its derivations claim.
3. That artoo-esp32 and the ESP32-P4 do NOT converge, and that artoo-esp32's
   values are exactly the pre-#256 numbers. Both boards passing their own
   expectation in isolation would still hold if a later edit moved artoo-esp32
   too, which is the failure this ticket is most exposed to.
"""

import os
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[2]
INCLUDE_DIR = REPO_ROOT / "include"

# Everything the probed headers pull in, staged into the probe's include path.
# config.h drags the two manifest .inc files and the FireBeetle pin inventory.
PROBE_HEADER_SET = (
    "config.h",
    "board_capabilities.inc",
    "build_flags.inc",
    "firebeetle_required_pins.inc",
    "protocol_check.h",
    "seq_store_index.h",
    "seq_store_util.h",
    "sequence_engine.h",
)

# What each board must produce. Order matches the printf in _probe_source().
EXPECTED_BY_BOARD = {
    # Pre-#256 values, unchanged. Both were sized against this board's heap, and
    # both must stay exactly where they are.
    "PA_BOARD_ARTOO_ESP32": {
        "seq_file_max_bytes": 12 * 1024,
        "seq_fs_free_floor": 24 * 1024,
    },
    # Re-derived from the sequence format's own ceiling. See the derivation in
    # include/seq_store_util.h.
    "PA_BOARD_FIREBEETLE2": {
        "seq_file_max_bytes": 24 * 1024,
        "seq_fs_free_floor": 48 * 1024,
    },
}

# The largest JSON the Learned Sequence format can produce: PC_MAX_STEPS (96)
# steps in each of the two branches, every step a dome command at PC_CMD_MAX
# (63). Measured against ArduinoJson 7.4.3 with the format's own serializer
# shape; see the table in include/seq_store_util.h.
FORMAT_MAX_SEQUENCE_JSON_BYTES = 18843


class BoardChipSizedConstants(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        compiler_name = os.environ.get("CXX", "c++")
        cls.compiler = shutil.which(compiler_name)
        if cls.compiler is None:
            raise RuntimeError(f"C++ compiler not found: {compiler_name}")
        cls._cache = {}

    @staticmethod
    def _probe_source(board_macro):
        return "\n".join(
            [
                f"#define PA_BOARD {board_macro}",
                # config.h requires these per-env and #errors without them
                # (#244). A probe stands in for a build environment, so it
                # declares what any environment must.
                "#define PA_LOG_LEVEL 2",
                "#define PA_HEAP_PROFILE 0",
                '#include "seq_store_util.h"',
                "#include <cstdio>",
                "int main() {",
                '    std::printf("%zu %zu\\n",',
                "        (size_t)SEQ_FILE_MAX_BYTES, (size_t)SEQ_FS_FREE_FLOOR);",
                "    return 0;",
                "}",
                "",
            ]
        )

    def _values(self, board_macro):
        cached = self._cache.get(board_macro)
        if cached is not None:
            return cached
        with tempfile.TemporaryDirectory() as tmp:
            staged = Path(tmp) / "include"
            staged.mkdir()
            for name in PROBE_HEADER_SET:
                shutil.copyfile(INCLUDE_DIR / name, staged / name)
            source_path = Path(tmp) / "chip_sized_constants_probe.cpp"
            source_path.write_text(self._probe_source(board_macro), encoding="utf-8")
            output_path = Path(tmp) / "chip_sized_constants_probe"
            compile_result = subprocess.run(
                [self.compiler, "-std=c++17", "-I", str(staged),
                 str(source_path), "-o", str(output_path)],
                capture_output=True, text=True, check=False,
            )
            self.assertEqual(compile_result.returncode, 0, compile_result.stderr)
            run_result = subprocess.run(
                [str(output_path)], capture_output=True, text=True,
                check=False, timeout=10,
            )
            self.assertEqual(run_result.returncode, 0, run_result.stderr)
        fields = [int(v) for v in run_result.stdout.split()]
        self.assertEqual(len(fields), 2, run_result.stdout)
        values = {
            "seq_file_max_bytes": fields[0],
            "seq_fs_free_floor": fields[1],
        }
        self._cache[board_macro] = values
        return values

    # -- 1. each board's declared values -------------------------------------

    def test_artoo_esp32_values_are_unchanged(self):
        self.assertEqual(
            self._values("PA_BOARD_ARTOO_ESP32"),
            EXPECTED_BY_BOARD["PA_BOARD_ARTOO_ESP32"],
        )

    def test_firebeetle2_values_are_the_re_derived_ones(self):
        self.assertEqual(
            self._values("PA_BOARD_FIREBEETLE2"),
            EXPECTED_BY_BOARD["PA_BOARD_FIREBEETLE2"],
        )

    # -- 2. the ESP32-P4 arm satisfies the ceilings it claims -----------------

    def test_p4_file_cap_holds_the_formats_largest_sequence(self):
        v = self._values("PA_BOARD_FIREBEETLE2")
        self.assertGreaterEqual(
            v["seq_file_max_bytes"], FORMAT_MAX_SEQUENCE_JSON_BYTES,
            "the ESP32-P4 cap must reach the format's own 96+96-step ceiling; "
            "not reaching it is the artoo-esp32 constraint this ticket removes",
        )

    def test_artoo_file_cap_is_below_the_formats_largest_sequence(self):
        """The inherited constraint itself, asserted so it is not read as a bug.

        artoo-esp32 genuinely cannot save a 96+96-step sequence: the raw body,
        the parsed document and the SeqStep staging are live at once and do not
        fit its measured free heap. That is a recorded property of the small
        board, and it is why the cap had to become per chip rather than simply
        be raised for everyone.
        """
        v = self._values("PA_BOARD_ARTOO_ESP32")
        self.assertLess(v["seq_file_max_bytes"], FORMAT_MAX_SEQUENCE_JSON_BYTES)

    def test_free_floor_covers_a_second_full_size_file_on_both_boards(self):
        for board in EXPECTED_BY_BOARD:
            with self.subTest(board=board):
                v = self._values(board)
                self.assertGreaterEqual(
                    v["seq_fs_free_floor"], v["seq_file_max_bytes"],
                    "the outgoing copy coexists with .tmp.json until the rename",
                )

    # -- 3. the regression: the two boards must not converge ------------------

    def test_the_two_boards_do_not_converge(self):
        artoo = self._values("PA_BOARD_ARTOO_ESP32")
        firebeetle = self._values("PA_BOARD_FIREBEETLE2")
        self.assertNotEqual(artoo, firebeetle)
        for key in ("seq_file_max_bytes", "seq_fs_free_floor"):
            with self.subTest(key=key):
                self.assertGreater(
                    firebeetle[key], artoo[key],
                    f"{key}: the ESP32-P4 must not inherit artoo-esp32's sizing",
                )

    def test_every_chip_arm_declares_every_constant(self):
        """A third chip target cannot inherit one of these by omission.

        Each per-chip block ends in an #error rather than falling through, so a
        new chip target that forgets one fails at the declaration instead of at
        some unrelated consumer. Assert the #error arm exists rather than
        trusting that it was written.
        """
        for header, needle in (
            ("seq_store_util.h",
             "the Learned Sequence per-file cap has no value for this chip target"),
        ):
            with self.subTest(header=header):
                text = (INCLUDE_DIR / header).read_text(encoding="utf-8")
                self.assertIn(f'#error "{needle}"', text)


if __name__ == "__main__":
    unittest.main()
