#!/usr/bin/env python3
"""Re-walk every task's Measured Chain recipe and fail when one exceeds its constant.

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
any own-frames stitched in by hand because the walker cannot follow the
indirect call that reaches them. The chain is

    chain = max over roots of (that root's total worst-case chain)
          + sum over stitched frames of (that symbol's own frame)

which is exactly what the two `tools/stack_usage_report.py` invocations in
`include/config.h`'s Console recipe compute between them.

WHAT IT CHECKS, AND WHAT IT DELIBERATELY DOES NOT
-------------------------------------------------
Covered arms are the ones whose recorded environment matches `--env`. For each,
the freshly walked chain must be <= the `include/config.h` constant for that
chip. Arms recorded against a different environment (the ESP32-P4 product
image, or a profiler image substituted because the product image's body is
emitted as data) are listed as not covered and are not guessed at.

The Xtensa walk is a floor, not a bound: objdump emits a large share of that
image's function bodies as data, so a chain crossing one is truncated. This
check can therefore MISS growth and cannot report FALSE growth, which is what
makes it safe to fail a build on (ADR 0040).

Two conditions fail beyond an exceedance, because both mean the recorded recipe
no longer describes the image and a silent pass would be the drift this exists
to catch:

- a root or stitched-frame symbol is absent (renamed, or inlined away);
- a covered root's body is emitted as data in the very image the recipe names,
  so the walk that produced the recorded figure cannot be reproduced.

Exit codes: 0 = every covered chain is within its constant; 1 = an input was
missing or unreadable; 2 = at least one covered recipe failed.
"""

from __future__ import annotations

import argparse
import json
import re
import sys
from pathlib import Path

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


def undecoded_share(img: sur.Image) -> tuple[int, int]:
    undec = sum(1 for f in img.funcs.values() if f.frame_kind == "undecoded")
    return undec, len(img.funcs)


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

    undec, total_funcs = undecoded_share(image.img)
    print(f"check_task_stack_chains  env={args.env}  chip={chip}  arch={image.arch}")
    print(f"  image  {image.elf.relative_to(ROOT)}")
    print(f"  recipe {RECIPES.relative_to(ROOT)}  ({len(recipes['tasks'])} tasks)")
    print(
        f"  function bodies objdump emitted as data: {undec} of {total_funcs}"
        f" -- every chain below is a LOWER bound"
    )
    print()

    rows: list[tuple[str, str, str]] = []
    failures: list[str] = []
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
        constant = constants[chip].get(constant_name)
        if constant is None:
            failures.append(
                f"{name}: {constant_name} is not declared in config.h's {chip} arm"
            )
            rows.append((name, "?", f"{constant_name} missing from config.h"))
            continue

        walked = 0
        row_notes: list[str] = []
        missing: list[str] = []
        blind = False
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
        if walked > constant:
            failures.append(
                f"{name}: chain {walked} B exceeds {constant_name} = {constant} B"
                f" by {walked - constant} B -- re-measure and re-derive the stack"
                " on both chips, with the reason"
            )
            rows.append((name, str(walked), f"OVER {constant_name}={constant}"))
        else:
            rows.append(
                (name, str(walked), f"within {constant_name}={constant}"
                 f" (headroom {constant - walked} B)")
            )

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
    print("every re-walked chain is within its recorded constant")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (Fatal, sur.Fatal) as exc:
        print(f"check_task_stack_chains: {exc}", file=sys.stderr)
        sys.exit(1)
