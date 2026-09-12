#!/usr/bin/env python3
"""Product drawings: the legacy set's answer to the default set's photographs.

ADR 0065 -- only what is SHOWN may differ between boards, so a Component Picker
card carries a drawing on one set and a photograph on the other and lays out the
same either way. That only holds while the two sets name the same products, and
nothing in the build would notice if they stopped: a missing drawing is a blank
card on the one board that cannot fall back to a photograph, on the surface an
operator selects hardware by.

These tests are that guard. They compare the sprite's symbols against the
Component Registry itself rather than against the photographs, so a product
added to the registry is caught before either picture exists.
"""

import re
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
REGISTRY = ROOT / "include" / "component_registry.inc"
LEGACY_ART = ROOT / "data" / "asset-sets" / "legacy" / "_product_art.html"
DEFAULT_ART = ROOT / "data" / "asset-sets" / "default" / "_product_art.html"

ART_PREFIX = "art-"

# A product the picker shows without a picture in either set. The registry row
# is the controller's own pins rather than something a builder buys, so there is
# no product to photograph and none to draw; the card is textual on both sets,
# which is the same card on both and not the gap ADR 0065 is about.
NO_PICTURE = {"esp32_gpio_ledc"}

PART_RE = re.compile(r"^PA_COMPONENT_PART\(\s*[A-Za-z0-9_]+\s*,\s*\"([^\"]+)\"", re.M)
SYMBOL_RE = re.compile(r'<symbol id="([^"]+)"')
VIEWBOX_RE = re.compile(r'<symbol id="[^"]+" viewBox="([^"]+)"')


def _registry_products():
    return set(PART_RE.findall(REGISTRY.read_text(encoding="utf-8")))


def _drawn_products():
    return {
        sid[len(ART_PREFIX):]
        for sid in SYMBOL_RE.findall(LEGACY_ART.read_text(encoding="utf-8"))
    }


class ProductDrawings(unittest.TestCase):
    def test_the_registry_parses(self):
        # A silent zero here would make every other test in this file pass.
        self.assertGreater(len(_registry_products()), 10)

    def test_every_product_the_picker_shows_has_a_drawing(self):
        missing = _registry_products() - _drawn_products() - NO_PICTURE
        self.assertEqual(
            set(),
            missing,
            "no line drawing for %s: the legacy set would show a blank card, and "
            "that board has no photograph to fall back to (ADR 0065)"
            % ", ".join(sorted(missing)),
        )

    def test_every_drawing_names_a_product(self):
        stray = _drawn_products() - _registry_products()
        self.assertEqual(
            set(),
            stray,
            "%s is drawn but is not a Component Registry product: either the "
            "product was renamed and the symbol was not, or the drawing is dead "
            "weight in the image" % ", ".join(sorted(stray)),
        )

    def test_the_two_sets_offer_the_same_products(self):
        # The picker asks the document for a drawing and falls back to
        # /<id>.webp, so a product drawn on legacy and unphotographed on default
        # is a card that differs between boards by more than what is shown.
        photographed = {
            p.stem for p in (ROOT / "data" / "asset-sets" / "default").glob("*.webp")
        }
        self.assertEqual(
            _drawn_products(),
            photographed,
            "the sets do not carry the same products: drawn only %s, "
            "photographed only %s"
            % (
                sorted(_drawn_products() - photographed),
                sorted(photographed - _drawn_products()),
            ),
        )

    def test_every_drawing_is_drawn_to_the_same_frame(self):
        # One hand across the set is the point of drawing them rather than
        # sourcing them, and a card lays a drawing out like a photograph.
        frames = set(VIEWBOX_RE.findall(LEGACY_ART.read_text(encoding="utf-8")))
        self.assertEqual({"0 0 400 300"}, frames)

    def test_a_drawing_carries_no_colour_of_its_own(self):
        # Inline is the only form in which the art follows the page's colour;
        # a literal hex would freeze one theme into the drawing.
        art = LEGACY_ART.read_text(encoding="utf-8")
        body = art[art.index("<svg"):]
        literals = re.findall(r'(?:fill|stroke)="(#[0-9a-fA-F]{3,8})"', body)
        self.assertEqual(
            [],
            literals,
            "hard-coded colour in the sprite: %s. Strokes are currentColor; the "
            "only literal allowed is the fallback in var(--pa-art-bg,...), which "
            "is a background, not ink." % literals,
        )

    def test_both_sets_carry_the_partial(self):
        # tools/gzip_fsdata.py resolves an include from the active set first and
        # refuses the build when it resolves nowhere, so a page may only name
        # this include while every set answers to it.
        self.assertTrue(LEGACY_ART.is_file())
        self.assertTrue(DEFAULT_ART.is_file())
        self.assertEqual([], SYMBOL_RE.findall(DEFAULT_ART.read_text(encoding="utf-8")))

    def test_each_partial_can_be_inlined(self):
        # A partial is text spliced into a page. HTML comments do not nest, so a
        # comment that quotes an include directive closes early and leaks the
        # rest of itself onto the page -- and tools/gzip_fsdata.py reads that
        # quoted directive as a real nested include and refuses the build. Both
        # wait until a page includes the partial, which nothing does yet, so
        # they are caught here rather than on the day the picker ships.
        directive = re.compile(r"<!--\s*PA:INCLUDE\s+[A-Za-z0-9_.\-/]+\s*-->")
        for path in (LEGACY_ART, DEFAULT_ART):
            text = path.read_text(encoding="utf-8")
            with self.subTest(partial=str(path.relative_to(ROOT))):
                self.assertIsNone(
                    directive.search(text),
                    "a quoted include directive is a nested include to the build",
                )
                outside = re.sub(r"<!--.*?-->", "", text, flags=re.S)
                self.assertNotIn("-->", outside, "a comment closed early and leaks onto the page")
                self.assertNotIn("<!--", outside, "a comment never closes")


if __name__ == "__main__":
    unittest.main()
