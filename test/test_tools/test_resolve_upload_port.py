#!/usr/bin/env python3
"""Tests for tools/resolve_upload_port.py.

The regression these exist for: `UPLOAD_PORT ?= /dev/ttyUSB0` in the Makefile
aimed an ESP32-P4 image at the artoo-esp32 on 2026-09-03, because a default is
a guess about which board when two are attached. The rule under test is that
ambiguity is refused, never resolved by picking.
"""

import os
import sys
import tempfile
import unittest

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "tools"))

import resolve_upload_port as rup  # noqa: E402


class Bench:
    """A temp directory posing as /dev plus /dev/serial/by-id."""

    def __init__(self, stack, devices: dict[str, str | None]):
        self.root = stack.enter_context(tempfile.TemporaryDirectory())
        self.by_id = os.path.join(self.root, "by-id")
        os.mkdir(self.by_id)
        self.devices = []
        for name, stable in devices.items():
            device = os.path.join(self.root, name)
            with open(device, "w"):
                pass
            self.devices.append(device)
            if stable:
                os.symlink(device, os.path.join(self.by_id, stable))
        self.patterns = (os.path.join(self.root, "tty*"),)

    def discover(self):
        return rup.discover(self.by_id, self.patterns)


class Discovery(unittest.TestCase):
    def setUp(self):
        import contextlib
        self.stack = contextlib.ExitStack()
        self.addCleanup(self.stack.close)

    def test_a_device_with_a_stable_name_reports_it(self):
        bench = Bench(self.stack, {"ttyACM0": "usb-Espressif_USB_JTAG-if00"})
        self.assertEqual(bench.discover(),
                         [(os.path.join(bench.root, "ttyACM0"),
                           "usb-Espressif_USB_JTAG-if00")])

    def test_a_device_without_a_stable_name_is_still_offered(self):
        """The by-id fallback must not hide a board: refusing a bench we could
        have served is its own failure."""
        bench = Bench(self.stack, {"ttyUSB0": None})
        self.assertEqual(bench.discover(),
                         [(os.path.join(bench.root, "ttyUSB0"), None)])

    def test_a_device_is_not_listed_twice_when_both_views_see_it(self):
        bench = Bench(self.stack, {"ttyACM0": "usb-P4-if00"})
        self.assertEqual(len(bench.discover()), 1)


class Resolution(unittest.TestCase):
    def setUp(self):
        import contextlib
        self.stack = contextlib.ExitStack()
        self.addCleanup(self.stack.close)
        self.warnings = []

    def resolve(self, bench, requested=None, origin=None):
        return rup.resolve(requested, bench.discover(), "firebeetle2", origin,
                           warn=self.warnings.append)

    def test_one_attached_device_is_unambiguous_and_is_used(self):
        bench = Bench(self.stack, {"ttyACM0": "usb-P4-if00"})
        self.assertEqual(self.resolve(bench), bench.devices[0])

    def test_two_attached_devices_are_refused_not_picked(self):
        """The regression. Either answer could be the wrong board, so there is
        no answer -- only a refusal."""
        bench = Bench(self.stack, {"ttyACM0": "usb-P4-if00", "ttyUSB0": "usb-CP2102-if00"})
        with self.assertRaises(SystemExit) as caught:
            self.resolve(bench)
        message = str(caught.exception)
        self.assertIn("ambiguous", message)
        for device in bench.devices:
            self.assertIn(device, message)

    def test_no_attached_device_is_refused(self):
        bench = Bench(self.stack, {})
        with self.assertRaises(SystemExit) as caught:
            self.resolve(bench)
        self.assertIn("No serial device found", str(caught.exception))

    def test_an_explicit_port_wins_over_ambiguity(self):
        bench = Bench(self.stack, {"ttyACM0": "usb-P4-if00", "ttyUSB0": "usb-CP2102-if00"})
        chosen = bench.devices[0]
        self.assertEqual(self.resolve(bench, requested=chosen, origin="command line"), chosen)

    def test_an_explicit_port_that_does_not_exist_is_refused_not_ignored(self):
        """A typo must not fall through to a guess -- that is how a named port
        becomes the wrong board."""
        bench = Bench(self.stack, {"ttyACM0": "usb-P4-if00"})
        with self.assertRaises(SystemExit) as caught:
            self.resolve(bench, requested="/dev/tty-nope", origin="command line")
        self.assertIn("does not exist", str(caught.exception))

    def test_an_ambient_port_warns_when_more_than_one_board_is_attached(self):
        bench = Bench(self.stack, {"ttyACM0": "usb-P4-if00", "ttyUSB0": "usb-CP2102-if00"})
        self.resolve(bench, requested=bench.devices[1], origin="environment")
        self.assertEqual(len(self.warnings), 1)
        self.assertIn("shell environment", self.warnings[0])

    def test_a_deliberate_port_does_not_warn(self):
        bench = Bench(self.stack, {"ttyACM0": "usb-P4-if00", "ttyUSB0": "usb-CP2102-if00"})
        self.resolve(bench, requested=bench.devices[1], origin="command line")
        self.assertEqual(self.warnings, [])

    def test_an_ambient_port_does_not_warn_when_it_cannot_be_wrong(self):
        bench = Bench(self.stack, {"ttyACM0": "usb-P4-if00"})
        self.resolve(bench, requested=bench.devices[0], origin="environment")
        self.assertEqual(self.warnings, [])


if __name__ == "__main__":
    unittest.main()


class UserMkCarriesNoAssumedPort(unittest.TestCase):
    """tools/configure.py is the other half of the same regression: it used to
    default UPLOAD_PORT to /dev/ttyUSB0 and write it into user.mk, which turned
    a guess into a file-origin value the resolver honours without even the
    ambient-environment warning. Tested here rather than in a file of its own
    because it is the same rule: no assumed USB path, anywhere."""

    def setUp(self):
        sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                        "..", "..", "tools"))
        import configure
        self.configure = configure

    def test_no_upload_port_default_exists_to_be_written(self):
        self.assertNotIn("UPLOAD_PORT", self.configure.DEFAULTS)

    def test_a_blank_answer_omits_the_line_so_the_port_is_resolved_per_flash(self):
        rendered = self.configure._render_user_mk("10.0.0.22", "artoo_esp32", "")
        self.assertNotIn("UPLOAD_PORT", rendered)

    def test_a_deliberate_answer_is_still_pinned(self):
        rendered = self.configure._render_user_mk("10.0.0.22", "firebeetle2", "/dev/ttyACM0")
        self.assertIn("UPLOAD_PORT = /dev/ttyACM0", rendered)
