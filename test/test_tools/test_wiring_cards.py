"""Wiring's product cards: what the generator refuses and what the drift check catches (#458).

The cards are prose a builder wires hardware from, so the two failures worth a
test are a stale card on the screen and a card that says something no sheet
proves. Every case runs against a scratch tree: the generator and the check
address every file through the generator's own path constants, so patching
those aims both somewhere else and keeps a test that goes wrong off the
committed partials.
"""

import contextlib
import json
import re
import sys
import tempfile
import unittest
import unittest.mock
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))

import check_wiring_cards_drift as check  # noqa: E402
import generate_wiring_cards as gen  # noqa: E402

REGISTRY = """
PA_COMPONENT_PART(18, "dy_sv5w", "DY-SV5W", COMPONENT_CATEGORY_SOUND, "soft_uart_binary", COMPONENT_STATUS_SUPPORTED,
                  0,
                  nullptr, 1)
PA_COMPONENT_PART(21, "dfplayer_mini", "DFPlayer Mini", COMPONENT_CATEGORY_SOUND, "dfplayer_serial", COMPONENT_STATUS_ROADMAP, 0, nullptr, 0)
"""

SHEET = """# A sheet

```yaml
wiring_card:
  id: "{id}"
  supply: "DC 5 V"
  draw: "UNKNOWN"
  logic: "{logic}"
  wires:
    - {{ from: "TX", to: "the board's sound RX", note: "crossed" }}
  hazards:
    - "Neither pad is ground."
  source: "{source}"
```

## 5. Electrical

```ini
# 9. not a section
```

### 5.2 Supply
"""


class Scratch:
    """A sheets directory, a registry and the two outputs, in a temp tree."""

    def __init__(self, stack, **sheets):
        self.dir = Path(stack.enter_context(tempfile.TemporaryDirectory()))
        self.sheets = self.dir / "spec-sheets"
        self.sheets.mkdir()
        self.registry = self.dir / "component_registry.inc"
        self.registry.write_text(REGISTRY, encoding="utf-8")
        self.cards = self.dir / "default" / "_wiring_cards.html"
        self.cards.parent.mkdir()
        self.empty = self.dir / "_wiring_cards.html"
        for name, fields in sheets.items():
            self.write(name, **fields)
        for attribute, value in (
            ("SHEETS_DIR", self.sheets),
            ("REGISTRY_PATH", self.registry),
            ("CARDS_OUTPUT_PATH", self.cards),
            ("EMPTY_OUTPUT_PATH", self.empty),
        ):
            stack.enter_context(unittest.mock.patch.object(gen, attribute, value))

    def write(self, name, id="dy_sv5w", logic="3.3 V", source="5.2"):
        (self.sheets / f"{name}.md").write_text(
            SHEET.format(id=id, logic=logic, source=source), encoding="utf-8"
        )

    def payload(self):
        text = self.cards.read_text(encoding="utf-8")
        return json.loads(re.search(r"<script[^>]*>(.*)</script>", text).group(1))

    def findings(self):
        errors = []
        check.check(errors)
        return errors


class WiringCards(unittest.TestCase):
    def setUp(self):
        self.stack = contextlib.ExitStack()
        self.addCleanup(self.stack.close)

    def test_a_card_edited_in_its_sheet_is_reported_twice_and_never_repaired(self):
        scratch = Scratch(self.stack, sound={})
        gen.generate(quiet=True)
        self.assertEqual(scratch.findings(), [])

        scratch.write("sound", logic="5 V")
        stale = scratch.cards.read_bytes()
        for _ in range(2):
            findings = scratch.findings()
            self.assertEqual(len(findings), 1, findings)
            self.assertIn("regenerate it", findings[0])
        self.assertEqual(scratch.cards.read_bytes(), stale, "the check wrote the partial it was checking")

    def test_the_common_partial_carries_no_card(self):
        scratch = Scratch(self.stack, sound={})
        gen.generate(quiet=True)
        self.assertEqual(scratch.payload()["dy_sv5w"]["logic"], "3.3 V")
        empty = scratch.empty.read_text(encoding="utf-8")
        self.assertEqual(re.sub(r"<!--.*?-->", "", empty, flags=re.S).strip(), "",
                         "a set without the cards resolves this file, so markup here is imaged everywhere")
        # And markup added to it by hand is drift, not a second source.
        scratch.empty.write_text(empty + "<p>DC 5 V</p>\n", encoding="utf-8")
        self.assertEqual(len(scratch.findings()), 1)

    def test_a_card_no_sheet_section_proves_is_refused(self):
        for fields, said in (
            ({"source": "9"}, "no section of this sheet"),
            ({"source": "5.2, 7.1"}, "'7.1'"),
            ({"id": "dy_sv6w"}, "is no row of"),
            ({"logic": "the lead is driven at 3.3 V"}, "a wire is a wire"),
        ):
            with self.subTest(fields), contextlib.ExitStack() as stack:
                Scratch(stack, sound=fields)
                with self.assertRaises(gen.CardError) as refused:
                    gen.generate(quiet=True)
                self.assertIn(said, str(refused.exception))

    def test_a_roadmap_products_card_is_not_generated(self):
        scratch = Scratch(self.stack, sound={}, dfplayer={"id": "dfplayer_mini"})
        gen.generate(quiet=True)
        self.assertEqual(list(scratch.payload()), ["dy_sv5w"])


if __name__ == "__main__":
    unittest.main()
