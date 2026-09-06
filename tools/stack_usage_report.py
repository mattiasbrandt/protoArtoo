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

WHAT IT CANNOT SEE, AND SAYS SO
-------------------------------
- Indirect calls (``callx*`` on Xtensa through a register that was not loaded
  from a literal, ``jalr`` on RISC-V through a computed address). Every one
  encountered is listed by address and source line. A chain through an
  indirect call is not followed, so a reported total is a lower bound unless
  the indirect-call list is empty.
- Interrupt and exception frames. ESP-IDF gives interrupts their own stack
  (``CONFIG_FREERTOS_ISR_STACKSIZE``), but the entry sequence on both ports
  spills some state to the interrupted task's stack before switching. That
  cost is not part of a call chain and is not counted here.
- Recursion. A cycle in the call graph is cut and reported; the total for a
  chain through a cut edge is a lower bound.
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
    """(pc, mnemonic, operands) for an objdump instruction line, else None.

    objdump separates address, raw bytes, mnemonic and operands with tabs, and
    prints the raw bytes as one unspaced blob whose width varies with the
    instruction length. Splitting on tabs is the only stable read of that.
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
    mnem = parts[2].strip()
    if not mnem:
        return None
    ops = parts[3].strip() if len(parts) > 3 else ""
    return pc, mnem, ops


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

    def _disassemble(self, objdump: Path) -> None:
        cur: Function | None = None
        srcline: str | None = None
        pending_lit: dict[str, int] = {}   # xtensa: reg -> literal address
        pending_auipc: dict[str, tuple[int, int]] = {}  # riscv: reg -> (pc, imm)
        prologue_left = 0

        for line in self._run([str(objdump), "-d", "-l", "-C", str(self.elf)]):
            if not line:
                continue
            m = FUNC_HEADER_RE.match(line)
            if m:
                cur = Function(int(m.group(1), 16), m.group(2))
                # The definition site is the first line marker AFTER the header;
                # the one before it belongs to the previous function.
                srcline = None
                self.funcs[cur.addr] = cur
                pending_lit.clear()
                pending_auipc.clear()
                # A stack adjustment always sits in the prologue. Bounding the
                # window keeps a mid-function `addi sp,sp,-N` (an alloca, or the
                # epilogue's restore) out of the frame figure.
                prologue_left = 24
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
            pc, mnem, ops = im
            cur.decoded += 1
            if prologue_left > 0:
                if self._ends_prologue(mnem, ops):
                    prologue_left = 0
                else:
                    prologue_left -= 1
                    self._maybe_frame(cur, mnem, ops)
            if self.arch == "xtensa":
                self._invalidate(XTENSA_NON_WRITING, r"a\d+", mnem, ops, pending_lit)
                self._xtensa_flow(cur, pc, mnem, ops, srcline, pending_lit)
            else:
                self._invalidate(RISCV_NON_WRITING, r"\w+", mnem, ops, pending_auipc)
                self._riscv_flow(cur, pc, mnem, ops, srcline, pending_auipc)

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

        Bounded by the next symbol start rather than by a fixed span: an
        address in .rodata or in a gap must resolve to nothing, or a stale
        register value gets attributed to whichever function happens to
        precede it and the walk follows an edge that does not exist.
        """
        if not self._starts:
            return None
        i = bisect.bisect_right(self._starts, addr) - 1
        if i < 0:
            return None
        start = self._starts[i]
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
    undec = [f for f in img.funcs.values() if f.frame_kind == "undecoded"]
    print(f"  function bodies objdump emitted as data, frame unknown: {len(undec)}"
          f" of {len(img.funcs)}")
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
