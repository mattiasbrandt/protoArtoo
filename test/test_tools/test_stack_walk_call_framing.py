"""A call edge is only followed from a real instruction boundary (#401).

`objdump -d` decodes a section as one linear byte stream. A function body can
contain a byte that no instruction starts at -- alignment padding after an
unconditional jump -- and objdump cannot know: it consumes the pad as the first
byte of the next instruction and emits several MISFRAMED instructions, built
from the tails and heads of real ones, before it lands on a real boundary
again. Some of those decode as calls, and a call's target is PC-relative, so it
moves with the link: in one build it points into the middle of a function and
is reported unresolved, in the next it lands exactly on some unrelated
function's entry and becomes a followed edge.

Measured in the artoo-esp32 image inside newlib's `__lshift`, which is where
this was found:

    401a8280:  86 03 00      j     401a8292     <- real
    401a8283:  00                                <- one byte of padding
    401a8284:  82 c3 15      addi  a8, a3, 21    <- real; the `bgeu` above it
    401a8287:  80 80 60      neg   a8, a8           points here
    401a828a:  8a 8c         add.n a8, a12, a8

read linearly from the jump as:

    401a8283:  c3 82 00      movf  a8, a2, b0    <- phantom
    401a8286:  15 80 80      call4 <an entry>    <- phantom CALL
    401a8289:  60 8a 8c      lsi   f6, a10, 0x230

`__lshift` was reported as calling `esp_task_wdt_deinit`, which it does not,
and the fabricated 96-byte subtree under it failed eight of twelve tasks on a
slice that had touched none of them. The #271 symbol-size bound cannot catch
this: the padding is INSIDE `[addr, addr + size)`.

The fixture below is that byte stream, with an `entry` prologue in front of it
and the phantom's printed target pointed at a symbol with a large frame, so a
followed edge shows up as depth rather than only as a name. The victim's frame
is 32x the real callee's, which is what makes "the walk got deeper for no
source change" the thing under test.

Both directions are pinned, because a fix for this that over-suppresses is the
same defect pointed the other way: the real `call8` in the same body must
survive, and the chain must be the real callee's depth and not the phantom's.
"""

import sys
import unittest
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPO_ROOT / "tools"))

import stack_usage_report as sur  # noqa: E402


FN_ADDR = 0x400E1000
FN_SIZE = 0x27
HELPER_ADDR = 0x400E2000     # the real callee: small frame
VICTIM_ADDR = 0x400E3000     # what the phantom call4 points at: large frame

PAD_AT = FN_ADDR + 0x0E      # the one byte of padding after the `j`
PHANTOM_MOVF = FN_ADDR + 0x0E
PHANTOM_CALL4 = FN_ADDR + 0x11
PHANTOM_LSI = FN_ADDR + 0x14
BRANCH_TARGET = FN_ADDR + 0x0F   # where the `bgeu` points: a real boundary
JUMP_TARGET = FN_ADDR + 0x1D     # where the `j` points: a real boundary
REAL_CALL8 = FN_ADDR + 0x22      # the real edge that must survive

HELPER_FRAME = 32
VICTIM_FRAME = 1024

SYMBOL_TABLE = [
    "SYMBOL TABLE:",
    f"{FN_ADDR:08x} g     F .flash.text\t{FN_SIZE:08x} shifter",
    f"{HELPER_ADDR:08x} g     F .flash.text\t00000008 realCallee",
    f"{VICTIM_ADDR:08x} g     F .flash.text\t00000008 unrelatedVictim",
]

# objdump's instruction lines are tab-separated: address, raw bytes, mnemonic,
# operands, and it prints the raw bytes most-significant first -- `15c382` is
# the three bytes 82 c3 15. Every raw field below is the real byte stream from
# the measurement above, so the framing arithmetic under test is the real one
# and not a convenient one.
DISASSEMBLY = [
    "Disassembly of section .flash.text:",
    "",
    f"{FN_ADDR:08x} <shifter>:",
    "/build/newlib/libc/stdlib/mprec.c:464",
    f"{FN_ADDR + 0x00:08x}:\t006136        \tentry\ta1, 48",
    "/build/newlib/libc/stdlib/mprec.c:494",
    f"{FN_ADDR + 0x03:08x}:\t15c382        \taddi\ta8, a3, 21",
    f"{FN_ADDR + 0x06:08x}:\t05bc87        \tbgeu\ta12, a8, {BRANCH_TARGET:08x} <shifter+0xf>",
    f"{FN_ADDR + 0x09:08x}:\t080c          \tmovi.n\ta8, 0",
    f"{FN_ADDR + 0x0b:08x}:\t000386        \tj\t{JUMP_TARGET:08x} <shifter+0x1d>",
    # --- one byte of padding at FN_ADDR+0x0e; objdump does not know, and the
    #     three lines below are what it emits instead ---------------------
    "/build/newlib/libc/stdlib/mprec.c:495",
    f"{PHANTOM_MOVF:08x}:\tc38200        \tmovf\ta8, a2, b0",
    f"{PHANTOM_CALL4:08x}:\t808015        \tcall4\t{VICTIM_ADDR:08x} <unrelatedVictim>",
    f"{PHANTOM_LSI:08x}:\t8c8a60        \tlsi\tf6, a10, 0x230",
    # --- objdump lands back on a real boundary here ----------------------
    f"{FN_ADDR + 0x17:08x}:\t418280        \tsrli\ta8, a8, 2",
    f"{FN_ADDR + 0x1a:08x}:\t1188e0        \tslli\ta8, a8, 2",
    f"{JUMP_TARGET:08x}:\t0198          \tl32i.n\ta9, a1, 0",
    "/build/newlib/libc/stdlib/mprec.c:518",
    f"{FN_ADDR + 0x1f:08x}:\t201110        \tor\ta1, a1, a1",
    f"{REAL_CALL8:08x}:\tffb865        \tcall8\t{HELPER_ADDR:08x} <realCallee>",
    f"{FN_ADDR + 0x25:08x}:\tf01d          \tretw.n",
    "",
    f"{HELPER_ADDR:08x} <realCallee>:",
    "/repo/src/helper.cpp:9",
    f"{HELPER_ADDR:08x}:\t002136        \tentry\ta1, {HELPER_FRAME}",
    f"{HELPER_ADDR + 3:08x}:\tf01d          \tretw.n",
    "",
    f"{VICTIM_ADDR:08x} <unrelatedVictim>:",
    "/repo/src/unrelated.cpp:9",
    f"{VICTIM_ADDR:08x}:\t004136        \tentry\ta1, {VICTIM_FRAME}",
    f"{VICTIM_ADDR + 3:08x}:\tf01d          \tretw.n",
]


class FakeImage(sur.Image):
    """sur.Image with objdump replaced by the canned listings above."""

    LISTING = DISASSEMBLY

    def _run(self, argv):
        return iter(SYMBOL_TABLE if "-t" in argv else self.LISTING)


def build_image(cls=FakeImage):
    return cls("fake", Path("fake.elf"), Path("objdump"), "xtensa")


class MisframedCallIsNotAnEdge(unittest.TestCase):
    def setUp(self):
        self.image = build_image()
        self.fn = self.image.funcs[FN_ADDR]

    def test_the_phantom_call_is_not_a_call_edge(self):
        """The defect itself."""
        targets = [target for target, _, _ in self.fn.calls]
        self.assertNotIn(
            VICTIM_ADDR, targets,
            "a misframed decode became a followed call edge - this is the "
            "fabricated edge that added 96 bytes to eight tasks (#401)",
        )

    def test_the_real_call_in_the_same_body_survives(self):
        """The other direction: over-suppression is the same defect reversed."""
        self.assertEqual(
            [(target, insn) for target, insn, _ in self.fn.calls],
            [(HELPER_ADDR, "call8")],
            "the real call8 after the desynchronised window was dropped; a fix "
            "that loses real edges turns the gate blind instead of honest",
        )

    def test_the_chain_is_the_real_callees_depth(self):
        """The behaviour the constants are compared against."""
        walker = sur.Walker([self.image], sur.DEFAULT_PRUNE)
        total, chain, _ = walker.depth(self.image, self.fn)
        self.assertEqual(total, HELPER_FRAME)
        self.assertEqual([entry[0] for entry in chain], ["realCallee"])

    def test_the_suppressed_edge_is_reported_not_swallowed(self):
        """An edge dropped in silence is as dishonest as one invented."""
        self.assertEqual(
            self.image.suppressed_calls,
            [("shifter", PHANTOM_CALL4, VICTIM_ADDR, "mprec.c:495")],
        )

    def test_only_the_three_misframed_decodes_are_rejected(self):
        """Every other instruction in the body is still read."""
        self.assertEqual(self.image.misframed_insns, 3)
        self.assertEqual(self.image.misframed_funcs, {"shifter"})
        self.assertEqual(self.image.unvalidated_bodies, set())
        # 14 instruction lines in the listing for this body, 3 misframed.
        self.assertEqual(self.fn.decoded, 11)

    def test_the_frame_is_still_the_real_prologue(self):
        self.assertEqual(self.fn.frame, 48)
        self.assertEqual(self.fn.frame_kind, "fixed")

    def test_the_padding_byte_is_not_a_boundary(self):
        insns = {}
        body_bytes = {}
        for line in DISASSEMBLY:
            got = sur.split_insn(line)
            if got is None:
                continue
            pc, nbytes, mnem, ops, raw = got
            if not FN_ADDR <= pc < FN_ADDR + FN_SIZE:
                continue
            insns[pc] = (nbytes, mnem, ops)
            octets = [int(raw[i:i + 2], 16) for i in range(0, len(raw), 2)][::-1]
            for offset, value in enumerate(octets):
                body_bytes[pc + offset] = value
        real = self.image._framing(
            FN_ADDR, FN_ADDR + FN_SIZE, insns, body_bytes)
        self.assertIsNotNone(real)
        self.assertNotIn(PAD_AT, real)
        self.assertIn(BRANCH_TARGET, real)
        self.assertIn(JUMP_TARGET, real)
        self.assertIn(REAL_CALL8, real)


class LengthRuleIsCheckedNotAssumed(unittest.TestCase):
    """The framing rests on one byte deciding an instruction's length.

    That is true of both encodings this tool reads, but it is checked against
    objdump's own byte count for every instruction rather than trusted: a
    toolchain that framed differently would otherwise have its whole image
    silently re-judged by a rule that does not hold for it. Where the two
    disagree the body is not validated, and every edge in it is kept.
    """

    def test_the_rule_matches_objdump_on_every_instruction_in_the_fixture(self):
        image = build_image()
        for line in DISASSEMBLY:
            got = sur.split_insn(line)
            if got is None:
                continue
            _pc, nbytes, mnem, _ops, raw = got
            if mnem.startswith("."):
                continue
            byte0 = int(raw[-2:], 16)
            self.assertEqual(
                image._insn_len(byte0), nbytes,
                f"the length rule disagrees with objdump on {mnem!r}",
            )

    def test_a_body_whose_lengths_disagree_is_not_validated(self):
        class WrongLengthImage(FakeImage):
            # `movi.n` is a 2-byte opcode; claiming four bytes for it is a
            # listing this rule cannot read, so the body must be left alone.
            LISTING = [
                line.replace("\t080c          \tmovi.n", "\t080c0000      \tmovi.n")
                for line in DISASSEMBLY
            ]

        image = build_image(WrongLengthImage)
        self.assertIn("shifter", image.unvalidated_bodies)
        self.assertEqual(image.misframed_insns, 0)
        self.assertIn(
            VICTIM_ADDR, [t for t, _, _ in image.funcs[FN_ADDR].calls],
            "an unvalidated body must keep every edge objdump gave it, "
            "including the bad ones; suppressing on a rule that does not hold "
            "would be a guess wearing a measurement's clothes",
        )


# =============================================================================
# The RISC-V arm
# =============================================================================
# The same code path serves the ESP32-P4 image with its own length rule and its
# own set of unconditional transfers, and the Xtensa fixture above exercises
# neither.
#
# What it must NOT do there is find anything. The defect is structurally
# Xtensa's: Xtensa mixes 2- and 3-byte instructions at byte granularity, so a
# pad of one or two bytes is absorbed into the decode of whatever follows and
# the sweep loses alignment. RISC-V instructions are 2-byte aligned (IALIGN=16)
# and their length comes from the first halfword, so padding occupies whole
# instruction slots - 0x0000 and `c.nop` are each one - and a linear sweep
# cannot lose alignment. So the fixture below is an ORDINARY body, with the
# `c.nop` pad after a compressed jump that GCC really emits, and the assertion
# is that every instruction in it survives and every edge is kept.
#
# That is the half of #401 that protects the ESP32-P4 arm: a fix aimed at the
# Xtensa image must not quietly shorten the chains on a board whose constants
# were measured without it.

RV_FN = 0x4FF00000
RV_FN_SIZE = 0x18
RV_HELPER = 0x4FF02000

RV_SYMBOLS = [
    "SYMBOL TABLE:",
    f"{RV_FN:08x} g     F .text\t{RV_FN_SIZE:08x} rvCaller",
    f"{RV_HELPER:08x} g     F .text\t00000008 rvRealCallee",
]

# addi sp,sp,-32 | beqz a0,+0x12 | c.j +0x0c | c.nop (the pad GCC emits to
# 4-align the branch target) | jal <helper> | c.nop | ret
RV_LISTING = [
    "Disassembly of section .text:",
    "",
    f"{RV_FN:08x} <rvCaller>:",
    "/repo/src/rv.c:10",
    f"{RV_FN + 0x00:08x}:\tfe010113      \taddi\tsp,sp,-32",
    f"{RV_FN + 0x04:08x}:\tc519          \tbeqz\ta0,{RV_FN + 0x12:08x} <rvCaller+0x12>",
    f"{RV_FN + 0x06:08x}:\ta029          \tc.j\t{RV_FN + 0x0c:08x} <rvCaller+0xc>",
    f"{RV_FN + 0x08:08x}:\t0001          \tc.nop",
    f"{RV_FN + 0x0a:08x}:\t0001          \tc.nop",
    "/repo/src/rv.c:12",
    f"{RV_FN + 0x0c:08x}:\t000f00ef      \tjal\t{RV_HELPER:08x} <rvRealCallee>",
    f"{RV_FN + 0x10:08x}:\t0001          \tc.nop",
    f"{RV_FN + 0x12:08x}:\t8082          \tret",
    "",
    f"{RV_HELPER:08x} <rvRealCallee>:",
    "/repo/src/rv.c:20",
    f"{RV_HELPER:08x}:\tff010113      \taddi\tsp,sp,-16",
    f"{RV_HELPER + 4:08x}:\t8082          \tret",
]


class RiscvImage(sur.Image):
    def _run(self, argv):
        return iter(RV_SYMBOLS if "-t" in argv else RV_LISTING)


class RiscvArmIsUntouched(unittest.TestCase):
    def setUp(self):
        self.image = RiscvImage("fake", Path("fake.elf"), Path("objdump"), "riscv")
        self.fn = self.image.funcs[RV_FN]

    def test_the_length_rule_matches_objdump_on_every_instruction(self):
        for line in RV_LISTING:
            got = sur.split_insn(line)
            if got is None:
                continue
            _pc, nbytes, mnem, _ops, raw = got
            byte0 = int(raw[-2:], 16)
            self.assertEqual(
                self.image._insn_len(byte0), nbytes,
                f"the RISC-V length rule disagrees with objdump on {mnem!r}",
            )

    def test_nothing_in_an_ordinary_riscv_body_is_called_misframed(self):
        self.assertEqual(self.image.misframed_insns, 0)
        self.assertEqual(self.image.suppressed_calls, [])
        self.assertEqual(self.image.unvalidated_bodies, set())
        self.assertEqual(self.fn.decoded, 8)

    def test_the_call_after_the_c_nop_padding_is_still_an_edge(self):
        """The `c.nop` pad must not take the call behind it down with it."""
        self.assertEqual(
            [(target, insn) for target, insn, _ in self.fn.calls],
            [(RV_HELPER, "jal")],
        )

    def test_the_frame_and_chain_are_read_from_the_real_prologue(self):
        self.assertEqual(self.fn.frame, 32)
        self.assertEqual(self.fn.frame_kind, "fixed")
        walker = sur.Walker([self.image], sur.DEFAULT_PRUNE)
        total, chain, _ = walker.depth(self.image, self.fn)
        self.assertEqual(total, 16)
        self.assertEqual([entry[0] for entry in chain], ["rvRealCallee"])


if __name__ == "__main__":
    unittest.main()
