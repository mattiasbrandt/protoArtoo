#!/usr/bin/env python3
"""Check that the operator surfaces hold the two mechanical rules of the Surface Anatomy.

ADR 0066 decided that every operator surface is drawn to one anatomy, in one
identity, and #399 sweeps the thirteen of them onto it. Most of that decision is
a matter of judgement and is checked by reading. Two halves of it are not, and
this is them - reported, never rewritten, the convention
`tools/check_action_registry_drift.py` set.

1. **No pictograph on an operator surface.** ADR 0066 retired the rule that
   preferred an emoji to a verbose label: a heading, a nav entry or a card is
   text plus, where a glyph earns its place, an icon from the project's own SVG
   sprite that inherits the text colour and keeps its label alongside. An emoji
   is none of those - it is a coloured picture the operator cannot restyle, that
   renders differently on every machine, and that carried no meaning the word
   beside it did not already have.

2. **Every icon reference resolves.** An icon is a `<use href="#i-name">`
   against the inline sprite `data/shell.js` injects with the chrome. A name
   that is not in that sprite renders as nothing at all - no error, no console
   warning, an empty box where a glyph should be - which is exactly the class of
   defect a check catches cheaply and a person never does.

WHAT THIS CHECK DELIBERATELY DOES NOT DO. The third rule of the token layer -
no colour literal outside `:root` - is not here. It needs the stylesheet parsed
and every value resolved through `:root` before it can tell "the rule contains
the string --warning" from "this rule paints amber", and
`test/test_web/test_style_token_layer.js` already does exactly that against the
real cascade. A second, weaker regex copy of a check that already exists is not
a second guard; it is a second thing to keep in step.

THE PENDING LIST. #399 sweeps thirteen surfaces in four slices, each merged
before the next starts, so between the first merge and the last there are
surfaces this check would fail that nobody has reached yet. Each is listed in
PENDING below with the slice that clears it, and the list is self-retiring: a
file listed here that has NO pictograph left fails the check just as loudly as
an unlisted file that has one. The list can therefore only shrink, and the slice
that sweeps a surface deletes its row in the same change.

Run it as `make check-surface-anatomy`. Its own unit tests, which drive each
check against fixtures and prove it can fail, are
`test/test_tools/test_surface_anatomy.py`.
"""

from __future__ import annotations

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
DATA = ROOT / "data"
SHELL = DATA / "shell.js"

# Everything the controller serves that a person reads or that paints what they
# read. Generated catalogs are included on purpose: droid_parts.js is operator
# vocabulary and an emoji would reach a surface through it.
SUFFIXES = {".html", ".js", ".css", ".txt"}

# Emoji and the pictographic neighbours that read as one: the Miscellaneous
# Symbols and Dingbats blocks, the supplemental arrows and symbols, and the
# variation selector that turns an otherwise textual glyph into a picture.
PICTOGRAPH = re.compile(
    "["
    "\U0001F000-\U0001FAFF"   # emoji, symbols and pictographs
    "☀-➿"           # miscellaneous symbols and dingbats
    "⬀-⯿"           # miscellaneous symbols and arrows
    "️"                  # variation selector-16, the emoji presentation mark
    "]"
)

ICON_SYMBOL = re.compile(r'\n    "([a-z0-9-]+)": "M')
ICON_USE = re.compile(r'href="#i-([a-z0-9-]+)"')
# The chrome names its icons by field rather than by markup - the rail builds
# `href="#i-${surface.icon}"` - so the literal <use> regex above cannot see
# them. This is the other half of the same question.
SURFACE_ICON = re.compile(r'\{ page: "[a-z]+", doc: "[^"]+", icon: "([a-z0-9-]+)"')

# Surfaces #399 has not swept yet, and the slice that clears each one. A row
# here is a promise, not a suppression: the file is still checked, and it fails
# if it is CLEAN, so the row cannot outlive the sweep that makes it true.
PENDING = {
    "sound.html": "#399 slice 3 (the Drive and Perform group)",
    "sound.js": "#399 slice 3 (the Drive and Perform group)",
    "rc.html": "#399 slice 3 (the Drive and Perform group)",
    "rc.js": "#399 slice 3 (the Drive and Perform group)",
    "seq.html": "#399 slice 3 (the Drive and Perform group)",
    "seq.js": "#399 slice 3 (the Drive and Perform group)",
    "wifi.html": "#399 slice 4 (Maintain and the rest)",
    "wifi.js": "#399 slice 4 (Maintain and the rest)",
    "firmware.html": "#399 slice 4 (Maintain and the rest)",
}


def served_files(data: Path) -> list[Path]:
    """Every file under data/ the controller serves, asset sets included."""
    return sorted(path for path in data.rglob("*") if path.suffix in SUFFIXES and path.is_file())


def icon_symbols(shell: Path) -> set[str]:
    """The names the inline sprite defines, read from the one file that holds it."""
    return set(ICON_SYMBOL.findall(shell.read_text()))


def check_pictographs(files: list[Path], data: Path, errors: list[str]) -> tuple[int, int]:
    """No pictograph on a swept surface, and no stale row on the pending list."""
    swept = 0
    pending_still_dirty = 0
    for path in files:
        name = path.relative_to(data).as_posix()
        found = sorted(set(PICTOGRAPH.findall(path.read_text(encoding="utf-8", errors="replace"))))
        listed = PENDING.get(name)
        if listed is None:
            swept += 1
            if found:
                errors.append(
                    f"data/{name}: {len(found)} pictographic character(s) {found} - "
                    "an operator surface carries none (ADR 0066)"
                )
        elif found:
            pending_still_dirty += 1
        else:
            errors.append(
                f"data/{name} is on the pending list for {listed} but carries no pictograph: "
                "delete its row in tools/check_surface_anatomy.py"
            )
    return swept, pending_still_dirty


def check_icon_references(files: list[Path], data: Path, symbols: set[str], errors: list[str]) -> int:
    """Every <use href="#i-name"> names a symbol the sprite defines."""
    uses = 0
    for path in files:
        name = path.relative_to(data).as_posix()
        used = set(ICON_USE.findall(path.read_text(encoding="utf-8", errors="replace")))
        uses += len(used)
        missing = sorted(used - symbols)
        if missing:
            errors.append(
                f"data/{name}: icon(s) {missing} are not in the sprite data/shell.js injects - "
                "a <use> that resolves to nothing renders an empty box"
            )
    return uses


def check_surface_icons(shell: Path, symbols: set[str], errors: list[str]) -> int:
    """Every surface in the nav names an icon the sprite has.

    The rail is the one place a builder navigates from, and a surface whose
    icon name is not in the sprite draws a 16 px hole beside its label on every
    screen. Nothing else would notice: the markup is well formed and the
    browser says nothing.
    """
    named = SURFACE_ICON.findall(shell.read_text())
    if not named:
        errors.append("data/shell.js declares no surface icons - SURFACES is where the nav reads them")
        return 0
    missing = sorted(set(named) - symbols)
    if missing:
        errors.append(
            f"data/shell.js: surface icon(s) {missing} are not in its own sprite - "
            "the nav would draw an empty box beside that surface"
        )
    return len(named)


def main() -> int:
    errors: list[str] = []
    files = served_files(DATA)
    if not files:
        print("No served files found under data/", file=sys.stderr)
        return 1

    symbols = icon_symbols(SHELL)
    if not symbols:
        errors.append("data/shell.js defines no icon symbols - the sprite is the source of truth for them")

    swept, pending_dirty = check_pictographs(files, DATA, errors)
    uses = check_icon_references(files, DATA, symbols, errors)
    surfaces = check_surface_icons(SHELL, symbols, errors)

    # Both outcomes print what was measured, so a passing run is still evidence.
    print(
        f"Surface Anatomy check: {len(files)} served files, {swept} swept and free of pictographs, "
        f"{pending_dirty} still waiting for their slice, "
        f"{len(symbols)} icon symbols, {uses} icon references and {surfaces} surface icons, all resolved."
        if not errors
        else f"Surface Anatomy check: {len(files)} served files, {len(symbols)} icon symbols, "
        f"{uses} icon references, {surfaces} surface icons."
    )

    if errors:
        print("Surface Anatomy violations:", file=sys.stderr)
        for error in errors:
            print(f"  - {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
