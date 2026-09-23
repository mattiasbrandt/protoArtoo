#!/usr/bin/env python3
"""Check that nothing a surface paints with invents a color the palette has not.

`data/style.css` declares the palette once, in `:root`, and CONTEXT.md "Status
Color" makes the rule explicit: "The palette is **dark only** and declared once
in `data/style.css`, so there is no second palette to drift against; a color
literal outside `:root` is a defect, which is what makes the rule true rather
than aspirational" (#327, amended by the operator 2026-09-16).

Two rules, and both compare against `data/style.css` rather than against a list
written down here:

1. **Every color a surface paints with is a color `:root` declares.** Alpha is
   ignored, so a `rgba(232, 168, 50, 0.6)` glow is the amber token at six
   tenths and passes, while `#3b82f6` is a second palette and does not.
2. **Every `var(--token)` in a paint context names a token `:root` declares.**
   A `var()` naming nothing renders its fallback in silence - no error, no
   console warning - which is the class of defect a check catches cheaply and
   a person never does.

WHERE IT LOOKS, AND WHY NOT IN THE STYLESHEET. Not in `data/style.css`:
`test/test_web/test_style_token_layer.js` already walks that file as a browser
does, with every value resolved through `:root` and the at-rule context kept,
which is the difference between "the rule contains the string --warning" and
"this rule paints amber". A second, weaker regex copy of a check that already
exists is not a second guard; it is a second thing to keep in step
(`tools/check_surface_anatomy.py` recorded that decision first, and this file
is the other half of it). Everything else `data/` serves and paints with IS
here, and nothing checked it before: an inline `<style>`, a `style` attribute,
an SVG paint attribute, and a JavaScript constant holding a color.

THE STANDALONE-EXPORT FORM IS ACCEPTED. A printable sheet carries no
stylesheet, so #366 gave `data/wiring.js` seven `var(--token,#fallback)` pairs
and the fallback is what a browser paints with. The form passes on its own
terms: the token must be declared, and the fallback is a literal like any other
and must be a color the palette has.

THE PENDING LIST. Two files paint with colors the palette does not have, and
neither is this ticket's to change. A row here is a promise, not a suppression:
the file is still read, and a row whose file has become CLEAN fails the check
just as loudly, so a row cannot outlive the repair it is waiting for.

Report, never rewrite - the convention `tools/check_action_registry_drift.py`
set. Run it as `make check-color-drift`. Its unit tests, which drive it against
fixtures and prove it can fail, are `test/test_tools/test_color_drift.py`.
"""

from __future__ import annotations

from pathlib import Path
import re
import sys

sys.path.insert(0, str(Path(__file__).resolve().parent))

from operator_copy import (  # noqa: E402  (after the path insert above)
    ROOT,
    blank_html_comments,
    blank_js_comments,
    js_strings,
    line_of,
    rel,
)

DATA = ROOT / "data"
STYLESHEET = DATA / "style.css"

# Owned by test/test_web/test_style_token_layer.js, which resolves the real
# cascade. See the docstring.
PALETTE_OWNED = {"data/style.css"}

# Files that paint with colors the palette does not declare, and the reason each
# is not this ticket's to repair. Self-retiring: a file listed here with nothing
# left to report fails, so the row goes with the fix.
PENDING: dict[str, str] = {
    "data/dome_panel_model.js": (
        "a verbatim port of AstroPixelsPlus/data/panels.html that "
        "tools/check_dome_panel_drift.py compares against the live dome, so "
        "re-pointing its palette changes what that comparison means (#353)"
    ),
    "data/asset-sets/legacy/_product_art.html": (
        "var(--pa-art-bg,#0c1525) names a token nothing declares, so the "
        "fallback is always what paints; which token a product drawing's ground "
        "should be is a decision for the asset sets, not for this check (#353)"
    ),
}

# What a browser paints with, and where it reads it from.
STYLE_BLOCK = re.compile(r"<style\b[^>]*>(.*?)</style\s*>", re.S | re.I)
STYLE_ATTRIBUTE = re.compile(r"\bstyle\s*=\s*\"([^\"]*)\"|\bstyle\s*=\s*'([^']*)'", re.I)
PAINT_ATTRIBUTE = re.compile(
    r"\b(?:fill|stroke|stop-color|flood-color|lighting-color)\s*=\s*\"([^\"]*)\"", re.I
)

HEX = re.compile(r"#([0-9a-fA-F]{3,8})\b")
FUNCTIONAL = re.compile(r"\b(rgba?|hsla?)\(([^)]*)\)", re.I)
NAMED = re.compile(
    r"(?<![-\w#])(white|black|red|green|blue|yellow|orange|gold|silver|gray|grey)(?![-\w])", re.I
)
VAR_SITE = re.compile(r"var\(\s*(--[\w-]+)")
CSS_COMMENT = re.compile(r"/\*.*?\*/", re.S)

# A JavaScript constant is a paint site when the whole string is a color value -
# `const INK = "var(--text,#111111)"` - rather than a sentence that mentions a
# color. "Pulsing red holos and logics (10 s)" in data/rc.js is copy about what
# the droid does, and flagging it is the cry-wolf finding that mutes a check.
WHOLE_COLOR = re.compile(
    r"^\s*(?:var\(\s*--[\w-]+\s*(?:,[^)]*)?\)|#[0-9a-fA-F]{3,8}|(?:rgba?|hsla?)\([^)]*\))\s*$", re.I
)


def parse_root(stylesheet: Path, errors: list[str]) -> dict[str, str]:
    """Every custom property `:root` declares, as written.

    A flat read of the one `:root` block. The `@media print` overrides are not
    a second palette: they re-point tokens at `--paper*`, which `:root` itself
    declares, so every value a browser can reach is in here.
    """
    text = CSS_COMMENT.sub(" ", stylesheet.read_text(encoding="utf-8"))
    start = text.find(":root {")
    if start < 0:
        errors.append(
            f"{rel(stylesheet)} has no `:root {{` block, so this check has no palette to compare "
            "against. Next move: restore it, or correct parse_root() in tools/check_color_drift.py"
        )
        return {}
    end = text.find("}", start)
    tokens: dict[str, str] = {}
    for declaration in text[start + len(":root {") : end].split(";"):
        if ":" not in declaration:
            continue
        name, _, value = declaration.partition(":")
        name, value = name.strip(), value.strip()
        if name.startswith("--") and value:
            tokens[name] = value
    return tokens


def as_rgb(value: str) -> tuple[int, int, int] | None:
    """A color value as (r, g, b), alpha discarded. None when it is not one."""
    hexadecimal = HEX.fullmatch(value.strip())
    if hexadecimal:
        digits = hexadecimal.group(1)
        if len(digits) in (3, 4):
            digits = "".join(digit * 2 for digit in digits[:3])
        if len(digits) in (6, 8):
            return tuple(int(digits[index : index + 2], 16) for index in (0, 2, 4))  # type: ignore
        return None
    call = FUNCTIONAL.fullmatch(value.strip())
    if not call:
        return None
    kind = call.group(1).lower()
    parts = [part.strip() for part in re.split(r"[,\s/]+", call.group(2)) if part.strip()]
    if len(parts) < 3:
        return None
    try:
        if kind.startswith("rgb"):
            return tuple(_channel(part) for part in parts[:3])  # type: ignore
        return _hsl_to_rgb(*(float(part.rstrip("deg%")) for part in parts[:3]))
    except ValueError:
        return None


def _channel(part: str) -> int:
    if part.endswith("%"):
        return round(float(part[:-1]) * 255 / 100)
    return max(0, min(255, round(float(part))))


def _hsl_to_rgb(hue: float, saturation: float, lightness: float) -> tuple[int, int, int]:
    hue = (hue % 360) / 360
    saturation, lightness = saturation / 100, lightness / 100
    chroma = (1 - abs(2 * lightness - 1)) * saturation
    second = chroma * (1 - abs((hue * 6) % 2 - 1))
    offset = lightness - chroma / 2
    sextant = int(hue * 6) % 6
    table = [
        (chroma, second, 0.0), (second, chroma, 0.0), (0.0, chroma, second),
        (0.0, second, chroma), (second, 0.0, chroma), (chroma, 0.0, second),
    ]
    return tuple(round((channel + offset) * 255) for channel in table[sextant])  # type: ignore


def palette_rgb(tokens: dict[str, str]) -> dict[tuple[int, int, int], list[str]]:
    """Every color the palette declares, back to the tokens that declare it."""
    colors: dict[tuple[int, int, int], list[str]] = {}
    for name, value in tokens.items():
        for literal in _literals(value):
            rgb = as_rgb(literal)
            if rgb is not None:
                colors.setdefault(rgb, []).append(name)
    return colors


def literals_in(value: str) -> list[tuple[int, str]]:
    """(offset within the value, literal) for every color written in it."""
    found = [(match.start(), match.group(0)) for match in HEX.finditer(value)]
    found += [(match.start(), match.group(0)) for match in FUNCTIONAL.finditer(value)]
    found += [(match.start(), match.group(0)) for match in NAMED.finditer(value)]
    return sorted(found)


def _literals(value: str) -> list[str]:
    """Every color literal written in one value: a box-shadow carries several."""
    return [literal for _, literal in literals_in(value)]


def paint_sites(path: Path) -> list[tuple[int, str, str]]:
    """(line, where, value) for every place this file tells a browser to paint.

    Comments are blanked first in whichever language the file is written in, so
    `#293` in a note about an issue is not read as a color - the false positive
    #366's worker named before any of this was written. CSS comments go too, and
    for the same reason: `data/_recovery_kernel.html` explains its own signal
    lights as "amber degraded, red stopped" and cites #324, and both read as
    paint to anything that does not blank them first.
    """
    raw = path.read_text(encoding="utf-8")
    sites: list[tuple[int, str, str]] = []

    if path.suffix == ".js":
        text = blank_js_comments(raw)
        for quote, value in js_strings(text):
            offset = quote + 1  # past the opening quote, so a line number lands right
            markup = blank_html_comments(value)
            for block in STYLE_BLOCK.finditer(markup):
                sites.extend(_declarations(text, offset + block.start(1), block.group(1)))
            for match in STYLE_ATTRIBUTE.finditer(markup):
                body = match.group(1) if match.group(1) is not None else match.group(2)
                sites.extend(_declarations(text, offset + match.start(), body))
            for match in PAINT_ATTRIBUTE.finditer(markup):
                sites.append((line_of(text, offset + match.start()), "paint attribute", match.group(1)))
            if WHOLE_COLOR.fullmatch(value):
                sites.append((line_of(text, offset), "color constant", value))
        return sites

    text = blank_html_comments(raw)
    for block in STYLE_BLOCK.finditer(text):
        sites.extend(_declarations(text, block.start(1), block.group(1)))
    body = STYLE_BLOCK.sub(lambda m: " " * len(m.group(0)), text)
    for match in STYLE_ATTRIBUTE.finditer(body):
        value = match.group(1) if match.group(1) is not None else match.group(2)
        sites.extend(_declarations(body, match.start(), value))
    for match in PAINT_ATTRIBUTE.finditer(body):
        sites.append((line_of(body, match.start()), "paint attribute", match.group(1)))
    return sites


def _declarations(text: str, base: int, body: str) -> list[tuple[int, str, str]]:
    """One site per `property: value` in a stylesheet or style attribute.

    Comments are blanked in place, so a declaration carries only what paints and
    every offset still lands on the line it was written on.
    """
    body = CSS_COMMENT.sub(lambda m: re.sub(r"[^\n]", " ", m.group(0)), body)
    sites = []
    offset = 0
    for piece in body.split(";"):
        if ":" in piece:
            name, _, value = piece.partition(":")
            # Splitting a whole <style> body on `;` leaves the previous rule's
            # closing brace and this rule's selector in front of the property.
            # The value is unaffected; this is so the message names the property.
            label = re.split(r"[{}]", name)[-1].strip() or name.strip()
            sites.append((line_of(text, base + offset + len(name) + 1), label, value))
        offset += len(piece) + 1
    return sites


def served(data: Path) -> list[Path]:
    return sorted(
        path
        for path in data.rglob("*")
        if path.is_file() and path.suffix in (".html", ".js") and rel(path) not in PALETTE_OWNED
    )


def check(data: Path | None = None, stylesheet: Path = STYLESHEET,
          pending: dict[str, str] | None = None) -> tuple[list[str], int, int]:
    """(findings, tokens read, paint sites read). Reads; never writes."""
    pending = PENDING if pending is None else pending
    findings: list[str] = []
    tokens = parse_root(stylesheet, findings)
    palette = palette_rgb(tokens)
    if not palette:
        findings.append(
            f"{rel(stylesheet)}'s `:root` declares no color, so every literal would be a finding. "
            "This is not a pass"
        )
        return findings, len(tokens), 0

    files = served(DATA if data is None else data)
    sites = 0
    for path in files:
        name = rel(path)
        listed = pending.get(name)
        own: list[str] = []
        for line, where, value in paint_sites(path):
            sites += 1
            own.extend(_judge(name, line, where, value, tokens, palette))
        if listed is None:
            findings.extend(own)
        elif not own:
            findings.append(
                f"{name} is on the pending list - {listed} - but paints nothing the palette does "
                "not have. Next move: delete its row in tools/check_color_drift.py"
            )
    return findings, len(tokens), sites


def _judge(name: str, line: int, where: str, value: str,
           tokens: dict[str, str], palette: dict[tuple[int, int, int], list[str]]) -> list[str]:
    findings = []
    for token in VAR_SITE.findall(value):
        if token not in tokens:
            findings.append(
                f"{name}:{line}: {where} reads var({token}), which data/style.css does not "
                "declare, so the fallback is what paints - silently. Next move: name a declared "
                "token, or declare this one in :root"
            )
    for _, literal in literals_in(value):
        rgb = as_rgb(literal)
        if rgb is None:
            findings.append(
                f"{name}:{line}: {where} paints {literal!r}, a named color. The palette is "
                "hex tokens in data/style.css :root. Next move: paint with the token instead"
            )
        elif rgb not in palette:
            findings.append(
                f"{name}:{line}: {where} paints {literal}, which data/style.css :root does not "
                "declare - a second palette to drift against (CONTEXT.md \"Status Color\"). "
                "Next move: paint with a token, or add the color to :root if it is a new one"
            )
    return findings


def main(**sources) -> int:
    """`make check-color-drift`. `sources` are check()'s arguments."""
    findings, tokens, sites = check(**sources)
    if findings:
        print("Color drift detected:", file=sys.stderr)
        for finding in findings:
            print(f"  - {finding}", file=sys.stderr)
        return 1
    print(
        f"Color check passed ({tokens} tokens in :root, {sites} paint sites read, "
        f"{len(PENDING)} file(s) still waiting for a decision)."
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
