#!/usr/bin/env python3
"""Check that Component Registry metadata stays aligned across the manifest, the firmware and the browser.

ADR 0042 as amended 2026-09-09 makes `include/component_registry.inc` the single
declaration of every product, from which the firmware tables, the identity
manifest and the operator lineup all derive. Most of that derivation is
mechanical and cannot drift: the tables are X-macro expansions of the manifest,
each sound driver returns its own row's capability word through
`componentPartCapabilities()`, and a `static_assert` in
`src/tasks/audio_task.cpp` ties the driver instances to the manifest's
selectable count.

What is left is what no compiler can see, and this is it. Three questions, all
answered by reading source text, and none of them by rewriting a file - the
convention `tools/check_action_registry_drift.py` set.

1. **Every capability a supported row declares has a consumer.** ADR 0042 is
   explicit that this is scoped to `supported` rows and never to `roadmap`
   ones: a roadmap row's capabilities have no driver to consume them by
   construction. The defect it guards against is the one ADR 0042's CAUTION
   records from the reference project - a flag declared on every entry and
   consulted by nothing, so the software confidently reproduced behaviour no
   real board would produce. `AUDIO_CAP_TRACK_COUNT` was exactly that here
   until #340.

2. **A row's Board Capability Gate and its `included` expression agree.** The
   `gate` column is what the identity payload reports as the reason a part is
   missing; `included` is what actually decides. A row that reports one gate
   and consults another - or none - tells a builder to go and check the wrong
   board fact.

3. **A family's `member_key` is a key the config serializer really reads and
   writes.** The manifest names the NVS key so an operator surface can, and
   nothing in the compiler connects that string to
   `src/config_serializer.cpp`. Rename either half alone and the member
   silently stops surviving a reboot.

Run it as `make check-component-drift`. Its own unit tests, which drive each
check against fixtures and prove it can fail, are
`test/test_tools/test_component_registry_drift.py`.
"""

from __future__ import annotations

from pathlib import Path
import re
import sys

ROOT = Path(__file__).resolve().parents[1]
MANIFEST = ROOT / "include" / "component_registry.inc"
AUDIO_DRIVER_HEADER = ROOT / "include" / "audio_driver.h"
BOARD_CAPABILITIES = ROOT / "include" / "board_capabilities.inc"
CONFIG_SERIALIZER = ROOT / "src" / "config_serializer.cpp"

# Where a capability consumer may live. Firmware branches on a bit; the browser
# branches on it too, and ADR 0042 counts both - two sound bits gate firmware
# behaviour and four are reported so the browser knows which fields are
# meaningful.
CONSUMER_DIRS = (ROOT / "src", ROOT / "include", ROOT / "data")
CONSUMER_SUFFIXES = (".c", ".cpp", ".h", ".hpp", ".inc", ".js")

# Declaring a bit is not consuming it. These two files are where the vocabulary
# and the per-product words are written down, so a hit in either proves nothing.
DECLARATION_FILES = {AUDIO_DRIVER_HEADER, MANIFEST}

CATEGORY_COLUMNS = 4
PART_COLUMNS = 9


def split_top_level(argument_text: str) -> list[str]:
    """Split one macro invocation's arguments on top-level commas.

    A capability column is an expression full of `|` and `::`, and an
    `included` column can be a parenthesised comparison, so a naive split on
    "," would cut a row in half.
    """
    arguments: list[str] = []
    depth = 0
    current = ""
    in_string = False
    for char in argument_text:
        if char == '"':
            in_string = not in_string
        if not in_string:
            if char in "([":
                depth += 1
            elif char in ")]":
                depth -= 1
            elif char == "," and depth == 0:
                arguments.append(current.strip())
                current = ""
                continue
        current += char
    arguments.append(current.strip())
    return arguments


def read_invocations(text: str, macro: str) -> list[list[str]]:
    """Every invocation of one X-macro in the manifest, as argument lists.

    Rows span several lines when a capability expression is long, so the
    closing parenthesis is found by balancing rather than by line.
    """
    rows: list[list[str]] = []
    for match in re.finditer(rf"^{re.escape(macro)}\(", text, re.MULTILINE):
        depth = 0
        index = match.end() - 1
        while index < len(text):
            if text[index] == "(":
                depth += 1
            elif text[index] == ")":
                depth -= 1
                if depth == 0:
                    break
            index += 1
        rows.append(split_top_level(text[match.end():index]))
    return rows


def unquote(value: str) -> str | None:
    """A manifest string literal's content, or None for `nullptr`."""
    value = value.strip()
    if value == "nullptr":
        return None
    if len(value) >= 2 and value[0] == '"' and value[-1] == '"':
        return value[1:-1]
    return value


def load_manifest(path: Path, errors: list[str]) -> tuple[list[list[str]], list[list[str]]]:
    """Parse the manifest into category and part rows.

    A malformed row is reported rather than raised: a checker that crashes on
    the file it is meant to report on tells whoever broke it nothing.
    """
    text = path.read_text(encoding="utf-8")
    categories = read_invocations(text, "PA_COMPONENT_CATEGORY")
    parts = read_invocations(text, "PA_COMPONENT_PART")

    if not categories or not parts:
        errors.append(
            f"{path.name} produced no rows - the manifest's macro spelling changed and "
            "this check has stopped reading it"
        )
        return [], []

    for row in categories:
        if len(row) != CATEGORY_COLUMNS:
            errors.append(
                f"category row has {len(row)} columns, expected {CATEGORY_COLUMNS}: {row}"
            )
    for row in parts:
        if len(row) != PART_COLUMNS:
            errors.append(f"part row has {len(row)} columns, expected {PART_COLUMNS}: {row}")

    if errors:
        return [], []
    return categories, parts


def capability_bit_names(path: Path, errors: list[str]) -> dict[str, str]:
    """`AUDIO_CAP_*` name -> its hex value, read from the interface header."""
    found = dict(
        re.findall(
            r"constexpr\s+uint8_t\s+(AUDIO_CAP_[A-Z_]+)\s*=\s*(0x[0-9A-Fa-f]+)",
            path.read_text(encoding="utf-8"),
        )
    )
    if not found:
        errors.append(
            f"no AUDIO_CAP_* constants found in {path.name} - the Sound family's capability "
            "vocabulary moved and this check has stopped reading it"
        )
    return found


def consumer_files(directories=CONSUMER_DIRS, exclude=None) -> list[Path]:
    exclude = DECLARATION_FILES if exclude is None else exclude
    files: list[Path] = []
    for directory in directories:
        for path in sorted(directory.rglob("*")):
            if path.suffix in CONSUMER_SUFFIXES and path not in exclude:
                files.append(path)
    return files


def file_uses(path: Path, name: str) -> bool:
    """True when this file READS the named bit, rather than defining it.

    The distinction is the whole point. `data/sound.js` mirrors the C++
    constants because a JS file cannot include a header, and a mirror that
    nothing then branches on is precisely the defect this check exists to
    report - the mirror would otherwise vouch for itself. Definition lines are
    dropped and what is left has to still name the bit.

    Matching on the NAME only, never on the bit value: 0x04 appears all over a
    firmware tree, and an earlier draft of this check accepted any of those as
    a consumer, which made it pass with AUDIO_CAP_TRACK_COUNT consulted by
    nothing.
    """
    definition = re.compile(
        rf"^\s*(?:#\s*define\s+{re.escape(name)}\b"
        rf"|(?:static\s+|const\s+|constexpr\s+|let\s+|var\s+|uint8_t\s+)+"
        rf"[A-Za-z_:<>\s]*\b{re.escape(name)}\s*=)"
    )
    for line in path.read_text(encoding="utf-8", errors="replace").splitlines():
        if name in line and not definition.match(line):
            return True
    return False


def check_capability_consumers(parts: list[list[str]], errors: list[str],
                               files: list[Path] | None = None,
                               vocabulary: dict[str, str] | None = None) -> None:
    """Every capability a supported row declares must have a consumer.

    Scoped to supported rows. A roadmap row's capabilities have no driver to
    consume them by construction, which is why ADR 0042 narrowed #302's
    suggested assertion to this half.
    """
    declared: dict[str, list[str]] = {}
    for row in parts:
        part_id = unquote(row[1])
        status = row[5].strip()
        if status != "COMPONENT_STATUS_SUPPORTED":
            continue
        for name in re.findall(r"AUDIO_CAP_[A-Z_]+", row[6]):
            declared.setdefault(name, []).append(part_id)

    if not declared:
        errors.append(
            "no supported row declares a capability - either the Sound rows lost theirs or "
            "the capability column moved"
        )
        return

    # A row naming a bit the interface header does not define would not
    # compile, but reading it here keeps the vocabulary and the rows answerable
    # to one place and makes a typo a sentence rather than a template error.
    if vocabulary is None:
        vocabulary = capability_bit_names(AUDIO_DRIVER_HEADER, errors)
    for name in sorted(name for name in declared if name not in vocabulary):
        errors.append(
            f"{name} is declared by a registry row but {AUDIO_DRIVER_HEADER.name} does not "
            "define it"
        )

    if files is None:
        files = consumer_files()
    for name, declaring_parts in sorted(declared.items()):
        if not any(file_uses(path, name) for path in files):
            errors.append(
                f"{name} is declared by {', '.join(sorted(set(declaring_parts)))} and consulted "
                "by nothing. A declared capability nobody reads is worse than no capability: "
                "the firmware promises an answer no caller ever asks for, and a surface renders "
                "a field the fitted module cannot produce (ADR 0042). Either give the bit a "
                "consumer or take it off the row"
            )


def check_board_capability_gates(parts: list[list[str]], errors: list[str],
                                 gates: set[str] | None = None) -> None:
    """A row's reported Board Capability Gate must exist, and must be the one it consults.

    The reported name is what tells a builder which board fact a missing part
    turns on, so a row that reports one gate and decides on another sends them
    to the wrong place.
    """
    if gates is None:
        gates = set(
            re.findall(
                r"PA_BOARD_CAPABILITY\((PA_CAP_[A-Z0-9_]+)\)",
                BOARD_CAPABILITIES.read_text(encoding="utf-8"),
            )
        )
        if not gates:
            errors.append(f"{BOARD_CAPABILITIES.name} produced no rows")
            return

    for row in parts:
        part_id = unquote(row[1])
        gate = unquote(row[7])
        included = row[8]
        if gate is None:
            # Universal. The `included` expression may still consult a PA_CAP_*
            # one day, and if it does the row has to say so - that is the half
            # a builder reads as the reason.
            for used in re.findall(r"PA_CAP_[A-Z0-9_]+", included):
                errors.append(f"{part_id} consults {used} in `included` but reports no gate")
            continue
        if gate not in gates:
            errors.append(
                f"{part_id} names {gate}, which is not a row in {BOARD_CAPABILITIES.name}"
            )
        elif gate not in included:
            errors.append(
                f"{part_id} reports {gate} as its Board Capability Gate but its `included` "
                f"expression does not consult it: {included}"
            )


def check_member_keys(categories: list[list[str]], errors: list[str],
                      serializer: Path | None = None) -> None:
    """A declared `member_key` must be a key the serializer both reads and writes.

    A Component Member that is not both read and written stops surviving a
    reboot, silently and with nothing else failing.
    """
    text = (serializer or CONFIG_SERIALIZER).read_text(encoding="utf-8")
    for row in categories:
        token = unquote(row[1])
        member_key = unquote(row[3])
        if member_key is None:
            continue
        mentions = text.count(f'"{member_key}"')
        if mentions < 2:
            errors.append(
                f"{token} declares member key {member_key}, which {CONFIG_SERIALIZER.name} "
                f"mentions {mentions} time(s) - a persisted member needs both a read and a write"
            )


def main() -> int:
    errors: list[str] = []
    categories, parts = load_manifest(MANIFEST, errors)

    if not errors:
        check_capability_consumers(parts, errors)
        check_board_capability_gates(parts, errors)
        check_member_keys(categories, errors)

    if errors:
        print("Component Registry drift detected:", file=sys.stderr)
        for error in errors:
            print(f"  - {error}", file=sys.stderr)
        return 1

    supported = sum(1 for row in parts if row[5].strip() == "COMPONENT_STATUS_SUPPORTED")
    members = sum(1 for row in categories if unquote(row[3]) is not None)
    print(
        f"Component Registry drift check passed "
        f"({len(categories)} families, {len(parts)} products, "
        f"{supported} supported, {members} with a Component Member)."
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
