"""Pin tools/refresh_component_headers.py (#437).

pioarduino's lib rebuild copies rebuilt archives into the framework libs package
but not the managed components' headers; on #437 that left Arduino compiling
wifi_init_config_t from esp_wifi_remote 1.6.3 while linking 1.6.5, and the
ESP32-C6 refused esp_wifi_init. Each case builds a scratch libs package and a
scratch managed_components/ and drives run() the way the pre: script does.
"""

import importlib.util
import json
import os
import tempfile
import time
import unittest
from pathlib import Path

SCRIPT = Path(__file__).parents[2] / "tools" / "refresh_component_headers.py"
_spec = importlib.util.spec_from_file_location("refresh_component_headers", SCRIPT)
rch = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(rch)

OLD_H = "typedef struct { int dump_hesigb_enable; int magic; } wifi_init_config_t;\n"
NEW_H = "typedef struct { int dump_hesigb_enable; int wifi_task_stack_size; int magic; } wifi_init_config_t;\n"
REL = Path("idf_v5.5/include/injected/esp_wifi.h")


class RefreshComponentHeadersTest(unittest.TestCase):
    def setUp(self):
        self._tmp = tempfile.TemporaryDirectory()
        root = Path(self._tmp.name)
        self.libs = root / "libs"
        self.include = self.libs / "esp32p4_es" / "include"
        self.managed = root / "project" / "managed_components"
        self.marker = self.libs / "sdkconfig"
        # the package as the tarball ships it: a stale managed-component header
        # beside an ESP-IDF component's own include dir
        self._write(self.include / "espressif__esp_wifi_remote" / REL, OLD_H)
        self._write(self.include / "esp_wifi" / "include" / "esp_wifi.h", "IDF OWN\n")
        # the component the rebuild actually compiled
        src = self.managed / "espressif__esp_wifi_remote"
        self._write(src / REL, NEW_H)
        self._write(src / "idf_component.yml", "description: x\nversion: 1.6.5\n")
        self.lib = self.libs / "esp32p4_es" / "lib"
        self._write(self.lib / "libespressif__esp_wifi_remote.a", "rebuilt\n")
        self.marker.write_text("# rebuilt\n")

    def tearDown(self):
        self._tmp.cleanup()

    @staticmethod
    def _write(path: Path, text: str):
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(text)

    def _run(self):
        return rch.run(self.include, self.marker, self.managed)

    def test_before_the_rebuild_nothing_is_touched(self):
        self.marker.unlink()
        self.assertIn("not rebuilt yet", self._run())
        self.assertEqual((self.include / "espressif__esp_wifi_remote" / REL).read_text(), OLD_H)
        self.assertFalse((self.include / rch.STAMP_NAME).exists())

    def test_first_run_after_the_rebuild_replaces_the_stale_header(self):
        line = self._run()
        self.assertEqual((self.include / "espressif__esp_wifi_remote" / REL).read_text(), NEW_H)
        self.assertIn("1 were stale and replaced", line)
        stamp = json.loads((self.include / rch.STAMP_NAME).read_text())
        self.assertEqual(stamp["components"]["espressif__esp_wifi_remote"]["version"], "1.6.5")

    def test_an_esp_idf_components_own_headers_are_left_alone(self):
        self._run()
        self.assertEqual((self.include / "esp_wifi" / "include" / "esp_wifi.h").read_text(), "IDF OWN\n")
        stamp = json.loads((self.include / rch.STAMP_NAME).read_text())
        self.assertEqual(list(stamp["components"]), ["espressif__esp_wifi_remote"])

    def test_a_later_build_verifies_and_passes(self):
        self._run()
        self.assertIn("headers match the rebuilt libraries (esp_wifi_remote 1.6.5)", self._run())

    def test_a_header_changed_after_the_match_fails_the_build(self):
        self._run()
        (self.include / "espressif__esp_wifi_remote" / REL).write_text(OLD_H)
        with self.assertRaises(SystemExit) as caught:
            self._run()
        self.assertIn("espressif__esp_wifi_remote/" + str(REL), str(caught.exception))

    def test_a_rebuilt_component_missing_from_managed_components_fails_the_build(self):
        self._write(self.include / "espressif__esp_hosted" / "host" / "esp_hosted.h", "x\n")
        self._write(self.lib / "libespressif__esp_hosted.a", "rebuilt\n")
        with self.assertRaises(SystemExit) as caught:
            self._run()
        self.assertIn("espressif__esp_hosted", str(caught.exception))
        self.assertFalse((self.include / rch.STAMP_NAME).exists())

    def test_a_component_the_rebuild_left_alone_keeps_its_tarball_headers(self):
        # the camera/video stack on the P4: in the package, never re-resolved
        self._write(self.include / "espressif__esp_video" / "include" / "esp_video.h", "tarball\n")
        archive = self.lib / "libespressif__esp_video.a"
        self._write(archive, "tarball\n")
        old = time.time() - 70 * 24 * 3600
        os.utime(archive, (old, old))
        line = self._run()
        self.assertIn("1 component(s) not rebuilt", line)
        self.assertEqual((self.include / "espressif__esp_video" / "include" / "esp_video.h").read_text(), "tarball\n")

    def test_a_tarball_header_with_no_source_is_reported_not_fatal(self):
        self._write(self.include / "espressif__esp_wifi_remote" / "include" / "gone.h", "old\n")
        line = self._run()
        self.assertIn("espressif__esp_wifi_remote/include/gone.h", line)
        self.assertTrue((self.include / rch.STAMP_NAME).exists())


if __name__ == "__main__":
    unittest.main()
