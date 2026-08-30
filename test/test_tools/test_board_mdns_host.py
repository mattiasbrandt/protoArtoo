"""Compiler proof that WIFI_MDNS_HOST is a per-Board-Variant default (#242).

Native Unity tests (test/test_native/) always build PA_BOARD_ARTOO_ESP32 (see
platformio.ini env:native build_flags), so they can only ever pin one board's
value in a single binary. The regression this ticket fixes -- both boards
silently converging on the same mDNS default -- can only be proven by
compiling include/config.h under each PA_BOARD value and comparing the actual
values, the same way test_firebeetle_required_pins.py proves the FireBeetle
pin-map compiles under PA_BOARD_FIREBEETLE2. This test does the analogous
thing for the mDNS hostname default.
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
CONFIG = INCLUDE_DIR / "config.h"

EXPECTED_HOST_BY_BOARD = {
    "PA_BOARD_ARTOO_ESP32": "artoo",
    "PA_BOARD_FIREBEETLE2": "firebeetle2",
}


class BoardMdnsHostDefault(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        compiler_name = os.environ.get("CXX", "c++")
        cls.compiler = shutil.which(compiler_name)
        if cls.compiler is None:
            raise RuntimeError(f"C++ compiler not found: {compiler_name}")

    def _compile_and_run(self, board_macro):
        source = "\n".join(
            [
                f"#define PA_BOARD {board_macro}",
                # config.h requires these per-env, and #errors without them (#244).
                # A probe stands in for a build environment, so it declares what any
                # environment must.
                "#define PA_LOG_LEVEL 2",
                "#define PA_HEAP_PROFILE 0",
                '#include "config.h"',
                "#include <cstdio>",
                "int main() { std::fputs(WIFI_MDNS_HOST, stdout); return 0; }",
                "",
            ]
        )
        with tempfile.TemporaryDirectory() as tmp:
            source_path = Path(tmp) / "mdns_host_probe.cpp"
            binary_path = Path(tmp) / "mdns_host_probe"
            source_path.write_text(source, encoding="utf-8")
            compile_result = subprocess.run(
                [
                    self.compiler,
                    "-std=c++17",
                    "-I",
                    str(INCLUDE_DIR),
                    str(source_path),
                    "-o",
                    str(binary_path),
                ],
                capture_output=True,
                text=True,
                check=False,
            )
            self.assertEqual(compile_result.returncode, 0, compile_result.stderr)
            run_result = subprocess.run(
                [str(binary_path)],
                capture_output=True,
                text=True,
                check=False,
                timeout=10,
            )
            self.assertEqual(run_result.returncode, 0, run_result.stderr)
            return run_result.stdout

    def test_artoo_esp32_default_is_unchanged(self):
        # artoo-esp32 must stay behaviour-identical: moving WIFI_MDNS_HOST
        # into the board section must not change its value.
        self.assertEqual(
            self._compile_and_run("PA_BOARD_ARTOO_ESP32"),
            EXPECTED_HOST_BY_BOARD["PA_BOARD_ARTOO_ESP32"],
        )

    def test_firebeetle2_default_is_board_specific(self):
        self.assertEqual(
            self._compile_and_run("PA_BOARD_FIREBEETLE2"),
            EXPECTED_HOST_BY_BOARD["PA_BOARD_FIREBEETLE2"],
        )

    def test_boards_never_converge_on_one_default(self):
        # The defect this ticket fixes: two controllers on one LAN must not
        # contest the same mDNS name. Assert the actual compiled values
        # differ, not just that each matches its own expectation in
        # isolation -- a copy-paste that set both boards to the same string
        # would still pass the two tests above individually.
        artoo_host = self._compile_and_run("PA_BOARD_ARTOO_ESP32")
        firebeetle_host = self._compile_and_run("PA_BOARD_FIREBEETLE2")
        self.assertNotEqual(artoo_host, firebeetle_host)

    def test_wifi_mdns_host_is_not_declared_globally(self):
        # Guard the structural fix, not just the values: WIFI_MDNS_HOST must
        # be declared once inside each board's #if/#elif arm, never at file
        # scope outside PA_BOARD selection (the original defect at :399).
        config_text = CONFIG.read_text(encoding="utf-8")
        declaration_re = re.compile(
            r'^\s*constexpr\s+char\s+WIFI_MDNS_HOST\[\]\s*=\s*"[^"]+"\s*;',
            re.MULTILINE,
        )
        declarations = declaration_re.findall(config_text)
        self.assertEqual(
            len(declarations),
            2,
            "expected exactly one WIFI_MDNS_HOST declaration per board arm, "
            f"found {len(declarations)}",
        )

        board_selection_re = re.compile(
            r"#if\s+PA_BOARD\s*==\s*PA_BOARD_ARTOO_ESP32.*?"
            r"#elif\s+PA_BOARD\s*==\s*PA_BOARD_FIREBEETLE2.*?"
            r'#else\s*\n\s*#error\s+"[^"]*mDNS[^"]*"\s*\n#endif',
            re.DOTALL,
        )
        match = board_selection_re.search(config_text)
        self.assertIsNotNone(
            match,
            "expected a PA_BOARD_ARTOO_ESP32 / PA_BOARD_FIREBEETLE2 / #error "
            "arm selecting WIFI_MDNS_HOST, mirroring the pin-map selection's "
            "own #else #error arm",
        )
        # Both declarations found above must live inside that one guarded
        # block, not somewhere else in the file.
        for declaration in declarations:
            self.assertIn(declaration, match.group(0))


if __name__ == "__main__":
    unittest.main()
