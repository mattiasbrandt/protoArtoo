"""A call through a dispatch table is walked when the table is stitched (#429).

The Console runs a status or api op through `executor(request->requestId,
sink)`, a pointer read from `g_statusExecutors`. The walker cannot follow a
call through a register it did not see loaded from a literal, so the recorded
Console chain could not include any op's frame - which is how the ESP32-P4
overflow under the last-run op (#427) stayed invisible.

`Image.stitch_table()` reads the table out of the image instead of from a list
in the recipe: every aligned word in the object that equals a function entry is
a row's function, and becomes a callee of the caller. So the walk takes the
deepest row itself, and a row added to the table later is walked without anyone
editing the recipe. The fixture's table interleaves name pointers with the
function pointers, as `{const char*, fn}` rows do, and one of those words is
placed inside a function's body, so "every word that lands in some function"
and "every word that is a function entry" give different answers.
"""

import sys
import unittest
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPO_ROOT / "tools"))

import stack_usage_report as sur  # noqa: E402

CALLER = 0x400F0000
SHALLOW_OP = 0x400F1000
DEEP_OP = 0x400F2000
TABLE = 0x3F400100
NAME_A = 0x3F400200
NAME_B = DEEP_OP + 2  # inside deepOp's body: an address in code, not an entry

SHALLOW_FRAME = 32
DEEP_FRAME = 4096

SYMBOLS = [
    "SYMBOL TABLE:",
    f"{CALLER:08x} l     F .flash.text\t00000008 dispatch(char const*)",
    f"{SHALLOW_OP:08x} l     F .flash.text\t00000005 shallowOp(unsigned long)",
    f"{DEEP_OP:08x} l     F .flash.text\t00000005 deepOp(unsigned long)",
    f"{TABLE:08x} l     O .flash.rodata\t00000010 g_opTable",
]


def _word(value):
    return value.to_bytes(4, "little").hex()


# objdump -s prints each line as the address, then up to four groups of bytes
# in memory order, then the ASCII column after two spaces.
TABLE_DUMP = [
    "",
    "fake.elf:     file format elf32-xtensa-le",
    "",
    "Contents of section .flash.rodata:",
    f" {TABLE:08x} {_word(NAME_A)} {_word(SHALLOW_OP)} {_word(NAME_B)} {_word(DEEP_OP)}"
    "  ..@?..@...@?..@",
]

LISTING = [
    "Disassembly of section .flash.text:",
    "",
    f"{CALLER:08x} <dispatch(char const*)>:",
    "/repo/src/console/console_module.cpp:3584",
    f"{CALLER:08x}:\t004136        \tentry\ta1, 32",
    "/repo/src/console/console_module.cpp:3590",
    f"{CALLER + 3:08x}:\t0008e0        \tcallx8\ta8",
    f"{CALLER + 6:08x}:\tf01d          \tretw.n",
    "",
    f"{SHALLOW_OP:08x} <shallowOp(unsigned long)>:",
    f"{SHALLOW_OP:08x}:\t004136        \tentry\ta1, {SHALLOW_FRAME}",
    f"{SHALLOW_OP + 3:08x}:\tf01d          \tretw.n",
    "",
    f"{DEEP_OP:08x} <deepOp(unsigned long)>:",
    f"{DEEP_OP:08x}:\t200136        \tentry\ta1, {DEEP_FRAME}",
    f"{DEEP_OP + 3:08x}:\tf01d          \tretw.n",
]


class TableImage(sur.Image):
    def _run(self, argv):
        if "-t" in argv:
            return iter(SYMBOLS)
        if "-s" in argv:
            return iter(TABLE_DUMP)
        return iter(LISTING)


class ATableStitchWalksEveryRow(unittest.TestCase):
    def setUp(self):
        self.image = TableImage("fake", Path("fake.elf"), Path("objdump"), "xtensa")
        self.caller = self.image.funcs[CALLER]

    def test_unstitched_the_indirect_call_is_a_gap(self):
        walker = sur.Walker([self.image], sur.DEFAULT_PRUNE)
        self.assertEqual(walker.depth(self.image, self.caller)[0], 0)
        self.assertEqual(len(self.caller.indirect), 1)

    def test_stitched_the_walk_takes_the_deepest_row(self):
        rows = self.image.stitch_table("dispatch", "g_opTable")
        self.assertEqual([fn.name for fn in rows],
                         ["shallowOp(unsigned long)", "deepOp(unsigned long)"])
        walker = sur.Walker([self.image], sur.DEFAULT_PRUNE)
        total, chain, _ = walker.depth(self.image, self.caller)
        self.assertEqual(total, DEEP_FRAME)
        self.assertEqual(chain[0][0], "deepOp(unsigned long)")
        self.assertEqual(chain[0][3], "stitched via g_opTable")

    def test_an_absent_table_is_reported_not_skipped(self):
        with self.assertRaises(KeyError):
            self.image.stitch_table("dispatch", "g_renamedTable")


if __name__ == "__main__":
    unittest.main()
