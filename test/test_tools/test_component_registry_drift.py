"""The Component Registry's drift check: reports mismatches, rewrites nothing (#340).

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
"""

import re
import unittest
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[2]
MANIFEST = REPO_ROOT / "include" / "component_registry.inc"
AUDIO_DRIVER_HEADER = REPO_ROOT / "include" / "audio_driver.h"
BOARD_CAPABILITIES = REPO_ROOT / "include" / "board_capabilities.inc"
CONFIG_SERIALIZER = REPO_ROOT / "src" / "config_serializer.cpp"

# Where a capability consumer may live. Firmware branches on a bit; the browser
# branches on it too, and ADR 0042 counts both - two sound bits gate firmware
# behaviour and four are reported so the browser knows which fields are
# meaningful.
CONSUMER_DIRS = (REPO_ROOT / "src", REPO_ROOT / "include", REPO_ROOT / "data")
CONSUMER_SUFFIXES = (".c", ".cpp", ".h", ".hpp", ".inc", ".js")

# Declaring a bit is not consuming it. These two files are where the vocabulary
# and the per-product words are written down, so a hit in either proves nothing.
DECLARATION_FILES = {AUDIO_DRIVER_HEADER, MANIFEST}


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


class ComponentRegistryFixture(unittest.TestCase):
    """Shared parse of the manifest, so a malformed row fails once and loudly."""

    @classmethod
    def setUpClass(cls) -> None:
        text = MANIFEST.read_text(encoding="utf-8")
        cls.categories = read_invocations(text, "PA_COMPONENT_CATEGORY")
        cls.parts = read_invocations(text, "PA_COMPONENT_PART")
        if not cls.categories or not cls.parts:
            raise AssertionError(
                "include/component_registry.inc produced no rows - the manifest's macro "
                "spelling changed and this check has stopped reading it"
            )
        for row in cls.categories:
            assert len(row) == 4, f"category row has {len(row)} columns, expected 4: {row}"
        for row in cls.parts:
            assert len(row) == 9, f"part row has {len(row)} columns, expected 9: {row}"


class CapabilityConsumers(ComponentRegistryFixture):
    def capability_bit_names(self) -> dict[str, str]:
        """`AUDIO_CAP_*` name -> its hex value, read from the interface header."""
        text = AUDIO_DRIVER_HEADER.read_text(encoding="utf-8")
        found = dict(
            re.findall(
                r"constexpr\s+uint8_t\s+(AUDIO_CAP_[A-Z_]+)\s*=\s*(0x[0-9A-Fa-f]+)", text
            )
        )
        self.assertTrue(
            found,
            "no AUDIO_CAP_* constants found in include/audio_driver.h - the Sound family's "
            "capability vocabulary moved and this check has stopped reading it",
        )
        return found

    def consumer_files(self) -> list[Path]:
        files: list[Path] = []
        for directory in CONSUMER_DIRS:
            for path in sorted(directory.rglob("*")):
                if path.suffix in CONSUMER_SUFFIXES and path not in DECLARATION_FILES:
                    files.append(path)
        return files

    @staticmethod
    def file_uses(path: Path, name: str) -> bool:
        """True when this file READS the named bit, rather than defining it.

        The distinction is the whole point. `data/sound.js` mirrors the C++
        constants because a JS file cannot include a header, and a mirror that
        nothing then branches on is precisely the defect this check exists to
        report - the mirror would otherwise vouch for itself. Definition lines
        are dropped and what is left has to still name the bit.

        Matching on the NAME only, never on the bit value: 0x04 appears all over
        a firmware tree, and an earlier draft of this check accepted any of
        those as a consumer, which made it pass with AUDIO_CAP_TRACK_COUNT
        consulted by nothing.
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

    def test_every_capability_a_supported_row_declares_has_a_consumer(self) -> None:
        # Scoped to supported rows. A roadmap row's capabilities have no driver
        # to consume them by construction, which is why ADR 0042 narrowed
        # #302's suggested assertion to this half.
        declared: dict[str, list[str]] = {}
        for row in self.parts:
            part_id = unquote(row[1])
            status = row[5].strip()
            capabilities = row[6]
            if status != "COMPONENT_STATUS_SUPPORTED":
                continue
            for name in re.findall(r"AUDIO_CAP_[A-Z_]+", capabilities):
                declared.setdefault(name, []).append(part_id)

        self.assertTrue(
            declared,
            "no supported row declares a capability - either the Sound rows lost theirs "
            "or the capability column moved",
        )

        # A row naming a bit the interface header does not define would compile
        # (it would not), but reading it here keeps the vocabulary and the rows
        # answerable to one place and makes a typo a sentence rather than a
        # template error.
        vocabulary = self.capability_bit_names()
        unknown = sorted(name for name in declared if name not in vocabulary)
        self.assertEqual(
            [],
            unknown,
            "a row declares a capability include/audio_driver.h does not define: "
            + ", ".join(unknown),
        )

        unconsumed = []
        for name, parts in sorted(declared.items()):
            consumed_in = [
                path.relative_to(REPO_ROOT).as_posix()
                for path in self.consumer_files()
                if self.file_uses(path, name)
            ]
            if not consumed_in:
                unconsumed.append(f"{name} (declared by {', '.join(sorted(set(parts)))})")

        self.assertEqual(
            [],
            unconsumed,
            "a supported product declares a capability nothing consults. A declared "
            "capability nobody reads is worse than no capability: the firmware promises "
            "an answer no caller ever asks for, and a surface renders a field the fitted "
            "module cannot produce (ADR 0042). Either give the bit a consumer or take it "
            "off the row. Unconsumed: " + "; ".join(unconsumed),
        )


class BoardCapabilityGates(ComponentRegistryFixture):
    def declared_gates(self) -> set[str]:
        text = BOARD_CAPABILITIES.read_text(encoding="utf-8")
        return set(re.findall(r"PA_BOARD_CAPABILITY\((PA_CAP_[A-Z0-9_]+)\)", text))

    def test_a_named_gate_exists_and_is_the_one_the_row_consults(self) -> None:
        gates = self.declared_gates()
        self.assertTrue(gates, "include/board_capabilities.inc produced no rows")

        problems = []
        for row in self.parts:
            part_id = unquote(row[1])
            gate = unquote(row[7])
            included = row[8]
            if gate is None:
                # Universal. The `included` expression may still consult a
                # PA_CAP_* one day, and if it does the row has to say so -
                # that is the half a builder reads as the reason.
                for used in re.findall(r"PA_CAP_[A-Z0-9_]+", included):
                    problems.append(
                        f"{part_id} consults {used} in `included` but reports no gate"
                    )
                continue
            if gate not in gates:
                problems.append(
                    f"{part_id} names {gate}, which is not a row in "
                    "include/board_capabilities.inc"
                )
            elif gate not in included:
                problems.append(
                    f"{part_id} reports {gate} as its Board Capability Gate but its "
                    f"`included` expression does not consult it: {included}"
                )

        self.assertEqual(
            [],
            problems,
            "the gate a row reports and the gate it consults must be the same one - the "
            "reported name is what tells a builder which board fact a missing part turns "
            "on. " + "; ".join(problems),
        )


class MemberKeys(ComponentRegistryFixture):
    def test_a_declared_member_key_is_read_and_written_by_the_serializer(self) -> None:
        serializer = CONFIG_SERIALIZER.read_text(encoding="utf-8")
        problems = []
        for row in self.categories:
            token = unquote(row[1])
            member_key = unquote(row[3])
            if member_key is None:
                continue
            quoted = f'"{member_key}"'
            if serializer.count(quoted) < 2:
                problems.append(
                    f"{token} declares member key {member_key}, which "
                    f"src/config_serializer.cpp mentions {serializer.count(quoted)} "
                    "time(s) - a persisted member needs both a read and a write"
                )

        self.assertEqual(
            [],
            problems,
            "a Component Member that is not both read and written stops surviving a "
            "reboot, silently and with nothing else failing. " + "; ".join(problems),
        )


if __name__ == "__main__":
    unittest.main()
