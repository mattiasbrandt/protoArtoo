"""Unit coverage for tools/check_surface_anatomy.py (#399).

Two jobs, the split `test_component_registry_drift.py` and
`test_action_registry_drift.py` both use.

**Fixtures prove each check can fail.** Every check is driven against a
hand-written data tree with the defect planted in it, so "this check would catch
that" is demonstrated rather than asserted, and without mutating a shipped file
to find out.

**Live-tree assertions keep the slice gate covering the repo.** The gate runs
`python3 -m unittest discover -s test/test_tools` and does not run
`make check-surface-anatomy`, so without the `RealTree` cases below the repo's
own surfaces would be outside the gate. `make check-surface-anatomy` and the CI
step are the operator-facing entry points; these are what make the gate fail on
a real one.
"""

import sys
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "tools"))

import check_surface_anatomy as anatomy  # noqa: E402


ROCKET = "\U0001F680"

# A sprite shaped exactly as data/shell.js writes one: the regex that reads it
# keys on the indentation and the leading "M" of an SVG path, so a fixture that
# only looked similar would pass a check that the real file fails.
SPRITE = '''  const ICONS = {
    "view-dashboard-outline": "M19,5V7H15V5H19Z",
    "steering": "M13,19.92C14.8,19.7Z",
  };
'''

SURFACES = '''  const SURFACES = [
    { page: "home", doc: "/dashboard.html", icon: "view-dashboard-outline", name: "Dashboard", aliases: [] },
    { page: "drive", doc: "/drive.html", icon: "steering", name: "Foot Drive", aliases: [] },
  ];
'''


def tree(tmp: str, files: dict) -> Path:
    data = Path(tmp) / "data"
    data.mkdir()
    for name, text in files.items():
        path = data / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(text, encoding="utf-8")
    return data


class Pictographs(unittest.TestCase):
    def test_a_swept_surface_carrying_one_fails(self):
        with tempfile.TemporaryDirectory() as tmp:
            data = tree(tmp, {"dashboard.html": f"<h1>Dashboard {ROCKET}</h1>"})
            errors = []
            anatomy.check_pictographs(anatomy.served_files(data), data, errors)
            self.assertEqual(len(errors), 1, errors)
            self.assertIn("dashboard.html", errors[0])
            self.assertIn(ROCKET, errors[0])

    def test_a_swept_surface_without_one_passes(self):
        with tempfile.TemporaryDirectory() as tmp:
            data = tree(tmp, {"dashboard.html": "<h1>Dashboard</h1>"})
            errors = []
            swept, pending = anatomy.check_pictographs(anatomy.served_files(data), data, errors)
            self.assertEqual(errors, [])
            self.assertEqual((swept, pending), (1, 0))

    def test_a_bare_variation_selector_is_a_pictograph(self):
        # U+FE0F is what turns an otherwise textual glyph into a coloured
        # picture, and it is invisible in a diff. A check that only looked at
        # the emoji blocks would pass "warning sign + VS16" and ship it.
        with tempfile.TemporaryDirectory() as tmp:
            data = tree(tmp, {"dashboard.html": "<p>⚠️ careful</p>"})
            errors = []
            anatomy.check_pictographs(anatomy.served_files(data), data, errors)
            self.assertEqual(len(errors), 1, errors)

    # The two below drive the pending mechanism through a FIXTURE list rather
    # than through the shipped one. They read the real PENDING until #399 slice
    # 4 emptied it, at which point they raised StopIteration - and a check whose
    # own coverage dies the moment its data goes empty is a check that stops
    # being tested exactly when it becomes a pure guard. A planted row is also
    # the stricter form: it pins what a row DOES, rather than whatever row the
    # sweep happened to have left.
    PENDING_FIXTURE = {"wifi.html": "#000 some slice"}

    def test_a_pending_surface_keeps_its_pictographs(self):
        # The point of the list: this file is not swept yet and must not fail.
        name, slice_ = next(iter(self.PENDING_FIXTURE.items()))
        with tempfile.TemporaryDirectory() as tmp:
            data = tree(tmp, {name: f"<p>{ROCKET}</p>"})
            errors = []
            with patch.object(anatomy, "PENDING", self.PENDING_FIXTURE):
                swept, pending = anatomy.check_pictographs(anatomy.served_files(data), data, errors)
            self.assertEqual(errors, [])
            self.assertEqual((swept, pending), (0, 1))
            self.assertTrue(slice_)

    def test_a_pending_surface_that_is_already_clean_fails(self):
        # The list is self-retiring. A row that has become true is a row that
        # should have been deleted by the slice that made it true, and leaving
        # it means the next reader believes a surface is unswept when it is not.
        name = next(iter(self.PENDING_FIXTURE))
        with tempfile.TemporaryDirectory() as tmp:
            data = tree(tmp, {name: "<p>nothing to see</p>"})
            errors = []
            with patch.object(anatomy, "PENDING", self.PENDING_FIXTURE):
                anatomy.check_pictographs(anatomy.served_files(data), data, errors)
            self.assertEqual(len(errors), 1, errors)
            self.assertIn("delete its row", errors[0])

    def test_a_file_the_controller_does_not_serve_is_not_checked(self):
        with tempfile.TemporaryDirectory() as tmp:
            data = tree(tmp, {"notes.md": f"{ROCKET}", "photo.webp": "binary-ish"})
            errors = []
            swept, _ = anatomy.check_pictographs(anatomy.served_files(data), data, errors)
            self.assertEqual(errors, [])
            self.assertEqual(swept, 0)


class IconReferences(unittest.TestCase):
    def test_a_use_naming_no_symbol_fails(self):
        with tempfile.TemporaryDirectory() as tmp:
            data = tree(tmp, {"dashboard.html": '<svg class="i"><use href="#i-power-nap"/></svg>'})
            errors = []
            anatomy.check_icon_references(anatomy.served_files(data), data, {"power-sleep"}, errors)
            self.assertEqual(len(errors), 1, errors)
            self.assertIn("power-nap", errors[0])

    def test_a_use_naming_a_symbol_passes(self):
        with tempfile.TemporaryDirectory() as tmp:
            data = tree(tmp, {"dashboard.html": '<svg class="i"><use href="#i-power-sleep"/></svg>'})
            errors = []
            uses = anatomy.check_icon_references(anatomy.served_files(data), data, {"power-sleep"}, errors)
            self.assertEqual(errors, [])
            self.assertEqual(uses, 1)


class SpriteAndSurfaces(unittest.TestCase):
    def test_the_symbols_are_read_from_the_shell(self):
        with tempfile.TemporaryDirectory() as tmp:
            shell = Path(tmp) / "shell.js"
            shell.write_text(SPRITE, encoding="utf-8")
            self.assertEqual(anatomy.icon_symbols(shell), {"view-dashboard-outline", "steering"})

    def test_a_surface_naming_no_symbol_fails(self):
        with tempfile.TemporaryDirectory() as tmp:
            shell = Path(tmp) / "shell.js"
            shell.write_text(SPRITE + SURFACES.replace("steering", "jigsaw-outline"), encoding="utf-8")
            errors = []
            anatomy.check_surface_icons(shell, {"view-dashboard-outline", "steering"}, errors)
            self.assertEqual(len(errors), 1, errors)
            self.assertIn("jigsaw-outline", errors[0])

    def test_every_surface_naming_a_symbol_passes(self):
        with tempfile.TemporaryDirectory() as tmp:
            shell = Path(tmp) / "shell.js"
            shell.write_text(SPRITE + SURFACES, encoding="utf-8")
            errors = []
            named = anatomy.check_surface_icons(shell, {"view-dashboard-outline", "steering"}, errors)
            self.assertEqual(errors, [])
            self.assertEqual(named, 2)

    def test_a_shell_with_no_sprite_at_all_fails(self):
        with tempfile.TemporaryDirectory() as tmp:
            shell = Path(tmp) / "shell.js"
            shell.write_text(SURFACES, encoding="utf-8")
            errors = []
            anatomy.check_surface_icons(shell, set(), errors)
            self.assertEqual(len(errors), 1, errors)


class RealTree(unittest.TestCase):
    """The shipped data/ tree, so the slice gate covers it."""

    def test_the_repo_passes_its_own_check(self):
        self.assertEqual(anatomy.main(), 0)

    def test_every_swept_surface_is_free_of_pictographs(self):
        errors = []
        swept, _ = anatomy.check_pictographs(anatomy.served_files(anatomy.DATA), anatomy.DATA, errors)
        self.assertEqual(errors, [])
        self.assertGreater(swept, 0, "no surface has been swept yet, which cannot be right")

    def test_every_icon_the_shipped_chrome_names_resolves(self):
        symbols = anatomy.icon_symbols(anatomy.SHELL)
        errors = []
        anatomy.check_icon_references(anatomy.served_files(anatomy.DATA), anatomy.DATA, symbols, errors)
        anatomy.check_surface_icons(anatomy.SHELL, symbols, errors)
        self.assertEqual(errors, [])

    def test_the_pending_list_names_files_that_exist(self):
        # A row for a file that was renamed or deleted can never be retired,
        # and would quietly excuse nothing for the rest of the sweep.
        missing = sorted(name for name in anatomy.PENDING if not (anatomy.DATA / name).is_file())
        self.assertEqual(missing, [])

    def test_nothing_is_still_waiting_for_a_slice(self):
        # #399 swept all thirteen surfaces; the last two rows went with slice 4.
        # A row added after that is not a promise about work in flight, it is an
        # exemption - so the list stays empty and this is what says so.
        self.assertEqual(anatomy.PENDING, {})


if __name__ == "__main__":
    unittest.main()
