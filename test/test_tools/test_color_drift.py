"""Unit coverage for tools/check_color_drift.py (#353).

The fixtures are synthetic: a made-up palette of three tokens. Copying the real
`:root` in would write the shipped palette down a second time, which is the
thing the checker exists to stop.

The `RealTree` case keeps the slice gate covering the repository: the gate runs
this directory and does not run `make check-color-drift`.
"""

import contextlib
import io
import sys
import tempfile
import unittest
from contextlib import redirect_stderr, redirect_stdout
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPO / "tools"))

import check_color_drift as color  # noqa: E402

PALETTE = """/* Fixture palette. */
:root {
  --ink: #e4eaf2;
  --seam: #223041;
  --warn: #e8a832;
  --shade: #000000;
  --space-sm: 8px;
}
"""


class Tree:
    def __init__(self, stack, *, files=None, palette=PALETTE):
        root = Path(stack.enter_context(tempfile.TemporaryDirectory()))
        self.data = root / "data"
        self.data.mkdir()
        self.stylesheet = root / "style.css"
        self.stylesheet.write_text(palette, encoding="utf-8")
        for name, body in (files or {}).items():
            path = self.data / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(body, encoding="utf-8")

    def findings(self, pending=None):
        found, _, _ = color.check(
            data=self.data, stylesheet=self.stylesheet, pending=pending or {}
        )
        return found


class ColorCheck(unittest.TestCase):
    def setUp(self):
        self.stack = self.enterContext(contextlib.ExitStack())

    def test_a_literal_the_palette_has_not_is_caught(self):
        tree = Tree(self.stack, files={"s.html": "<style>.a{color:#3b82f6}</style>"})
        found = tree.findings()
        self.assertEqual(1, len(found), found)
        self.assertIn("#3b82f6", found[0])

    def test_a_literal_the_palette_has_passes(self):
        tree = Tree(self.stack, files={"s.html": "<style>.a{color:#e4eaf2}</style>"})
        self.assertEqual([], tree.findings())

    def test_alpha_is_not_a_new_color(self):
        # A glow is the token at six tenths, not a second amber.
        tree = Tree(
            self.stack,
            files={"s.html": "<style>.a{box-shadow:0 0 6px rgba(232, 168, 50, 0.6)}</style>"},
        )
        self.assertEqual([], tree.findings())

    def test_rgb_and_hex_are_the_same_color(self):
        tree = Tree(self.stack, files={"s.html": "<style>.a{background:rgb(34, 48, 65)}</style>"})
        self.assertEqual([], tree.findings())

    def test_a_var_naming_no_token_is_caught(self):
        # It renders the fallback in silence: no error, no console warning.
        tree = Tree(self.stack, files={"s.html": '<svg><rect fill="var(--nope,#e4eaf2)"/></svg>'})
        found = tree.findings()
        self.assertEqual(1, len(found), found)
        self.assertIn("var(--nope)", found[0])

    def test_the_standalone_export_form_passes(self):
        # #366's printable sheet: the token is declared and the fallback is a
        # color the palette has, so the pair stands on its own terms.
        tree = Tree(self.stack, files={"w.js": 'const INK = "var(--ink,#e4eaf2)";\n'})
        self.assertEqual([], tree.findings())

    def test_an_issue_reference_in_a_comment_is_not_a_color(self):
        # `#293` matches a naive hex regex. Named by #366's worker as the false
        # positive to avoid before any of this was written - in all three
        # comment languages a surface is written in.
        tree = Tree(
            self.stack,
            files={
                "s.html": "<!-- see #293 -->\n<style>/* #324, and red means stopped */\n"
                ".a{color:#e4eaf2}</style>",
                "s.js": "// #360 and #366\nconst A = 1;\n/* blue carries no state */\n",
            },
        )
        self.assertEqual([], tree.findings())

    def test_prose_that_mentions_a_color_is_not_a_paint_site(self):
        # data/rc.js describes a dome sequence as "Pulsing red holos and logics".
        # That is copy about what the droid does, not a color the page paints.
        tree = Tree(self.stack, files={"r.js": 'const D = "Pulsing red holos and logics (10 s)";\n'})
        self.assertEqual([], tree.findings())

    def test_a_named_color_in_a_paint_context_is_caught(self):
        tree = Tree(self.stack, files={"s.html": '<svg><rect fill="red"/></svg>'})
        found = tree.findings()
        self.assertEqual(1, len(found), found)
        self.assertIn("named color", found[0])

    def test_a_style_block_inside_a_template_literal_is_read(self):
        # data/dome_panel_model.js ships its whole SVG, <style> and all, as one
        # template literal. A checker that only reads .html files misses it.
        tree = Tree(
            self.stack,
            files={"m.js": "window.SVG = `<svg><style>\n.p{fill:#ea580c}\n</style></svg>`;\n"},
        )
        found = tree.findings()
        self.assertEqual(1, len(found), found)
        self.assertIn("#ea580c", found[0])

    def test_a_pending_row_suppresses_its_file_and_only_its_file(self):
        tree = Tree(
            self.stack,
            files={"m.js": 'window.A = "#ea580c";\n', "s.html": "<style>.a{color:#ea580c}</style>"},
        )
        found = tree.findings(pending={color.rel(tree.data / "m.js"): "waiting on a decision"})
        self.assertEqual(1, len(found), found)
        self.assertIn("s.html", found[0])

    def test_a_pending_row_that_has_been_repaired_fails(self):
        # A row is a promise, not a suppression: it cannot outlive the fix.
        tree = Tree(self.stack, files={"m.js": 'window.A = "#e4eaf2";\n'})
        found = tree.findings(pending={color.rel(tree.data / "m.js"): "waiting on a decision"})
        self.assertEqual(1, len(found), found)
        self.assertIn("delete its row", found[0])

    def test_a_palette_with_no_color_refuses_to_pass(self):
        # Silence would be the wrong answer: with no palette, every literal is
        # a finding and none of them means anything.
        tree = Tree(self.stack, palette=":root {\n  --space-sm: 8px;\n}\n")
        found = tree.findings()
        self.assertTrue(any("This is not a pass" in finding for finding in found), found)


class RealTree(unittest.TestCase):
    def test_the_repository_passes(self):
        out, err = io.StringIO(), io.StringIO()
        with redirect_stdout(out), redirect_stderr(err):
            status = color.main()
        self.assertEqual(0, status, err.getvalue())
        self.assertIn("Color check passed", out.getvalue())

    def test_the_stylesheet_itself_is_left_to_the_web_suite(self):
        self.assertIn("data/style.css", color.PALETTE_OWNED)


if __name__ == "__main__":
    unittest.main()
