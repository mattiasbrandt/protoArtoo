"""The console catalog's help offsets must address the help file in BYTES.

`help <op>` seeks a LittleFS file with the offset compiled into the catalog
(consoleGetHelpText(), src/console/console_module.cpp). The generator computed
those offsets with len() on a str, which counts characters: the registry's
descriptions carry en and em dashes, three UTF-8 bytes each, so every row after
the first dash was addressed two bytes early and the error accumulated - 104 of
194 rows were misaddressed by up to 46 bytes (#281).

The first test is the one that matters: it checks the files as shipped.
"""

import re
import sys
import tempfile
import unittest
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPO_ROOT / "tools"))

import generate_console_catalog  # noqa: E402

CATALOG_CPP = REPO_ROOT / "src" / "console" / "console_catalog.cpp"
HELP_TXT = REPO_ROOT / "data" / "console_help.txt"

# One generated table row: the name first, then help_offset/help_length, which
# the generator emits with those exact trailing comments.
ENTRY_RE = re.compile(
    r'\{\s*\n\s*"([^"]+)",\s*\n.*?(\d+),\s*//\s*help_offset\s*\n\s*(\d+),\s*//\s*help_length',
    re.DOTALL,
)


def shipped_rows():
    table = CATALOG_CPP.read_text(encoding="utf-8").split(
        "static const ConsoleCatalogEntry g_catalogEntries[] = {", 1
    )[1]
    return [(m.group(1), int(m.group(2)), int(m.group(3))) for m in ENTRY_RE.finditer(table)]


class ShippedHelpOffsetsAddressTheirOwnRow(unittest.TestCase):
    def test_every_catalog_offset_lands_on_that_operations_row(self):
        data = HELP_TXT.read_bytes()
        rows = shipped_rows()
        self.assertGreater(len(rows), 100, "catalog table did not parse")

        misaddressed = []
        for name, offset, length in rows:
            chunk = data[offset:offset + length]
            field0 = chunk.split(b"|", 1)[0].decode("utf-8", "replace")
            if field0 != name:
                misaddressed.append((name, offset, field0))

        self.assertEqual(
            [], misaddressed[:5],
            f"{len(misaddressed)} of {len(rows)} catalog rows address another operation's "
            "help text; `help <op>` would answer with the wrong prose",
        )

    def test_lengths_do_not_run_past_the_row(self):
        data = HELP_TXT.read_bytes()
        for name, offset, length in shipped_rows():
            chunk = data[offset:offset + length]
            self.assertNotIn(
                b"\n", chunk, f"{name}'s help_length runs into the next row"
            )
            self.assertEqual(
                b"\n", data[offset + length:offset + length + 1],
                f"{name}'s help_length stops short of the end of its row",
            )


class GeneratorCountsBytesNotCharacters(unittest.TestCase):
    def test_a_row_with_a_non_ascii_dash_does_not_shift_later_rows(self):
        entries = [
            {"name": "a.action.first", "display_name": "First",
             "description": "plain ascii", "executor": "doFirst", "params": []},
            {"name": "a.action.dashed", "display_name": "Dashed",
             "description": "an em dash — three bytes, one character",
             "executor": "doDashed", "params": []},
            {"name": "a.action.after", "display_name": "After",
             "description": "the row the miscount used to displace",
             "executor": "doAfter", "params": []},
        ]

        with tempfile.TemporaryDirectory() as tmp:
            out = Path(tmp) / "console_help.txt"
            offsets = generate_console_catalog.generate_help_text(entries, out)
            data = out.read_bytes()

        for entry in entries:
            name = entry["name"]
            offset, length = offsets[name]
            self.assertEqual(
                name.encode(), data[offset:offset + len(name)],
                f"{name}'s offset does not land on its own row",
            )
            self.assertEqual(b"\n", data[offset + length:offset + length + 1])


if __name__ == "__main__":
    unittest.main()
