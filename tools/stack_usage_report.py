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
The linked image (what actually ships). Every function's frame comes from its
prologue's stack-pointer adjustment -- ``entry a1, N`` on Xtensa, ``addi
sp,sp,-N`` on RISC-V -- and the call graph from its direct call instructions.
This covers framework, newlib and ROM code as well, which is where the
``printf`` chain lives.

No frame comes from ``-fstack-usage``. When the build left ``.su`` files
(GCC writes them next to each object file when ``-fstack-usage`` is in
``PLATFORMIO_BUILD_SRC_FLAGS``, for ``src/`` only), the forty largest records
are printed after the chains as the compiler's own declaration, for reading by
eye. Nothing compares them with the walk, and the walk does not need them.

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

WHICH BYTES ARE CODE AT ALL
---------------------------
Xtensa only. The linker keeps an instruction/literal property table,
``.xt.prop``, and ``objdump -d`` decides from it whether a stretch of bytes is
code or literal data. binutils' ``xtensa_find_table_entry()``
(opcodes/xtensa-dis.c) answers for an address with the record containing it,
OR THE NEXT RECORD AFTER IT, so an address in a gap between records takes the
next record's flags; when that record is a literal, the bytes print as literal
words. The artoo-esp32 image keeps only part of its per-object tables -- the
map lists a thousand per-object ``.xt.prop`` sections among the discarded
input sections -- and at 0e005614 objdump printed 2,563 of its 7,127 function
bodies wholly as data: ESP-IDF's lwIP, wpa_supplicant and mDNS, the closed
WiFi libraries, a few ``src/`` functions (#430). Each such body had a frame of
0 and no calls, so every chain through one was cut there.

``Image._recover_data_bodies()`` disassembles a copy of the image with
``.xt.prop`` and ``.xt.lit`` removed, where objdump has no table to consult and
decodes every byte as an instruction, and takes from that listing ONLY the
bodies the product listing printed as data. Stripping the tables for the whole
image would be wrong: where they mark data inside a body correctly, the
stripped listing decodes it as instructions, and 24 bodies that decode
normally would misframe and lose real calls. A recovered body goes through the
symbol-size bound and the framing fixpoint below like any other body. The
coverage report counts the bodies recovered and the ones still printed as
data.

The ESP32-P4 image carries no property table and decodes whole.

WHAT IT CANNOT SEE, AND SAYS SO
-------------------------------
- Indirect calls (``callx*`` on Xtensa through a register that was not loaded
  from a literal, ``jalr`` on RISC-V through a computed address). Every one
  encountered is listed by address and source line. A chain through an
  indirect call is not followed, so a reported total is a lower bound unless
  the indirect-call list is empty. The one exception is a call through a
  dispatch table named with ``--stitch-table CALLER=TABLE``: every function the
  table holds in the image is walked as a callee of CALLER
  (``Image.stitch_table()``). The ``callx``/``jalr`` itself is still listed.
  So is a call through a function pointer the program sets at run time and
  a library calls through from several places, named with ``--stitch-pointer
  POINTER=CALLEE`` (``Image.stitch_pointer_calls()``): ESP-IDF's log print
  hook is the one this project installs.
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
Build the environment (``make build BUILD_ENV=firebeetle2_profiler``). To have
the compiler's ``.su`` records printed beside the walk as well, build with the
flag -- no platformio.ini edit needed, PlatformIO exposes build_src_flags as an
environment variable::

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
import tempfile
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
# `lw a5,-1780(gp) # 4ff13d0c <esp_log_vprint_func>` -- RISC-V objdump resolves
# a gp- or pc-relative operand to the address it names, after a `#`.
RISCV_REF_RE = re.compile(r"#\s*([0-9a-f]+)\s+<[^>]+>\s*$")
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
                 "decoded", "refs")

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
        # Addresses this body's instructions load or name: an Xtensa `l32r`
        # literal's value, a RISC-V operand objdump resolves after `#`. What
        # `Image.stitch_pointer_calls()` finds a pointer's callers by.
        self.refs: set[int] = set()


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
        # Entries of the bodies the listing printed with no instruction at all
        # (see `_read_body()`), and of the ones `_recover_data_bodies()` then
        # read from a listing that decodes them. Entries rather than names: a
        # static function's name is not unique in the image.
        self.data_bodies: set[int] = set()
        self.recovered_bodies: set[int] = set()
        # (caller, callee) edges `drop_infeasible_calls()` removed.
        self.dropped_calls: list[tuple[str, str]] = []
        # Data objects by name, read on the first `table_entries()` call only:
        # a walk that stitches no table does not pay for a second symbol read.
        self._objdump = objdump
        self._objects: dict[str, list[tuple[int, int, str]]] | None = None
        self._empty_sections: set[str] | None = None
        self._disassemble(objdump)
        if self.arch == "xtensa" and self.data_bodies:
            self._recover_data_bodies(objdump)
        self._starts = sorted(self.funcs)
        self._drop_internal_branches()
        for fn in self.funcs.values():
            if fn.decoded == 0:
                # objdump emitted this body as raw words rather than
                # instructions, even after `_recover_data_bodies()`. A frame of
                # 0 here means "not read", not "leaf", and treating the two
                # alike would silently drop a whole subtree. See "WHICH BYTES
                # ARE CODE AT ALL" in the module docstring.
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

    # -- dispatch tables ----------------------------------------------------
    def _data_objects(self) -> dict[str, list[tuple[int, int, str]]]:
        """Data objects in the image by name: [(address, size, section)], read once."""
        if self._objects is None:
            self._objects = {}
            for line in self._run([str(self._objdump), "-t", "-C", str(self.elf)]):
                head, sep, tail = line.partition("\t")
                if not sep or " O " not in head:
                    continue
                size_name = tail.split(None, 1)
                if len(size_name) != 2:
                    continue
                try:
                    addr = int(head.split()[0], 16)
                    size = int(size_name[0], 16)
                except (ValueError, IndexError):
                    continue
                self._objects.setdefault(size_name[1].strip(), []).append(
                    (addr, size, head.split()[-1]))
        return self._objects

    def _sections_without_contents(self) -> set[str]:
        """Sections the ELF allocates but stores no bytes for (.bss, .noinit).

        `objdump -h` prints each section's flags on the line after it; one
        without CONTENTS is zero, or uninitialised, until the program runs.
        """
        if self._empty_sections is None:
            self._empty_sections = set()
            name = None
            for line in self._run([str(self._objdump), "-h", str(self.elf)]):
                fields = line.split()
                if len(fields) >= 7 and fields[0].isdigit():
                    name = fields[1]
                    continue
                if name is not None and line.startswith(" "):
                    if "CONTENTS" not in line:
                        self._empty_sections.add(name)
                    name = None
        return self._empty_sections

    def table_entries(self, table: str) -> list[int]:
        """Function entries held by one data object in the image, in order.

        A dispatch table is an array of rows in read-only data, and a row's
        function pointer is a word equal to some function's entry address. So
        the object's bytes are read out of the image and every aligned
        little-endian word that is a function entry is taken; the other words
        (a row's name pointer, padding) name no entry and fall away. Reading the
        table rather than listing its rows in a recipe is what keeps a new row
        from being missed.

        Raises KeyError when the object is absent, Fatal when its name is
        ambiguous or its bytes cannot be read.
        """
        found = self._data_objects().get(table)
        if not found:
            raise KeyError(table)
        if len(found) != 1:
            raise Fatal(f"{len(found)} data objects are named {table} in {self.elf}; "
                        "a stitched table must name one")
        addr, size, section = found[0]
        if section in self._sections_without_contents():
            # A table in .bss holds nothing until the program fills it, so the
            # image names no function in it; a pointer set at run time is what
            # `pointer_tables` is for.
            return []
        if addr % 4 or size % 4:
            raise Fatal(f"{table} at 0x{addr:08x} ({size} B) is not word-aligned; "
                        "it does not read as a table of pointers")
        data: dict[int, int] = {}
        for line in self._run([str(self._objdump), "-s",
                               f"--start-address=0x{addr:x}",
                               f"--stop-address=0x{addr + size:x}", str(self.elf)]):
            # ` 3f42d4b0 2565403f 30e10e40 6c65403f 44e00e40  %e@?0..@le@?D..@`:
            # address, up to four groups of bytes in memory order, two spaces,
            # the ASCII column.
            if not line.startswith(" "):
                continue
            fields = line[1:].split("  ", 1)[0].split()
            try:
                at = int(fields[0], 16)
                raw = bytes.fromhex("".join(fields[1:]))
            except (ValueError, IndexError):
                continue
            for offset, value in enumerate(raw):
                data[at + offset] = value
        entries = []
        for at in range(addr, addr + size, 4):
            if any(at + i not in data for i in range(4)):
                raise Fatal(f"objdump -s did not cover {table} at 0x{at:08x}")
            word = int.from_bytes(bytes(data[at + i] for i in range(4)), "little")
            if word in self.funcs:
                entries.append(word)
        return entries

    def stitch_table(self, caller: str, table: str) -> list[Function]:
        """Add a call edge from `caller` to every function `table` holds.

        For an indirect call the walker cannot follow on its own: a call
        through a pointer loaded from a dispatch table. Every row's function
        becomes a callee of every symbol named `caller`, so the walk takes the
        deepest row and the report shows it. The edge is marked `stitched via
        <table>` in the chain. Raises KeyError when the caller or the table is
        absent, Fatal when the table holds no function entry at all.
        """
        callers = self.by_name(caller)
        if not callers:
            raise KeyError(caller)
        targets = self.table_entries(table)
        if not targets:
            raise Fatal(f"{table} holds no function entry; it is not a dispatch table")
        for fn in callers:
            for target in targets:
                fn.calls.append((target, f"stitched via {table}", None))
        return [self.funcs[target] for target in targets]

    def stitch_calls(self, caller: str, callees: list[str]) -> list[Function]:
        """Add a call edge from `caller` to each function named in `callees`.

        For a call through a pointer that is set at run time, so that neither
        the listing nor any table in the image says where it goes: a handler
        registered with esp_register_shutdown_handler() and called by
        esp_restart(), or the print hook installed with esp_log_set_vprintf()
        and called by esp_log_writev(). The recipe names the targets, with the
        registration that justifies each. The edge is marked `stitched call`.
        Raises KeyError naming the first absent symbol.
        """
        callers = self.by_name(caller)
        if not callers:
            raise KeyError(caller)
        targets: list[Function] = []
        for name in callees:
            found = self.by_name(name)
            if not found:
                raise KeyError(name)
            targets.extend(found)
        for fn in callers:
            for target in targets:
                fn.calls.append((target.addr, "stitched call", None))
        return targets

    def stitch_pointer_calls(self, pointer: str, callees: list[str]) -> list[Function]:
        """Add a call edge to `callees` from every function that calls through `pointer`.

        For a function pointer the program sets at run time and a library calls
        through from more than one place: ESP-IDF's log print hook,
        `esp_log_vprint_func`, which `esp_log_set_vprintf()` sets and
        `esp_log_va()` and `esp_log()` both call (components/log/src/log.c,
        esp_private/log_print.h). The callers are not named by the recipe but
        found in the listing - every function that loads or names the
        pointer's address AND makes an indirect call - so a library update that
        adds a caller is walked without editing the recipe. Setting the
        pointer is not a call, and `esp_log_set_vprintf()`, which only stores
        it, makes none. Over-approximates a function that loads the pointer and
        makes some other indirect call too, which is the safe direction for a
        floor. The edge is marked `stitched via <pointer>`.

        Returns the callers. Raises KeyError naming an absent pointer or
        callee, Fatal when the pointer's name is ambiguous or nothing in the
        image calls through it - the recipe no longer describes the image.
        """
        found = self._data_objects().get(pointer)
        if not found:
            raise KeyError(pointer)
        if len(found) != 1:
            raise Fatal(f"{len(found)} data objects are named {pointer} in {self.elf}; "
                        "a stitched pointer must name one")
        addr = found[0][0]
        targets: list[Function] = []
        for name in callees:
            hit = self.by_name(name)
            if not hit:
                raise KeyError(name)
            targets.extend(hit)
        callers = [fn for fn in self.funcs.values() if addr in fn.refs and fn.indirect]
        if not callers:
            raise Fatal(f"nothing in {self.elf} calls through {pointer}; "
                        "the stitch no longer describes this image")
        for fn in callers:
            for target in targets:
                fn.calls.append((target.addr, f"stitched via {pointer}", None))
        return sorted(callers, key=lambda f: f.addr)

    def drop_infeasible_calls(self, caller: str, callees: list[str]) -> list[tuple[str, str]]:
        """Remove the call edges from `caller` to each of `callees`.

        For a call the listing shows but that cannot execute, which the walk
        has no way to tell from one that can: a log statement behind a check
        its only caller always passes. ESP-IDF's esp_cache_get_alignment()
        opens with ESP_RETURN_ON_FALSE(out_alignment, ..., "null pointer")
        (esp_cache_msync.c:281), and every caller in the linked images -
        esp_heap_adjust_alignment_to_hw() (heap_align_hw.c:54), and on the
        ESP32-P4 sdmmc_host_check_buffer_alignment() too - passes the address
        of a local. The log call behind it is reachable from every
        malloc in the image, and with the IDF log hook stitched it put the
        hook on eleven of twelve artoo-esp32 chains (#430). The recipe that
        names an edge carries that evidence beside it.

        Every named edge must exist: a callee this caller no longer calls
        raises Fatal, so an entry that has gone stale fails loudly rather than
        pruning nothing and reading as a fact. Raises KeyError naming an absent
        symbol. Returns the (caller, callee) pairs removed.
        """
        callers = self.by_name(caller)
        if not callers:
            raise KeyError(caller)
        dropped: list[tuple[str, str]] = []
        for name in callees:
            targets = {fn.addr for fn in self.by_name(name)}
            if not targets:
                raise KeyError(name)
            removed = 0
            for fn in callers:
                kept = [call for call in fn.calls if call[0] not in targets]
                removed += len(fn.calls) - len(kept)
                fn.calls = kept
            if not removed:
                raise Fatal(f"{caller} no longer calls {name} in {self.elf}; "
                            "the infeasible-call entry is stale")
            dropped.append((caller, name))
        self.dropped_calls.extend(dropped)
        return dropped

    def adopt_archive_bodies(self, names: list[str], archives: "ArchiveBodies",
                             pointer_tables: dict[str, str] | None = None) -> list[str]:
        """Walk bodies the product listing printed as data from their archive members.

        objdump emits some Xtensa bodies in the linked image as data (see the
        module docstring). `_recover_data_bodies()` decodes nearly all of them
        from a stripped copy, but a decode only resolves a call whose target a
        literal names: a closed library calling its OS adapter through a
        pointer the program sets at run time is still an indirect call there.
        The archive member the linker took the body from records more - the
        `entry a1, N` that is its frame, and relocations naming everything its
        literal pool loads, table pointers included. For each named function,
        and every function reached from it that way whose body the product
        listing also printed as data, this takes the frame from the member and
        makes every function the member's relocations name a callee.

        A relocation naming a data object stands for a call through a table of
        pointers: every function entry that object holds in the image becomes a
        callee. `pointer_tables` maps a pointer the program sets at run time
        (so the image holds no address in it) to the table it is set to point
        at. Both over-approximate - every row, whether or not that path calls
        it, and every function a relocation names, whether it is called or only
        has its address taken - which is the safe direction for a floor.

        Returns the names adopted. Raises Fatal when a named function is absent,
        was decoded by the product listing, or has no archive body.
        """
        pointer_tables = pointer_tables or {}
        adopted: list[str] = []
        queue: list[Function] = []
        for name in names:
            found = self.by_name(name)
            if not found:
                raise Fatal(f"archive body {name}: not in the image")
            for fn in found:
                if fn.frame_kind == "archive":
                    continue  # already adopted, through another recipe arm
                if not self._printed_as_data(fn):
                    raise Fatal(f"archive body {name}: the product listing decodes it "
                                f"({fn.frame_kind}); walk the image instead")
                if archives.body(fn.name) is None:
                    raise Fatal(f"archive body {name}: no archive member defines it")
                queue.append(fn)
        seen: set[int] = set()
        while queue:
            fn = queue.pop()
            if fn.addr in seen:
                continue
            seen.add(fn.addr)
            body = archives.body(fn.name)
            if body is None:
                continue
            frame, refs = body
            fn.frame = frame
            fn.frame_kind = "archive"
            fn.calls = []
            for ref in sorted(refs):
                targets = self.by_name(ref)
                if not targets:
                    table = pointer_tables.get(ref, ref)
                    if self._could_be_pointer_table(table):
                        # table_entries() raises on an ambiguous name or an
                        # unreadable object; that propagates, because walking
                        # on would drop this body's callees without saying so.
                        targets = [self.funcs[a] for a in self.table_entries(table)]
                for target in targets:
                    fn.calls.append((target.addr, "archive reloc", None))
                    if self._printed_as_data(target) and target.addr not in seen:
                        queue.append(target)
            adopted.append(fn.name)
        return adopted

    def _could_be_pointer_table(self, name: str) -> bool:
        """Whether a data object a relocation names can hold function pointers.

        Only a word-aligned object of whole words can; a string, a byte buffer
        or a packed struct a closed library's literal pool also names cannot,
        and is not a table. Decided here rather than by catching
        `table_entries()`'s refusal, so that its other refusals - an ambiguous
        name, bytes objdump did not cover - still stop the walk.
        """
        found = self._data_objects().get(name)
        if not found:
            return False
        return all(addr % 4 == 0 and size % 4 == 0 for addr, size, _section in found)

    def _printed_as_data(self, fn: Function) -> bool:
        """Whether the product listing printed this body as data, recovered or not."""
        return fn.frame_kind == "undecoded" or fn.addr in self.recovered_bodies

    # The linked image's Xtensa property tables. `.xt.lit` goes with `.xt.prop`
    # because objdump reads literal extents from it the same way; the spike on
    # #430 stripped both, and that is the copy these figures were measured on.
    XTENSA_PROPERTY_SECTIONS = (".xt.prop", ".xt.lit")

    def _recover_data_bodies(self, objdump: Path) -> None:
        """Read the bodies objdump printed as data from a copy without .xt.prop.

        Xtensa only; see "WHICH BYTES ARE CODE AT ALL" in the module docstring
        for why objdump prints real code as literal words in this image. With
        the property tables removed it has nothing to consult and decodes every
        byte as an instruction, so those bodies read in full.

        Only the bodies the product listing printed as data are taken from the
        stripped listing. The rest keep what the product listing gave them,
        because stripping the tables also costs something: where they marked
        data inside a body correctly, the stripped listing decodes that data as
        instructions, and on the artoo-esp32 image 24 bodies that decode
        normally misframe there and lose one or two real calls
        (`servoOutputDrivesPart` among them, #430). A recovered body goes
        through the same symbol-size bound and framing fixpoint as any other,
        and those can only drop an edge, never add one.

        A body the stripped listing still prints as data stays in
        `data_bodies`, and is labelled undecoded like before.
        """
        hidden = set(self.data_bodies)
        before = {addr: self.funcs[addr] for addr in hidden}
        objcopy = objdump.with_name(objdump.name.replace("objdump", "objcopy"))
        with tempfile.TemporaryDirectory(prefix="stack_walk_") as tmp:
            stripped = Path(tmp) / "no-xt-prop.elf"
            argv = [str(objcopy)]
            for section in self.XTENSA_PROPERTY_SECTIONS:
                argv.append(f"--remove-section={section}")
            # _run() raises Fatal on a non-zero exit; objcopy prints nothing on
            # success, so there is nothing to read from it.
            for _line in self._run(argv + [str(self.elf), str(stripped)]):
                pass
            self.data_bodies = set()
            self._disassemble(objdump, stripped, hidden)
        for addr in hidden:
            fn = self.funcs[addr]
            if fn is before[addr]:
                # The stripped listing carried no header here, so this body was
                # not read again: it is still the data the first read found.
                self.data_bodies.add(addr)
            elif addr not in self.data_bodies:
                self.recovered_bodies.add(addr)

    def _disassemble(self, objdump: Path, elf: Path | None = None,
                     only: set[int] | None = None) -> None:
        """Read the listing into one Function per symbol, body by body.

        Each body is buffered and then handed to `_read_body()` whole, rather
        than folded in as the lines arrive. That is not a style choice: the
        framing check `_read_body()` performs needs every instruction in the
        body before it can say which of them are real, and an instruction's own
        branch target can lie either side of it.

        `elf` and `only` are for `_recover_data_bodies()`: read another listing
        of this same image, and replace just the bodies at the entries in
        `only`, leaving every other body as the first read left it.
        """
        cur: Function | None = None
        cur_end: int | None = None
        srcline: str | None = None
        body: list[tuple[int, int, str, str, str | None, str]] = []

        for line in self._run([str(objdump), "-d", "-l", "-C", str(elf or self.elf)]):
            if not line:
                continue
            m = FUNC_HEADER_RE.match(line)
            if m:
                if cur is not None:
                    self._read_body(cur, cur_end, body)
                addr = int(m.group(1), 16)
                if only is not None and addr not in only:
                    # Not a body this read replaces: skip its lines, which the
                    # `cur is None` test below does for every line up to the
                    # next header.
                    cur = None
                    continue
                cur = Function(addr, m.group(2))
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
    # guarantees termination, and that is not belt-and-braces: a round can DROP
    # anchors as well as add them, because an added one splits the run it
    # lands in and that can take instructions out of the real set which named
    # anchors of their own. Measured on the artoo-esp32 image (#429): of the
    # bodies framed, 1,469 settle in one round, 3,080 in two and 9 in three;
    # the unsized region objdump calls `clearRxEdgeLatch()-0x1044` oscillates
    # between 94 and 97 anchors and never settles. A body that has not settled
    # is reported as unvalidated and keeps every edge objdump gave it, rather
    # than being half-judged.
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

        So the two are solved together, by a fixpoint grown FROM THE ENTRY:
        frame from the anchors proven so far, collect the anchors named by
        instructions that framing says are real, and repeat until the set stops
        changing. The entry is the one boundary known without asking objdump,
        so every anchor admitted is one a framed instruction names.

        It used to start from the other end, with every candidate anchor, and
        drop the ones no real instruction named. That keeps a bogus anchor
        whenever phantoms name each other, and a desynchronised window can do
        exactly that: framing from the bogus anchor is what makes the phantom
        naming it "real". `seqStorePrepare` in the artoo-esp32 image held such
        a set -- a phantom `bany` naming the address one byte past the real
        resume point, from which the run reaches that `bany` again -- and the
        bogus anchor it also named, one byte into a real `l32i`, condemned the
        calls to seqJsonParseVariant, protocolCheck, unlock and stagingFree
        (src/seq_store.cpp:276-281). SeqDisp walked 3,680 B against its
        recorded 4,432 B (#429). Grown from the entry, a phantom is admitted
        only if the framing already reached it from a proven boundary, which
        the pad skip in `_true_boundaries()` keeps it from doing.

        It is NOT monotone, and assuming it was is a mistake this comment used
        to carry: an anchor added splits the run it lands in, which can take
        instructions out of the real set that named anchors of their own. The
        round cap is what guarantees termination. A body that has not settled
        within `ANCHOR_ROUNDS` returns None here and keeps every edge. (`_stext`
        is unvalidated for the other reason in `_read_body()`: its entry
        address carries no instruction line for this to start from.)
        """
        anchors = {entry}
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

        if not any(not insns[pc][1].startswith(".") for pc in order):
            # objdump printed this body as data, or printed nothing for it:
            # there is no instruction here whose framing could be checked, no
            # frame and no call. It is recorded as such rather than counted as
            # an unvalidated body, so that `_recover_data_bodies()` can read it
            # again from another listing without leaving this read's
            # bookkeeping behind.
            self.data_bodies.add(fn.addr)
            return

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
        # RISC-V: registers the prologue has loaded with a known constant, so a
        # register-sized `add sp,sp,rX` can still be read as a fixed frame.
        prologue_consts: dict[str, int] = {}

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
                    self._maybe_frame(fn, mnem, ops, prologue_consts)
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


    def _maybe_frame(self, fn: Function, mnem: str, ops: str,
                     consts: dict[str, int] | None = None) -> None:
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
        # same way.
        m = re.match(r"^sp,\s*sp,\s*" + IMM_RE + r"$", ops)
        if mnem in ("addi", "c.addi16sp", "c.addi4spn") and m:
            delta = parse_imm(m.group(1))
            if delta < 0:
                fn.frame += -delta
                fn.frame_kind = "fixed"
            return
        # A frame past `addi`'s reach can also arrive through a register the
        # prologue loads with a constant first:
        #
        #   consoleExecuteDomeApiGetSequenceLastRun:
        #       addi sp,sp,-144   ...   lui t0,0xffffe   ...   add sp,sp,t0
        #
        # is a fixed 144 + 8192 B frame, not a variable one. Reading it as
        # "dynamic" counted 144 B and hid the 8 KB that overflowed the ESP32-P4
        # Console (#427, #429). So constants loaded by `lui`/`li`/`addi` are
        # tracked, and only an adjustment by a register holding something else
        # is a variable-length frame.
        if consts is None:
            consts = {}
        m = re.match(r"^sp,\s*sp,\s*(\w+)$", ops)
        if mnem in ("add", "c.add", "sub") and m:
            value = consts.get(m.group(1))
            if value is None:
                fn.frame_kind = "dynamic"
                return
            delta = value if mnem != "sub" else -value
            if delta < 0:
                fn.frame += -delta
                fn.frame_kind = "fixed"
            return
        m = re.match(r"^(\w+),\s*(?:(\w+),\s*)?" + IMM_RE + r"$", ops)
        if m and mnem in ("lui", "c.lui") and m.group(2) is None:
            value = (parse_imm(m.group(3)) & 0xFFFFF) << 12
            consts[m.group(1)] = value - (1 << 32) if value & 0x80000000 else value
            return
        if m and mnem in ("li", "c.li") and m.group(2) is None:
            consts[m.group(1)] = parse_imm(m.group(3))
            return
        if m and mnem in ("addi", "c.addi") and m.group(2) in consts:
            consts[m.group(1)] = consts[m.group(2)] + parse_imm(m.group(3))
            return
        # Anything else writing a tracked register makes it unknown again.
        dest = re.match(r"^(\w+)\s*(,|$)", ops)
        if dest and not RISCV_NON_WRITING.match(mnem):
            consts.pop(dest.group(1), None)

    # -- control flow -------------------------------------------------------
    def _xtensa_flow(self, fn, pc, mnem, ops, srcline, pending_lit) -> None:
        if mnem == "l32r":
            m = re.match(r"^(a\d+),", ops)
            val = L32R_VALUE_RE.search(ops)
            if val:
                fn.refs.add(int(val.group(1), 16))
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
        ref = RISCV_REF_RE.search(ops)
        if ref:
            fn.refs.add(int(ref.group(1), 16))
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


class ArchiveBodies:
    """Frames and callees of functions as the static archives record them.

    Xtensa only: it reads `entry a1, N` as the frame. Used by
    Image.adopt_archive_bodies() for bodies the linked image does not decode.
    For each function it answers (frame bytes, names its section's relocations
    reference) - R_XTENSA_32 in the literal pool and R_XTENSA_SLOT0_OP on a
    direct call, section symbols excluded. An archive is read the first time a
    function it defines is asked for.
    """

    def __init__(self, objdump: Path, libdir: Path):
        self.objdump = objdump
        self.nm = objdump.with_name(objdump.name.replace("objdump", "nm"))
        self.libdir = libdir
        self._defined_in: dict[str, Path] | None = None
        self._bodies: dict[str, tuple[int, set[str]]] = {}
        self._read: set[Path] = set()

    def _run(self, argv: list[str]):
        out = subprocess.run(argv, capture_output=True, text=True, errors="replace")
        if out.returncode != 0:
            raise Fatal(f"{argv[0]} exited {out.returncode}: {out.stderr.strip()[:400]}")
        return out.stdout.splitlines()

    def _index(self) -> dict[str, Path]:
        if self._defined_in is None:
            self._defined_in = {}
            archives = sorted(self.libdir.glob("*.a"))
            if not archives:
                raise Fatal(f"no archives under {self.libdir}")
            for line in self._run([str(self.nm), "-A", "--defined-only", *map(str, archives)]):
                # /path/libx.a:member.o:00000000 T symbol
                parts = line.rsplit(" ", 2)
                if len(parts) != 3 or parts[1] not in ("T", "t"):
                    continue
                self._defined_in.setdefault(parts[2], Path(parts[0].split(":", 1)[0]))
        return self._defined_in

    def _read_archive(self, archive: Path) -> None:
        self._read.add(archive)
        member = None
        owners: dict[tuple[str, str], list[str]] = {}
        for line in self._run([str(self.objdump), "-t", str(archive)]):
            m = re.match(r"^(\S+):\s+file format", line)
            if m:
                member = m.group(1)
                continue
            m = re.match(r"^[0-9a-f]+ (.{7}) (\S+)\s+[0-9a-f]+ (\S+)$", line)
            if m and "F" in m.group(1) and member:
                owners.setdefault((member, m.group(2)), []).append(m.group(3))
        frames: dict[str, int] = {}
        cur = None
        for line in self._run([str(self.objdump), "-d", str(archive)]):
            m = re.match(r"^[0-9a-f]+ <([^>+]+)>:$", line)
            if m:
                cur = m.group(1)
                continue
            m = re.search(r"\sentry\s+a1,\s*(\d+)", line)
            if m and cur is not None:
                frames.setdefault(cur, int(m.group(1)))
                cur = None
        refs: dict[str, set[str]] = {}
        member = section = None
        for line in self._run([str(self.objdump), "-r", str(archive)]):
            m = re.match(r"^(\S+):\s+file format", line)
            if m:
                member = m.group(1)
                continue
            m = re.match(r"^RELOCATION RECORDS FOR \[([^\]]+)\]:", line)
            if m:
                section = m.group(1)
                continue
            m = re.match(r"^[0-9a-f]+ (?:R_XTENSA_32|R_XTENSA_SLOT0_OP)\s+(\S+)", line)
            if not m or member is None or section is None:
                continue
            target = re.sub(r"\+0x[0-9a-f]+$", "", m.group(1))
            if target.startswith("."):
                continue
            for fn in owners.get((member, section), []):
                refs.setdefault(fn, set()).add(target)
        for fn, frame in frames.items():
            self._bodies.setdefault(fn, (frame, refs.get(fn, set())))

    def body(self, name: str) -> tuple[int, set[str]] | None:
        """(frame, referenced names) for `name`, or None when no archive has it."""
        archive = self._index().get(name)
        if archive is None:
            return None
        if archive not in self._read:
            self._read_archive(archive)
        return self._bodies.get(name)


def resolve_archive_dir(env: str) -> Path:
    """The framework archives the env's image is linked from."""
    budgets = json.loads(BUDGETS.read_text())
    platform, rec = "esp32", budgets["platforms"]["esp32"]
    for name, r in budgets["platforms"].items():
        if env in r.get("envs", []):
            platform, rec = name, r
            break
    core_dir = Path(os.path.expanduser(rec.get("core_dir", "~/.platformio")))
    libs = rec.get("libs_dir", platform)
    return core_dir / "packages" / "framework-arduinoespressif32-libs" / libs / "lib"


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
        # (entry, stack members in its cycle) -> result; see depth().
        self._memo: dict[tuple[int, frozenset[int]], tuple[int, list, bool]] = {}
        self._components: dict[int, frozenset[int]] | None = None
        self._states: dict[frozenset[int], int] = {}
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

    # Distinct (function, cycle context) states one strongly connected
    # component may take before the walk refuses to go on. See depth(): the
    # count is exponential in the size of a component only in the worst case,
    # and a call graph is nowhere near it -- measured at #430 with the IDF log
    # hook stitched, the largest artoo-esp32 component (49 functions) takes
    # 14,921 states and the largest firebeetle2 one (44) 20,725, and each walk
    # finishes in under a second. The cap is what makes the walk bounded in time
    # whatever a future stitch does to the graph, and reaching it is a Fatal
    # rather than a quietly shallower answer.
    MAX_COMPONENT_STATES = 1_000_000

    def _component(self, addr: int) -> frozenset[int]:
        """The strongly connected component of the call graph holding addr.

        Computed once, over every function in every image, on the first walk:
        like the memo, it assumes every stitch is in place before the first
        depth() call, which is the order every caller here keeps. Iterative
        Tarjan, because the graph is deeper than Python's recursion limit
        allows a recursive one to be.
        """
        if self._components is None:
            nodes = {}
            for img in self.images:
                for a, fn in img.funcs.items():
                    nodes.setdefault(a, fn)

            def successors(a: int) -> list[int]:
                out = []
                for target, _insn, _src in nodes[a].calls:
                    found = self.lookup(target)
                    if found is None or self.is_pruned(found[1]):
                        continue
                    out.append(found[1].addr)
                return out

            index: dict[int, int] = {}
            low: dict[int, int] = {}
            on_stack: set[int] = set()
            stack: list[int] = []
            components: dict[int, frozenset[int]] = {}
            counter = 0
            for start in nodes:
                if start in index:
                    continue
                index[start] = low[start] = counter
                counter += 1
                stack.append(start)
                on_stack.add(start)
                work = [(start, iter(successors(start)))]
                while work:
                    v, it = work[-1]
                    for w in it:
                        if w not in index:
                            index[w] = low[w] = counter
                            counter += 1
                            stack.append(w)
                            on_stack.add(w)
                            work.append((w, iter(successors(w))))
                            break
                        if w in on_stack:
                            low[v] = min(low[v], index[w])
                    else:
                        work.pop()
                        if work:
                            parent = work[-1][0]
                            low[parent] = min(low[parent], low[v])
                        if low[v] == index[v]:
                            members = []
                            while True:
                                w = stack.pop()
                                on_stack.discard(w)
                                members.append(w)
                                if w == v:
                                    break
                            comp = frozenset(members)
                            for w in members:
                                components[w] = comp
            self._components = components
        return self._components.get(addr, frozenset((addr,)))

    def depth(self, img: Image, fn: Function, stack=()):
        """(bytes, chain, cut_below) for the deepest path from fn.

        A call to a function already on the call stack is a real call, so its
        frame is counted, but the walk does not go round the cycle again: the
        edge is cut and reported, and ``cut_below`` says whether this result was
        shortened by such a cut anywhere beneath it. `stack` is the functions
        above this one on the path being walked.

        WHAT A RESULT DEPENDS ON
        ------------------------
        Not the whole stack: only the part of it inside fn's own strongly
        connected component. A function on the stack that fn can reach again
        is, by definition, in fn's component - everything above it that is
        not is unreachable from here, and cannot be what a cut cuts. So a
        result is memoised under (fn, the stack's members in fn's component),
        and is correct wherever that key recurs. Outside any cycle the key is
        (fn, nothing), which is the plain memo this walk always had.

        That fixes two faults this method has had, without trading one for
        the other:

        - A cut result used to be memoised by fn alone and reused under a
          different stack, which made the report depend on the order roots
          were passed: on the firebeetle2 image `--root domeTask` alone gave
          3008 bytes but 3296 when safetyMonitorTask was walked first (#245,
          #248 were measured with that bug). The key now carries the stack
          the cut depended on, so the order cannot matter.
        - The fix for that stopped memoising cut results at all, and every
          ancestor of a cycle then re-walked its whole subtree once per path
          reaching it: exponential. With the IDF log hook stitched, the
          firebeetle2 walk did not finish in 20 minutes (#430).

        The number of keys in one component is bounded by
        MAX_COMPONENT_STATES; reaching it raises Fatal.
        """
        comp = self._component(fn.addr)
        local = frozenset(a for a in stack if a in comp)
        if fn.addr in local:
            self.cut_cycles.append(fn.name)
            return 0, [("<recursion cut>", 0, None)], True
        key = (fn.addr, local)
        if key in self._memo:
            return self._memo[key]
        if len(comp) > 1:
            states = self._states.get(comp, 0) + 1
            if states > self.MAX_COMPONENT_STATES:
                raise Fatal(
                    f"the {len(comp)}-function cycle through {fn.name} took more than "
                    f"{self.MAX_COMPONENT_STATES} walk states; a stitch has made the "
                    "call graph too cyclic to walk exactly")
            self._states[comp] = states
        below = local | {fn.addr}
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
            sub, chain, sub_cut = self.depth(timg, tfn, below if tfn.addr in comp else ())
            # Any cut anywhere among the branches can have suppressed the one
            # that would have won, so the maximum itself is suspect, not just
            # the branch that was cut.
            cut_below = cut_below or sub_cut
            total = tfn.frame + sub
            if total > best_sub:
                best_sub, best_chain, best_edge = total, chain, (tfn, insn, srcline)
        if best_edge is None:
            result = (0, [], cut_below)
        else:
            tfn, insn, srcline = best_edge
            result = (best_sub, [(tfn.name, tfn.frame, srcline, insn)] + best_chain,
                      cut_below)
        self._memo[key] = result
        return result

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
    ap.add_argument("--stitch-table", action="append", default=[],
                    metavar="CALLER=TABLE",
                    help="follow an indirect call through a dispatch table: every "
                         "function TABLE holds becomes a callee of CALLER "
                         "(repeatable; the 'tables' of a task_stack_recipes.json arm)")
    ap.add_argument("--stitch-call", action="append", default=[],
                    metavar="CALLER=CALLEE",
                    help="follow a call through a pointer set at run time: CALLEE "
                         "becomes a callee of CALLER (repeatable; the 'calls' of a "
                         "task_stack_recipes.json arm)")
    ap.add_argument("--stitch-pointer", action="append", default=[],
                    metavar="POINTER=CALLEE",
                    help="follow every call through a function pointer set at run "
                         "time: CALLEE becomes a callee of each function that loads "
                         "POINTER and calls indirectly (repeatable; the 'pointer_calls' "
                         "of task_stack_recipes.json)")
    ap.add_argument("--drop-call", action="append", default=[],
                    metavar="CALLER=CALLEE",
                    help="remove a call edge that cannot execute (repeatable; the "
                         "'infeasible_calls' of task_stack_recipes.json, with the "
                         "evidence it cannot run)")
    ap.add_argument("--archive-body", action="append", default=[], metavar="FUNCTION",
                    help="walk an undecoded body from its archive member (repeatable; "
                         "the 'archive_bodies' of a task_stack_recipes.json arm)")
    ap.add_argument("--pointer-table", action="append", default=[],
                    metavar="POINTER=TABLE",
                    help="a pointer set at run time to TABLE, for --archive-body")
    args = ap.parse_args(argv)
    prune = () if args.no_prune else DEFAULT_PRUNE

    platform, arch, objdump, elf, rom_elf = resolve_paths(args.env, not args.no_rom)
    build_dir = ROOT / ".pio" / "build" / args.env

    images = [Image("image", elf, objdump, arch)]
    if rom_elf is not None:
        images.append(Image("rom", rom_elf, objdump, arch))
    walker = Walker(images, prune)
    img = images[0]
    stitched = []
    for spec in args.stitch_table:
        caller, sep, table = spec.partition("=")
        if not sep or not caller or not table:
            raise Fatal(f"--stitch-table wants CALLER=TABLE, got {spec!r}")
        try:
            stitched.append((caller, table, len(img.stitch_table(caller, table))))
        except KeyError as exc:
            raise Fatal(f"--stitch-table {spec}: {exc.args[0]} is not in the image") from exc
    for spec in args.stitch_call:
        caller, sep, callee = spec.partition("=")
        if not sep or not caller or not callee:
            raise Fatal(f"--stitch-call wants CALLER=CALLEE, got {spec!r}")
        try:
            img.stitch_calls(caller, [callee])
        except KeyError as exc:
            raise Fatal(f"--stitch-call {spec}: {exc.args[0]} is not in the image") from exc
    if args.archive_body:
        pointers = {}
        for spec in args.pointer_table:
            ptr, sep, table = spec.partition("=")
            if not sep or not ptr or not table:
                raise Fatal(f"--pointer-table wants POINTER=TABLE, got {spec!r}")
            pointers[ptr] = table
        img.adopt_archive_bodies(args.archive_body,
                                 ArchiveBodies(objdump, resolve_archive_dir(args.env)),
                                 pointers)
    pointer_callers = []
    for spec in args.stitch_pointer:
        pointer, sep, callee = spec.partition("=")
        if not sep or not pointer or not callee:
            raise Fatal(f"--stitch-pointer wants POINTER=CALLEE, got {spec!r}")
        try:
            callers = img.stitch_pointer_calls(pointer, [callee])
        except KeyError as exc:
            raise Fatal(f"--stitch-pointer {spec}: {exc.args[0]} is not in the image") from exc
        pointer_callers.append((pointer, callee, [fn.name for fn in callers]))
    for spec in args.drop_call:
        caller, sep, callee = spec.partition("=")
        if not sep or not caller or not callee:
            raise Fatal(f"--drop-call wants CALLER=CALLEE, got {spec!r}")
        try:
            img.drop_infeasible_calls(caller, [callee])
        except KeyError as exc:
            raise Fatal(f"--drop-call {spec}: {exc.args[0]} is not in the image") from exc

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
    for caller, table, count in stitched:
        print(f"  stitched {caller} -> every row of {table} ({count} functions)")
    for pointer, callee, callers in pointer_callers:
        print(f"  stitched every call through {pointer} -> {callee}"
              f" (from {', '.join(callers)})")
    for caller, callee in img.dropped_calls:
        print(f"  dropped {caller} -> {callee} (cannot execute)")
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
    if img.recovered_bodies:
        print(f"  function bodies the product listing printed as data, decoded from a"
              f" copy without {'/'.join(Image.XTENSA_PROPERTY_SECTIONS)}:"
              f" {len(img.recovered_bodies)}")
    print(f"  function bodies still emitted as data, frame unknown: {len(undec)}"
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
