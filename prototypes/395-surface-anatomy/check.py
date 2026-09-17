#!/usr/bin/env python3
"""#398 - static checks for the mockups. No dependencies.

    python3 prototypes/395-surface-anatomy/check.py

Fails (exit 1) on:
  - a colour literal (#rgb, #rrggbb, rgb(), hsl()) in anatomy.css outside the
    :root block - #327's rule, the one that makes "tokens once" true;
  - an emoji or other pictographic character in any .html, .js or .css file
    here (ADR 0066: no emoji on surfaces);
  - a mockup that references an icon symbol chrome.js does not define;
  - a product card whose picture does not exist on one of the two boards: the
    drawing must be a symbol the page defines (the legacy set) and the
    photograph must be a file in the default set, since the card falls through
    from one to the other and a missing picture is invisible until the switch
    is flipped;
  - a `data-board-only` block naming a board board.js does not have.
Prints what it measured either way.
"""
import pathlib
import re
import sys

HERE = pathlib.Path(__file__).resolve().parent
PAGES = ["dashboard.html", "parts.html", "setup.html"]
COLOUR = re.compile(r"#[0-9a-fA-F]{3,8}\b|\brgba?\(|\bhsla?\(")
PICTO = re.compile(r"[\U0001F000-\U0001FAFF☀-➿⬀-⯿️]")

failures = []

css = (HERE / "anatomy.css").read_text()
# Strip comments, then split off the :root blocks (screen and print).
body = re.sub(r"/\*.*?\*/", "", css, flags=re.S)
roots = re.findall(r":root\s*\{[^}]*\}", body)
outside = body
for block in roots:
    outside = outside.replace(block, "", 1)
literals = [m.group(0) for m in COLOUR.finditer(outside)]
print(f"anatomy.css: {len(roots)} :root blocks, {len(literals)} colour literals outside them")
if literals:
    failures.append(f"colour literals outside :root: {literals}")

icons = set(re.findall(r'"([a-z0-9-]+)":\s*"M', (HERE / "chrome.js").read_text()))
print(f"chrome.js: {len(icons)} icon symbols")

for name in sorted(p.name for p in HERE.iterdir() if p.suffix in {".html", ".js", ".css"}):
    text = (HERE / name).read_text()
    picto = PICTO.findall(text)
    if picto:
        failures.append(f"{name}: pictographic characters {sorted(set(picto))}")
    used = set(re.findall(r'href="#i-([a-z0-9-]+)"', text))
    missing = used - icons
    if missing:
        failures.append(f"{name}: icons not in chrome.js: {sorted(missing)}")
    print(f"{name}: {len(picto)} pictographs, {len(used)} icon uses, {len(missing)} missing")

# --- the two boards, and the pictures each of them has -----------------------
# The boards are read out of board.js rather than restated here: one list.
board_js = (HERE / "board.js").read_text()
boards = dict(
    (name, {"set": asset_set, "product": product})
    for name, asset_set, product in re.findall(
        r'(\w+): \{\s*assetSet: "(\w+)",\s*label: "[^"]*",\s*productId: "(\w+)"', board_js
    )
)
photo_root = re.search(r'PHOTO_ROOT = "([^"]+)"', board_js).group(1)
photo_dir = (HERE / photo_root).resolve()
print(f"board.js: {len(boards)} boards - " + ", ".join(f"{n} ({b['set']})" for n, b in sorted(boards.items())))
if len(boards) != 2 or {b["set"] for b in boards.values()} != {"legacy", "default"}:
    failures.append(f"board.js: expected one legacy board and one default board, read {boards}")

for name in PAGES:
    text = (HERE / name).read_text()
    slots = re.findall(r'data-product="([a-z0-9_]+)"', text)
    if not slots:
        continue
    symbols = set(re.findall(r'<symbol id="art-([a-z0-9_]+)"', text))
    # A card marked `board` draws whichever Body Controller the image carries,
    # so it is the legacy board's product that needs a drawing and the default
    # board's that needs a photograph.
    drawn = {s for s in slots if s != "board"}
    photographed = set(drawn)
    for board in boards.values():
        if "board" not in slots:
            continue
        (drawn if board["set"] == "legacy" else photographed).add(board["product"])
    missing_art = sorted(drawn - symbols)
    missing_photo = sorted(p for p in photographed if not (photo_dir / f"{p}.webp").is_file())
    print(f"{name}: {len(slots)} product cards, {len(drawn)} drawings, "
          f"{len(photographed)} photographs, {len(missing_art) + len(missing_photo)} missing")
    if missing_art:
        failures.append(f"{name}: no drawing in this page for {missing_art}")
    if missing_photo:
        failures.append(f"{name}: no photograph in {photo_root} for {missing_photo}")

only = [(name, b) for name in PAGES
        for b in re.findall(r'data-board-only="([a-z0-9_]+)"', (HERE / name).read_text())]
unknown = sorted({b for _, b in only} - set(boards))
print(f"pages: {len(only)} board-only blocks, {len(unknown)} naming a board board.js does not have")
if unknown:
    failures.append(f"data-board-only names unknown boards: {unknown}")

if failures:
    print("FAIL")
    for f in failures:
        print(" -", f)
    sys.exit(1)
print("PASS")
