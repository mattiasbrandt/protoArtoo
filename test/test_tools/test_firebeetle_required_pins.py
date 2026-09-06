"""Compiler proof for the FireBeetle required-pin guard inventory.

The expected symbols and diagnostics below are deliberately independent of the
production X-macro list.  That keeps a deleted production row from silently
reducing the number of probes this test executes.
"""

import os
import re
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[2]
INCLUDE_DIR = REPO_ROOT / "include"
INVENTORY = INCLUDE_DIR / "firebeetle_required_pins.inc"
CONFIG = INCLUDE_DIR / "config.h"

EXPECTED_PINS = {
    # Newly assigned in Slice 2 (#190) — 14 pins
    "PIN_SBUS1_RX": "firebeetle2: PIN_SBUS1_RX is unassigned - see #190",
    "PIN_SBUS2_RX": "firebeetle2: PIN_SBUS2_RX is unassigned - see #190",
    "PIN_RC_CH3": "firebeetle2: PIN_RC_CH3 is unassigned - see #190",
    "PIN_RC_CH4": "firebeetle2: PIN_RC_CH4 is unassigned - see #190",
    "PIN_RC_CH5": "firebeetle2: PIN_RC_CH5 is unassigned - see #190",
    "PIN_RC_CH6": "firebeetle2: PIN_RC_CH6 is unassigned - see #190",
    "PIN_AUDIO_TX": "firebeetle2: PIN_AUDIO_TX is unassigned - see #190",
    "PIN_AUDIO_RX": "firebeetle2: PIN_AUDIO_RX is unassigned - see #190",
    "PIN_ARM1_SERVO": "firebeetle2: PIN_ARM1_SERVO is unassigned - see #190",
    "PIN_ARM2_SERVO": "firebeetle2: PIN_ARM2_SERVO is unassigned - see #190",
    "PIN_ARM3_SERVO": "firebeetle2: PIN_ARM3_SERVO is unassigned - see #190",
    "PIN_ARM4_SERVO": "firebeetle2: PIN_ARM4_SERVO is unassigned - see #190",
    "PIN_ARM5_SERVO": "firebeetle2: PIN_ARM5_SERVO is unassigned - see #190",
    "PIN_DOME_ESC": "firebeetle2: PIN_DOME_ESC is unassigned - see #190",
    # Already assigned in prior slices — 6 pins, included for coherence verification
    "PIN_I2C_SCL": "firebeetle2: PIN_I2C_SCL is unassigned - see spec sheet §Exposed GPIO table",
    "PIN_I2C_SDA": "firebeetle2: PIN_I2C_SDA is unassigned - see spec sheet §Exposed GPIO table",
    "PIN_DRIVE_TX": "firebeetle2: PIN_DRIVE_TX is unassigned - see spec sheet §Recommended allocation",
    "PIN_DRIVE_RX": "firebeetle2: PIN_DRIVE_RX is unassigned - see spec sheet §Recommended allocation",
    "PIN_DOME_TX": "firebeetle2: PIN_DOME_TX is unassigned - see spec sheet §Recommended allocation",
    "PIN_DOME_RX": "firebeetle2: PIN_DOME_RX is unassigned - see spec sheet §Recommended allocation",
}

ROW_RE = re.compile(
    r'^PA_FIREBEETLE_REQUIRED_PIN\(\s*([A-Z0-9_]+)\s*,\s*"([^"]+)"\s*\)\s*$',
    re.MULTILINE,
)


class FireBeetleRequiredPinGuards(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        compiler_name = os.environ.get("CXX", "c++")
        cls.compiler = shutil.which(compiler_name)
        if cls.compiler is None:
            raise RuntimeError(f"C++ compiler not found: {compiler_name}")

    def _inventory_rows(self):
        return ROW_RE.findall(INVENTORY.read_text(encoding="utf-8"))

    def _compile_source(self, source):
        with tempfile.TemporaryDirectory() as tmp:
            source_path = Path(tmp) / "required_pin_probe.cpp"
            object_path = Path(tmp) / "required_pin_probe.o"
            source_path.write_text(source, encoding="utf-8")
            return subprocess.run(
                [
                    self.compiler,
                    "-std=c++17",
                    "-I",
                    str(INCLUDE_DIR),
                    "-c",
                    str(source_path),
                    "-o",
                    str(object_path),
                ],
                capture_output=True,
                text=True,
                check=False,
            )

    def _compile_probe(self, unassigned_pin=None):
        declarations = []
        for index, pin in enumerate(EXPECTED_PINS, start=1):
            value = "PA_PIN_UNASSIGNED" if pin == unassigned_pin else str(index)
            declarations.append(f"constexpr uint8_t {pin} = {value};")

        return self._compile_source(
            "\n".join(
                [
                    "#include <cstdint>",
                    "constexpr uint8_t PA_PIN_UNASSIGNED = 0xFF;",
                    *declarations,
                    "#define PA_FIREBEETLE_REQUIRED_PIN(pin, diagnostic) \\",
                    "    static_assert(pin != PA_PIN_UNASSIGNED, diagnostic);",
                    '#include "firebeetle_required_pins.inc"',
                    "#undef PA_FIREBEETLE_REQUIRED_PIN",
                    "int main() { return 0; }",
                    "",
                ]
            )
        )

    def test_inventory_matches_independent_expected_contract(self):
        rows = self._inventory_rows()
        self.assertEqual(len(rows), 20)  # 14 required (Slice 2) + 6 already-assigned
        self.assertEqual(dict(rows), EXPECTED_PINS)

    def test_all_assigned_probe_compiles(self):
        result = self._compile_probe()
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_actual_firebeetle_config_expands_all_required_guards(self):
        result = self._compile_source(
            "\n".join(
                [
                    "#define PA_BOARD PA_BOARD_FIREBEETLE2",
                    # config.h requires these per-env and #errors without them
                    # (#244); a probe stands in for a build environment.
                    "#define PA_LOG_LEVEL 2",
                    "#define PA_HEAP_PROFILE 0",
                    '#include "config.h"',
                    "",
                ]
            )
        )
        # As of Slice 2 (#190), all required FireBeetle pins are assigned, so the
        # config compiles successfully. The required-pin guards are satisfied.
        # Duplicate and reserved-pin guards are verified via manual demonstrations
        # in the acceptance criteria (showing compiler diagnostics when deliberately
        # violating the constraints).
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_firebeetle_sbus_guards_alias_rc_channels_one_and_two(self):
        config = CONFIG.read_text(encoding="utf-8")
        marker = "#elif PA_BOARD == PA_BOARD_FIREBEETLE2"
        end_marker = '#else\n  #error "PA_BOARD value not recognized in pin-map selection"'
        self.assertIn(marker, config)
        self.assertIn(end_marker, config)
        firebeetle_block = config.split(marker, 1)[1].split(end_marker, 1)[0]

        expected_aliases = {
            "PIN_SBUS1_RX": "PIN_RC_CH1",
            "PIN_SBUS2_RX": "PIN_RC_CH2",
        }
        for sbus_pin, rc_pin in expected_aliases.items():
            with self.subTest(sbus_pin=sbus_pin):
                declaration = re.compile(
                    rf"^\s*constexpr\s+uint8_t\s+{sbus_pin}\s*=\s*{rc_pin}\s*;",
                    re.MULTILINE,
                )
                self.assertRegex(firebeetle_block, declaration)

    def test_each_unassigned_pin_fails_with_its_own_diagnostic(self):
        for pin, diagnostic in EXPECTED_PINS.items():
            with self.subTest(pin=pin):
                result = self._compile_probe(unassigned_pin=pin)
                self.assertNotEqual(result.returncode, 0, result.stderr)
                self.assertIn(diagnostic, result.stderr)


if __name__ == "__main__":
    unittest.main()
