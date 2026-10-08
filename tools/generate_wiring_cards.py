#!/usr/bin/env python3
"""Generate Wiring's product wiring cards from docs/products.yaml.

A product in docs/products.yaml may carry one short, fixed-shape `wiring_card`:
how to wire and power that product, every line proven by a section of the spec
sheet its `sheet` names. This turns those cards into the one partial the
Wiring page includes (#458). Neither file is read at run: the cards reach the
image by being generated into it, the catalog rule #301 set.

    docs/products.yaml  (each product's `wiring_card`, checked against its `sheet`)
       |-> data/asset-sets/default/_wiring_cards.html   the cards and their plate
       '-> data/_wiring_cards.html                      the same name, no cards

TWO OUTPUTS, AND THE SECOND IS EMPTY ON PURPOSE. The cards are reference
content, which an Asset Set may carry and another may not (ADR 0065, amended
2026-09-30): the default set ships them and the legacy set does not. A page
names the partial once, and tools/gzip_fsdata.py resolves it from the build's
set and then from the common data root, refusing the build when it finds it in
neither. So the common root carries a partial of the same name holding nothing,
the way data/asset-sets/default/_product_art.html is the empty counterpart to
the legacy set's drawings. The page then finds cards in its own document or
finds none; it never asks which board it is on. Both files are this generator's,
so the empty one cannot quietly gain markup.

WHAT A CARD IS, AND WHAT IT IS NOT. The Component Registry id is the key
(the product's `id`), and only a `supported` product's card is generated: a
roadmap product cannot be fitted, so Wiring could never show its card and the
image would pay for text nobody reads. A card carries no pin of
any board. Pins are per board and a card is per product, so the page puts the
running firmware's own answer beside each card (data/wiring.js).

This generator refuses, each because the alternative ships something wrong:

  - two products with one id: the page finds a card by it;
  - a card whose product names no `sheet`, or a sheet that is not there;
  - a missing or unknown field, or a value that is not a quoted string: YAML
    reads a bare `5` or `off` as a number or a boolean, and a card is text;
  - a `source` naming a section its sheet does not have. Every line of a
    card is proven by a section, so a citation of nothing is a guess;
  - "lead", "driven" or "drives" (GLOSSARY.md "Wiring" _Avoid_), and anything
    outside ASCII, which is where a pictograph would arrive from.

A value the sheet does not know is written `UNKNOWN` on the card, never filled.

Staleness is the one thing this cannot catch, because it is what happens when
nobody runs it. tools/check_wiring_cards_drift.py runs this generator with its
writes intercepted and byte-compares, and the slice gate carries that check.
"""

from __future__ import annotations

import argparse
import json
import re
import sys
from pathlib import Path

import yaml

ROOT = Path(__file__).resolve().parent.parent


def rel(path):
    """Repo-relative where the path is in the tree, plain otherwise."""
    try:
        return str(Path(path).resolve().relative_to(ROOT))
    except ValueError:
        return str(path)


PRODUCTS_PATH = ROOT / "docs" / "products.yaml"
SHEETS_DIR = ROOT / "docs" / "spec-sheets"
CARDS_OUTPUT_PATH = ROOT / "data" / "asset-sets" / "default" / "_wiring_cards.html"
EMPTY_OUTPUT_PATH = ROOT / "data" / "_wiring_cards.html"
GENERATOR_NAME = rel(__file__)

# The id the page finds the cards by, and the ids of the plate it draws them
# on (data/wiring.js). Written here and nowhere in data/wiring.html: a page
# built without the cards has no plate either.
CARDS_ELEMENT_ID = "wiring-product-cards"

FIELDS = ("supply", "draw", "logic", "wires", "hazards", "source")
WIRE_FIELDS = ("from", "to", "note")

FENCE_RE = re.compile(r"^```.*?^```\s*$", re.M | re.S)
HEADING_RE = re.compile(r"^#{1,6} +(.+?)\s*$", re.M)
SECTION_NUMBER_RE = re.compile(r"\d+(\.\d+)*")
SUPPORTED = "supported"

# GLOSSARY.md "Wiring" _Avoid_: a wire is a wire, and nothing is "driven".
AVOIDED_WORDS = re.compile(r"\b(leads?|driven|drives)\b", re.I)


class CardError(Exception):
    """One or more cards cannot be generated; `problems` says why, one per line."""

    def __init__(self, problems):
        super().__init__("\n".join(problems))
        self.problems = problems


def sheet_headings(text):
    """Every heading of a sheet, with the fenced blocks taken out first: a `#`
    line inside one is a comment in somebody's config file, not a section."""
    return HEADING_RE.findall(FENCE_RE.sub("", text))


def cites(headings, token):
    """Whether `token` names one of the sheet's sections: by its number
    (`5.2`, or `6` for `6. Getting the wire to work`) or by its whole title.
    A title is matched whole, so `Wiring` cites a section called Wiring and
    not one whose title only begins with that word."""
    if SECTION_NUMBER_RE.fullmatch(token):
        return any(heading.startswith(token + " ") or heading.startswith(token + ". ")
                   for heading in headings)
    return token in headings


def _text(where, key, value, problems):
    if not isinstance(value, str) or not value.strip():
        problems.append(f"{where}: `{key}` must be a quoted, non-empty string")
        return ""
    if not value.isascii():
        problems.append(f"{where}: `{key}` carries a character outside ASCII: {value!r}")
    avoided = AVOIDED_WORDS.search(value)
    if avoided:
        problems.append(
            f"{where}: `{key}` says {avoided.group(0)!r}; a wire is a wire and nothing "
            f'is "driven" (GLOSSARY.md "Wiring")'
        )
    return value.strip()


def read_card(where, card, headings, problems):
    """One card as the page gets it, or None when it cannot be generated."""
    before = len(problems)
    if not isinstance(card, dict):
        problems.append(f"{where}: `wiring_card` holds no mapping")
        return None
    for key in card:
        if key not in FIELDS:
            problems.append(f"{where}: unknown field `{key}`")
    for key in FIELDS:
        if key not in card:
            problems.append(f"{where}: missing field `{key}`")
    if len(problems) > before:
        return None

    out = {}
    for key in ("supply", "draw", "logic"):
        out[key] = _text(where, key, card[key], problems)

    wires = card["wires"]
    if not isinstance(wires, list):
        problems.append(f"{where}: `wires` must be a list")
        wires = []
    out["wires"] = []
    for position, wire in enumerate(wires):
        at = f"wires[{position}]"
        if not isinstance(wire, dict) or any(key not in WIRE_FIELDS for key in wire):
            problems.append(f"{where}: `{at}` must be a mapping of from, to and an optional note")
            continue
        row = {key: _text(where, f"{at}.{key}", wire.get(key), problems) for key in ("from", "to")}
        if "note" in wire:
            row["note"] = _text(where, f"{at}.note", wire["note"], problems)
        out["wires"].append(row)

    hazards = card["hazards"]
    if not isinstance(hazards, list):
        problems.append(f"{where}: `hazards` must be a list")
        hazards = []
    out["hazards"] = [
        _text(where, f"hazards[{position}]", hazard, problems)
        for position, hazard in enumerate(hazards)
    ]

    # The citation stays out of the image: it is what a maintainer checks a
    # line against, and a builder at the bench has no sheet to open.
    source = _text(where, "source", card["source"], problems)
    for token in (part.strip() for part in source.split(",")):
        if token and not cites(headings, token):
            problems.append(f"{where}: `source` cites {token!r}, which is no section of its sheet")
    return None if len(problems) > before else out


def load_cards(products_path=None, sheets_dir=None):
    """Every generated card, keyed by registry id, in the products' order."""
    products_path = products_path or PRODUCTS_PATH
    sheets_dir = sheets_dir or SHEETS_DIR
    document = yaml.safe_load(products_path.read_text(encoding="utf-8"))
    products = document.get("products") if isinstance(document, dict) else None
    if not isinstance(products, list):
        raise CardError([f"{rel(products_path)}: holds no `products` list"])
    problems = []
    cards = {}
    seen = set()
    for position, product in enumerate(products):
        if not isinstance(product, dict):
            problems.append(f"{rel(products_path)}: products[{position}] is no mapping")
            continue
        product_id = product.get("id")
        where = f"{rel(products_path)} ({product_id})"
        if product_id in seen:
            problems.append(f"{where}: a second product with this id")
            continue
        seen.add(product_id)
        if "wiring_card" not in product:
            continue
        sheet_name = product.get("sheet")
        sheet = sheets_dir / str(sheet_name)
        if not sheet_name or not sheet.is_file():
            problems.append(
                f"{where}: a wiring card is proven by its product's `sheet`, and "
                f"{sheet_name!r} is no file in {rel(sheets_dir)}/"
            )
            continue
        where = f"{where} wiring_card, against {rel(sheet)}"
        headings = sheet_headings(sheet.read_text(encoding="utf-8"))
        card = read_card(where, product["wiring_card"], headings, problems)
        if card is not None and product.get("status") == SUPPORTED:
            cards[product_id] = card
    if problems:
        raise CardError(problems)
    return cards


STAMP = "DO NOT EDIT MANUALLY"

CARDS_HEADER = f"""<!--
  {STAMP}. Generated by {GENERATOR_NAME} from the
  wiring cards in {rel(PRODUCTS_PATH)}. Edit a product's card there, then run
  the generator; tools/check_wiring_cards_drift.py fails while the two disagree.

  Wiring's product wiring cards (#458): how to wire and power each product, by
  its Component Registry id. Reference content, which this set carries and the
  legacy set does not (ADR 0065, amended 2026-09-30). The plate is here and not
  in data/wiring.html, so a build without the cards has no plate either.
  data/wiring.js draws a card for each fitted product and puts the running
  board's own pins beside it; no card names a pin.
-->
"""

EMPTY_PARTIAL = f"""<!--
  {STAMP}. Generated by {GENERATOR_NAME}.

  The common root's counterpart to the default set's product wiring cards
  (#458, ADR 0065 amended 2026-09-30). It is deliberately empty: a set that
  carries no cards resolves the include here, and Wiring then finds no cards in
  its document and draws no section for them.

  It exists because tools/gzip_fsdata.py resolves an included partial from the
  active set and then the common data root, and refuses the build when it finds
  it in neither. A partial is not imaged, so this file costs the image nothing.
-->
"""

# The plate the cards are drawn on. Its heading and its count are written by
# data/wiring.js, which is their one home: the saved bench copy prints them too.
PLATE = (
    '<div class="card wiring-products-card hidden" id="wiring-products-card">\n'
    '  <div class="sect">\n'
    '    <h2 id="wiring-products-heading"></h2>\n'
    '    <span class="sub" id="wiring-products-summary" role="status" aria-live="polite"></span>\n'
    "  </div>\n"
    '  <div class="wcards" id="wiring-products"></div>\n'
    "</div>\n"
)


def cards_partial(cards):
    """The default set's partial: the plate, then the cards as one JSON object.

    The object sits in a script element of a type no browser runs, read by the
    page with JSON.parse. `<`, `>` and `&` are written as escapes so no card
    text can close the element or be taken for markup by the staging scanner.
    """
    payload = json.dumps(cards, ensure_ascii=True, separators=(",", ":"))
    payload = payload.replace("<", "\\u003c").replace(">", "\\u003e").replace("&", "\\u0026")
    return (
        CARDS_HEADER
        + PLATE
        + f'<script type="application/json" id="{CARDS_ELEMENT_ID}">{payload}</script>\n'
    )


def generate(quiet=False, products_path=None, sheets_dir=None, cards_path=None, empty_path=None):
    """Write both partials and return the cards. Raises CardError on a bad card."""
    cards = load_cards(products_path, sheets_dir)
    cards_path = cards_path or CARDS_OUTPUT_PATH
    empty_path = empty_path or EMPTY_OUTPUT_PATH
    cards_path.write_text(cards_partial(cards), encoding="utf-8")
    empty_path.write_text(EMPTY_PARTIAL, encoding="utf-8")
    if not quiet:
        print(f"Generated {rel(cards_path)} ({len(cards)} cards: {', '.join(cards)})")
        print(f"Generated {rel(empty_path)} (no cards)")
    return cards


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--quiet", action="store_true", help="print nothing on success")
    args = parser.parse_args(argv)
    try:
        generate(quiet=args.quiet)
    except CardError as error:
        print("Wiring cards cannot be generated:", file=sys.stderr)
        for problem in error.problems:
            print(f"  - {problem}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
