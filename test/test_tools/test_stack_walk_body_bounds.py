"""A function body ends at its symbol size, not at the next symbol (#271).

`objdump -d` disassembles whatever lies between one symbol header and the next.
On Xtensa what lies immediately after a function's `retw` is usually its literal
pool - raw address words - and objdump renders some of those words as
instructions. A word that decodes as `call8` and happens to point at a real
function entry became a call edge the walk then followed.

That is not a theoretical hazard. In the artoo-esp32 image at #271:

    rcDiagnosticsSourceName(RcBindingSource)   29 bytes, one retw.n

was read as a **3002-instruction** function with call edges reaching mdns,
mbedtls, lwIP and the WiFi driver, two of which landed on real entries and were
followed. Which two depends on where the literal words point, and relinking
moves that. Two builds of identical source differing only in a compiled-in
branch name returned WebEvents chains **688 bytes apart** - which is what made
the walk unusable as a gate, because it would fail honest slices on any branch
whose name length differed from the one a constant was recorded on.

These tests drive the parser with a synthetic listing rather than a built image,
so they run without a toolchain and pin the exact shape of the defect:

    <fn>:            entry / retw.n          <- 6 bytes of real code
                     .byte padding
                     40088fec                <- a literal word objdump renders
                                                as `call8 <victim>`
    <victim>:        the entry that word points at

The walk must see two instructions in `fn`, no call edge out of it, and must
resolve an address inside the literal pool to nothing rather than to `fn`.
"""

import sys
import unittest
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPO_ROOT / "tools"))

import stack_usage_report as sur  # noqa: E402


# Addresses chosen so the literal pool sits between the two symbols.
FN_ADDR = 0x4011000C
FN_SIZE = 6           # entry + retw.n
POOL_ADDR = 0x40110014
VICTIM_ADDR = 0x40151770
VICTIM_SIZE = 0x20

SYMBOL_TABLE = [
    "SYMBOL TABLE:",
    f"{FN_ADDR:08x} l     F .flash.text\t{FN_SIZE:08x} tinyMapper(Source)",
    f"{VICTIM_ADDR:08x} g     F .flash.text\t{VICTIM_SIZE:08x} unrelatedVictim",
    # A data symbol must never be mistaken for a function.
    "3f404734 g     O .rodata\t00000010 someTable",
]

# objdump's instruction lines are tab-separated: address, raw bytes, mnemonic,
# operands. The line at POOL_ADDR is the literal word rendered as a call - the
# exact shape that produced the phantom edge.
DISASSEMBLY = [
    "Disassembly of section .flash.text:",
    "",
    f"{FN_ADDR:08x} <tinyMapper(Source)>:",
    "/repo/include/rc_diagnostics.h:5",
    f"{FN_ADDR:08x}:\t004136        \tentry\ta1, 32",
    "/repo/include/rc_diagnostics.h:17",
    f"{FN_ADDR + 3:08x}:\tf01d      \tretw.n",
    # --- everything below is the literal pool, NOT this function's code ---
    f"{FN_ADDR + 5:08x}:\t00          \t.byte\t00",
    f"{POOL_ADDR:08x}:\tfc4825        \tcall8\t{VICTIM_ADDR:08x} <unrelatedVictim>",
    f"{POOL_ADDR + 3:08x}:\t0e8921        \tl32r\ta2, 400d3a38 <_stext> (3f404734 <someTable>)",
    f"{POOL_ADDR + 6:08x}:\t004136        \tentry\ta1, 1024",
    "",
    f"{VICTIM_ADDR:08x} <unrelatedVictim>:",
    "/repo/src/unrelated.cpp:9",
    f"{VICTIM_ADDR:08x}:\t004136        \tentry\ta1, 64",
    f"{VICTIM_ADDR + 3:08x}:\tf01d      \tretw.n",
]


class FakeImage(sur.Image):
    """sur.Image with objdump replaced by the canned listings above."""

    def _run(self, argv):
        return iter(SYMBOL_TABLE if "-t" in argv else DISASSEMBLY)


def build_image(**overrides):
    image = FakeImage.__new__(FakeImage)
    image.label = "fake"
    image.elf = Path("fake.elf")
    image.arch = "xtensa"
    image.funcs = {}
    image._starts = []
    image.interior_jumps = []
    image.sizes = overrides.get("sizes", image._read_symbol_sizes(Path("objdump")))
    image.unsized = set()
    image._disassemble(Path("objdump"))
    image._starts = sorted(image.funcs)
    image._drop_internal_branches()
    for fn in image.funcs.values():
        if fn.decoded == 0:
            fn.frame_kind = "undecoded"
    return image


class SymbolSizeParsing(unittest.TestCase):
    def test_function_sizes_are_read_from_the_symbol_table(self):
        image = build_image()
        self.assertEqual(image.sizes[FN_ADDR], FN_SIZE)
        self.assertEqual(image.sizes[VICTIM_ADDR], VICTIM_SIZE)

    def test_data_symbols_are_not_treated_as_functions(self):
        """Only ' F ' entries. An ' O ' data symbol is not a call target."""
        image = build_image()
        self.assertNotIn(0x3F404734, image.sizes)


class BodyBounds(unittest.TestCase):
    def test_the_body_stops_at_its_symbol_size(self):
        image = build_image()
        fn = image.funcs[FN_ADDR]
        self.assertEqual(
            fn.decoded, 2,
            "the body is entry + retw.n; anything more means the literal pool "
            "behind it was counted as this function's code",
        )

    def test_data_directives_inside_the_body_are_not_instructions(self):
        """The `.byte` at FN_ADDR+5 is inside the symbol size and is still data.

        It must not count towards `decoded`: a body objdump renders ENTIRELY as
        data would otherwise report decoded > 0, escape the `undecoded` label,
        and have its frame of 0 read as "leaf" rather than "not read" - which
        drops the whole subtree beneath it without saying so.
        """
        image = build_image()
        self.assertEqual(image.funcs[FN_ADDR].decoded, 2)
        self.assertEqual(image.funcs[FN_ADDR].frame_kind, "fixed")

    def test_no_call_edge_escapes_from_the_literal_pool(self):
        """The defect itself: a pool word rendered as `call8 <real entry>`."""
        image = build_image()
        fn = image.funcs[FN_ADDR]
        targets = [target for target, _, _ in fn.calls]
        self.assertNotIn(
            VICTIM_ADDR, targets,
            "a literal-pool word decoded as a call became a followed edge - "
            "this is the phantom edge that made the walk irreproducible",
        )
        self.assertEqual(fn.calls, [])
        self.assertEqual(fn.indirect, [])

    def test_the_frame_is_the_real_prologues(self):
        """A second `entry` in the pool must not be folded into the frame."""
        image = build_image()
        self.assertEqual(image.funcs[FN_ADDR].frame, 32)
        self.assertEqual(image.funcs[VICTIM_ADDR].frame, 64)

    def test_an_address_in_the_pool_belongs_to_no_function(self):
        image = build_image()
        self.assertIsNone(
            image.owner_of(POOL_ADDR),
            "an address after a function's end resolved to that function, which "
            "is how a stale target gets attributed to whichever symbol precedes it",
        )
        self.assertIs(image.owner_of(FN_ADDR), image.funcs[FN_ADDR])
        self.assertIs(image.owner_of(VICTIM_ADDR + 3), image.funcs[VICTIM_ADDR])

    def test_the_walk_from_the_bounded_body_is_zero_deep(self):
        image = build_image()
        walker = sur.Walker([image], sur.DEFAULT_PRUNE)
        total, chain, _ = walker.depth(image, image.funcs[FN_ADDR])
        self.assertEqual(total, 0)
        self.assertEqual(chain, [])

    def test_without_the_size_the_phantom_edge_comes_back(self):
        """Prove the bound is what removes it, not something else in the parse.

        Same listing, sizes withheld, so the parser falls back to "read until
        the next symbol" - the behaviour every body had before #271. The edge
        reappears, which is what the fix is measured against.
        """
        image = build_image(sizes={})
        fn = image.funcs[FN_ADDR]
        self.assertIn(
            VICTIM_ADDR, [target for target, _, _ in fn.calls],
            "the unbounded parse should still show the phantom edge; if it does "
            "not, this test is no longer exercising the defect",
        )
        self.assertGreater(fn.decoded, 2)


if __name__ == "__main__":
    unittest.main()
