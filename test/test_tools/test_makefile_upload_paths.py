#!/usr/bin/env python3
"""The Makefile's network upload paths must not leave the callback port to chance.

Regression, 2026-09-03: `make uploadfs BUILD_ENV=artoo_esp32` failed with
"No response from device" after a successful firmware OTA. espota had been left
to pick its own local callback port (measured: host_port=21870); a default-deny
inbound firewall drops the device's connect-back on a random port, and the
failure reads as if the board were absent. OTA_HOST_PORT exists to pin that
port, and tools/ota_upload.py applies it -- but `uploadfs` called PlatformIO's
espota directly and never received it, so an FS image could not be pushed to
the artoo at all.

These read the Makefile as data rather than running it: an upload path is the
kind of thing that is exercised once a week on a bench and silently rots in
between.
"""

import os
import re
import unittest

REPO_ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..")
MAKEFILE = os.path.join(REPO_ROOT, "Makefile")


def recipe_lines() -> list[str]:
    with open(MAKEFILE, encoding="utf-8") as handle:
        return [line for line in handle if line.startswith("\t") or line.lstrip().startswith("python3")]


class OtaUploadsPinTheCallbackPort(unittest.TestCase):
    def setUp(self):
        self.lines = [line for line in recipe_lines() if "tools/ota_upload.py" in line]

    def test_the_makefile_actually_has_ota_upload_paths_to_check(self):
        """Guards the assertion below against silently passing on zero matches
        if the invocation is ever renamed or moved."""
        self.assertGreaterEqual(len(self.lines), 2)

    def test_every_ota_upload_pins_the_host_port(self):
        for line in self.lines:
            with self.subTest(line=line.strip()[:90]):
                self.assertIn("--host-port", line)

    def test_the_pinned_port_comes_from_the_shared_variable(self):
        """A literal port in one recipe and OTA_HOST_PORT in another is how the
        two drift apart, and only one of them has a firewall rule."""
        for line in self.lines:
            with self.subTest(line=line.strip()[:90]):
                self.assertIn("--host-port $(OTA_HOST_PORT)", line)


class FilesystemOtaUsesTheWrapper(unittest.TestCase):
    def setUp(self):
        with open(MAKEFILE, encoding="utf-8") as handle:
            body = handle.read()
        match = re.search(r"^uploadfs:.*?(?=\n\n|\n#)", body, re.S | re.M)
        self.assertIsNotNone(match, "uploadfs recipe not found in the Makefile")
        self.recipe = match.group(0)

    def test_the_network_branch_pushes_the_fs_image_through_ota_upload(self):
        self.assertIn("tools/ota_upload.py", self.recipe)
        self.assertIn("--spiffs", self.recipe)

    def test_the_network_branch_pins_the_host_port(self):
        self.assertIn("--host-port $(OTA_HOST_PORT)", self.recipe)


if __name__ == "__main__":
    unittest.main()
