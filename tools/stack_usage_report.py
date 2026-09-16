#!/usr/bin/env python3
"""Worst-case static stack report for one built PlatformIO environment.

WHY THIS EXISTS
---------------
Task stack sizes in src/main.cpp are literals justified by a measured
high-water mark (HWM). An HWM only ever reports the deepest path that
actually ran, so it cannot say whether a branch that has not run yet is
deeper. Issue #245 needs the other half of that evidence on both chip
targets: the compiler's own frame size for every function on the call chain
below a task entry point, and which source branch of the task owns the
deepest one.

WHAT IT MEASURES
----------------
Two independent sources, both reported so they can be compared:

1. ``-fstack-usage`` (the compiler's declaration). Emitted for project code
   only, by building with the flag in ``PLATFORMIO_BUILD_SRC_FLAGS`` -- see
   USAGE below. Written by GCC next to each object file as ``<name>.su``.
   This covers ``src/`` and nothing else.

2. The linked image (what actually ships). Every function's frame comes from
   its prologue's stack-pointer adjustment, and the call graph from its
   direct call instructions. This covers framework, newlib and ROM code as
   well, which is where the ``printf`` chain lives and which ``-fstack-usage``
   cannot see at all.

Source 2 is the one the worst-case chain is computed from, because it is the
only one that reaches libc. Source 1 is printed beside it as the compiler's
declaration for the same functions; a disagreement is reported, never netted
out.

WHERE A FUNCTION BODY ENDS
--------------------------
At ``symbol address + symbol size``, read from the ELF symbol table -- NOT at
the next symbol ``objdump`` happens to print.

This matters more than it sounds. ``objdump -d`` disassembles whatever lies
between one symbol header and the next, and on Xtensa what lies immediately
after a function's ``retw`` is usually its literal pool: raw address words. Some
of those words decode as ``call8``/``call12`` instructions, and a few of those
"calls" land exactly on some unrelated function's entry. The walk then followed
an edge that does not exist.

Reading a body to the next header made ``rcDiagnosticsSourceName`` -- an
enum-to-string mapper, 29 bytes, one ``retw.n`` -- into a 3002-instruction
function with call edges into mdns, mbedtls, lwIP and the WiFi driver. Two of
those edges landed on real function entries and were followed, and *which* two
depends on where the literal words happen to point, which a relinked image
changes. Two builds of identical source whose only difference was a
compiled-in branch name returned WebEvents chains 688 bytes apart (#271).

So: instructions outside ``[addr, addr + size)`` are not decoded, do not
contribute frames, and yield no call edges, and ``owner_of()`` resolves an
address in the gap after a function to nothing rather than to that function.
The 1% of function symbols carrying no size are still bounded by the next
symbol, and are counted in the coverage report.

WHICH BYTES INSIDE A BODY ARE CODE
----------------------------------
Only the ones that sit on a real instruction boundary -- see
``_true_boundaries()``.

The bound above is necessary and not sufficient. ``objdump -d`` decodes a
section as one linear byte stream, and a body can contain a byte that no
instruction starts at: alignment padding after an unconditional jump, usually a
single ``0x00``. objdump consumes it as the first byte of the next instruction
and emits several MISFRAMED instructions, built from the tails and heads of
real ones, before it lands on a real boundary again. Some of those decode as
calls, and a call's target on both ports is PC-relative, so it moves with the
link: in one build it points into the middle of a function and is reported
unresolved, in the next it lands exactly on some unrelated function's entry and
becomes a followed edge.

That is what made newlib's ``__lshift`` appear to call ``esp_task_wdt_deinit``
and put a fabricated 96 bytes on eight of twelve tasks -- every one whose chain
runs through ``printf`` float formatting -- in a build whose source touched none
of them (#401). The padding is *inside* the symbol size, so the #271 bound
cannot see it.

What IS provable is the framing, from nothing objdump has not already printed.
Every in-body branch target is a boundary the hardware guarantees, and an
instruction's length comes from its first byte on both ports, so re-framing
forward from a branch target says exactly which of objdump's addresses are
real. An address that is not yields no call edge and no literal.

Reachability would be the wrong test, and was tried first: objdump's own
listing is what is wrong, so a branch target inside a desynchronised window is
an address the listing never framed, and "unreachable" then condemns most of
the image -- measured at 3,388 real calls dropped, against 5 fabricated ones.

The defect is the Xtensa image's and not the ESP32-P4's. Xtensa mixes 2- and
3-byte instructions at byte granularity, so a pad of one or two bytes vanishes
into the next decode; RISC-V instructions are 2-byte aligned and their length
comes from the first halfword, so padding takes whole instruction slots and a
linear sweep cannot lose alignment. The check runs on both arms regardless --
it is what shows the RISC-V arm has nothing to report, and on the firebeetle2
image it reports exactly that: no suppressed edge, and a walk byte-identical to
the one before this check existed.

Misframed decodes are counted in the coverage report, and any that decoded as a
direct call onto a real entry -- the fabricated edges -- are listed by name.

WHAT IT CANNOT SEE, AND SAYS SO
-------------------------------
- Indirect calls (``callx*`` on Xtensa through a register that was not loaded
  from a literal, ``jalr`` on RISC-V through a computed address). Every one
  encountered is listed by address and source line. A chain through an
  indirect call is not followed, so a reported total is a lower bound unless
  the indirect-call list is empty.
- Code in a region carrying no function symbol at all. It is attributed to
  nothing rather than to whichever symbol precedes it, so a chain through one
  stops there: a lower bound rather than an invented path.
- Interrupt and exception frames. ESP-IDF gives interrupts their own stack
  (``CONFIG_FREERTOS_ISR_STACKSIZE``), but the entry sequence on both ports
  spills some state to the interrupted task's stack before switching. That
  cost is not part of a call chain and is not counted here.
- Recursion. A cycle in the call graph is cut and reported; the total for a
  chain through a cut edge is a lower bound.
- The real instructions inside a desynchronised window. They are known to be
  there -- the re-framing finds their boundaries -- but objdump never printed
  them, so their frames and calls are not read. Counted, and the same
  direction as every other gap here: a chain can be missed, never invented.
- Tail calls (shown as ``tail-j``) are counted as if both frames were live at
  once. A real tail call releases the caller's frame first, so a chain through
  one is an over-estimate -- the safe direction for sizing, but not exact.

USAGE
-----
Build the environment with the flag first (no platformio.ini edit needed --
PlatformIO exposes build_src_flags as an environment variable)::

    export PATH="$HOME/.platformio/penv/bin:$PATH"
    PLATFORMIO_BUILD_SRC_FLAGS="-Wall -Wextra -Werror -fstack-usage" \
      make build BUILD_ENV=firebeetle2_profiler

Then report::

    python3 tools/stack_usage_report.py --env firebeetle2_profiler \
      --root safetyMonitorTask --callsites safetyMonitorTask

Env-to-platform, core dir and size tool all come from
tools/build_budgets.json, the same registry the Makefile reads, so a new
board variant does not need a second place to be registered.

Exit codes: 0 = report produced; 1 = an input was missing or unreadable;
2 = a requested root symbol is not in the image.
"""

from __future__ import annotations

import argparse
import bisect
import hashlib
import json
import os
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
BUDGETS = ROOT / "tools" / "build_budgets.json"

# Per-platform toolchain facts. Kept explicit rather than globbed: the pools
# hold objdump binaries for several chips (esp32s2, esp32s3, xespv2 variants),
# and picking the wrong one decodes the wrong instruction set silently.
PLATFORM_TOOLS = {
    "esp32": {
        "arch": "xtensa",
        "objdump": "packages/toolchain-xtensa-esp-elf/bin/xtensa-esp32-elf-objdump",
        "rom_elf": "packages/tool-esp-rom-elfs/{ver}/esp32_rev0_rom.elf",
    },
    "esp32p4": {
        "arch": "riscv",
        "objdump": "packages/toolchain-riscv32-esp/bin/riscv32-esp-elf-objdump",
        "rom_elf": "packages/tool-esp-rom-elfs/{ver}/esp32p4_rev0_rom.elf",
    },
}


class Fatal(Exception):
    """An input this report cannot be produced without."""


# =============================================================================
# Disassembly model
# =============================================================================

FUNC_HEADER_RE = re.compile(r"^([0-9a-f]+)\s+<(.+)>:$")
# objdump -l emits "file:line" markers, and "symbol():" lines, between insns.
SRCLINE_RE = re.compile(r"^(/[^\s:]+|[A-Za-z]:[^\s:]+):(\d+)(?:\s|$)")

TARGET_RE = re.compile(r"([0-9a-f]+)\s+<([^>]+)>")
# `l32r a8, 400074 <lit> (40114c <hook>)` -- objdump resolves the literal pool
# word itself, which is what an Xtensa long call actually jumps to.
L32R_VALUE_RE = re.compile(r"\(([0-9a-f]+)\s+<[^>]+>\)\s*$")
# An operand can be decimal or hex depending on magnitude: `entry a1, 32` and
# `entry a1, 0x220` are both emitted.
IMM_RE = r"(-?(?:0x[0-9a-f]+|\d+))"

# Instructions that do not write their first operand. Anything else is treated
# as clobbering it, which can only turn a resolvable call into a reported gap --
# the safe direction, never an invented edge.
XTENSA_NON_WRITING = re.compile(
    # `callx8 a8` READS a8 -- treating its operand as a destination drops the
    # literal that says where the call goes, and the whole printf chain then
    # reports as an indirect-call gap.
    r"^(s\d*[a-z]*i(\.n)?|s32c1i|memw|nop|b[a-z]+|j|jx|ret|retw|call\d|callx\d)")
RISCV_NON_WRITING = re.compile(r"^(c\.)?(s[bhwd]|fs[wd]|b[a-z]+|j|jr|jal|jalr|ret|nop|ecall|fence)")


def parse_imm(text: str) -> int:
    return int(text, 16) if text.startswith(("0x", "-0x")) else int(text, 10)


def split_insn(line: str):
    """(pc, length, mnemonic, operands, raw bytes) for an objdump instruction line.

    None when the line is not an instruction.

    objdump separates address, raw bytes, mnemonic and operands with tabs, and
    prints the raw bytes as one unspaced blob whose width varies with the
    instruction length. Splitting on tabs is the only stable read of that.

    The blob is returned as well as its width: `Image._read_body()` reads the
    bytes back out of it to re-frame a body from a known instruction boundary,
    which is how a misframed decode is told from a real one (#401). A blob that
    is not an even number of hex digits is not a length we can trust, and the
    line is rejected rather than rounded.
    """
    parts = line.split("\t")
    if len(parts) < 3:
        return None
    head = parts[0].strip()
    if not head.endswith(":"):
        return None
    try:
        pc = int(head[:-1], 16)
    except ValueError:
        return None
    raw = parts[1].strip()
    if not raw or len(raw) % 2 or not all(c in "0123456789abcdefABCDEF" for c in raw):
        return None
    mnem = parts[2].strip()
    if not mnem:
        return None
    ops = parts[3].strip() if len(parts) > 3 else ""
    return pc, len(raw) // 2, mnem, ops, raw.lower()


class Function:
    __slots__ = ("addr", "name", "frame", "frame_kind", "calls", "indirect", "src",
                 "decoded")

    def __init__(self, addr: int, name: str):
        self.addr = addr
        self.name = name
        self.frame = 0
        # none = no adjustment seen | fixed | dynamic | undecoded
        self.frame_kind = "none"
        self.decoded = 0  # instructions objdump actually decoded in this body
        self.calls: list[tuple[int, str, str | None]] = []  # (target addr, insn, srcline)
        self.indirect: list[tuple[int, str, str | None]] = []  # (pc, insn text, srcline)
        self.src: str | None = None


class Image:
    """One ELF, disassembled into an address-keyed call graph."""

    def __init__(self, label: str, elf: Path, objdump: Path, arch: str):
        self.label = label
        self.elf = elf
        self.arch = arch
        self.funcs: dict[int, Function] = {}
        self._starts: list[int] = []
        self.interior_jumps: list[tuple[str, int]] = []
        # Real extents first: _disassemble() needs them to know where each body
        # stops, and reading them from the symbol table is the only way to tell
        # a function's last instruction from the literal pool behind it.
        self.sizes: dict[int, int] = self._read_symbol_sizes(objdump)
        self.unsized: set[int] = set()
        # Addresses objdump decoded inside a body that are not real
        # instruction boundaries -- see _true_boundaries(). Counted for the
        # coverage report; `suppressed_calls` names the ones that decoded as a
        # direct call onto a real function entry, which is the fabricated edge
        # this walk no longer follows (#401). `unvalidated_bodies` are the
        # bodies whose framing could not be checked at all.
        self.misframed_insns: int = 0
        self.misframed_funcs: set[str] = set()
        self.unvalidated_bodies: set[str] = set()
        self.suppressed_calls: list[tuple[str, int, int, str | None]] = []
        self._disassemble(objdump)
        self._starts = sorted(self.funcs)
        self._drop_internal_branches()
        for fn in self.funcs.values():
            if fn.decoded == 0:
                # objdump emitted this body as raw words rather than
                # instructions. A frame of 0 here means "not read", not "leaf",
                # and treating the two alike would silently drop a whole
                # subtree. See the Xtensa .xt.prop note in the module docstring.
                fn.frame_kind = "undecoded"

    def _drop_internal_branches(self) -> None:
        """Keep only tail jumps that are real calls: ones landing on a function
        entry other than this function's own.

        Two things are filtered here, and both need the full symbol table, so
        neither can be decided while parsing:

        - A `j` inside the jumping function is ordinary control flow. Keeping
          it would turn every loop in a large function -- newlib's vfprintf
          especially -- into a self-recursive edge, and the walk would report a
          cut cycle instead of a chain.
        - A `j` into the *interior* of a different function is not a call. In
          this image those come from literal pools and jump tables decoded as
          instructions, and following one invented a path from vfprintf into
          scanf and on into the C++ unwinder. Counted as a gap instead.
        """
        for fn in self.funcs.values():
            kept = []
            for call in fn.calls:
                if call[1].startswith("tail-"):
                    if self._same_function(fn, call[0]):
                        continue
                    if call[0] not in self.funcs:
                        self.interior_jumps.append((fn.name, call[0]))
                        continue
                kept.append(call)
            fn.calls = kept

    # -- loading ------------------------------------------------------------
    def _run(self, argv: list[str]):
        proc = subprocess.Popen(argv, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                                text=True, errors="replace")
        assert proc.stdout is not None
        for line in proc.stdout:
            yield line.rstrip("\n")
        proc.stdout.close()
        rc = proc.wait()
        if rc != 0:
            err = proc.stderr.read() if proc.stderr else ""
            raise Fatal(f"{argv[0]} exited {rc} on {self.elf}: {err.strip()[:400]}")

    def _read_symbol_sizes(self, objdump: Path) -> dict[int, int]:
        """{entry address: st_size} for every function symbol in the image.

        `objdump -t` prints one symbol per line as

            4015175c l     F .flash.text\t00000017 post_enable_pcb

        - address, flag field, section, TAB, size, name. The ' F ' flag is what
        marks a function; splitting on the tab is the only stable read, because
        both the flag field and a demangled C++ name contain spaces.
        """
        sizes: dict[int, int] = {}
        for line in self._run([str(objdump), "-t", "-C", str(self.elf)]):
            head, sep, tail = line.partition("\t")
            if not sep or " F " not in head:
                continue
            try:
                addr = int(head.split()[0], 16)
                size = int(tail.split()[0], 16)
            except (ValueError, IndexError):
                continue
            # An alias at the same address must not shrink the extent.
            sizes[addr] = max(sizes.get(addr, 0), size)
        return sizes

    def _disassemble(self, objdump: Path) -> None:
        """Read the listing into one Function per symbol, body by body.

        Each body is buffered and then handed to `_read_body()` whole, rather
        than folded in as the lines arrive. That is not a style choice: the
        framing check `_read_body()` performs needs every instruction in the
        body before it can say which of them are real, and an instruction's own
        branch target can lie either side of it.
        """
        cur: Function | None = None
        cur_end: int | None = None
        srcline: str | None = None
        body: list[tuple[int, int, str, str, str | None, str]] = []

        for line in self._run([str(objdump), "-d", "-l", "-C", str(self.elf)]):
            if not line:
                continue
            m = FUNC_HEADER_RE.match(line)
            if m:
                if cur is not None:
                    self._read_body(cur, cur_end, body)
                cur = Function(int(m.group(1), 16), m.group(2))
                body = []
                # The definition site is the first line marker AFTER the header;
                # the one before it belongs to the previous function.
                srcline = None
                self.funcs[cur.addr] = cur
                size = self.sizes.get(cur.addr, 0)
                if size:
                    cur_end = cur.addr + size
                else:
                    # No size in the symbol table (assembly stubs, mostly). Fall
                    # back to "until the next header", which is what every body
                    # used to get -- reported in the coverage section, because
                    # these are the bodies that can still absorb a literal pool.
                    cur_end = None
                    self.unsized.add(cur.addr)
                continue
            sm = SRCLINE_RE.match(line)
            if sm:
                srcline = f"{os.path.basename(sm.group(1))}:{sm.group(2)}"
                if cur is not None and cur.src is None:
                    cur.src = srcline
                continue
            im = split_insn(line)
            if im is None or cur is None:
                continue
            pc, nbytes, mnem, ops, raw = im
            if cur_end is not None and pc >= cur_end:
                # Past this symbol's real extent: its literal pool, alignment
                # padding, or an unsymbolised region. Whatever objdump made of
                # those bytes is not this function's code, and a word that
                # happens to decode as a call to a real entry is the edge that
                # made this walk irreproducible across relinks (#271).
                continue
            body.append((pc, nbytes, mnem, ops, srcline, raw))
        if cur is not None:
            self._read_body(cur, cur_end, body)

    # Instruction length from the first byte, per ISA. Both encodings answer
    # that question from byte 0 alone, which is what lets a body be re-framed
    # from a known boundary without a second disassembler:
    #
    #   Xtensa code density: op0 = byte0 & 0xF. 0x8..0xD are the narrow
    #     (2-byte) opcodes; everything else is 3 bytes. The ESP32's LX6 core
    #     has no FLIX, so there is no 4-byte form to miss.
    #   RISC-V: bits 1:0 != 0b11 is a 16-bit compressed instruction; otherwise
    #     32 bits (the 48- and 64-bit forms need bits 4:2 == 0b111, which
    #     RV32IMAFC does not emit).
    #
    # Assumed nowhere: `_read_body()` checks this rule against objdump's own
    # byte count for EVERY instruction it lists, and declines to validate a
    # body where the two ever disagree.
    @staticmethod
    def _xtensa_insn_len(byte0: int) -> int:
        return 2 if 0x8 <= (byte0 & 0xF) <= 0xD else 3

    @staticmethod
    def _riscv_insn_len(byte0: int) -> int:
        return 2 if (byte0 & 0x3) != 0x3 else 4

    def _insn_len(self, byte0: int) -> int:
        return (self._xtensa_insn_len(byte0) if self.arch == "xtensa"
                else self._riscv_insn_len(byte0))

    # Mnemonics after which execution does not continue at the next address,
    # so the byte that follows one may be padding that no instruction starts
    # at. `_true_boundaries()` steps a re-framing run over that padding.
    XTENSA_UNCOND = ("j", "j.l", "jx", "ret", "ret.n", "retw", "retw.n",
                     "ill", "ill.n", "rfe", "rfi", "rfwo", "rfwu", "rfde")
    RISCV_UNCOND = ("j", "c.j", "tail", "ret", "c.ret", "jr", "c.jr",
                    "mret", "sret", "uret", "unimp", "c.unimp")

    # Alignment padding after an unconditional transfer, in bytes. Three
    # covers the 4-byte case the assembler emits; measured cases in the
    # artoo-esp32 image use one (`__lshift` at 0x401a8283, `_onRx` at
    # 0x400e770f) and two (`__lshift` at 0x401a81e3).
    #
    # Xtensa only, and that is not a shortcut. Xtensa mixes 2- and 3-byte
    # instructions at byte granularity, so a pad of one or two bytes is
    # absorbed into the decode of whatever follows and the sweep loses
    # alignment. RISC-V instructions are 2-byte aligned (IALIGN=16) and their
    # length comes from the first halfword, so padding occupies whole
    # instruction slots -- 0x0000 and `c.nop` are both one -- and a linear
    # sweep cannot lose alignment. There is nothing there to step over, and
    # skipping the zero half of a `c.nop` would be the tool inventing a
    # desynchronisation the encoding forbids.
    MAX_PAD_BYTES = 3

    # Rounds of the anchor fixpoint in `_framing()`. The cap is what
    # guarantees termination, and that is not belt-and-braces: a round can ADD
    # anchors as well as drop them, because dropping one lengthens the run
    # before it and that can make instructions real which were not, and those
    # name anchors of their own. Measured on the artoo-esp32 image, the
    # unsized region objdump calls `softUartRxIsr()-0x1028` oscillates between
    # 88 and 92 anchors and never settles; `_stext` settles at round 5. A body
    # that has not settled is reported as unvalidated and keeps every edge
    # objdump gave it, rather than being half-judged.
    ANCHOR_ROUNDS = 8

    def _is_uncond(self, mnem: str) -> bool:
        return mnem in (self.XTENSA_UNCOND if self.arch == "xtensa"
                        else self.RISCV_UNCOND)

    def _is_in_body_target(self, mnem: str, ops: str, lo: int, hi: int) -> int | None:
        """The in-body address this instruction branches or jumps to, or None.

        Only branches and jumps count, not calls: a call's target is another
        function, and what this is collecting is addresses that must be real
        instruction boundaries INSIDE this body -- see `_anchors()`.
        """
        if self.arch == "xtensa":
            branchy = (mnem in ("j", "j.l") or mnem.startswith("loop")
                       or (mnem.startswith("b") and not mnem.startswith("break")))
        else:
            branchy = mnem in ("j", "c.j") or mnem.startswith(("b", "c.b"))
        if not branchy:
            return None
        t = TARGET_RE.search(ops)
        if not t:
            return None
        addr = int(t.group(1), 16)
        return addr if lo <= addr < hi else None

    def _anchors(self, entry: int, lo: int, hi: int,
                 insns: dict[int, tuple[int, str, str]],
                 among: set[int] | None = None) -> set[int]:
        """Addresses inside this body claimed to be instruction boundaries.

        The entry, plus every in-body branch or jump target objdump resolved.
        A branch target is a boundary by construction -- the hardware will
        start executing an instruction there -- but only if the instruction
        naming it is itself real. `among` restricts the collection to
        instructions at those addresses, which is what makes the fixpoint in
        `_framing()` possible.
        """
        found = {entry}
        for pc, (_nbytes, mnem, ops) in insns.items():
            if among is not None and pc not in among:
                continue
            target = self._is_in_body_target(mnem, ops, lo, hi)
            if target is not None:
                found.add(target)
        return found

    def _framing(self, entry: int, hi: int,
                 insns: dict[int, tuple[int, str, str]],
                 body_bytes: dict[int, int]) -> set[int] | None:
        """The body's real instruction boundaries, or None if not settled.

        A misframed instruction can name an in-body target of its own, and that
        bogus anchor is not harmless: it seeds a re-framing run from an address
        that is not a boundary, and every real instruction until the next
        anchor is then condemned. Measured in `HTTPClient::setCookie`, where a
        phantom `blti` named 0x400e2305 and took a real `call8
        String::indexOf` down with it.

        So the two are solved together, by a fixpoint: frame with every
        candidate anchor, keep only the anchors named by instructions the
        framing says are real, and repeat. Where it settles, it settles on the
        anchors named by correctly framed instructions, which are exactly the
        ones the hardware guarantees.

        It is NOT monotone, and assuming it was is a mistake this comment used
        to carry: dropping an anchor lengthens the run before it, which can
        make instructions real that were not, and those name anchors of their
        own. Two of the artoo-esp32 image's 7,019 bodies do not settle inside
        `ANCHOR_ROUNDS`, both unsized pseudo-symbols; they return None here and
        keep every edge.
        """
        anchors = self._anchors(entry, entry, hi, insns)
        for _ in range(self.ANCHOR_ROUNDS):
            real = self._true_boundaries(sorted(anchors), hi, body_bytes, insns)
            if real is None:
                return None
            kept = self._anchors(entry, entry, hi, insns, among=real)
            if kept == anchors:
                return real
            anchors = kept
        return None

    def _true_boundaries(self, anchors: list[int], hi: int,
                         body_bytes: dict[int, int],
                         insns: dict[int, tuple[int, str, str]]) -> set[int] | None:
        """Every real instruction boundary in this body, or None if unknowable.

        WHY THIS EXISTS (#401)
        ----------------------
        `objdump -d` decodes a section as one linear byte stream. Where that
        stream contains a byte no instruction starts at -- alignment padding
        after an unconditional jump -- the decoder cannot know, consumes it as
        the first byte of the next instruction, and emits several MISFRAMED
        instructions, built from the tails and heads of real ones, before it
        lands on a real boundary again. Some of those decode as calls.

        Measured in the artoo-esp32 image, inside newlib's `__lshift`::

            401a8280:  86 03 00      j     401a8292     <- real
            401a8283:  00                                <- one byte of padding
            401a8284:  82 c3 15      addi  a8, a3, 21    <- real; a branch target
            401a8287:  80 80 60      neg   a8, a8        <- real

        read linearly from the jump as::

            401a8283:  c3 82 00      movf  a8, a2, b0    <- phantom
            401a8286:  15 80 80      call4 <an entry>    <- phantom CALL
            401a8289:  60 8a 8c      lsi   f6, a10, 0x230

        A call's target is PC-relative on both ports, so the phantom's target
        moves with the link. In one build it points into the middle of a
        function and is reported unresolved; in the next it lands exactly on
        some unrelated function's entry and becomes a followed edge. That is
        how `__lshift` came to "call" `esp_task_wdt_deinit` and put a
        fabricated 96 bytes on eight of twelve tasks -- every one whose chain
        runs through `printf` float formatting -- in a build whose source
        touched none of them (#401).

        The #271 symbol-size bound cannot see this: the padding is INSIDE
        `[addr, addr + size)`.

        HOW
        ---
        Re-frame the body forward from each anchor, using `_insn_len()`. An
        anchor is a boundary the hardware proves, and framing forward from a
        real boundary stays correct until the next padding. Each anchor's run
        covers up to the next anchor; the union is the body's real boundary
        set, and an address objdump printed that is not in it is not code.

        On Xtensa a run also steps over the padding after an unconditional
        transfer, which is where the desync starts and the one place objdump
        cannot help -- it framed the pad as the head of an instruction. The pad
        is zero bytes, so stepping over zeros there recovers the real boundary
        without needing a branch to point at it. An address some branch DOES
        point at is a boundary by construction, so it ends the skip; that is
        what keeps a real instruction whose first byte happens to be zero from
        being stepped over. `MAX_PAD_BYTES` says why this is Xtensa's alone.

        Returns None when the body carries no bytes to re-frame from, which
        leaves the caller validating nothing rather than suppressing
        everything.
        """
        if not anchors or not body_bytes:
            return None
        anchor_set = set(anchors)
        real: set[int] = set()
        for i, start in enumerate(anchors):
            limit = anchors[i + 1] if i + 1 < len(anchors) else hi
            pc = start
            while pc < limit:
                byte0 = body_bytes.get(pc)
                if byte0 is None:
                    # The listing did not cover this address, so the run cannot
                    # be continued. Stopping loses boundaries; inventing them
                    # would lose the point.
                    break
                real.add(pc)
                step = self._insn_len(byte0)
                here = insns.get(pc)
                if (self.arch != "xtensa" or here is None
                        or not self._is_uncond(here[1])):
                    pc += step
                    continue
                pc += step
                skipped = 0
                while (skipped < self.MAX_PAD_BYTES and pc < limit
                       and pc not in anchor_set and body_bytes.get(pc) == 0):
                    pc += 1
                    skipped += 1
        return real

    def _read_body(self, fn: Function, end: int | None,
                   body: list[tuple[int, int, str, str, str | None, str]]) -> None:
        """Fold one buffered body into `fn`, on real instruction boundaries only.

        A body is buffered and read whole rather than folded in as its lines
        arrive, because `_true_boundaries()` needs every instruction in it
        before it can say which of them are real: an instruction's own branch
        target can lie either side of it.
        """
        insns: dict[int, tuple[int, str, str]] = {}
        order: list[int] = []
        srclines: dict[int, str | None] = {}
        body_bytes: dict[int, int] = {}
        length_rule_holds = True
        for pc, nbytes, mnem, ops, srcline, raw in body:
            if pc in insns:
                # objdump prints each address once; a duplicate would mean the
                # listing was not read in address order, and silently keeping
                # the second would make the framing arithmetic wrong.
                continue
            insns[pc] = (nbytes, mnem, ops)
            srclines[pc] = srcline
            order.append(pc)
            # objdump prints an instruction's raw bytes most-significant first,
            # so the byte AT pc is the last hex pair. Verified against
            # `objdump -s` on the artoo-esp32 image: `15c382` is the three
            # bytes 82 c3 15.
            octets = [int(raw[i:i + 2], 16) for i in range(0, len(raw), 2)][::-1]
            for offset, value in enumerate(octets):
                body_bytes[pc + offset] = value
            if not mnem.startswith(".") and self._insn_len(octets[0]) != nbytes:
                length_rule_holds = False

        hi = end if end is not None else (max(order) + insns[max(order)][0]
                                          if order else fn.addr)
        real = None
        if order and length_rule_holds and fn.addr in insns:
            real = self._framing(fn.addr, hi, insns, body_bytes)
        if real is None:
            # No basis to validate this body's framing, so validate none of it
            # and say so. Suppressing on a guess would be the same class of
            # fault as inventing an edge, pointed the other way.
            self.unvalidated_bodies.add(fn.name)

        pending_lit: dict[str, int] = {}   # xtensa: reg -> literal address
        pending_auipc: dict[str, tuple[int, int]] = {}  # riscv: reg -> (pc, imm)
        # A stack adjustment always sits in the prologue. Bounding the window
        # keeps a mid-function `addi sp,sp,-N` (an alloca, or the epilogue's
        # restore) out of the frame figure.
        prologue_left = 24

        for pc in order:
            nbytes, mnem, ops = insns[pc]
            misframed = real is not None and pc not in real
            if mnem.startswith(".") or misframed:
                # `.byte`, `.short`, `.word`: objdump saying "these bytes are
                # data", inside the body. A misframed decode is the same thing
                # by a different route -- bytes that are not an instruction.
                #
                # Neither counts towards `decoded`: a body objdump renders
                # ENTIRELY as data would otherwise report decoded > 0, escape
                # the `undecoded` label, and have its frame of 0 read as "leaf"
                # rather than "not read" - which drops the whole subtree beneath
                # it without saying so.
                #
                # Clearing the tracked registers is the same safe direction as
                # everywhere else here: it can turn a resolvable call into a
                # reported gap, never the reverse. A misframed window hides real
                # instructions nobody read -- one sits at 0x401c481c, which
                # objdump never printed a line for -- and any of them may have
                # written the register a later `callx8` reads. It costs depth:
                # `tlsf_walk_pool -> default_walker` (tlsf.c:214) is loaded by an
                # `l32r` before the window and called after it, and that edge is
                # now an indirect-call gap rather than a resolved callee.
                if misframed:
                    # All-zero bytes off a boundary are the alignment padding
                    # itself, not a decode that went wrong. Counting them would
                    # inflate the desynchronisation figure with the thing that
                    # causes it: on the ESP32-P4 image, whose encoding cannot
                    # desynchronise at all, every one of the twelve is a `unimp`
                    # pad slot inside a crypto register helper.
                    padding = all(body_bytes.get(pc + i) == 0
                                  for i in range(nbytes))
                    self._note_misframed(fn, pc, mnem, ops, srclines[pc], padding)
                pending_lit.clear()
                pending_auipc.clear()
                continue
            fn.decoded += 1
            if prologue_left > 0:
                if self._ends_prologue(mnem, ops):
                    prologue_left = 0
                else:
                    prologue_left -= 1
                    self._maybe_frame(fn, mnem, ops)
            if self.arch == "xtensa":
                self._invalidate(XTENSA_NON_WRITING, r"a\d+", mnem, ops, pending_lit)
                self._xtensa_flow(fn, pc, mnem, ops, srclines[pc], pending_lit)
            else:
                self._invalidate(RISCV_NON_WRITING, r"\w+", mnem, ops, pending_auipc)
                self._riscv_flow(fn, pc, mnem, ops, srclines[pc], pending_auipc)

    def _note_misframed(self, fn: Function, pc: int, mnem: str, ops: str,
                        srcline: str | None, padding: bool = False) -> None:
        """Record a decoded address that is not a real instruction boundary.

        Dropping these silently would trade one dishonest number for another:
        the coverage section says what this walk cannot see, and a misframed
        decode is exactly that. A phantom that decoded as a DIRECT call landing
        on a real function entry is listed by name, because that one is an edge
        the walk would have followed and reported as real (#401).

        A data directive is not a misframe -- it is objdump correctly saying
        "these bytes are not code" -- so it is not counted here even when the
        framing puts it off a boundary, which padding always does. Neither is
        `padding`: an all-zero word off a boundary is the alignment fill, and
        counting it would report the cause as one of its own symptoms.
        """
        if padding or mnem.startswith("."):
            return
        self.misframed_insns += 1
        self.misframed_funcs.add(fn.name)
        is_call = ((mnem.startswith("call") and not mnem.startswith("callx"))
                   if self.arch == "xtensa" else mnem in ("jal", "c.jal"))
        if not is_call:
            return
        t = TARGET_RE.search(ops)
        if t and int(t.group(1), 16) in self.sizes:
            self.suppressed_calls.append(
                (fn.name, pc, int(t.group(1), 16), srcline))

    @staticmethod
    def _invalidate(non_writing, reg_pat, mnem, ops, tracked: dict) -> None:
        """Drop a tracked register as soon as anything else writes it.

        Without this, `l32r a8, <lit>` followed by `l32i.n a8, a8, 0` (loading
        through a function pointer) would resolve the later `callx8 a8` to the
        pointer *variable* rather than reporting an indirect call.
        """
        if non_writing.match(mnem):
            return
        m = re.match(r"^(" + reg_pat + r")\s*(,|$)", ops)
        if m:
            tracked.pop(m.group(1), None)

    # -- frame sizes --------------------------------------------------------
    def _ends_prologue(self, mnem: str, ops: str) -> bool:
        """First instruction after which a stack adjustment is no longer setup.

        Bounds the accumulation in _maybe_frame: once the function has called
        something or has started giving stack back, any later adjustment is an
        alloca or the epilogue, and folding those in would inflate the frame.
        """
        sp = "a1" if self.arch == "xtensa" else "sp"
        if mnem.startswith(("call", "jal", "ret", "retw", "j", "jr", "jx")):
            return True
        m = re.match(r"^" + sp + r",\s*(?:" + sp + r",\s*)?" + IMM_RE + r"$", ops)
        if m and mnem.startswith(("addi", "addmi", "c.addi")):
            return parse_imm(m.group(1)) > 0
        return False


    def _maybe_frame(self, fn: Function, mnem: str, ops: str) -> None:
        """Accumulate the prologue's stack-pointer adjustments into fn.frame.

        A frame is NOT always one instruction. GCC splits an allocation that
        does not fit the ISA's immediate, and RISC-V's `addi` immediate is 12
        bits signed, so anything over 2032 bytes arrives in two steps:

            domeLinkTask:  addi sp,sp,-224   ...13 register saves...
                           addi sp,sp,-2032

        Reading only the first gave 224 where -fstack-usage says 2256 -- a
        2 KB under-read on a task whose stack this project sizes by hand.
        Accumulating is what makes the two sources agree.

        The caller stops feeding this at the first call or the first positive
        adjustment, so an epilogue restore or an alloca cannot be folded in.
        """
        if fn.frame_kind == "dynamic":
            return
        if self.arch == "xtensa":
            # Windowed ABI: `entry a1, N` allocates the whole frame, N already
            # including the 16-byte caller-save area the window spill uses.
            # Verified against -fstack-usage on a fixture: the .su figure and
            # the ENTRY operand agree exactly.
            m = re.match(r"^a1,\s*" + IMM_RE + r"$", ops)
            if mnem == "entry" and m:
                fn.frame += parse_imm(m.group(1))
                fn.frame_kind = "fixed"
                return
            # CALL0-ABI / leaf functions adjust a1 directly.
            m = re.match(r"^a1,\s*a1,\s*" + IMM_RE + r"$", ops)
            if mnem in ("addi", "addi.n", "addmi") and m:
                delta = parse_imm(m.group(1))
                if delta < 0:
                    fn.frame += -delta
                    fn.frame_kind = "fixed"
                return
            if mnem in ("add", "add.n", "sub") and ops.startswith("a1,"):
                fn.frame_kind = "dynamic"
            return
        # RISC-V: `addi sp,sp,-N`; objdump prints the compressed c.addi16sp the
        # same way. A register-sized adjustment is a variable-length frame.
        m = re.match(r"^sp,\s*sp,\s*" + IMM_RE + r"$", ops)
        if mnem in ("addi", "c.addi16sp", "c.addi4spn") and m:
            delta = parse_imm(m.group(1))
            if delta < 0:
                fn.frame += -delta
                fn.frame_kind = "fixed"
            return
        if mnem in ("add", "sub") and ops.startswith("sp,sp,"):
            fn.frame_kind = "dynamic"

    # -- control flow -------------------------------------------------------
    def _xtensa_flow(self, fn, pc, mnem, ops, srcline, pending_lit) -> None:
        if mnem == "l32r":
            m = re.match(r"^(a\d+),", ops)
            val = L32R_VALUE_RE.search(ops)
            if m and val:
                # objdump dereferences the literal pool itself and prints the
                # word in parentheses. When it cannot (about 1.5% of l32r sites,
                # measured), the register is left untracked so a later callx is
                # reported as an indirect-call gap rather than guessed at.
                pending_lit[m.group(1)] = int(val.group(1), 16)
            return
        if mnem.startswith("call") and not mnem.startswith("callx"):
            t = TARGET_RE.search(ops)
            if t:
                fn.calls.append((int(t.group(1), 16), mnem, srcline))
            # CALL4/8/12 rotate the register window, so nothing loaded into
            # a4..a15 before the call is still there after it. Keeping a stale
            # literal across one produces call edges that do not exist.
            pending_lit.clear()
            return
        if mnem.startswith("callx"):
            target = pending_lit.get(ops.strip())
            if target is not None:
                fn.calls.append((target, mnem + " (via literal)", srcline))
            else:
                fn.indirect.append((pc, f"{mnem} {ops}", srcline))
            pending_lit.clear()
            return
        if mnem in ("j", "jx"):
            t = TARGET_RE.search(ops)
            # A tail jump out of the function reuses this frame; still a call
            # edge for depth purposes, and marked so in the report. Whether it
            # actually leaves is settled by _drop_internal_branches().
            if t:
                fn.calls.append((int(t.group(1), 16), "tail-" + mnem, srcline))
            elif mnem == "jx":
                fn.indirect.append((pc, f"{mnem} {ops}", srcline))

    def _riscv_flow(self, fn, pc, mnem, ops, srcline, pending_auipc) -> None:
        if mnem == "auipc":
            m = re.match(r"^(\w+),\s*0x([0-9a-f]+)", ops)
            if m:
                pending_auipc[m.group(1)] = (pc, int(m.group(2), 16) << 12)
            return
        if mnem in ("jal", "c.jal"):
            t = TARGET_RE.search(ops)
            if t:
                fn.calls.append((int(t.group(1), 16), mnem, srcline))
            pending_auipc.clear()  # caller-saved registers do not survive a call
            return
        if mnem in ("jalr", "c.jalr", "jr", "c.jr"):
            t = TARGET_RE.search(ops)
            if t:
                fn.calls.append((int(t.group(1), 16), mnem, srcline))
                return
            m = re.match(r"^(?:\w+,\s*)?(-?\d+)\((\w+)\)", ops)
            if m and m.group(2) in pending_auipc:
                base_pc, hi = pending_auipc[m.group(2)]
                fn.calls.append((base_pc + hi + int(m.group(1)), mnem + " (auipc pair)", srcline))
            else:
                fn.indirect.append((pc, f"{mnem} {ops}", srcline))
            pending_auipc.clear()
            return
        if mnem in ("j", "c.j"):
            t = TARGET_RE.search(ops)
            if t:
                fn.calls.append((int(t.group(1), 16), "tail-j", srcline))

    def _same_function(self, fn: Function, addr: int) -> bool:
        owner = self.owner_of(addr)
        return owner is not None and owner.addr == fn.addr

    # -- lookup -------------------------------------------------------------
    def owner_of(self, addr: int) -> Function | None:
        """The function containing addr, or None if it is not inside one.

        Bounded by the symbol's own size, falling back to the next symbol start
        where the table carries no size: an address in .rodata, in a literal
        pool, or in a gap must resolve to nothing, or it gets attributed to
        whichever function happens to precede it and the walk follows an edge
        that does not exist.
        """
        if not self._starts:
            return None
        i = bisect.bisect_right(self._starts, addr) - 1
        if i < 0:
            return None
        start = self._starts[i]
        size = self.sizes.get(start, 0)
        if size:
            return self.funcs[start] if addr < start + size else None
        if i + 1 < len(self._starts) and addr >= self._starts[i + 1]:
            return None
        return self.funcs[start]

    def is_entry(self, addr: int) -> bool:
        return addr in self.funcs

    def by_name(self, name: str) -> list[Function]:
        base = name.split("(")[0]
        return [f for f in self.funcs.values() if f.name.split("(")[0] == base]


# =============================================================================
# Depth walk
# =============================================================================

# Functions that do not return: the chip is already on its way to a panic
# handler, which runs on its own stack. A chain that only gets deep by going
# through one of these does not describe a stack a running task has to fit in,
# and leaving them in makes every root report the same abort path. Pruned by
# default, listed in the report, and restorable with --no-prune.
DEFAULT_PRUNE = (
    "__assert_func", "abort", "_exit", "_abort",
    "panic_abort", "esp_system_abort", "esp_restart_noos",
    "_esp_error_check_failed", "_esp_error_check_failed_without_abort",
    "__cxa_pure_virtual", "std::terminate()", "__cxxabiv1::__terminate",
)


class Walker:
    """Max cumulative frame bytes from a root, across one or two images."""

    def __init__(self, images: list[Image], prune: tuple[str, ...] = ()):
        self.images = images
        self.prune = set(prune)
        self._memo: dict[int, tuple[int, list]] = {}
        self.cut_cycles: list[str] = []
        self.pruned: set[str] = set()
        self.unresolved: list[tuple[str, int]] = []  # (function name, target addr)

    def is_pruned(self, fn: Function) -> bool:
        base = fn.name.split("(")[0]
        if base in self.prune or fn.name in self.prune:
            self.pruned.add(fn.name)
            return True
        return False

    def lookup(self, addr: int) -> tuple[Image, Function] | None:
        """The called function, or None when the target has no symbol entry.

        Only an exact function-entry match counts. The image contains calls to
        addresses carrying no symbol of their own -- `call8 400e2a40
        <HTTPClient::generateCookieString(String*)+0xb0>` inside
        heap_caps_realloc_base is one -- and attributing those to the preceding
        symbol gives the callee somebody else's name AND somebody else's frame
        size. That is worse than not knowing: it inflated this task's chain by
        over a kilobyte through a heap function that appeared to call an HTTP
        client. Unresolved targets are counted and listed instead, which makes
        the affected totals lower bounds rather than wrong numbers.
        """
        for img in self.images:
            if img.is_entry(addr):
                return img, img.funcs[addr]
        return None

    def depth(self, img: Image, fn: Function, stack: tuple[int, ...] = ()):
        """(bytes, chain, cut_below) for the deepest path from fn.

        ``cut_below`` says whether this result was shortened by a recursion cut
        anywhere beneath it. Only results with no cut below them are memoised.

        That distinction is the whole point of the third element. A cut result
        is valid ONLY for the call stack that produced it -- the same function
        reached from somewhere else may complete the cycle differently, or not
        enter it at all. Caching one and reusing it elsewhere made the report
        depend on the order roots were passed on the command line: on the
        firebeetle2 image `--root domeTask` alone gave 3008 bytes but 3296 when
        safetyMonitorTask was walked first, and safetyMonitorTask gave 2768
        alone against 2480 after domeTask. Both #245's table and #248's issue
        body were measured with that bug present.
        """
        if fn.addr in stack:
            self.cut_cycles.append(fn.name)
            return 0, [("<recursion cut>", 0, None)], True
        if fn.addr in self._memo:
            sub, chain = self._memo[fn.addr]
            return sub, chain, False
        best_sub, best_chain, best_edge = 0, [], None
        cut_below = False
        for target, insn, srcline in fn.calls:
            found = self.lookup(target)
            if found is None:
                self.unresolved.append((fn.name, target))
                continue
            timg, tfn = found
            if self.is_pruned(tfn):
                continue
            sub, chain, sub_cut = self.depth(timg, tfn, stack + (fn.addr,))
            # Any cut anywhere among the branches can have suppressed the one
            # that would have won, so the maximum itself is suspect, not just
            # the branch that was cut.
            cut_below = cut_below or sub_cut
            total = tfn.frame + sub
            if total > best_sub:
                best_sub, best_chain, best_edge = total, chain, (tfn, insn, srcline)
        if best_edge is None:
            result = (0, [])
        else:
            tfn, insn, srcline = best_edge
            result = (best_sub, [(tfn.name, tfn.frame, srcline, insn)] + best_chain)
        if not cut_below:
            self._memo[fn.addr] = result
        return result[0], result[1], cut_below

    def callsite_table(self, img: Image, fn: Function) -> list[tuple]:
        rows = []
        for target, insn, srcline in fn.calls:
            found = self.lookup(target)
            if found is None:
                rows.append((srcline or "?", f"<unresolved 0x{target:08x}>", insn, None))
                continue
            timg, tfn = found
            if self.is_pruned(tfn):
                rows.append((srcline or "?", tfn.name + " [pruned]", insn, None))
                continue
            sub, _, _ = self.depth(timg, tfn, (fn.addr,))
            rows.append((srcline or "?", tfn.name, insn, tfn.frame + sub))
        return rows


# =============================================================================
# -fstack-usage ingestion
# =============================================================================

SU_RE = re.compile(r"^(.+?):(\d+):(\d+):(.+?)\t(\d+)\t(\w+)$")


def read_su(build_dir: Path) -> list[tuple[str, str, int, str]]:
    """(function, file:line, bytes, qualifier) for every .su record found."""
    out = []
    for path in sorted(build_dir.rglob("*.su")):
        for line in path.read_text(errors="replace").splitlines():
            m = SU_RE.match(line)
            if not m:
                continue
            out.append((m.group(4).strip(), f"{os.path.basename(m.group(1))}:{m.group(2)}",
                        int(m.group(5)), m.group(6)))
    return out


# =============================================================================
# Wiring
# =============================================================================

def resolve_paths(env: str, rom: bool):
    budgets = json.loads(BUDGETS.read_text())
    platform = "esp32"
    for name, rec in budgets["platforms"].items():
        if env in rec.get("envs", []):
            platform = name
            break
    tools = PLATFORM_TOOLS.get(platform)
    if tools is None:
        raise Fatal(f"no toolchain mapping for platform '{platform}'")
    core_dir = Path(os.path.expanduser(
        budgets["platforms"][platform].get("core_dir", "~/.platformio")))
    objdump = core_dir / tools["objdump"]
    if not objdump.is_file():
        raise Fatal(f"objdump not found: {objdump}")
    elf = ROOT / ".pio" / "build" / env / "firmware.elf"
    if not elf.is_file():
        raise Fatal(f"no built image: {elf} (build the env first)")
    rom_elf = None
    if rom:
        pattern = tools["rom_elf"]
        base = core_dir / "packages" / "tool-esp-rom-elfs"
        cands = sorted(base.glob(pattern.split("{ver}/")[1])) if base.is_dir() else []
        cands += sorted(base.glob("*/" + pattern.split("{ver}/")[1]))
        if not cands:
            raise Fatal(f"ROM elf not found under {base} (pass --no-rom to skip)")
        rom_elf = cands[0]
    return platform, tools["arch"], objdump, elf, rom_elf


def sha256(path: Path) -> str:
    h = hashlib.sha256()
    with open(path, "rb") as fh:
        for chunk in iter(lambda: fh.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()[:16]


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--env", required=True, help="PlatformIO env whose build to read")
    ap.add_argument("--root", action="append", default=[], required=True,
                    help="function to report a worst-case chain for (repeatable)")
    ap.add_argument("--callsites", action="append", default=[],
                    help="function whose call sites to break down by source line")
    ap.add_argument("--no-rom", action="store_true",
                    help="do not load the chip ROM elf (chains into ROM stop early)")
    ap.add_argument("--no-prune", action="store_true",
                    help="follow non-returning abort/panic functions too")
    ap.add_argument("--frames", action="append", default=[],
                    help="report just this symbol's own frame size (repeatable). "
                         "Per-function reads of the linked image, independent of "
                         "the call graph -- use when comparing the same chain "
                         "across targets.")
    args = ap.parse_args(argv)
    prune = () if args.no_prune else DEFAULT_PRUNE

    platform, arch, objdump, elf, rom_elf = resolve_paths(args.env, not args.no_rom)
    build_dir = ROOT / ".pio" / "build" / args.env

    images = [Image("image", elf, objdump, arch)]
    if rom_elf is not None:
        images.append(Image("rom", rom_elf, objdump, arch))
    walker = Walker(images, prune)
    img = images[0]

    ver = subprocess.run([str(objdump), "--version"], capture_output=True, text=True,
                         check=True).stdout.splitlines()[0]
    su = read_su(build_dir)

    print("=" * 78)
    print(f"stack_usage_report  env={args.env}  platform={platform}  arch={arch}")
    print(f"  elf      {elf.relative_to(ROOT)}  sha256:{sha256(elf)}  {elf.stat().st_size} B")
    if rom_elf is not None:
        print(f"  rom elf  {rom_elf.name}  sha256:{sha256(rom_elf)}")
    print(f"  objdump  {ver}")
    print(f"  .su      {len(su)} records under {build_dir.relative_to(ROOT)}")
    print("=" * 78)

    status = 0
    for root in args.root:
        cands = img.by_name(root)
        print()
        if not cands:
            print(f"ROOT {root}: NOT FOUND in image -- cannot report")
            status = 2
            continue
        for fn in sorted(cands, key=lambda f: f.addr):
            sub, chain, _ = walker.depth(img, fn)
            print(f"ROOT {fn.name}  @0x{fn.addr:08x}  ({fn.src or 'no line info'})")
            print(f"  worst-case chain, deepest first call site kept:")
            print(f"    {'bytes':>7}  {'cumulative':>10}  function / call site")
            print(f"    {fn.frame:>7}  {fn.frame:>10}  {fn.name}   [frame {fn.frame_kind}]")
            running = fn.frame
            for name, frame, srcline, *rest in chain:
                if name == "<recursion cut>":
                    print(f"    {'-':>7}  {running:>10}  <recursion cut; total is a lower bound>")
                    break
                running += frame
                insn = rest[1] if len(rest) > 1 else ""
                where = f"  <- {srcline}" if srcline else ""
                print(f"    {frame:>7}  {running:>10}  {name}  ({insn}){where}")
            print(f"  TOTAL worst-case static chain: {fn.frame + sub} bytes")

    for target in args.callsites:
        for fn in sorted(img.by_name(target), key=lambda f: f.addr):
            print()
            print(f"CALL SITES of {fn.name}  (own frame {fn.frame} B, {fn.frame_kind})")
            print(f"    {'callee+subtree':>14}  source line       callee")
            rows = walker.callsite_table(img, fn)
            for srcline, name, insn, total in sorted(
                    rows, key=lambda r: (-(r[3] or 0), r[0])):
                tot = "unresolved" if total is None else str(total)
                print(f"    {tot:>14}  {srcline:<17} {name}  ({insn})")

    if args.frames:
        print()
        print("Own frame size per named symbol (linked image, prologue-derived):")
        print(f"    {'bytes':>7}  {'kind':<8} symbol")
        total = 0
        for want in args.frames:
            found = img.by_name(want) or (images[1].by_name(want) if len(images) > 1 else [])
            if not found:
                print(f"    {'ABSENT':>7}  {'-':<8} {want}")
                status = max(status, 2)
                continue
            for fn in sorted(found, key=lambda f: f.addr):
                print(f"    {fn.frame:>7}  {fn.frame_kind:<8} {fn.name}  @0x{fn.addr:08x}")
                total += fn.frame
        print(f"    {total:>7}  {'':<8} SUM of the symbols above")

    if su:
        print()
        print("-fstack-usage records for src/ functions (compiler declaration):")
        print(f"    {'bytes':>7}  {'qual':<8} file:line            function")
        for name, where, nbytes, qual in sorted(su, key=lambda r: -r[2])[:40]:
            print(f"    {nbytes:>7}  {qual:<8} {where:<20} {name}")
        print(f"    ({len(su)} records total; 40 largest shown)")

    print()
    print("Coverage gaps in this report:")
    if prune:
        hit = sorted(walker.pruned)
        print(f"  non-returning functions pruned from the walk: {hit if hit else 'none reached'}")
        print(f"    (prune list: {', '.join(prune)}; --no-prune to include them)")
    ind = [(f.name, len(f.indirect)) for f in img.funcs.values() if f.indirect]
    print(f"  indirect call sites: {sum(n for _, n in ind)} across {len(ind)} functions")
    print(f"  jumps into another function's interior, not followed: {len(img.interior_jumps)}")
    print(f"  misframed decodes (objdump resumed inside an instruction after"
          f" in-body padding), not code: {img.misframed_insns}"
          f" across {len(img.misframed_funcs)} functions"
          f"; {len(img.unvalidated_bodies)} bodies could not be framing-checked")
    if img.suppressed_calls:
        print(f"    of those, {len(img.suppressed_calls)} decoded as a direct call"
              f" landing on a real function entry -- fabricated edges this walk"
              f" did NOT follow (#401):")
        for name, pc, target, srcline in sorted(img.suppressed_calls)[:10]:
            owner = img.funcs.get(target)
            where = f" <- {srcline}" if srcline else ""
            print(f"      {name} @0x{pc:08x} -> 0x{target:08x}"
                  f" {owner.name if owner else '?'}{where}")
    undec = [f for f in img.funcs.values() if f.frame_kind == "undecoded"]
    print(f"  function bodies objdump emitted as data, frame unknown: {len(undec)}"
          f" of {len(img.funcs)}")
    print(f"  bodies bounded by their symbol size: {len(img.funcs) - len(img.unsized)}"
          f" of {len(img.funcs)}; {len(img.unsized)} carry no size and are bounded"
          f" by the next symbol")
    if walker.unresolved:
        uniq = sorted(set(walker.unresolved))
        print(f"  call targets with no symbol entry, not followed: "
              f"{len(walker.unresolved)} ({len(uniq)} distinct)")
        for name, addr in uniq[:10]:
            print(f"    {name} -> 0x{addr:08x}")
    if walker.cut_cycles:
        print(f"  recursion cut at: {sorted(set(walker.cut_cycles))}")
    print("  interrupt/exception frames are not counted (separate ISR stack)")
    return status


if __name__ == "__main__":
    try:
        sys.exit(main())
    except Fatal as exc:
        print(f"stack_usage_report: {exc}", file=sys.stderr)
        sys.exit(1)
