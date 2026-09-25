#!/usr/bin/env python3
"""Re-walk every task's Measured Chain recipe and fail when one outgrows its chip's rule.

WHY THIS EXISTS
---------------
`include/config.h` carries a `*_MEASURED_CHAIN_BYTES` constant per task per
chip, and a `static_assert` that the task's stack covers it. That assert stops
the *constant* being trimmed. Nothing noticed the *chain* growing past it --
which is the half of the problem #226 found the expensive way, with a reboot on
both boards (ADR 0040).

A recorded chain is a hand-written number. This re-derives it from the linked
image, so a slice that deepens a call chain past its recorded constant fails
here instead of on a board.

WHAT A RECIPE IS
----------------
`tools/task_stack_recipes.json` records, per task and per chip arm: the
PlatformIO environment the figure was measured on, the root symbols walked, and
the ways an indirect call, or a body, the walker cannot follow is stitched back
in:

- `frames`: own frames ABOVE the root, added by hand, where the call through a
  pointer is what reaches the root (the Console's `cli->onCommand`);
- `tables`: a dispatch table BELOW the root. Every function the table holds
  becomes a callee of the function that calls through it, so the walk takes
  the deepest row itself (`stack_usage_report.Image.stitch_table()`);
- `calls`: named callees of a caller that calls them through a pointer set at
  run time, so no table in the image holds them - a registered shutdown
  handler under esp_restart() (`Image.stitch_calls()`, #428);
- `archive_bodies`: functions whose body the image emits as data, walked from
  the archive member they were linked from instead - the frame from `entry`,
  callees from its relocations, with `pointer_tables` naming the table a
  run-time pointer is set to (`Image.adopt_archive_bodies()`, #428).

Like `tables`, all three are facts about the image rather than one task, so
each is applied once, to the whole graph, before the first walk.

One stitch belongs to no task at all, and lives at the top of the recipe file
instead of in an arm: `pointer_calls`, a function pointer the program sets at
run time that a library calls through from several places - ESP-IDF's log
print hook, which src/main.cpp sets to paLogIdfVprintf. Every function the
listing shows loading the pointer and calling indirectly gets an edge to the
named callee (`Image.stitch_pointer_calls()`, #430), so every task that can
reach an ESP-IDF log call walks the hook, on each chip the entry names. It is
applied after every arm's stitches and archive bodies, which rewrite a body's
calls, and before the first walk.

The recipe file does not carry the log-hook entry yet. Stitched, it moves
eleven of twelve artoo-esp32 chains past their recorded constants, and the
stack raises that would follow cost more heap than #430 may spend without an
operator decision; the measured entry and figures are on #430.

The chain is

    chain = max over roots of (that root's total worst-case chain,
                               walked with every stitched table's rows)
          + sum over stitched frames of (that symbol's own frame)

which is exactly what the two `tools/stack_usage_report.py` invocations in
`include/config.h`'s Console recipe compute between them.

WHAT IT CHECKS, AND WHAT IT DELIBERATELY DOES NOT
-------------------------------------------------
Covered arms are the ones whose recorded environment matches `--env`. Arms
recorded against a different environment (the other chip's product image, or a
profiler image substituted because the product image's body is emitted as data)
are listed as not covered and are not guessed at. A covered arm is judged per
chip (ADR 0040, amendment of 2026-09-25, #429) - see `judge_arm()`:

- artoo-esp32 is byte-exact: the freshly walked chain must be <= the
  `*_MEASURED_CHAIN_BYTES` constant. It is the scarce chip, and every byte of
  growth should stop a slice.
- ESP32-P4 is judged by allocation: an arm fails only when ADR 0040's rule
  applied to the walk, ceil512(ceil(chain x 1.25)), exceeds its
  `*_STACK_BYTES`. A walk that has merely moved from its recorded figure is a
  note, and the figure is re-derived when the task is next touched. The P4 is
  walked by a coordinator's post-merge run rather than by the slice that moves
  it, so failing on drift there failed whichever slice came next.

Every walk is a floor, not a bound: an indirect call that is not stitched is
not followed, a call-graph cycle is cut, and on artoo-esp32 a body objdump
still prints as data after `stack_usage_report`'s recovery pass reads as a
frame of 0 with no calls - the header line counts both the recovered bodies and
the ones left. This check can therefore MISS growth and cannot report FALSE
growth, which is what makes it safe to fail a build on (ADR 0040).

Two conditions fail beyond an exceedance, because both mean the recorded recipe
no longer describes the image and a silent pass would be the drift this exists
to catch:

- a root, stitched-frame, stitched-table or table-caller symbol is absent
  (renamed, or inlined away), or a stitched pointer is absent or nothing in
  the image calls through it any more;
- a covered root's body is emitted as data in the very image the recipe names,
  so the walk that produced the recorded figure cannot be reproduced.

Exit codes: 0 = every covered chain passes its chip's judgement; 1 = an input
was missing or unreadable; 2 = at least one covered recipe failed.
"""

from __future__ import annotations

import argparse
import json
import re
import sys
from pathlib import Path
from typing import NamedTuple

# Sibling module in tools/, which is on sys.path for both entry points: this
# script run directly, and the tooling tests that import it.
import stack_usage_report as sur

ROOT = Path(__file__).resolve().parents[1]
RECIPES = ROOT / "tools" / "task_stack_recipes.json"
CONFIG_H = ROOT / "include" / "config.h"

# The per-chip task-stack ladder in include/config.h. Parsed rather than
# compiled: this runs as a slice-gate row, where a C++ probe per environment is
# cost the row does not need. The parse is strict -- a changed block shape
# raises instead of returning half a ladder -- and
# test/test_tools/test_task_stack_recipes.py proves it agrees with the
# compiler probe that reads the same header for real.
#
# The block is found from its #else sentinel and read backwards, NOT by matching
# `#if defined(PA_CHIP_TARGET_ESP32P4)` forwards: config.h has a second ladder
# on the same condition (UART_PORT_MAX), and a forward non-greedy match
# straddles the two and returns one arm from each.
STACK_LADDER_SENTINEL = '#error "task stack sizes have no value for this chip target"'
LADDER_OPEN = "#if defined(PA_CHIP_TARGET_ESP32P4)\n"
LADDER_MIDDLE = "#elif defined(PA_CHIP_TARGET_ESP32)\n"
LADDER_CLOSE = "#else\n"
CONSTEXPR_RE = re.compile(
    r"^constexpr uint32_t (?P<name>[A-Z0-9_]+) = (?P<value>\d+);", re.MULTILINE
)


class Fatal(Exception):
    """An input this check cannot be produced without."""


# How a re-walked chain is judged, per chip (ADR 0040's 2026-09-25 amendment).
# A chip in neither set is refused rather than defaulted: which way a new chip
# is judged is a decision, and the looser default would be the silent one.
BYTE_EXACT_CHIPS = frozenset({"esp32"})
ALLOCATION_RULE_CHIPS = frozenset({"esp32p4"})


def rule_stack(chain: int) -> int:
    """ADR 0040's sizing rule: ceil512(ceil(chain x 1.25)), in integers."""
    need = (chain * 5 + 3) // 4
    return ((need + 511) // 512) * 512


class Verdict(NamedTuple):
    failure: str | None  # why this arm fails the check; None when it passes
    detail: str          # the row's status column
    note: str | None     # printed under the table; never fails the check


def judge_arm(chip: str, walked: int, chain_name: str, chain: int,
              stack_name: str, stack: int) -> Verdict:
    """Judge one re-walked arm against its recorded chain and its stack."""
    if chip in BYTE_EXACT_CHIPS:
        if walked > chain:
            return Verdict(
                f"chain {walked} B exceeds {chain_name} = {chain} B by "
                f"{walked - chain} B -- re-measure and re-derive the stack on "
                "both chips, with the reason",
                f"OVER {chain_name}={chain}", None)
        return Verdict(
            None, f"within {chain_name}={chain} (headroom {chain - walked} B)", None)
    if chip in ALLOCATION_RULE_CHIPS:
        need = rule_stack(walked)
        drift = None
        if walked != chain:
            drift = (
                f"walked {walked} B against the recorded {chain_name} = {chain} B"
                f" ({walked - chain:+d} B); {stack_name} still covers it by the"
                " rule - re-derive the figure when this task is next touched"
            )
        if need > stack:
            return Verdict(
                f"the rule on the walked chain, {walked} -> {need} B, exceeds "
                f"{stack_name} = {stack} B by {need - stack} B -- re-derive the "
                "chain and raise the stack by the rule, with the reason",
                f"OVER {stack_name}={stack} (rule {walked} -> {need})", None)
        return Verdict(
            None,
            f"within {stack_name}={stack} (rule {walked} -> {need};"
            f" recorded {chain_name}={chain})",
            drift)
    raise Fatal(
        f"no judgement recorded for chip '{chip}': ADR 0040 decides per chip "
        "whether a chain is byte-exact or judged by its allocation"
    )


def parse_chip_constants() -> dict[str, dict[str, int]]:
    """{chip: {CONSTANT: value}} from include/config.h's per-chip stack block."""
    where = CONFIG_H.relative_to(ROOT)
    text = CONFIG_H.read_text(encoding="utf-8")
    if text.count(STACK_LADDER_SENTINEL) != 1:
        raise Fatal(
            f"{where}: expected exactly one {STACK_LADDER_SENTINEL!r}; found "
            f"{text.count(STACK_LADDER_SENTINEL)}. This check locates the "
            "task-stack ladder by that sentinel and cannot read the constants "
            "it enforces without it."
        )
    end = text.index(STACK_LADDER_SENTINEL)
    close = text.rindex(LADDER_CLOSE, 0, end)
    open_at = text.rindex(LADDER_OPEN, 0, close)
    block = text[open_at + len(LADDER_OPEN) : close]
    if block.count(LADDER_MIDDLE) != 1:
        raise Fatal(
            f"{where}: the task-stack ladder does not have exactly one "
            f"{LADDER_MIDDLE.strip()!r} arm; its shape changed and this parse "
            "would return half a ladder"
        )
    arms = dict(zip(("esp32p4", "esp32"), block.split(LADDER_MIDDLE)))
    out: dict[str, dict[str, int]] = {}
    for chip, arm_text in arms.items():
        arm = {
            m.group("name"): int(m.group("value"))
            for m in CONSTEXPR_RE.finditer(arm_text)
        }
        if not arm:
            raise Fatal(f"{where}: no constexpr uint32_t declarations in the {chip} arm")
        out[chip] = arm
    return out


def load_recipes() -> dict:
    try:
        return json.loads(RECIPES.read_text(encoding="utf-8"))
    except OSError as exc:
        raise Fatal(f"cannot read {RECIPES.relative_to(ROOT)}: {exc}") from exc
    except ValueError as exc:
        raise Fatal(f"{RECIPES.relative_to(ROOT)} is not valid JSON: {exc}") from exc


class ImageChains:
    """One built image, answering 'what is this symbol's chain / own frame'."""

    def __init__(self, env: str, no_rom: bool = False):
        platform, arch, objdump, elf, rom_elf = sur.resolve_paths(env, not no_rom)
        self.env = env
        self.platform = platform
        self.arch = arch
        self.elf = elf
        images = [sur.Image("image", elf, objdump, arch)]
        if rom_elf is not None:
            images.append(sur.Image("rom", rom_elf, objdump, arch))
        self.img = images[0]
        self.walker = sur.Walker(images, sur.DEFAULT_PRUNE)
        self._objdump = objdump
        self._archives: sur.ArchiveBodies | None = None
        # The bodies whose extent the symbol table does not give, so the walker
        # still reads them "until the next symbol". Those are the only bodies
        # that can still absorb a literal pool, so a chain running through one
        # is the only chain whose reproducibility across a relink is not
        # structurally guaranteed. Named on the row rather than assumed away.
        self.unsized_names = {
            self.img.funcs[addr].name
            for addr in self.img.unsized
            if addr in self.img.funcs
        }

    def adopt_archive_bodies(self, names: list[str], pointers: dict[str, str]) -> list[str]:
        """Walk undecoded bodies from their archive members; the names adopted."""
        if self._archives is None:
            self._archives = sur.ArchiveBodies(self._objdump,
                                               sur.resolve_archive_dir(self.env))
        return self.img.adopt_archive_bodies(names, self._archives, pointers)

    def stitch_table(self, caller: str, table: str) -> int:
        """Stitch one dispatch table into the call graph; the row count.

        Applied before any chain is walked: the walker memoises a function's
        depth, and a depth taken before the edges existed would be reused after.
        """
        return len(self.img.stitch_table(caller, table))

    def chain_total(self, name: str) -> tuple[int, list[str]]:
        """(worst-case chain from this root, notes). Raises when absent.

        A name resolving to several symbols takes the deepest of them: over-
        reporting is the safe direction for a floor, and the count is noted so
        an unexpected duplicate is visible rather than silently chosen between.
        """
        cands = self.img.by_name(name)
        if not cands:
            raise KeyError(name)
        notes: list[str] = []
        if len(cands) > 1:
            notes.append(
                f"{name}: {len(cands)} symbols with this name; deepest taken"
            )
        best, best_chain = 0, []
        for fn in cands:
            if fn.frame_kind == "undecoded":
                notes.append(
                    f"{name}: objdump emitted this body as data in {self.env}; "
                    "the recorded walk cannot be reproduced from this image"
                )
                continue
            sub, chain, _ = self.walker.depth(self.img, fn)
            if fn.frame + sub > best:
                best, best_chain = fn.frame + sub, chain
        through = sorted(
            {entry[0] for entry in best_chain if entry[0] in self.unsized_names}
        )
        if through:
            notes.append(
                f"{name}: the deepest chain runs through {len(through)} symbol(s) "
                f"carrying no size ({', '.join(through[:3])}"
                f"{', ...' if len(through) > 3 else ''}), whose extent is read as "
                "'until the next symbol'; this arm is not structurally protected "
                "against a relink moving its chain"
            )
        return best, notes

    def own_frame(self, name: str) -> tuple[int, list[str]]:
        """(own frame bytes of this symbol, notes). Raises when absent."""
        cands = self.img.by_name(name)
        if not cands:
            raise KeyError(name)
        notes: list[str] = []
        if len(cands) > 1:
            notes.append(
                f"{name}: {len(cands)} symbols with this name; largest frame taken"
            )
        return max(fn.frame for fn in cands), notes


def undecoded_share(img: sur.Image) -> tuple[int, int, int]:
    """(bodies still printed as data, bodies recovered from the stripped copy, all)."""
    undec = sum(1 for f in img.funcs.values() if f.frame_kind == "undecoded")
    return undec, len(img.recovered_bodies), len(img.funcs)


def stitch_pointer_calls(img: sur.Image, recipes: dict, chip: str
                         ) -> tuple[list[tuple[str, str, list[str]]], list[str]]:
    """Apply the recipe file's image-wide `pointer_calls` for this chip.

    Returns ([(pointer, callee, caller names)], [failure reasons]). A pointer or
    callee that is absent, or a pointer nothing calls through, is a failure: the
    recipe no longer describes the image, and walking on without the edge would
    pass every chain the hook deepens.
    """
    applied: list[tuple[str, str, list[str]]] = []
    failures: list[str] = []
    for entry in recipes.get("pointer_calls", []):
        if chip not in entry["chips"]:
            continue
        for callee in entry["callees"]:
            try:
                callers = img.stitch_pointer_calls(entry["pointer"], [callee])
            except KeyError as exc:
                failures.append(
                    f"pointer_calls {entry['pointer']} -> {callee}: {exc.args[0]} is "
                    "absent from the image -- re-record the stitch")
                continue
            except sur.Fatal as exc:
                failures.append(f"pointer_calls {entry['pointer']} -> {callee}: {exc}")
                continue
            applied.append((entry["pointer"], callee, [fn.name for fn in callers]))
    return applied, failures


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument(
        "--env", default="artoo_esp32",
        help="PlatformIO env whose built image to re-walk (default: artoo_esp32)",
    )
    parser.add_argument(
        "--no-rom", action="store_true",
        help="do not load the chip ROM elf (chains into ROM stop early)",
    )
    args = parser.parse_args(argv)

    recipes = load_recipes()
    constants = parse_chip_constants()
    image = ImageChains(args.env, args.no_rom)
    chip = image.platform
    if chip not in constants:
        raise Fatal(f"no config.h arm for platform '{chip}'")

    # A stitched table is a fact about the image, not about one task - the
    # call it stands for is made whichever task reaches it - so each is applied
    # once, and all of them before the first walk (see ImageChains.stitch_table).
    stitched: dict[tuple[str, str], int] = {}
    stitch_missing: dict[tuple[str, str], str] = {}
    called: set[tuple[str, str]] = set()
    adopted: list[str] = []
    for task in recipes["tasks"]:
        arm = task["chips"].get(chip)
        if arm is None or arm["env"] != args.env:
            continue
        for stitch in arm.get("tables", []):
            key = (stitch["caller"], stitch["table"])
            if key in stitched or key in stitch_missing:
                continue
            try:
                stitched[key] = image.stitch_table(*key)
            except KeyError as exc:
                stitch_missing[key] = str(exc.args[0])
        for call in arm.get("calls", []):
            for callee in call["callees"]:
                key = (call["caller"], callee)
                if key in called or key in stitch_missing:
                    continue
                try:
                    image.img.stitch_calls(call["caller"], [callee])
                    called.add(key)
                except KeyError as exc:
                    stitch_missing[key] = str(exc.args[0])
        if arm.get("archive_bodies"):
            adopted.extend(image.adopt_archive_bodies(
                arm["archive_bodies"], arm.get("pointer_tables", {})))

    pointer_calls, pointer_failures = stitch_pointer_calls(image.img, recipes, chip)

    undec, recovered, total_funcs = undecoded_share(image.img)
    print(f"check_task_stack_chains  env={args.env}  chip={chip}  arch={image.arch}")
    print(f"  image  {image.elf.relative_to(ROOT)}")
    print(f"  recipe {RECIPES.relative_to(ROOT)}  ({len(recipes['tasks'])} tasks)")
    if recovered:
        print(f"  function bodies the product listing printed as data, decoded from"
              f" a copy without .xt.prop: {recovered}")
    print(
        f"  function bodies still emitted as data: {undec} of {total_funcs}"
        f" -- every chain below is a floor: an unstitched indirect call, a cut"
        f" cycle or such a body is not walked"
    )
    for (caller, table), count in sorted(stitched.items()):
        print(f"  stitched {caller} -> every row of {table} ({count} functions)")
    for caller, callee in sorted(called):
        print(f"  stitched {caller} -> {callee} (called through a run-time pointer)")
    for pointer, callee, callers in pointer_calls:
        print(f"  stitched every call through {pointer} -> {callee}"
              f" (from {', '.join(callers)})")
    if adopted:
        print(f"  walked from their archive members: {len(adopted)} undecoded bodies"
              f" ({', '.join(sorted(set(adopted))[:6])}{', ...' if len(set(adopted)) > 6 else ''})")
    print()

    rows: list[tuple[str, str, str]] = []
    failures: list[str] = list(pointer_failures)
    notes: list[str] = []
    covered = 0

    for task in recipes["tasks"]:
        name = task["task"]
        arm = task["chips"].get(chip)
        if arm is None:
            rows.append((name, "-", f"absent on {chip} (not built into this image)"))
            continue
        if arm["env"] != args.env:
            rows.append(
                (name, "-", f"not covered: recorded on {arm['env']}")
            )
            continue
        constant_name = task["chain_constant"]
        stack_name = task["stack_constant"]
        undeclared = [
            n for n in (constant_name, stack_name) if n not in constants[chip]
        ]
        if undeclared:
            failures.extend(
                f"{name}: {n} is not declared in config.h's {chip} arm"
                for n in undeclared
            )
            rows.append((name, "?", f"{', '.join(undeclared)} missing from config.h"))
            continue
        constant = constants[chip][constant_name]
        stack = constants[chip][stack_name]

        walked = 0
        row_notes: list[str] = []
        missing: list[str] = []
        blind = False
        missing.extend(
            stitch_missing[(s["caller"], s["table"])]
            for s in arm.get("tables", [])
            if (s["caller"], s["table"]) in stitch_missing
        )
        missing.extend(
            stitch_missing[(c["caller"], callee)]
            for c in arm.get("calls", [])
            for callee in c["callees"]
            if (c["caller"], callee) in stitch_missing
        )
        try:
            for root in arm["roots"]:
                sub, sub_notes = image.chain_total(root)
                row_notes.extend(sub_notes)
                blind = blind or any("emitted this body as data" in n for n in sub_notes)
                walked = max(walked, sub)
            for frame in arm.get("frames", []):
                add, sub_notes = image.own_frame(frame)
                row_notes.extend(sub_notes)
                walked += add
        except KeyError as exc:
            missing.append(str(exc.args[0]))

        notes.extend(f"{name}: {n}" for n in row_notes)
        if missing:
            failures.append(
                f"{name}: symbol(s) absent from the image: {', '.join(missing)}"
                " -- the recorded recipe no longer describes this build"
                " (renamed, or inlined away); re-record it"
            )
            rows.append((name, "?", "symbol absent"))
            continue
        if blind:
            failures.append(
                f"{name}: a root's body is emitted as data in {args.env}, so the"
                " recorded walk cannot be reproduced from the image this recipe"
                " names; re-record the arm against an image where it decodes"
            )
            rows.append((name, str(walked), "root body is data in this image"))
            continue

        covered += 1
        verdict = judge_arm(chip, walked, constant_name, constant, stack_name, stack)
        if verdict.failure is not None:
            failures.append(f"{name}: {verdict.failure}")
        if verdict.note is not None:
            notes.append(f"{name}: {verdict.note}")
        rows.append((name, str(walked), verdict.detail))

    width = max(len(r[0]) for r in rows) if rows else 4
    for task_name, walked, detail in rows:
        print(f"  {task_name:<{width}}  {walked:>7}  {detail}")

    if notes:
        print()
        for note in notes:
            print(f"  note: {note}")

    print()
    print(f"{covered} of {len(recipes['tasks'])} recipes re-walked on {args.env}")
    if failures:
        print()
        for failure in failures:
            print(f"FAIL {failure}")
        return 2
    if chip in ALLOCATION_RULE_CHIPS:
        print("every re-walked chain fits its stack by the rule")
    else:
        print("every re-walked chain is within its recorded constant")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (Fatal, sur.Fatal) as exc:
        print(f"check_task_stack_chains: {exc}", file=sys.stderr)
        sys.exit(1)
