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

    def test_a_body_whose_anchors_never_settle_is_not_validated(self):
        """The fixpoint is not monotone, so it can fail to settle.

        Adding an anchor splits the run it lands in, which can take
        instructions out of the real set that named anchors of their own. On
        the artoo-esp32 image one of 7,123 bodies oscillates rather than
        settles, an unsized pseudo-symbol (#429). The round cap is what
        terminates it, and what matters is where it lands: unvalidated, with
        every edge kept. Forced here by allowing no rounds at all, rather than by
        contriving a body that oscillates.
        """

        class NoRoundsImage(FakeImage):
            ANCHOR_ROUNDS = 0

        image = build_image(NoRoundsImage)
        self.assertIn("shifter", image.unvalidated_bodies)
        self.assertEqual(image.misframed_insns, 0)
        self.assertIn(
            VICTIM_ADDR, [t for t, _, _ in image.funcs[FN_ADDR].calls],
            "a body whose framing never settled must keep every edge objdump "
            "gave it; half-judging one is a guess, not a measurement",
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
# A phantom that names a branch target of its own
# =============================================================================
# A misframed instruction can decode as a BRANCH, and its target is then an
# address claimed to be a real instruction boundary that is not one. Seeding a
# re-framing run from it condemns every real instruction until the next anchor
# - including any call among them.
#
# Measured in `HTTPClient::setCookie` in the artoo-esp32 image: a phantom
# `blti` at 0x400e22e9 named 0x400e2305, and a real `call8 String::indexOf` at
# 0x400e2309 was reported misframed because of it. The fixture below is that
# body's byte stream, with the phantom's printed target moved to one byte
# before a real `call8` so the same interval split happens at a readable scale.
#
# The fixpoint in `_framing()` is what settles it: anchors are grown from the
# entry, and only an instruction the framing has reached from a proven boundary
# may name one. A phantom that only phantoms reach is never admitted - the
# section after this one is the case where pruning from every candidate
# instead let a set of them keep each other.

BOGUS_FN = 0x400E4000
BOGUS_FN_SIZE = 0x1E
BOGUS_HELPER = 0x400E5000

BOGUS_PAD_AT = BOGUS_FN + 0x06
BOGUS_ANCHOR = BOGUS_FN + 0x14       # what the phantom `blti` points at
BOGUS_REAL_CALL8 = BOGUS_FN + 0x15   # the real edge it would take down
BOGUS_JUMP_TARGET = BOGUS_FN + 0x1C

BOGUS_SYMBOLS = [
    "SYMBOL TABLE:",
    f"{BOGUS_FN:08x} g     F .flash.text\t{BOGUS_FN_SIZE:08x} cookieSetter",
    f"{BOGUS_HELPER:08x} g     F .flash.text\t00000008 indexOf",
]

BOGUS_LISTING = [
    "Disassembly of section .flash.text:",
    "",
    f"{BOGUS_FN:08x} <cookieSetter>:",
    "/repo/libraries/HTTPClient/src/HTTPClient.cpp:1520",
    f"{BOGUS_FN + 0x00:08x}:\t006136        \tentry\ta1, 48",
    f"{BOGUS_FN + 0x03:08x}:\t000386        \tj\t{BOGUS_JUMP_TARGET:08x} <cookieSetter+0x1c>",
    # --- one byte of padding at +0x06; the four lines below are what objdump
    #     emits instead, and the fourth of them decodes as a BRANCH ---------
    "/repo/libraries/HTTPClient/src/HTTPClient.cpp:1530",
    f"{BOGUS_FN + 0x06:08x}:\tc1a200        \tmul16u\ta10, a2, a0",
    f"{BOGUS_FN + 0x09:08x}:\t111020        \tslli\ta1, a0, 14",
    f"{BOGUS_FN + 0x0c:08x}:\t55a520        \textui\ta10, a2, 21, 6",
    f"{BOGUS_FN + 0x0f:08x}:\t180ca6        \tblti\ta12, -1, {BOGUS_ANCHOR:08x} <cookieSetter+0x14>",
    # --- objdump lands back on a real boundary here ----------------------
    "/repo/libraries/HTTPClient/src/HTTPClient.cpp:1535",
    f"{BOGUS_FN + 0x12:08x}:\t201110        \tor\ta1, a1, a1",
    f"{BOGUS_REAL_CALL8:08x}:\tffb865        \tcall8\t{BOGUS_HELPER:08x} <indexOf>",
    f"{BOGUS_FN + 0x18:08x}:\t080c          \tmovi.n\ta8, 0",
    f"{BOGUS_FN + 0x1a:08x}:\t0198          \tl32i.n\ta9, a1, 0",
    f"{BOGUS_JUMP_TARGET:08x}:\tf01d          \tretw.n",
    "",
    f"{BOGUS_HELPER:08x} <indexOf>:",
    "/repo/src/helper.cpp:9",
    f"{BOGUS_HELPER:08x}:\t002136        \tentry\ta1, 32",
    f"{BOGUS_HELPER + 3:08x}:\tf01d          \tretw.n",
]


class BogusAnchorImage(sur.Image):
    def _run(self, argv):
        return iter(BOGUS_SYMBOLS if "-t" in argv else BOGUS_LISTING)


class APhantomBranchMustNotAnchorAnything(unittest.TestCase):
    def setUp(self):
        self.image = BogusAnchorImage(
            "fake", Path("fake.elf"), Path("objdump"), "xtensa")
        self.fn = self.image.funcs[BOGUS_FN]

    def test_the_real_call_after_the_phantom_branch_survives(self):
        """Without the fixpoint this edge is the one that disappears."""
        self.assertEqual(
            [(target, insn) for target, insn, _ in self.fn.calls],
            [(BOGUS_HELPER, "call8")],
            "the phantom blti's target was trusted as an instruction boundary, "
            "which splits the interval holding the real call8 and condemns it",
        )

    def test_the_phantom_branchs_target_is_not_a_boundary(self):
        insns = {}
        body_bytes = {}
        for line in BOGUS_LISTING:
            got = sur.split_insn(line)
            if got is None:
                continue
            pc, nbytes, mnem, ops, raw = got
            if not BOGUS_FN <= pc < BOGUS_FN + BOGUS_FN_SIZE:
                continue
            insns[pc] = (nbytes, mnem, ops)
            octets = [int(raw[i:i + 2], 16) for i in range(0, len(raw), 2)][::-1]
            for offset, value in enumerate(octets):
                body_bytes[pc + offset] = value
        # The candidate set does contain it: that is the whole hazard.
        self.assertIn(
            BOGUS_ANCHOR,
            self.image._anchors(
                BOGUS_FN, BOGUS_FN, BOGUS_FN + BOGUS_FN_SIZE, insns),
        )
        real = self.image._framing(
            BOGUS_FN, BOGUS_FN + BOGUS_FN_SIZE, insns, body_bytes)
        self.assertIsNotNone(real)
        self.assertNotIn(BOGUS_ANCHOR, real)
        self.assertNotIn(BOGUS_PAD_AT, real)
        self.assertIn(BOGUS_REAL_CALL8, real)

    def test_only_the_four_phantoms_are_rejected(self):
        self.assertEqual(self.image.misframed_insns, 4)
        self.assertEqual(self.image.suppressed_calls, [])
        self.assertEqual(self.image.unvalidated_bodies, set())
        # 11 instruction lines in this body, 4 of them misframed.
        self.assertEqual(self.fn.decoded, 7)

    def test_the_chain_still_reaches_the_real_callee(self):
        walker = sur.Walker([self.image], sur.DEFAULT_PRUNE)
        total, chain, _ = walker.depth(self.image, self.fn)
        self.assertEqual(total, 32)
        self.assertEqual([entry[0] for entry in chain], ["indexOf"])


# =============================================================================
# Phantoms that name each other
# =============================================================================
# The fixpoint above used to START from every candidate anchor and drop the
# unsupported ones. That removes a phantom anchor only if nothing real-looking
# names it, and a desynchronised window can hold several phantoms that do
# exactly that for each other: framing from one bogus anchor makes a phantom
# branch "real", and that phantom names the bogus anchor back. Such a set
# survives every round, and the run it seeds condemns real code behind it.
#
# Measured in `seqStorePrepare` in the artoo-esp32 image (#429). After the `j`
# at 0x400fe6d5 there is one byte of padding; the real code resumes at
# 0x400fe6d9, which the `beqz.n` at 0x400fe6b1 names. Framed from 0x400fe6da
# instead - one byte on - the run lands on objdump's own phantoms: the
# `bnez.n` at 0x400fe6e3, the `blt`s at 0x400fe6e7 and 0x400fe6ea and the
# `bany` at 0x400fe6ed, which names 0x400fe6da again. The `blt` at 0x400fe6e7
# names 0x400fe72d, one byte into a real `l32i`, and the run from there frames
# 0x400fe72d, 0x400fe730, 0x400fe733... past the real 0x400fe735, 0x400fe74a,
# 0x400fe753 and 0x400fe75e: the calls to seqJsonParseVariant, protocolCheck,
# unlock and stagingFree (src/seq_store.cpp:276-281). SeqDisp walked 3,680 B
# against its recorded 4,432 B, with growth under protocolCheck invisible.
#
# The fixture is that body's bytes at their real addresses, from the `beqz.n`
# through the stagingFree call, with an `entry` in front and a `retw.n` after.
# The call targets are fixture helpers; protocolCheck's frame is the large one,
# so a dropped edge shows as depth.

SEQ_FN = 0x400FE6AE
SEQ_FN_SIZE = 0xB5
SEQ_UNLOCK = 0x400E6000
SEQ_PCFAIL = 0x400E6100
SEQ_RMDTOR = 0x400E6200
SEQ_STAGING_ALLOC = 0x400E6300
SEQ_PARSE_VARIANT = 0x400E6400
SEQ_PROTOCOL_CHECK = 0x400E6500
SEQ_STAGING_FREE = 0x400E6600

SEQ_REAL_RESUME = 0x400FE6D9        # after the pad; the `beqz.n` names it
SEQ_SELF_ANCHOR = 0x400FE6DA        # the phantom anchor that names itself
SEQ_CONDEMNING_ANCHOR = 0x400FE72D  # named from inside that phantom run
SEQ_PHANTOM_BRANCHES = (0x400FE6E3, 0x400FE6E7, 0x400FE6EA, 0x400FE6ED)
SEQ_HIDDEN_CALLS = {
    0x400FE735: SEQ_PARSE_VARIANT,
    0x400FE74A: SEQ_PROTOCOL_CHECK,
    0x400FE753: SEQ_UNLOCK,
    0x400FE75E: SEQ_STAGING_FREE,
}
SEQ_PROTOCOL_CHECK_FRAME = 2048

SEQ_HELPERS = [
    (SEQ_UNLOCK, "unlock()", 32),
    (SEQ_PCFAIL, "pcFail(char const*, char const*)", 48),
    (SEQ_RMDTOR, "ResourceManager::~ResourceManager()", 32),
    (SEQ_STAGING_ALLOC, "stagingAlloc(SeqStaging&)", 64),
    (SEQ_PARSE_VARIANT, "seqJsonParseVariant(SeqDraft&)", 96),
    (SEQ_PROTOCOL_CHECK, "protocolCheck(SeqDraft&)", SEQ_PROTOCOL_CHECK_FRAME),
    (SEQ_STAGING_FREE, "stagingFree(SeqStaging&)", 32),
]

SEQ_SYMBOLS = ["SYMBOL TABLE:",
               f"{SEQ_FN:08x} g     F .flash.text\t{SEQ_FN_SIZE:08x} seqStorePrepare(char const*)"]
SEQ_SYMBOLS += [f"{addr:08x} l     F .flash.text\t00000005 {name}"
                for addr, name, _ in SEQ_HELPERS]


def _seq(pc, raw, mnem, ops=""):
    return f"{pc:08x}:\t{raw:<14}\t{mnem}\t{ops}".rstrip()


def _seq_target(addr):
    return f"{addr:08x} <seqStorePrepare(char const*)+0x{addr - SEQ_FN:x}>"


SEQ_LISTING = [
    "Disassembly of section .flash.text:",
    "",
    f"{SEQ_FN:08x} <seqStorePrepare(char const*)>:",
    "/repo/src/seq_store.cpp:240",
    _seq(SEQ_FN, "00b136", "entry", "a1, 88"),
    _seq(0x400FE6B1, "44ac", "beqz.n", f"a4, {_seq_target(SEQ_REAL_RESUME)}"),
    _seq(0x400FE6B3, "201110", "or", "a1, a1, a1"),
    _seq(0x400FE6B6, "fe9ce5", "call8", f"{SEQ_UNLOCK:08x} <unlock()>"),
    _seq(0x400FE6B9, "4ff781", "l32r", "a8, 400d2698 <_stext+0x2678>"),
    _seq(0x400FE6BC, "a04480", "addx4", "a4, a4, a8"),
    _seq(0x400FE6BF, "04c8", "l32i.n", "a12, a4, 0"),
    _seq(0x400FE6C1, "5006b1", "l32r", "a11, 400d26dc <_stext+0x26bc>"),
    _seq(0x400FE6C4, "02ad", "mov.n", "a10, a2"),
    _seq(0x400FE6C6, "201110", "or", "a1, a1, a1"),
    _seq(0x400FE6C9, "f97c25", "call8", f"{SEQ_PCFAIL:08x} <pcFail(char const*, char const*)>"),
    _seq(0x400FE6CC, "40c1a2", "addi", "a10, a1, 64"),
    _seq(0x400FE6CF, "201110", "or", "a1, a1, a1"),
    _seq(0x400FE6D2, "d97765", "call8", f"{SEQ_RMDTOR:08x} <ResourceManager::~ResourceManager()>"),
    _seq(0x400FE6D5, "ffe0c6", "j", "400fe65c <seqRetry()>"),
    # --- one byte of padding at 0x400fe6d8; the real code resumes at 6d9 ---
    _seq(0x400FE6D8, "c14200", "mul16u", "a4, a2, a0"),
    _seq(0x400FE6DB, "c46240", "extui", "a6, a4, 2, 13"),
    _seq(0x400FE6DE, "a238", "l32i.n", "a3, a2, 40"),
    _seq(0x400FE6E0, "0caca0", "lsi", "f10, a12, 48"),
    _seq(0x400FE6E3, "0ccc", "bnez.n", f"a12, {_seq_target(0x400FE6E7)}"),
    _seq(0x400FE6E5, "620b", "addi.n", "a6, a2, -1"),
    _seq(0x400FE6E7, "422967", "blt", f"a9, a6, {_seq_target(SEQ_CONDEMNING_ANCHOR)}"),
    _seq(0x400FE6EA, "aa2a67", "blt", "a10, a6, 400fe698 <seqEarlier()>"),
    _seq(0x400FE6ED, "e981a7", "bany", f"a1, a10, {_seq_target(SEQ_SELF_ANCHOR)}"),
    _seq(0x400FE6F0, "e04c", "movi.n", "a0, 78"),
    _seq(0x400FE6F2, "0008", "l32i.n", "a0, a0, 0"),
    # --- objdump is back on a real boundary here -----------------------------
    _seq(0x400FE6F4, "1cc1c2", "addi", "a12, a1, 28"),
    _seq(0x400FE6F7, "06ad", "mov.n", "a10, a6"),
    _seq(0x400FE6F9, "04bd", "mov.n", "a11, a4"),
    _seq(0x400FE6FB, "201110", "or", "a1, a1, a1"),
    _seq(0x400FE6FE, "fe90a5", "call8", f"{SEQ_STAGING_ALLOC:08x} <stagingAlloc(SeqStaging&)>"),
    _seq(0x400FE701, "bacc", "bnez.n", f"a10, {_seq_target(0x400FE710)}"),
    _seq(0x400FE703, "201110", "or", "a1, a1, a1"),
    _seq(0x400FE706, "fe97e5", "call8", f"{SEQ_UNLOCK:08x} <unlock()>"),
    _seq(0x400FE709, "4ff5c1", "l32r", "a12, 400d26e0 <_stext+0x26c0>"),
    _seq(0x400FE70C, "ffec46", "j", _seq_target(0x400FE6C1)),
    # --- one byte of padding at 0x400fe70f; the real code resumes at 710 ---
    _seq(0x400FE70F, "a08200", "addx4", "a8, a2, a0"),
    _seq(0x400FE712, "418af3", "lsip", "f15, a10, 0x104"),
    _seq(0x400FE715, "80a082", "movi", "a8, 128"),
    _seq(0x400FE718, "818a", "add.n", "a8, a1, a8"),
    _seq(0x400FE71A, "1189", "s32i.n", "a8, a1, 4"),
    _seq(0x400FE71C, "b50782", "l8ui", "a8, a7, 181"),
    _seq(0x400FE71F, "04ad", "mov.n", "a10, a4"),
    _seq(0x400FE721, "0189", "s32i.n", "a8, a1, 0"),
    _seq(0x400FE723, "2c27f2", "l32i", "a15, a7, 176"),
    _seq(0x400FE726, "b407e2", "l8ui", "a14, a7, 180"),
    _seq(0x400FE729, "2b27d2", "l32i", "a13, a7, 172"),
    _seq(0x400FE72C, "2927b2", "l32i", "a11, a7, 164"),
    _seq(0x400FE72F, "2a27c2", "l32i", "a12, a7, 168"),
    "/repo/src/seq_store.cpp:277",
    _seq(0x400FE732, "201110", "or", "a1, a1, a1"),
    _seq(0x400FE735, "fda1a5", "call8", f"{SEQ_PARSE_VARIANT:08x} <seqJsonParseVariant(SeqDraft&)>"),
    _seq(0x400FE738, "000462", "l8ui", "a6, a4, 0"),
    _seq(0x400FE73B, "011616", "beqz", f"a6, {_seq_target(0x400FE750)}"),
    _seq(0x400FE73E, "80a082", "movi", "a8, 128"),
    _seq(0x400FE741, "80b180", "add", "a11, a1, a8"),
    _seq(0x400FE744, "20a440", "or", "a10, a4, a4"),
    "/repo/src/seq_store.cpp:278",
    _seq(0x400FE747, "201110", "or", "a1, a1, a1"),
    _seq(0x400FE74A, "fa62a5", "call8", f"{SEQ_PROTOCOL_CHECK:08x} <protocolCheck(SeqDraft&)>"),
    _seq(0x400FE74D, "000462", "l8ui", "a6, a4, 0"),
    "/repo/src/seq_store.cpp:279",
    _seq(0x400FE750, "201110", "or", "a1, a1, a1"),
    _seq(0x400FE753, "fe9325", "call8", f"{SEQ_UNLOCK:08x} <unlock()>"),
    _seq(0x400FE756, "a6dc", "bnez.n", "a6, 400fe774 <seqLater()>"),
    _seq(0x400FE758, "1cc1a2", "addi", "a10, a1, 28"),
    "/repo/src/seq_store.cpp:281",
    _seq(0x400FE75B, "201110", "or", "a1, a1, a1"),
    _seq(0x400FE75E, "fe88a5", "call8", f"{SEQ_STAGING_FREE:08x} <stagingFree(SeqStaging&)>"),
    _seq(0x400FE761, "f01d", "retw.n"),
]
for _addr, _name, _frame in SEQ_HELPERS:
    SEQ_LISTING += [
        "",
        f"{_addr:08x} <{_name}>:",
        "/repo/src/helper.cpp:9",
        _seq(_addr, f"{(_frame // 8) << 12 | 0x136:06x}", "entry", f"a1, {_frame}"),
        _seq(_addr + 3, "f01d", "retw.n"),
    ]


class SeqStorePrepareImage(sur.Image):
    def _run(self, argv):
        return iter(SEQ_SYMBOLS if "-t" in argv else SEQ_LISTING)


def _seq_body():
    insns, body_bytes = {}, {}
    for line in SEQ_LISTING:
        got = sur.split_insn(line)
        if got is None:
            continue
        pc, nbytes, mnem, ops, raw = got
        if not SEQ_FN <= pc < SEQ_FN + SEQ_FN_SIZE:
            continue
        insns[pc] = (nbytes, mnem, ops)
        octets = [int(raw[i:i + 2], 16) for i in range(0, len(raw), 2)][::-1]
        for offset, value in enumerate(octets):
            body_bytes[pc + offset] = value
    return insns, body_bytes


class PhantomsThatNameEachOtherAnchorNothing(unittest.TestCase):
    def setUp(self):
        self.image = SeqStorePrepareImage(
            "fake", Path("fake.elf"), Path("objdump"), "xtensa")
        self.fn = self.image.funcs[SEQ_FN]

    def test_the_self_supporting_anchors_are_not_boundaries(self):
        insns, body_bytes = _seq_body()
        real = self.image._framing(SEQ_FN, SEQ_FN + SEQ_FN_SIZE, insns, body_bytes)
        self.assertIsNotNone(real)
        self.assertIn(SEQ_REAL_RESUME, real)
        for bogus in (SEQ_SELF_ANCHOR, SEQ_CONDEMNING_ANCHOR) + SEQ_PHANTOM_BRANCHES:
            self.assertNotIn(bogus, real, f"0x{bogus:08x} is a phantom boundary")
        for pc in SEQ_HIDDEN_CALLS:
            self.assertIn(pc, real, f"the real call at 0x{pc:08x} was condemned")

    def test_the_four_calls_behind_the_window_are_followed(self):
        edges = {(target, insn) for target, insn, _ in self.fn.calls}
        for target in SEQ_HIDDEN_CALLS.values():
            self.assertIn((target, "call8"), edges)
        self.assertEqual(self.image.suppressed_calls, [])
        walker = sur.Walker([self.image], sur.DEFAULT_PRUNE)
        total, chain, _ = walker.depth(self.image, self.fn)
        self.assertEqual(total, SEQ_PROTOCOL_CHECK_FRAME)
        self.assertEqual([entry[0] for entry in chain], ["protocolCheck(SeqDraft&)"])


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



# A frame too large for `addi` can arrive through a register the prologue
# loads with a constant. The ESP32-P4 Console op that answers the last-run query
# (#427) is exactly this, bytes as linked:
#
#     addi sp,sp,-144   lui t0,0xffffe   ...   add sp,sp,t0
#
# a fixed 8,336 B frame. Read as "dynamic", it counted 144 B, and even with the
# op table stitched (#429) the walk could not see the 8 KB that overflowed.

RV_BIG = 0x40021900
RV_BIG_SIZE = 0x14

RV_BIG_SYMBOLS = [
    "SYMBOL TABLE:",
    f"{RV_BIG:08x} l     F .text\t{RV_BIG_SIZE:08x} lastRunOp(unsigned long)",
]

RV_BIG_LISTING = [
    "Disassembly of section .text:",
    "",
    f"{RV_BIG:08x} <lastRunOp(unsigned long)>:",
    "/repo/src/console/console_module.cpp:900",
    f"{RV_BIG + 0x00:08x}:\t7175          \taddi\tsp,sp,-144",
    f"{RV_BIG + 0x02:08x}:\t72f9          \tlui\tt0,0xffffe",
    f"{RV_BIG + 0x04:08x}:\t6789          \tlui\ta5,0x2",
    f"{RV_BIG + 0x06:08x}:\tc706          \tsw\tra,140(sp)",
    f"{RV_BIG + 0x08:08x}:\t06c78713      \taddi\ta4,a5,108",
    f"{RV_BIG + 0x0c:08x}:\t9116          \tadd\tsp,sp,t0",
    f"{RV_BIG + 0x0e:08x}:\t970a          \tadd\ta4,a4,sp",
    f"{RV_BIG + 0x10:08x}:\t842a          \tmv\ts0,a0",
    f"{RV_BIG + 0x12:08x}:\t8082          \tret",
]


class RiscvConstantFrameImage(sur.Image):
    LISTING = RV_BIG_LISTING

    def _run(self, argv):
        return iter(RV_BIG_SYMBOLS if "-t" in argv else self.LISTING)


class ARegisterSizedFrameOfAKnownConstantIsFixed(unittest.TestCase):
    def _frame(self, cls):
        fn = cls("fake", Path("fake.elf"), Path("objdump"), "riscv").funcs[RV_BIG]
        return fn.frame, fn.frame_kind

    def test_the_lui_loaded_adjustment_is_counted(self):
        self.assertEqual(self._frame(RiscvConstantFrameImage), (144 + 8192, "fixed"))

    def test_a_register_the_prologue_overwrote_is_still_variable(self):
        class Overwritten(RiscvConstantFrameImage):
            # `add t0,t0,a0` before the adjustment: t0 no longer holds the
            # constant, so the frame is variable and only the addi is counted.
            LISTING = [line.replace("\tlui\ta5,0x2", "\tadd\tt0,t0,a0")
                       for line in RV_BIG_LISTING]
        self.assertEqual(self._frame(Overwritten), (144, "dynamic"))


if __name__ == "__main__":
    unittest.main()
