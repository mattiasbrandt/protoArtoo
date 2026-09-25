"""Calls through run-time pointers, and bodies only the archive can read (#428).

A requested restart runs esp_restart() on SafetyMonitor, and esp_restart()
calls every handler registered with esp_register_shutdown_handler() through a
pointer array filled at run time. Neither the listing nor a table in the image
names them, so `Image.stitch_calls()` adds the edges the recipe names.

One of those handlers, esp_wifi_stop, is a closed library's function, and on
artoo-esp32 objdump emits its body in the linked image as data: frame unknown,
no calls. The archive member it was linked from still records its `entry a1, N`
and, through relocations, everything its literal pool loads.
`Image.adopt_archive_bodies()` walks such a body from the member: the frame from
`entry`, callees from the relocations, a referenced table of pointers expanded
to its rows (through `pointer_tables` when the program only sets the pointer at
run time), and undecoded callees adopted the same way.
"""

import sys
import unittest
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPO_ROOT / "tools"))

import stack_usage_report as sur  # noqa: E402

RESTART = 0x400F0000
HANDLER = 0x400F1000       # decoded, open code
CLOSED = 0x400F2000        # emitted as data in the image
CLOSED_INNER = 0x400F3000  # emitted as data, reached only from CLOSED
TABLE_ROW = 0x400F4000     # a function the closed code calls through a table
TABLE = 0x3FFB0100         # the table the run-time pointer is set to
POINTER = 0x3FFB0200       # the pointer, zero in the image

HANDLER_FRAME = 64
CLOSED_FRAME = 48
INNER_FRAME = 96
ROW_FRAME = 1024

SYMBOLS = [
    "SYMBOL TABLE:",
    f"{RESTART:08x} g     F .flash.text\t00000008 esp_restart",
    f"{HANDLER:08x} g     F .flash.text\t00000005 sync_timers",
    f"{CLOSED:08x} g     F .flash.text\t00000008 closed_stop",
    f"{CLOSED_INNER:08x} g     F .flash.text\t00000008 closed_inner",
    f"{TABLE_ROW:08x} g     F .flash.text\t00000005 osi_take",
    f"{TABLE:08x} g     O .dram0.data\t00000008 g_osi_table",
    f"{POINTER:08x} g     O .dram0.bss\t00000004 g_osi_ptr",
]


def _word(value):
    return value.to_bytes(4, "little").hex()


TABLE_DUMP = [
    "",
    "fake.elf:     file format elf32-xtensa-le",
    "",
    "Contents of section .dram0.data:",
    f" {TABLE:08x} {_word(0x3F400000)} {_word(TABLE_ROW)}  ........",
]

LISTING = [
    "Disassembly of section .flash.text:",
    "",
    f"{RESTART:08x} <esp_restart>:",
    f"{RESTART:08x}:\t004136        \tentry\ta1, 32",
    f"{RESTART + 3:08x}:\t0008e0        \tcallx8\ta8",
    f"{RESTART + 6:08x}:\tf01d          \tretw.n",
    "",
    f"{HANDLER:08x} <sync_timers>:",
    f"{HANDLER:08x}:\t004136        \tentry\ta1, {HANDLER_FRAME}",
    f"{HANDLER + 3:08x}:\tf01d          \tretw.n",
    "",
    # objdump emitted these two as raw words, not instructions.
    f"{CLOSED:08x} <closed_stop>:",
    f"{CLOSED:08x}:\t00613600 \t.word 0x00613600",
    "",
    f"{CLOSED_INNER:08x} <closed_inner>:",
    f"{CLOSED_INNER:08x}:\t00c13600 \t.word 0x00c13600",
    "",
    f"{TABLE_ROW:08x} <osi_take>:",
    f"{TABLE_ROW:08x}:\t004136        \tentry\ta1, {ROW_FRAME}",
    f"{TABLE_ROW + 3:08x}:\tf01d          \tretw.n",
]


class ShutdownImage(sur.Image):
    def _run(self, argv):
        if "-t" in argv:
            return iter(SYMBOLS)
        if "-s" in argv:
            return iter(TABLE_DUMP)
        return iter(LISTING)


class FakeArchives(sur.ArchiveBodies):
    """What the archive members record, as ArchiveBodies.body() answers it."""

    BODIES = {
        "closed_stop": (CLOSED_FRAME, {"closed_inner", "g_osi_ptr", "some_string"}),
        "closed_inner": (INNER_FRAME, set()),
    }

    def __init__(self):
        pass

    def body(self, name):
        return self.BODIES.get(name)


class ARunTimePointerIsStitchedByName(unittest.TestCase):
    def setUp(self):
        self.image = ShutdownImage("fake", Path("fake.elf"), Path("objdump"), "xtensa")

    def test_unstitched_the_handler_is_invisible(self):
        walker = sur.Walker([self.image], sur.DEFAULT_PRUNE)
        self.assertEqual(walker.depth(self.image, self.image.funcs[RESTART])[0], 0)

    def test_stitched_the_walk_takes_the_handler(self):
        self.image.stitch_calls("esp_restart", ["sync_timers"])
        walker = sur.Walker([self.image], sur.DEFAULT_PRUNE)
        total, chain, _ = walker.depth(self.image, self.image.funcs[RESTART])
        self.assertEqual(total, HANDLER_FRAME)
        self.assertEqual(chain[0][3], "stitched call")

    def test_an_absent_callee_is_reported_not_skipped(self):
        with self.assertRaises(KeyError):
            self.image.stitch_calls("esp_restart", ["renamed_handler"])


class AnUndecodedBodyIsWalkedFromItsArchiveMember(unittest.TestCase):
    def setUp(self):
        self.image = ShutdownImage("fake", Path("fake.elf"), Path("objdump"), "xtensa")
        self.image.stitch_calls("esp_restart", ["closed_stop"])

    def test_without_its_archive_body_the_closed_handler_reads_zero(self):
        self.assertEqual(self.image.funcs[CLOSED].frame_kind, "undecoded")
        walker = sur.Walker([self.image], sur.DEFAULT_PRUNE)
        self.assertEqual(walker.depth(self.image, self.image.funcs[RESTART])[0], 0)

    def test_adopted_it_walks_its_frame_its_callees_and_the_pointer_table(self):
        adopted = self.image.adopt_archive_bodies(
            ["closed_stop"], FakeArchives(), {"g_osi_ptr": "g_osi_table"})
        self.assertEqual(sorted(adopted), ["closed_inner", "closed_stop"])
        self.assertEqual(self.image.funcs[CLOSED].frame, CLOSED_FRAME)
        self.assertEqual(self.image.funcs[CLOSED].frame_kind, "archive")
        walker = sur.Walker([self.image], sur.DEFAULT_PRUNE)
        total, chain, _ = walker.depth(self.image, self.image.funcs[RESTART])
        # The table row is deeper than the undecoded inner function.
        self.assertEqual(total, CLOSED_FRAME + ROW_FRAME)
        self.assertEqual([step[0] for step in chain], ["closed_stop", "osi_take"])

    def test_a_decoded_function_is_refused(self):
        with self.assertRaises(sur.Fatal):
            self.image.adopt_archive_bodies(["sync_timers"], FakeArchives())

    def test_a_function_no_member_defines_is_refused(self):
        with self.assertRaises(sur.Fatal):
            self.image.adopt_archive_bodies(["closed_missing"], FakeArchives())


if __name__ == "__main__":
    unittest.main()
