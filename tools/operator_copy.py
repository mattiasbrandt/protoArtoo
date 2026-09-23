#!/usr/bin/env python3
"""What counts as operator copy, for the checks that read it.

Three D2 checks (#353) ask the same question before they ask their own: which
bytes of `data/` and of the action registry does a builder actually read? A
comment is not copy, a CSS selector is not copy, an element id is not copy, and
a checker that cannot tell the difference reports a defect in every second
comment and gets muted. `#293` in a comment looks exactly like a color literal;
"the controller" in a comment looks exactly like the word #298 banned. Both were
named as false positives before a line of this was written (#353, the note from
#366's worker).

So the extraction lives here once rather than three times: "a list written down
twice is a list that drifts" is the convention the checks themselves enforce.

WHAT IS COPY:

- a JavaScript string literal, with comments blanked first;
- the text between HTML tags, and the four attributes a person reads
  (`title`, `aria-label`, `placeholder`, `alt`);
- a `display_name` or `description` in `docs/action-registry.yaml`.

WHAT IS NOT:

- anything inside a comment, in either language. Both are blanked in place, so
  every offset this module reports still points at the real line.
- `data/console_help.txt`. It is generated from the registry by
  `tools/generate_console_catalog.py` and byte-checked against it by
  `tools/check_action_registry_drift.py`, so its copy is read at its source and
  a finding names the file an edit actually goes in.
- a hyphen- or underscore-joined token (`controller-classic-outline`), which is
  an identifier however English it reads. The Part-vs-product routing on #353
  made that exclusion explicit: "the check must not flag identifiers".
"""

from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path
import re
import sys

ROOT = Path(__file__).resolve().parents[1]
DATA = ROOT / "data"
REGISTRY = ROOT / "docs" / "action-registry.yaml"

# Generated from the registry; checked at its source instead. See the docstring.
GENERATED = {"data/console_help.txt"}

# The attributes on an element that a person reads or hears.
READ_ATTRIBUTES = ("title", "aria-label", "placeholder", "alt")


@dataclass(frozen=True)
class Copy:
    """One run of operator-visible text, and where it is written."""

    path: str  # repository-relative
    line: int  # 1-based, in the file as written
    text: str
    kind: str  # "string", "text", "attribute" or "registry"
    # True for text inside an SVG `<symbol>`: a drawing of a product, carrying
    # the legend printed on THAT product. It is still copy - a builder reads it -
    # but it is not a statement about the running board, so a check about board
    # naming skips it. `data/asset-sets/legacy/_product_art.html` draws the
    # Sabertooth's own `S1 S2 0V 5V` terminal block, which collides exactly with
    # the Artoo PCB's serial-lane legend and is not the same fact.
    in_symbol: bool = False

    def where(self) -> str:
        return f"{self.path}:{self.line}"


def rel(path: Path) -> str:
    try:
        return str(path.resolve().relative_to(ROOT))
    except ValueError:
        return str(path)


def blank_js_comments(text: str) -> str:
    """Blank `//` and `/* */` comments, keeping every other byte in place.

    String-aware, because `"http://x"` is not a comment, and regex-aware,
    because `apAddress.replace(/^http:\\/\\//, "")` in `data/wifi.js` is not one
    either - that exact line is why this reads regular-expression literals
    rather than assuming division. Offsets are preserved so a caller can still
    count newlines to the real line number, and newlines inside a block comment
    survive for the same reason.

    What proves it is not this sentence: the check's own test blanks every
    `data/*.js` file and hands the result to `node --check`, so a `/` read the
    wrong way shows up as a syntax error rather than as a quietly eaten line.
    """
    out = list(text)
    index, end = 0, len(text)
    while index < end:
        char = text[index]
        if char == "/" and index + 1 < end and text[index + 1] not in "/*" and _starts_regex(text, index):
            index = _skip_regex(text, index)
            continue
        if char in "'\"`":
            quote = char
            index += 1
            while index < end:
                if text[index] == "\\":
                    index += 2
                    continue
                if text[index] == quote:
                    index += 1
                    break
                index += 1
            continue
        if char == "/" and index + 1 < end and text[index + 1] == "/":
            while index < end and text[index] != "\n":
                out[index] = " "
                index += 1
            continue
        if char == "/" and index + 1 < end and text[index + 1] == "*":
            while index + 1 < end and not (text[index] == "*" and text[index + 1] == "/"):
                if text[index] != "\n":
                    out[index] = " "
                index += 1
            out[index] = " "
            if index + 1 < end:
                out[index + 1] = " "
            index += 2
            continue
        index += 1
    return "".join(out)


# A `/` opens a regular expression unless a value could stand to its left. The
# operand characters are the closers and the end of a name or number; the
# keywords are the names after which a value cannot stand, so `return /x/` is a
# regex and `count / x` is division.
REGEX_KEYWORDS = frozenset(
    "return typeof instanceof in of new delete void case do else yield await throw".split()
)
NAME_END = re.compile(r"[\w$]$")
WORD_BEFORE = re.compile(r"[\w$]+$")


def _starts_regex(text: str, index: int) -> bool:
    before = text[:index].rstrip()
    if not before:
        return True
    if before[-1] in ")]}":
        return False
    if NAME_END.search(before):
        word = WORD_BEFORE.search(before)
        return bool(word and word.group(0) in REGEX_KEYWORDS)
    return True


def _skip_regex(text: str, index: int) -> int:
    """Index just past the closing `/` of the literal opening at `index`."""
    index += 1
    end = len(text)
    in_class = False
    while index < end:
        char = text[index]
        if char == "\\":
            index += 2
            continue
        if char == "\n":  # an unterminated literal: not a regex after all
            return index
        if char == "[":
            in_class = True
        elif char == "]":
            in_class = False
        elif char == "/" and not in_class:
            return index + 1
        index += 1
    return index


HTML_COMMENT = re.compile(r"<!--.*?-->", re.S)


def blank_html_comments(text: str) -> str:
    """Blank `<!-- -->` comments, keeping newlines and every offset in place."""
    return HTML_COMMENT.sub(lambda m: re.sub(r"[^\n]", " ", m.group(0)), text)


JS_STRING = re.compile(r"'((?:[^'\\\n]|\\.)*)'|\"((?:[^\"\\\n]|\\.)*)\"|`((?:[^`\\]|\\.)*)`", re.S)


def js_strings(text: str) -> list[tuple[int, str]]:
    """(offset, value) for every string literal in already-blanked JavaScript.

    A template literal is returned whole, interpolations and all: the copy a
    builder reads is the text around `${...}`, and splitting it would lose the
    sentence the words make together.
    """
    found = []
    for match in JS_STRING.finditer(text):
        value = next(group for group in match.groups() if group is not None)
        found.append((match.start(), value))
    return found


HTML_TAG = re.compile(r"<[^>]*>", re.S)
ATTRIBUTE = re.compile(
    r"\b(" + "|".join(READ_ATTRIBUTES) + r")\s*=\s*\"([^\"]*)\"",
    re.I,
)
SCRIPT_BLOCK = re.compile(r"<script\b[^>]*>(.*?)</script\s*>", re.S | re.I)
STYLE_BLOCK = re.compile(r"<style\b[^>]*>(.*?)</style\s*>", re.S | re.I)
SYMBOL_BLOCK = re.compile(r"<symbol\b[^>]*>.*?</symbol\s*>", re.S | re.I)


def line_of(text: str, offset: int) -> int:
    return text.count("\n", 0, offset) + 1


def _blank(text: str, start: int, end: int) -> str:
    """Blank a span, keeping newlines so later offsets still resolve."""
    return text[:start] + re.sub(r"[^\n]", " ", text[start:end]) + text[end:]


def html_copy(path: Path, text: str) -> list[Copy]:
    """Text nodes, read attributes, and the strings of any inline script.

    A `<style>` block holds no copy and is dropped. A `<script>` block does -
    `data/_recovery_kernel.html` carries its whole panel in one - so it is read
    the way a `.js` file is, with its comments blanked first.
    """
    name = rel(path)
    blanked = blank_html_comments(text)
    found: list[Copy] = []

    for match in SCRIPT_BLOCK.finditer(blanked):
        body = blank_js_comments(match.group(1))
        base = match.start(1)
        for offset, value in js_strings(body):
            found.append(Copy(name, line_of(blanked, base + offset), value, "string"))
        blanked = _blank(blanked, match.start(1), match.end(1))

    for match in STYLE_BLOCK.finditer(blanked):
        blanked = _blank(blanked, match.start(1), match.end(1))

    symbols = [(m.start(), m.end()) for m in SYMBOL_BLOCK.finditer(blanked)]
    drawn = lambda offset: any(start <= offset < end for start, end in symbols)

    for match in ATTRIBUTE.finditer(blanked):
        found.append(
            Copy(name, line_of(blanked, match.start(2)), match.group(2), "attribute",
                 drawn(match.start(2)))
        )

    for offset, value in _text_nodes(blanked):
        if value.strip():
            found.append(Copy(name, line_of(blanked, offset), value, "text", drawn(offset)))
    return found


def _text_nodes(text: str) -> list[tuple[int, str]]:
    """(offset, run) for everything outside a tag."""
    nodes = []
    cursor = 0
    for tag in HTML_TAG.finditer(text):
        if tag.start() > cursor:
            nodes.append((cursor, text[cursor : tag.start()]))
        cursor = tag.end()
    if cursor < len(text):
        nodes.append((cursor, text[cursor:]))
    return nodes


def js_copy(path: Path, text: str) -> list[Copy]:
    """Every string literal, comments blanked, HTML comments inside them too.

    A template literal often carries a whole fragment of markup, comments
    included (`data/shell.js` builds the chrome that way), so the HTML comment
    blanker runs over the extracted value as well.
    """
    name = rel(path)
    blanked = blank_js_comments(text)
    return [
        Copy(name, line_of(blanked, offset), blank_html_comments(value), "string")
        for offset, value in js_strings(blanked)
    ]


def served_files(data: Path = DATA) -> list[Path]:
    """Every `data/` file that carries copy, generated catalogs excluded."""
    return sorted(
        path
        for path in data.rglob("*")
        if path.is_file() and path.suffix in (".html", ".js") and rel(path) not in GENERATED
    )


def surface_copy(data: Path = DATA) -> list[Copy]:
    """All operator copy served from `data/`."""
    found: list[Copy] = []
    for path in served_files(data):
        text = path.read_text(encoding="utf-8")
        found.extend(html_copy(path, text) if path.suffix == ".html" else js_copy(path, text))
    return found


def registry_copy(registry: Path = REGISTRY) -> list[Copy]:
    """`display_name` and `description` for every action-registry entry.

    Values come from the registry's own loader - a bare `off` is the string,
    not `False` (#249) - while the line comes from a scan of the same text for
    the field itself, so a finding names the line an editor jumps to rather
    than the head of the entry it sits in.
    """
    sys.path.insert(0, str(Path(__file__).resolve().parent))
    from registry_yaml import load_registry_yaml

    document = load_registry_yaml(registry)
    entries = (document or {}).get("entries") or []
    text = registry.read_text(encoding="utf-8")
    name = rel(registry)

    # Walk the entries in file order, pairing each with the block of lines it
    # was written as. The registry is one flat sequence of two-space mappings
    # and carries no block scalar, so a field is always one line.
    starts = [
        text.count("\n", 0, match.start()) + 1
        for match in re.finditer(r"^- name:\s*\S+\s*$", text, re.M)
    ]
    field_lines = [
        (number, match.group(1))
        for number, match in (
            (index + 1, re.match(r"^\s{2}(display_name|description):", line))
            for index, line in enumerate(text.splitlines())
        )
        if match
    ]

    found: list[Copy] = []
    for index, entry in enumerate(entries):
        if index >= len(starts):
            break
        start = starts[index]
        end = starts[index + 1] if index + 1 < len(starts) else len(text.splitlines()) + 1
        block = {field: number for number, field in field_lines if start <= number < end}
        for field in ("display_name", "description"):
            value = entry.get(field)
            if isinstance(value, str) and value:
                found.append(
                    Copy(name, block.get(field, start), value, f"registry {field} of {entry['name']}")
                )
    return found


IDENTIFIER_JOINED = re.compile(r"[-_]")


def is_identifier_context(text: str, start: int, end: int) -> bool:
    """True when the match is welded into a longer token by `-` or `_`.

    `controller-classic-outline` is a sprite name and `en_rc_ch1` is an NVS key;
    neither is copy, however readable. Space, punctuation and case are all fine.
    """
    before = text[start - 1] if start > 0 else ""
    after = text[end] if end < len(text) else ""
    return bool(IDENTIFIER_JOINED.fullmatch(before) or IDENTIFIER_JOINED.fullmatch(after))
