"""Compiler proof for the per-Board-Variant UART controller allocation (#254).

Native Unity tests (test/test_native/) always build PA_BOARD_ARTOO_ESP32 (see
platformio.ini env:native build_flags), so a single binary can only ever pin
one board's values. The defect this ticket fixes -- the ESP32-P4 inheriting
artoo-esp32's UART2 share because both boards hardcoded the same controller --
is by definition a cross-board fact, so it is proven the same way
test_board_mdns_host.py proves the mDNS default: compile include/config.h once
under each PA_BOARD value and compare what each actually produces.

Two things are asserted, and the second is the one that matters:

1. Each board's allocation and PA_CAP_DEDICATED_AUDIO_UART value.
2. That config.h's own static_asserts REJECT the incoherent combinations --
   compiled against a patched copy of config.h, so the guard is shown firing
   rather than assumed to. A guard nobody has seen fail is a claim.
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

# Everything include/config.h pulls in. Copied into the probe's include path so
# a negative probe can patch config.h without touching the working tree.
CONFIG_HEADER_SET = (
    "config.h",
    "board_capabilities.inc",
    "build_flags.inc",
    "firebeetle_required_pins.inc",
)

# The allocation each board is expected to declare, independent of config.h so
# a copy-paste that converged the two boards cannot also silently update the
# expectation. Values are (UART_PORT_DRIVE, UART_PORT_DOME, UART_PORT_AUDIO,
# UART_PORT_MAX, PA_CAP_DEDICATED_AUDIO_UART).
EXPECTED_BY_BOARD = {
    # Classic ESP32: SOC_UART_HP_NUM = 3, so UART0 console + UART1 drive leaves
    # one controller for both the dome link and audio RX.
    "PA_BOARD_ARTOO_ESP32": (1, 2, 2, 2, 0),
    # ESP32-P4: SOC_UART_HP_NUM = 5, so audio gets UART3 to itself.
    "PA_BOARD_FIREBEETLE2": (1, 2, 3, 4, 1),
}


class BoardUartAllocation(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        compiler_name = os.environ.get("CXX", "c++")
        cls.compiler = shutil.which(compiler_name)
        if cls.compiler is None:
            raise RuntimeError(f"C++ compiler not found: {compiler_name}")

    @staticmethod
    def _probe_source(board_macro):
        return "\n".join(
            [
                f"#define PA_BOARD {board_macro}",
                # config.h requires these per-env, and #errors without them
                # (#244). A probe stands in for a build environment, so it
                # declares what any environment must.
                "#define PA_LOG_LEVEL 2",
                "#define PA_HEAP_PROFILE 0",
                '#include "config.h"',
                "#include <cstdio>",
                "int main() {",
                '    std::printf("%u %u %u %u %d\\n",',
                "        (unsigned)UART_PORT_DRIVE, (unsigned)UART_PORT_DOME,",
                "        (unsigned)UART_PORT_AUDIO, (unsigned)UART_PORT_MAX,",
                "        (int)PA_CAP_DEDICATED_AUDIO_UART);",
                "    return 0;",
                "}",
                "",
            ]
        )

    def _staged_include_dir(self, tmp, config_patch=None):
        """Copy the config.h header set into tmp, optionally patching config.h.

        config_patch is a (pattern, replacement) pair applied to config.h with
        re.subn; the substitution must match exactly once, so a rewritten
        config.h turns the negative probe into a loud test failure rather than
        a silently vacuous one.
        """
        staged = Path(tmp) / "include"
        staged.mkdir()
        for name in CONFIG_HEADER_SET:
            shutil.copyfile(INCLUDE_DIR / name, staged / name)

        if config_patch is not None:
            pattern, replacement = config_patch
            config_path = staged / "config.h"
            patched, count = re.subn(
                pattern, replacement, config_path.read_text(encoding="utf-8")
            )
            self.assertEqual(
                count,
                1,
                f"probe patch {pattern!r} matched {count} times in config.h, expected 1",
            )
            config_path.write_text(patched, encoding="utf-8")
        return staged

    def _compile(self, board_macro, config_patch=None, link=True):
        with tempfile.TemporaryDirectory() as tmp:
            staged = self._staged_include_dir(tmp, config_patch)
            source_path = Path(tmp) / "uart_allocation_probe.cpp"
            source_path.write_text(self._probe_source(board_macro), encoding="utf-8")
            output_path = Path(tmp) / ("uart_allocation_probe" if link else "probe.o")
            command = [self.compiler, "-std=c++17", "-I", str(staged)]
            if not link:
                command.append("-c")
            command += [str(source_path), "-o", str(output_path)]

            compile_result = subprocess.run(
                command, capture_output=True, text=True, check=False
            )
            if not link or compile_result.returncode != 0:
                return compile_result, None

            run_result = subprocess.run(
                [str(output_path)],
                capture_output=True,
                text=True,
                check=False,
                timeout=10,
            )
            self.assertEqual(run_result.returncode, 0, run_result.stderr)
            return compile_result, tuple(int(v) for v in run_result.stdout.split())

    def _allocation(self, board_macro):
        compile_result, values = self._compile(board_macro)
        self.assertEqual(compile_result.returncode, 0, compile_result.stderr)
        self.assertIsNotNone(values)
        return values

    def test_artoo_esp32_allocation_is_the_three_controller_share(self):
        self.assertEqual(
            self._allocation("PA_BOARD_ARTOO_ESP32"),
            EXPECTED_BY_BOARD["PA_BOARD_ARTOO_ESP32"],
        )

    def test_firebeetle2_gives_audio_its_own_controller(self):
        self.assertEqual(
            self._allocation("PA_BOARD_FIREBEETLE2"),
            EXPECTED_BY_BOARD["PA_BOARD_FIREBEETLE2"],
        )

    def test_the_two_boards_do_not_converge_on_one_allocation(self):
        """The regression itself: the P4 must not inherit artoo's UART share.

        Both boards passing their own expectation in isolation would still hold
        if a copy-paste put audio back on the dome link's controller everywhere,
        so assert the compiled values differ and say which way round.
        """
        artoo = self._allocation("PA_BOARD_ARTOO_ESP32")
        firebeetle = self._allocation("PA_BOARD_FIREBEETLE2")
        self.assertNotEqual(artoo, firebeetle)

        artoo_dome, artoo_audio = artoo[1], artoo[2]
        fb_dome, fb_audio = firebeetle[1], firebeetle[2]
        self.assertEqual(artoo_dome, artoo_audio, "artoo-esp32 must keep the share")
        self.assertNotEqual(fb_dome, fb_audio, "firebeetle2 audio must not share the dome controller")

    def test_capability_disagreeing_with_the_allocation_fails_to_compile(self):
        """Flip firebeetle2's audio controller back onto the dome link's.

        This is the exact half-applied change the guard exists for: the
        capability still says 'audio has its own', so the ownership handoff
        stays compiled out, while both consumers sit on one controller again.
        """
        compile_result, _ = self._compile(
            "PA_BOARD_FIREBEETLE2",
            config_patch=(r"constexpr uint8_t UART_PORT_AUDIO = 3;",
                          "constexpr uint8_t UART_PORT_AUDIO = 2;"),
            link=False,
        )
        self.assertNotEqual(compile_result.returncode, 0, compile_result.stdout)
        self.assertIn(
            "PA_CAP_DEDICATED_AUDIO_UART must agree with the UART controller allocation",
            compile_result.stderr,
        )

    def test_capability_flipped_without_moving_the_port_fails_to_compile(self):
        """The mirror image: claim the capability on a board that still shares."""
        compile_result, _ = self._compile(
            "PA_BOARD_ARTOO_ESP32",
            config_patch=(r"#define PA_CAP_DEDICATED_AUDIO_UART 0 ",
                          "#define PA_CAP_DEDICATED_AUDIO_UART 1 "),
            link=False,
        )
        self.assertNotEqual(compile_result.returncode, 0, compile_result.stdout)
        self.assertIn(
            "PA_CAP_DEDICATED_AUDIO_UART must agree with the UART controller allocation",
            compile_result.stderr,
        )

    def test_a_controller_the_chip_does_not_have_fails_to_compile(self):
        """artoo-esp32 has three HP UARTs; UART3 does not exist there.

        Without this guard HardwareSerial::begin() rejects the index at runtime
        with a log_e and returns, so the lane is simply silent -- the failure
        mode is 'audio stopped working', with nothing pointing at the cause.
        """
        compile_result, _ = self._compile(
            "PA_BOARD_ARTOO_ESP32",
            config_patch=(r"constexpr uint8_t UART_PORT_AUDIO = 2;  // shared",
                          "constexpr uint8_t UART_PORT_AUDIO = 3;  // shared"),
            link=False,
        )
        self.assertNotEqual(compile_result.returncode, 0, compile_result.stdout)
        self.assertIn(
            "UART_PORT_AUDIO names a UART controller this chip target does not have",
            compile_result.stderr,
        )

    def test_uart0_cannot_be_allocated_to_a_firmware_lane(self):
        """UART0 is the console on both chip targets and is never a lane."""
        compile_result, _ = self._compile(
            "PA_BOARD_FIREBEETLE2",
            config_patch=(r"constexpr uint8_t UART_PORT_AUDIO = 3;",
                          "constexpr uint8_t UART_PORT_AUDIO = 0;"),
            link=False,
        )
        self.assertNotEqual(compile_result.returncode, 0, compile_result.stdout)
        self.assertIn(
            "UART0 is the console lane and must not be allocated to a firmware consumer",
            compile_result.stderr,
        )

    def test_every_board_arm_declares_the_allocation(self):
        """Structural guard: a third Board Variant cannot inherit by omission.

        A new board arm that forgot UART_PORT_AUDIO would not fail to compile
        by itself -- it would fail only once a consumer referenced it, which is
        a confusing place to find out. Assert one declaration of each constant
        per board arm instead.
        """
        config_text = (INCLUDE_DIR / "config.h").read_text(encoding="utf-8")
        for constant in ("UART_PORT_DRIVE", "UART_PORT_DOME", "UART_PORT_AUDIO"):
            with self.subTest(constant=constant):
                declarations = re.findall(
                    rf"^\s*constexpr\s+uint8_t\s+{constant}\s*=\s*\d+\s*;",
                    config_text,
                    re.MULTILINE,
                )
                self.assertEqual(
                    len(declarations),
                    len(EXPECTED_BY_BOARD),
                    f"expected exactly one {constant} declaration per board arm",
                )


if __name__ == "__main__":
    unittest.main()
