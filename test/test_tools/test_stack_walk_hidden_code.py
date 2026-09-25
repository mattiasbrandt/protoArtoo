"""Code the Xtensa listing prints as data (#430).

The artoo-esp32 image keeps only part of its `.xt.prop` property table, and
objdump decodes an address in a gap between records by the next record's
flags, so a third of the image's function bodies print as literal words: frame
0, no calls. `Image._recover_data_bodies()` reads those bodies - and only those
- from a copy of the image with the tables removed. Taking the stripped copy for
every body would be wrong: where the tables mark in-body data correctly, the
stripped listing decodes it as instructions and a body that was fine loses a
real call.

The fakes below answer objdump for two listings of one image: the product one,
and the one of the copy objcopy writes, told apart by the copy's path.
"""

import sys
import unittest
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPO_ROOT / "tools"))

import stack_usage_report as sur  # noqa: E402

ROOT_FN = 0x40100000   # decodes in both listings; calls the other two
HIDDEN = 0x40101000    # printed as data in the product listing only
DEEP = 0x40102000      # decodes in both; reached only from HIDDEN
STILL_DATA = 0x40103000  # printed as data in both listings
LOG_VA = 0x40104000    # calls through the hook pointer
SETTER = 0x40105000    # stores the hook pointer, calls nothing
HOOK = 0x40106000      # the installed hook
POINTER = 0x3FFB0470   # the pointer variable

HIDDEN_FRAME = 96
DEEP_FRAME = 512
HOOK_FRAME = 2048

SYMBOLS = [
    "SYMBOL TABLE:",
    f"{ROOT_FN:08x} g     F .flash.text\t00000009 rootTask",
    f"{HIDDEN:08x} g     F .flash.text\t00000009 hiddenBody",
    f"{DEEP:08x} g     F .flash.text\t00000005 deepCallee",
    f"{STILL_DATA:08x} g     F .flash.text\t00000004 stillData",
    f"{LOG_VA:08x} g     F .flash.text\t0000000b esp_log_va",
    f"{SETTER:08x} g     F .flash.text\t00000008 esp_log_set_vprintf",
    f"{HOOK:08x} l     F .flash.text\t00000005 paLogIdfVprintf(char const*, __va_list_tag)",
    f"{POINTER:08x} g     O .dram0.data\t00000004 esp_log_vprint_func",
]


def body(addr, *insns):
    """Listing lines for one body: (raw bytes, mnemonic, operands) per insn."""
    lines, pc = [], addr
    for raw, mnem, ops in insns:
        lines.append(f"{pc:08x}:\t{raw:<14}\t{mnem}\t{ops}".rstrip())
        pc += len(raw) // 2
    return lines


def header(addr, name):
    return ["", f"{addr:08x} <{name}>:"]


ENTRY = "004136"   # entry a1, N: 3 bytes, first byte 0x36
RETW = "f01d"      # retw.n: 2 bytes, first byte 0x1d
CALL8 = "000025"   # call8: 3 bytes, first byte 0x25

COMMON = (
    header(ROOT_FN, "rootTask")
    + body(ROOT_FN, (ENTRY, "entry", "a1, 32"),
           (CALL8, "call8", f"{HIDDEN:08x} <hiddenBody>"),
           (RETW, "retw.n", ""))
    + header(STILL_DATA, "stillData")
    + body(STILL_DATA, ("00613600", ".word", "0x00613600"))
    + header(LOG_VA, "esp_log_va")
    + body(LOG_VA, (ENTRY, "entry", "a1, 32"),
           ("fff981", "l32r", f"a8, 40100f00 <rootTask-0x100> ({POINTER:08x} <esp_log_vprint_func>)"),
           ("0088a2", "l32i", "a8, a8, 0"),
           ("0008e0", "callx8", "a8"))
    + header(SETTER, "esp_log_set_vprintf")
    + body(SETTER, (ENTRY, "entry", "a1, 32"),
           ("fff921", "l32r", f"a2, 40100f04 <rootTask-0xfc> ({POINTER:08x} <esp_log_vprint_func>)"),
           (RETW, "retw.n", ""))
    + header(HOOK, "paLogIdfVprintf(char const*, __va_list_tag)")
    + body(HOOK, (ENTRY, "entry", f"a1, {HOOK_FRAME}"), (RETW, "retw.n", ""))
)

# The product listing: hiddenBody is literal words, and deepCallee decodes as
# a leaf.
PRODUCT = (
    ["Disassembly of section .flash.text:"]
    + COMMON
    + header(HIDDEN, "hiddenBody")
    + body(HIDDEN, ("00613600", ".word", "0x00613600"),
           ("25000000", ".word", "0x25000000"))
    + header(DEEP, "deepCallee")
    + body(DEEP, (ENTRY, "entry", f"a1, {DEEP_FRAME}"), (RETW, "retw.n", ""))
)

# The stripped listing: hiddenBody decodes, and so does deepCallee - but with a
# call the product listing does not have, the way in-body data the property
# table marked correctly decodes as a phantom instruction once it is gone.
STRIPPED = (
    ["Disassembly of section .flash.text:"]
    + COMMON
    + header(HIDDEN, "hiddenBody")
    + body(HIDDEN, (ENTRY, "entry", f"a1, {HIDDEN_FRAME}"),
           (CALL8, "call8", f"{DEEP:08x} <deepCallee>"),
           (RETW, "retw.n", ""))
    + header(DEEP, "deepCallee")
    + body(DEEP, (ENTRY, "entry", f"a1, {DEEP_FRAME}"),
           (CALL8, "call8", f"{HOOK:08x} <paLogIdfVprintf(char const*, __va_list_tag)>"))
)


class TwoListingImage(sur.Image):
    """Answers objdump from PRODUCT, or from STRIPPED for objcopy's copy."""

    def _run(self, argv):
        self.argvs = getattr(self, "argvs", []) + [argv]
        if argv[0].endswith("objcopy"):
            return iter([])
        if "-t" in argv:
            return iter(SYMBOLS)
        if any(arg.endswith("no-xt-prop.elf") for arg in argv):
            return iter(STRIPPED)
        return iter(PRODUCT)


def image(arch="xtensa"):
    return TwoListingImage("fake", Path("fake.elf"), Path("/tc/xtensa-esp32-elf-objdump"), arch)


class ABodyPrintedAsDataIsReadFromTheStrippedCopy(unittest.TestCase):
    def setUp(self):
        self.image = image()

    def test_objcopy_removes_both_property_tables(self):
        copies = [a for a in self.image.argvs if a[0].endswith("objcopy")]
        self.assertEqual(len(copies), 1)
        self.assertEqual(copies[0][0], "/tc/xtensa-esp32-elf-objcopy")
        self.assertIn("--remove-section=.xt.prop", copies[0])
        self.assertIn("--remove-section=.xt.lit", copies[0])

    def test_the_hidden_body_gets_its_frame_and_its_call(self):
        fn = self.image.funcs[HIDDEN]
        self.assertEqual((fn.frame, fn.frame_kind), (HIDDEN_FRAME, "fixed"))
        self.assertEqual([c[0] for c in fn.calls], [DEEP])
        self.assertEqual(self.image.recovered_bodies, {HIDDEN})

    def test_a_decoded_body_keeps_the_product_listing(self):
        # The stripped listing gives deepCallee a call; the walk must not take it.
        self.assertEqual(self.image.funcs[DEEP].calls, [])
        walker = sur.Walker([self.image], sur.DEFAULT_PRUNE)
        total, chain, _ = walker.depth(self.image, self.image.funcs[ROOT_FN])
        self.assertEqual(total, HIDDEN_FRAME + DEEP_FRAME)
        self.assertEqual([step[0] for step in chain], ["hiddenBody", "deepCallee"])

    def test_a_body_still_data_stays_undecoded_and_counted(self):
        self.assertEqual(self.image.funcs[STILL_DATA].frame_kind, "undecoded")
        self.assertEqual(self.image.data_bodies, {STILL_DATA})
        # Printed as data is not "could not be framing-checked".
        self.assertNotIn("stillData", self.image.unvalidated_bodies)

    def test_riscv_is_never_stripped(self):
        riscv = image("riscv")
        self.assertFalse(any(a[0].endswith("objcopy") for a in riscv.argvs))
        self.assertEqual(riscv.funcs[HIDDEN].frame_kind, "undecoded")

    def test_a_recovered_body_can_still_be_walked_from_its_archive(self):
        class Archives(sur.ArchiveBodies):
            def __init__(self):
                pass

            def body(self, name):
                return {"hiddenBody": (HIDDEN_FRAME, {"stillData"})}.get(name)

        adopted = self.image.adopt_archive_bodies(["hiddenBody"], Archives())
        self.assertEqual(adopted, ["hiddenBody"])
        with self.assertRaises(sur.Fatal):
            self.image.adopt_archive_bodies(["deepCallee"], Archives())


if __name__ == "__main__":
    unittest.main()
