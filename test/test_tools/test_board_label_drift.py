"""Unit coverage for tools/check_board_label_drift.py (#353).

The fixtures are synthetic: made-up boards, made-up components and a made-up
silkscreen legend. Copying the real labels in would write them down a second
time, which is the thing the checker exists to stop.

The `RealTree` case keeps the slice gate covering the repository: the gate runs
this directory and does not run `make check-board-label-drift`.
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

import check_board_label_drift as board  # noqa: E402

LABELS = """// Fixture label inventory.
// Define PA_COMPONENT_LABEL(board_name, component_name, label_text) first.
PA_COMPONENT_LABEL(fixture_a, enable_widget, "W1")
PA_COMPONENT_LABEL(fixture_b, enable_widget, "PAD 7")
PA_COMPONENT_LABEL(fixture_a, enable_gizmo, "G1")
PA_COMPONENT_LABEL(fixture_b, enable_gizmo, "PAD 8")
PA_COMPONENT_LABEL(fixture_a, enable_rc_ch1, "CH1")
PA_COMPONENT_LABEL(fixture_b, enable_rc_ch1, "PAD 9")
"""

# The Outputs table, in the shape include/board_outputs.h writes it.
OUTPUTS = """inline constexpr BoardOutput BOARD_OUTPUTS[] = {
    {"gizmo", "enable_gizmo", LEDC_CH_GIZMO, AUX_LED_PIN_DISABLED, "enableGizmo", "gizmoType"},
};
"""

# The Lane manifest, header comment included: its own text quotes the macro
# signature, and a reader that does not skip comments invents `enable_name`.
LANES = """// Fixture lane inventory.
// Define PA_BOARD_LANE(name, uart_port, tx_pin, rx_pin) before including it.
PA_BOARD_LANE(widget, UART_PORT_WIDGET, PIN_WIDGET_TX, PIN_WIDGET_RX)
"""

REGISTRY = """entries:
- name: fixture.config.enable_widget
  display_name: Widget
  description: Use the widget.
  domain: system
"""


class Tree:
    def __init__(self, stack, *, surface="", html="", registry=REGISTRY, labels=LABELS):
        root = Path(stack.enter_context(tempfile.TemporaryDirectory()))
        self.labels = root / "component_labels.inc"
        self.labels.write_text(labels, encoding="utf-8")
        self.outputs = root / "board_outputs.h"
        self.outputs.write_text(OUTPUTS, encoding="utf-8")
        self.lanes = root / "board_lanes.inc"
        self.lanes.write_text(LANES, encoding="utf-8")
        self.data = root / "data"
        self.data.mkdir()
        if surface:
            (self.data / "surface.js").write_text(surface, encoding="utf-8")
        if html:
            (self.data / "surface.html").write_text(html, encoding="utf-8")
        self.registry = root / "action-registry.yaml"
        self.registry.write_text(registry, encoding="utf-8")

    def findings(self):
        found, _, _ = board.check(
            labels_path=self.labels, outputs=self.outputs, lanes=self.lanes,
            data=self.data, registry=self.registry, pins=False,
        )
        return found


class BoardLabelCheck(unittest.TestCase):
    def setUp(self):
        self.stack = self.enterContext(contextlib.ExitStack())

    def test_a_lane_label_baked_into_copy_is_caught(self):
        tree = Tree(self.stack, surface='const A = "Use the module connected to W1.";\n')
        found = tree.findings()
        self.assertEqual(1, len(found), found)
        self.assertIn('"W1" is what one board prints for enable_widget', found[0])

    def test_the_other_board_label_for_the_same_component_is_caught_too(self):
        # Neither board's legend belongs in a string: each is false on the other.
        tree = Tree(self.stack, surface='const A = "Wire it to PAD 7.";\n')
        self.assertEqual(1, len(tree.findings()))

    def test_an_output_label_is_caught(self):
        tree = Tree(self.stack, surface='const A = "Plug the servo into G1.";\n')
        found = tree.findings()
        self.assertEqual(1, len(found), found)
        self.assertIn("an Output", found[0])

    def test_a_label_that_is_neither_an_output_nor_a_lane_is_left_alone(self):
        # enable_rc_ch1 is in the inventory but in neither manifest: a receiver's
        # channel 1 is channel 1 on every board, and flagging "RC CH1" is the
        # cry-wolf finding that gets a check muted.
        tree = Tree(self.stack, surface='const A = "Bind it to RC CH1.";\n')
        self.assertEqual([], tree.findings())

    def test_a_registry_display_name_is_caught(self):
        dirty = REGISTRY.replace("display_name: Widget", 'display_name: "W1 - Widget"')
        found = Tree(self.stack, registry=dirty).findings()
        self.assertEqual(1, len(found), found)
        self.assertIn("registry display_name", found[0])

    def test_another_products_own_legend_is_not_this_boards(self):
        # The Sabertooth's terminal block is printed S1 S2 0V 5V. It is drawn in
        # an SVG <symbol> in data/asset-sets/legacy/_product_art.html and collides
        # exactly with the Artoo PCB's serial-lane legend without being the same
        # fact. Outside the symbol the same words are a finding.
        drawn = "<svg><symbol id=\"art-x\"><text>W1 G1 0V</text></symbol></svg>\n"
        self.assertEqual([], Tree(self.stack, html=drawn).findings())
        loose = "<p>W1</p>\n"
        self.assertEqual(1, len(Tree(self.stack, html=loose).findings()))

    def test_the_lane_manifests_header_comment_is_not_read_as_a_lane(self):
        # `Define PA_BOARD_LANE(name, ...)` in the header once produced a lane
        # called `name` and a finding about an unlabelled `enable_name`.
        named = board.board_named_components(
            Tree(self.stack).outputs, Tree(self.stack).lanes, []
        )
        self.assertEqual({"enable_gizmo", "enable_widget"}, set(named))

    def test_an_output_with_no_label_is_reported(self):
        # "A board with an unlabelled Output has nothing to call it by."
        stripped = "\n".join(
            line for line in LABELS.splitlines() if "enable_gizmo" not in line
        ) + "\n"
        found = Tree(self.stack, labels=stripped).findings()
        self.assertEqual(1, len(found), found)
        self.assertIn("declares no label for enable_gizmo", found[0])


class RealTree(unittest.TestCase):
    def test_the_repository_passes(self):
        out, err = io.StringIO(), io.StringIO()
        with redirect_stdout(out), redirect_stderr(err):
            status = board.main()
        self.assertEqual(0, status, err.getvalue())
        self.assertIn("Board label check passed", out.getvalue())

    def test_the_pin_half_is_the_one_tool_367_built(self):
        # The criterion is that this ticket adds no second pin comparison. What
        # proves it is that the findings ARE check_pin_drift's: swap its check()
        # for one that reports, and this tool reports it.
        import check_pin_drift

        original = check_pin_drift.check
        try:
            check_pin_drift.check = lambda *a, **k: check_pin_drift.Report(
                errors=["fixture: a pin the document does not state"]
            )
            found, _, _ = board.check()
        finally:
            check_pin_drift.check = original
        self.assertIn("fixture: a pin the document does not state", found)


if __name__ == "__main__":
    unittest.main()
