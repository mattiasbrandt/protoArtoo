"""Wiring's product cards: what the generator refuses and what the drift check catches (#458).

The cards are prose a builder wires hardware from, so the two failures worth a
test are a stale card on the screen and a card that says something its sheet
does not prove. Every case runs against a scratch tree: the generator and the check
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

PRODUCT = """
  - id: "{id}"
    status: {status}
    sheet: {sheet}
    wiring_card:
      supply: "DC 5 V"
      draw: "UNKNOWN"
      logic: "{logic}"
      wires:
        - {{ from: "TX", to: "the board's sound RX", note: "crossed" }}
      hazards:
        - "Neither pad is ground."
      source: "{source}"
"""

SHEET = """# A sheet

## 5. Electrical

```ini
# 9. not a section
```

### 5.2 Supply
"""


class Scratch:
    """A products file, a sheets directory and the two outputs, in a temp tree."""

    def __init__(self, stack, **products):
        self.dir = Path(stack.enter_context(tempfile.TemporaryDirectory()))
        self.sheets = self.dir / "spec-sheets"
        self.sheets.mkdir()
        (self.sheets / "sound.md").write_text(SHEET, encoding="utf-8")
        self.products = self.dir / "products.yaml"
        self.cards = self.dir / "default" / "_wiring_cards.html"
        self.cards.parent.mkdir()
        self.empty = self.dir / "_wiring_cards.html"
        self.write(**products)
        for attribute, value in (
            ("PRODUCTS_PATH", self.products),
            ("SHEETS_DIR", self.sheets),
            ("CARDS_OUTPUT_PATH", self.cards),
            ("EMPTY_OUTPUT_PATH", self.empty),
        ):
            stack.enter_context(unittest.mock.patch.object(gen, attribute, value))

    def write(self, **products):
        """One product per keyword, each a dict of the fields to change."""
        entries = []
        for fields in products.values():
            values = {"id": "dy_sv5w", "status": "supported", "sheet": "sound.md",
                      "logic": "3.3 V", "source": "5.2", **fields}
            entries.append(PRODUCT.format(**values))
        self.products.write_text("products:" + "".join(entries), encoding="utf-8")

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

    def test_a_card_edited_in_the_products_file_is_reported_twice_and_never_repaired(self):
        scratch = Scratch(self.stack, sound={})
        gen.generate(quiet=True)
        self.assertEqual(scratch.findings(), [])

        scratch.write(sound={"logic": "5 V"})
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
            ({"source": "9"}, "no section of its sheet"),
            ({"source": "5.2, 7.1"}, "'7.1'"),
            ({"sheet": "dy-sv6w.md"}, "is no file in"),
            ({"sheet": "null"}, "is no file in"),
            ({"logic": "the lead is driven at 3.3 V"}, "a wire is a wire"),
        ):
            with self.subTest(fields), contextlib.ExitStack() as stack:
                Scratch(stack, sound=fields)
                with self.assertRaises(gen.CardError) as refused:
                    gen.generate(quiet=True)
                self.assertIn(said, str(refused.exception))

    def test_a_sheet_saved_with_crlf_line_endings_still_yields_its_card(self):
        scratch = Scratch(self.stack, sound={})
        sheet = scratch.sheets / "sound.md"
        sheet.write_bytes(sheet.read_bytes().replace(b"\n", b"\r\n"))
        gen.generate(quiet=True)
        self.assertEqual(list(scratch.payload()), ["dy_sv5w"])

    def test_a_roadmap_products_card_is_not_generated(self):
        scratch = Scratch(self.stack, sound={}, dfplayer={"id": "dfplayer_mini", "status": "roadmap"})
        gen.generate(quiet=True)
        self.assertEqual(list(scratch.payload()), ["dy_sv5w"])


if __name__ == "__main__":
    unittest.main()
