#!/usr/bin/env python3
"""Walk every task's Measured Chain recipe: check it, record it, or generate its header.

THREE MODES
-----------
    python3 tools/check_task_stack_chains.py [--env artoo_esp32]
        CHECK (the slice-gate row). Re-walk an image that is already built and
        fail when a chain has outgrown what its chip allows. Writes nothing.

    python3 tools/check_task_stack_chains.py --rewrite --chip esp32
        REWRITE. Build the chip's product image, walk it, and record every
        walked chain in the recipe; print, per task, the stack the rule wants
        against the stack it has and what accepting it would cost in RAM; then
        regenerate include/task_stack_figures.h. A stack changes only with
        --accept TASK (repeatable) or --accept-all: a raise is the operator's
        decision, never a side effect of re-walking.

    python3 tools/check_task_stack_chains.py --generate
        GENERATE. Regenerate include/task_stack_figures.h from the recipe as it
        stands, walking nothing - after resolving a merge in the recipe, say.

WHY THIS EXISTS
---------------
`tools/task_stack_recipes.json` is the one home of every Recorded Chain (ADR
0040, amended 2026-09-27): per task and per chip, the chain the product image
walked (`chain_bytes`), the task's stack (`stack_bytes`), the `reason` when
that stack is not what the rule gives, and a `note` saying why the chain is as
deep as it is today. `include/task_stack_figures.h` is generated from it and
committed; `include/config.h` includes it and asserts every stack covers its
chain. That assert stops the *stack* being trimmed. Nothing in it notices the
*chain* growing past the recorded figure -- the half of the problem #226 found
the expensive way, with a reboot on both boards (ADR 0040).

A recorded chain is a number written down once. This re-derives it from the
linked image, so a slice that deepens a call chain past it fails here instead
of on a board, and a re-derivation is this tool's rewrite mode rather than a
hand edit.

The rule state of an arm -- on the rule, above it, or declining it -- is never
stored. It is derived from `stack_bytes` against rule_stack(`chain_bytes`)
every time it is needed (rule_state()), so it cannot disagree with the figures.

WHAT A RECIPE IS
----------------
Besides the figures, each arm records the PlatformIO environment it is walked
on, the root symbols walked, and the ways an indirect call, or a body, the
walker cannot follow is stitched back in:

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

The opposite correction is image-wide too: `infeasible_calls`, call edges the
listing shows but that cannot execute - a log statement behind a check its only
caller always passes - each with its reason and the sites that prove it
(`Image.drop_infeasible_calls()`, #430). They are removed after every stitch and
before the first walk. An entry whose edge is no longer in the image fails the
row, like an absent root, so a stale entry cannot hide anything.

The log-hook entry is on for both chips (#430). The artoo-esp32 image links
newlib nano printf, which is what makes every chain there fit its stack with
the hook walked; the ESP32-P4 keeps full newlib and its stacks were raised by
the allocation rule to cover it.

The chain is

    chain = max over roots of (that root's total worst-case chain,
                               walked with every stitched table's rows)
          + sum over stitched frames of (that symbol's own frame)

and `--rewrite` prints both halves of it per task, with the deepest route.

WHAT IT CHECKS, AND WHAT IT DELIBERATELY DOES NOT
-------------------------------------------------
Covered arms are the ones whose recorded environment matches `--env` (in
rewrite mode, the chip's product image). Arms recorded against a different
environment (the other chip's product image, or a profiler image substituted
because the product image's body is emitted as data) are listed as not covered
and are neither guessed at nor rewritten. A covered arm is judged per chip (ADR
0040, amendment of 2026-09-25, #429) - see `judge_arm()`:

- artoo-esp32 is byte-exact: the freshly walked chain must be <= the recorded
  `chain_bytes`. It is the scarce chip, and every byte of growth should stop a
  slice.
- ESP32-P4 is judged by allocation: an arm fails only when ADR 0040's rule
  applied to the walk, ceil512(ceil(chain x 1.25)), exceeds its recorded
  `stack_bytes`. A walk that has merely moved from its recorded figure is a
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
  (renamed, or inlined away), a stitched pointer is absent or nothing in the
  image calls through it any more, or an infeasible call is no longer there;
- a covered root's body is emitted as data in the very image the recipe names,
  so the walk that produced the recorded figure cannot be reproduced.

Rewrite mode walks the same way and fails the same two ways; when either
happens it writes nothing at all, because a recipe half re-recorded is worse
than one left as it was.

Exit codes: 0 = every covered chain passes its chip's judgement (check), or the
recipe and header now hold the walk (rewrite, generate); 1 = an input was
missing or unreadable, or the build failed; 2 = at least one covered recipe
failed.
"""

from __future__ import annotations

import argparse
import json
import subprocess
import sys
from pathlib import Path
from typing import NamedTuple

# Sibling module in tools/, which is on sys.path for both entry points: this
# script run directly, and the tooling tests that import it.
import stack_usage_report as sur

ROOT = Path(__file__).resolve().parents[1]
RECIPES = ROOT / "tools" / "task_stack_recipes.json"
FIGURES_H = ROOT / "include" / "task_stack_figures.h"

# The per-chip ladder the generated header emits, P4 first like the other
# per-chip ladders in include/config.h. `#if defined` rather than `#if`:
# PA_CHIP_TARGET_* are presence macros defined only for the selected chip, so
# `#if` on the undefined one would silently take the wrong branch.
CHIP_MACROS = (
    ("esp32p4", "PA_CHIP_TARGET_ESP32P4"),
    ("esp32", "PA_CHIP_TARGET_ESP32"),
)
NO_CHIP_ERROR = '#error "task stack sizes have no value for this chip target"'

# What rewrite mode writes into the note of an arm whose chain it has just
# moved. The old note explained the old figure and would now be wrong, so it is
# replaced rather than kept; the recipe test refuses a committed placeholder, so
# the author cannot forget to write the real one.
NOTE_PLACEHOLDER_PREFIX = "REWRITE:"
NOTE_PLACEHOLDER = (
    f"{NOTE_PLACEHOLDER_PREFIX} say why this chain is as deep as it is today - its "
    "deepest route (printed by --rewrite) and what on that route puts it there."
)

# .clang-format's ColumnLimit; a figure-table row past it wraps the way
# clang-format would wrap it.
HEADER_COLUMNS = 100

# Routes are printed for the note's author, not parsed. A long one keeps its
# head - the project code that chose the path - counts the rest, and names the
# widest frames anywhere on it, which is usually what makes the chain deep.
ROUTE_HEAD = 9
ROUTE_WIDEST = 4


class Fatal(Exception):
    """An input this check cannot be produced without."""


# How a re-walked chain is judged, per chip (ADR 0040's 2026-09-25 amendment).
# A chip in neither set is refused rather than defaulted: which way a new chip
# is judged is a decision, and the looser default would be the silent one.
BYTE_EXACT_CHIPS = frozenset({"esp32"})
ALLOCATION_RULE_CHIPS = frozenset({"esp32p4"})


def rule_stack(chain: int) -> int:
    """ADR 0040's sizing rule: ceil512(ceil(chain x 1.25)), in integers.

    The one Python copy of the rule. The one C++ copy is taskStackByTheRule()
    in the header this tool generates; every other reader calls one of the two.
    """
    need = (chain * 5 + 3) // 4
    return ((need + 511) // 512) * 512


def rule_state(chain: int, stack: int) -> str:
    """Where a stack sits against the rule for its chain. Derived, never stored.

    'applied' is exactly the rule; 'above' is past it and 'declined' is short
    of it while still covering the chain - both need a recorded `reason`.
    'short' is a stack below its own chain, which include/config.h's
    static_assert refuses to compile.
    """
    if stack < chain:
        return "short"
    want = rule_stack(chain)
    if stack == want:
        return "applied"
    return "above" if stack > want else "declined"


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
                f"{walked - chain} B -- re-record it with --rewrite --chip {chip}"
                " and decide the stack, with the reason",
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
                f"{stack_name} = {stack} B by {need - stack} B -- re-record it "
                f"with --rewrite --chip {chip} and raise the stack by the rule,"
                " with the reason",
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


# -- the recipe ----------------------------------------------------------------

REQUIRED_TASK_FIELDS = ("task", "created_in", "chain_constant", "stack_constant", "chips")
REQUIRED_ARM_FIELDS = ("env", "roots", "chain_bytes", "stack_bytes", "note")


def load_recipes() -> dict:
    """The recipe file, refused unless every arm carries what the header needs."""
    try:
        recipes = json.loads(RECIPES.read_text(encoding="utf-8"))
    except OSError as exc:
        raise Fatal(f"cannot read {rel(RECIPES)}: {exc}") from exc
    except ValueError as exc:
        raise Fatal(f"{rel(RECIPES)} is not valid JSON: {exc}") from exc
    problems = recipe_shape_problems(recipes)
    if problems:
        raise Fatal(f"{rel(RECIPES)}:\n  " + "\n  ".join(problems))
    return recipes


def recipe_shape_problems(recipes: dict) -> list[str]:
    """Why a header cannot be generated from this recipe; empty when it can."""
    problems: list[str] = []
    chips = dict(CHIP_MACROS)
    for index, task in enumerate(recipes.get("tasks", [])):
        name = task.get("task", f"tasks[{index}]")
        problems.extend(
            f"{name}: no '{field}'" for field in REQUIRED_TASK_FIELDS if field not in task)
        for chip, arm in task.get("chips", {}).items():
            if chip not in chips:
                problems.append(f"{name}: arm for unknown chip '{chip}'")
                continue
            problems.extend(
                f"{name} on {chip}: no '{field}'"
                for field in REQUIRED_ARM_FIELDS if field not in arm)
            for field in ("chain_bytes", "stack_bytes"):
                value = arm.get(field)
                # bool is an int in Python; a `true` here is a typo, not a size.
                if field in arm and (type(value) is not int or value <= 0):
                    problems.append(f"{name} on {chip}: '{field}' is {value!r}, not a byte count")
    if not recipes.get("tasks"):
        problems.append("no tasks")
    return problems


def save_recipes(recipes: dict) -> None:
    # indent=2 with ASCII escapes is the file's committed form; writing it the
    # same way keeps a rewrite's diff to the figures and notes that moved.
    RECIPES.write_text(json.dumps(recipes, indent=2) + "\n", encoding="utf-8")


def rel(path: Path) -> str:
    try:
        return str(path.relative_to(ROOT))
    except ValueError:
        return str(path)


# -- the generated header ------------------------------------------------------

def _stack_comment(chain: int, stack: int) -> str:
    want = rule_stack(chain)
    state = rule_state(chain, stack)
    if state == "applied":
        return f"the rule: {chain} -> {(chain * 5 + 3) // 4} -> {stack}"
    if state == "above":
        return f"above the rule ({want}), the recipe says why"
    if state == "declined":
        return f"rule declined ({want}), the recipe says why"
    return "BELOW its chain: include/config.h's static_assert fails the build"


def render_header(recipes: dict) -> str:
    """include/task_stack_figures.h as the recipe says it should read."""
    out = [
        "// =============================================================================",
        "// include/task_stack_figures.h",
        "//",
        f"// Generated from {rel(RECIPES)} by tools/check_task_stack_chains.py",
        "// DO NOT EDIT MANUALLY",
        "//",
        "// Every task's Recorded Chain and stack, per chip target. The recipe file is the",
        "// one home of these figures (ADR 0040, amended 2026-09-27): it carries the",
        "// recipe each chain was walked by, why each chain is as deep as it is, and the",
        "// reason wherever a stack is not what the rule gives. To re-derive them, walk",
        "// the chip's product image and record the result:",
        "//",
        "//   python3 tools/check_task_stack_chains.py --rewrite --chip esp32",
        "//",
        "// which rewrites the recipe and this file together; --help says the rest.",
        "// test/test_tools/test_task_stack_figures_drift.py fails when this file is not",
        "// what the recipe generates.",
        "//",
        "// Included by include/config.h once the chip target is mapped. config.h carries",
        "// the sizing rule's rationale and the static_assert that every stack covers",
        "// its chain.",
        "// =============================================================================",
        "#pragma once",
        "",
        "#include <stdint.h>",
        "",
        "// ADR 0040's sizing rule: the chain plus 25%, rounded up to the next 512 bytes.",
        "// Integer arithmetic throughout - a float round-trip is how an off-by-one",
        "// arrives. The one C++ copy; tools/check_task_stack_chains.py rule_stack() is",
        "// the one Python copy, and the recipe test proves the two agree.",
        "constexpr uint32_t taskStackByTheRule(uint32_t chainBytes) {",
        "    return (((chainBytes * 5U + 3U) / 4U + 511U) / 512U) * 512U;",
        "}",
        "",
        "// One row per task on the selected chip, so a test can walk every arm without",
        "// a hand-kept list of them.",
        "struct TaskStackFigure {",
        "    const char* task;",
        "    uint32_t chainBytes;",
        "    uint32_t stackBytes;",
        "};",
        "",
        "// `#if defined` rather than `#if`: PA_CHIP_TARGET_* are presence macros defined",
        "// only for the selected chip (include/config.h \"Chip target mapping\"), not 0/1",
        "// Board Capability Gates, so `#if` on the undefined one would silently take the",
        "// wrong branch. Keying on the chip target rather than on PA_BOARD means a second",
        "// board variant on either chip inherits the right sizes without a new case.",
        "// HostedRecovery is declared only where PA_CAP_HOSTED_WIFI is 1, so a board",
        "// that turns the capability on elsewhere fails config.h's static_assert rather",
        "// than inheriting a figure measured on someone else's silicon.",
    ]
    for position, (chip, macro) in enumerate(CHIP_MACROS):
        out.append(f"{'#if' if position == 0 else '#elif'} defined({macro})")
        rows = []
        for task in recipes["tasks"]:
            arm = task["chips"].get(chip)
            if arm is None:
                continue
            chain, stack = arm["chain_bytes"], arm["stack_bytes"]
            out.append(f"constexpr uint32_t {task['chain_constant']} = {chain};")
            out.append(f"constexpr uint32_t {task['stack_constant']} = {stack};"
                       f"  // {_stack_comment(chain, stack)}")
            row = (f'    {{"{task["task"]}", {task["chain_constant"]}, '
                   f'{task["stack_constant"]}}},')
            if len(row) > HEADER_COLUMNS:
                row = (f'    {{"{task["task"]}", {task["chain_constant"]},\n'
                       f'     {task["stack_constant"]}}},')
            rows.append(row)
        out.append("")
        out.append("constexpr TaskStackFigure TASK_STACK_FIGURES[] = {")
        out.extend(rows)
        out.append("};")
    out.extend(["#else", f"  {NO_CHIP_ERROR}", "#endif", ""])
    return "\n".join(out)


def header_drift() -> str | None:
    """Why the committed header is not what the recipe generates; None when it is.

    Reads both files and writes neither: a check that repaired what it found
    would pass on the evidence it had just manufactured.
    """
    try:
        built = render_header(load_recipes()).encode("utf-8")
    except Fatal as exc:
        return f"{rel(RECIPES)} cannot generate a header: {exc}"
    if not FIGURES_H.exists():
        return f"{rel(FIGURES_H)} is missing"
    committed = FIGURES_H.read_bytes()
    if committed != built:
        return (
            f"{rel(FIGURES_H)} is not what {rel(RECIPES)} generates "
            f"({len(committed)} bytes committed, {len(built)} generated)"
        )
    return None


def write_header(recipes: dict) -> bool:
    """Write the header from the recipe; whether its bytes changed."""
    text = render_header(recipes)
    if FIGURES_H.exists() and FIGURES_H.read_text(encoding="utf-8") == text:
        return False
    FIGURES_H.write_text(text, encoding="utf-8")
    return True


# -- walking an image ----------------------------------------------------------

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

    def chain_total(self, name: str) -> tuple[int, list[str], list[tuple[str, int]]]:
        """(worst-case chain from this root, notes, its route). Raises when absent.

        A name resolving to several symbols takes the deepest of them: over-
        reporting is the safe direction for a floor, and the count is noted so
        an unexpected duplicate is visible rather than silently chosen between.
        The route is the deepest path's (function, own frame), root first.
        """
        cands = self.img.by_name(name)
        if not cands:
            raise KeyError(name)
        notes: list[str] = []
        if len(cands) > 1:
            notes.append(
                f"{name}: {len(cands)} symbols with this name; deepest taken"
            )
        best, best_chain, best_fn = 0, [], cands[0]
        for fn in cands:
            if fn.frame_kind == "undecoded":
                notes.append(
                    f"{name}: objdump emitted this body as data in {self.env}; "
                    "the recorded walk cannot be reproduced from this image"
                )
                continue
            sub, chain, _ = self.walker.depth(self.img, fn)
            if fn.frame + sub > best:
                best, best_chain, best_fn = fn.frame + sub, chain, fn
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
        route = [(name, best_fn.frame)] + [(entry[0], entry[1]) for entry in best_chain]
        return best, notes, route

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


def drop_infeasible_calls(img: sur.Image, recipes: dict, chip: str
                          ) -> tuple[list[tuple[str, str]], list[str]]:
    """Apply the recipe file's image-wide `infeasible_calls` for this chip.

    Returns ([(caller, callee) removed], [failure reasons]). An absent symbol or
    an edge the image no longer has is a failure: the entry no longer describes
    the image, and it must be re-recorded or removed, never skipped.
    """
    dropped: list[tuple[str, str]] = []
    failures: list[str] = []
    for entry in recipes.get("infeasible_calls", []):
        if chip not in entry["chips"]:
            continue
        for callee in entry["callees"]:
            try:
                dropped.extend(img.drop_infeasible_calls(entry["caller"], [callee]))
            except KeyError as exc:
                failures.append(
                    f"infeasible_calls {entry['caller']} -> {callee}: {exc.args[0]} is "
                    "absent from the image -- re-record or remove the entry")
            except sur.Fatal as exc:
                failures.append(f"infeasible_calls {entry['caller']} -> {callee}: {exc}")
    return dropped, failures


class Prepared(NamedTuple):
    """The image-wide stitches, applied once before the first walk."""
    missing: dict[tuple[str, str], str]  # (caller, table-or-callee) -> absent symbol
    failures: list[str]                  # image-wide entries that no longer apply


def prepare_image(image: ImageChains, recipes: dict, chip: str) -> Prepared:
    """Apply every stitch the covered arms and the recipe file name; print them.

    A stitched table is a fact about the image, not about one task - the call
    it stands for is made whichever task reaches it - so each is applied once,
    and all of them before the first walk (see ImageChains.stitch_table).
    """
    stitched: dict[tuple[str, str], int] = {}
    missing: dict[tuple[str, str], str] = {}
    called: set[tuple[str, str]] = set()
    adopted: list[str] = []
    for task in recipes["tasks"]:
        arm = task["chips"].get(chip)
        if arm is None or arm["env"] != image.env:
            continue
        for stitch in arm.get("tables", []):
            key = (stitch["caller"], stitch["table"])
            if key in stitched or key in missing:
                continue
            try:
                stitched[key] = image.stitch_table(*key)
            except KeyError as exc:
                missing[key] = str(exc.args[0])
        for call in arm.get("calls", []):
            for callee in call["callees"]:
                key = (call["caller"], callee)
                if key in called or key in missing:
                    continue
                try:
                    image.img.stitch_calls(call["caller"], [callee])
                    called.add(key)
                except KeyError as exc:
                    missing[key] = str(exc.args[0])
        if arm.get("archive_bodies"):
            adopted.extend(image.adopt_archive_bodies(
                arm["archive_bodies"], arm.get("pointer_tables", {})))

    pointer_calls, pointer_failures = stitch_pointer_calls(image.img, recipes, chip)
    dropped, drop_failures = drop_infeasible_calls(image.img, recipes, chip)

    undec, recovered, total_funcs = undecoded_share(image.img)
    print(f"  image  {rel(image.elf)}")
    print(f"  recipe {rel(RECIPES)}  ({len(recipes['tasks'])} tasks)")
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
    for caller, callee in dropped:
        print(f"  dropped {caller} -> {callee} (cannot execute; see infeasible_calls)")
    if adopted:
        print(f"  walked from their archive members: {len(adopted)} undecoded bodies"
              f" ({', '.join(sorted(set(adopted))[:6])}{', ...' if len(set(adopted)) > 6 else ''})")
    print()
    return Prepared(missing, pointer_failures + drop_failures)


class Walk(NamedTuple):
    walked: int
    route: list[tuple[str, int]]  # the deepest root's (function, frame), root first
    frames: list[tuple[str, int]]  # stitched frames above the root, own sizes
    notes: list[str]
    failure: str | None           # why this arm could not be walked
    detail: str | None            # the row's status column when it could not


def walk_arm(image: ImageChains, name: str, arm: dict, prepared: Prepared) -> Walk:
    """Walk one covered arm: its deepest root plus its stitched frames."""
    walked = 0
    route: list[tuple[str, int]] = []
    frames: list[tuple[str, int]] = []
    notes: list[str] = []
    missing = [
        prepared.missing[(s["caller"], s["table"])]
        for s in arm.get("tables", [])
        if (s["caller"], s["table"]) in prepared.missing
    ]
    missing.extend(
        prepared.missing[(c["caller"], callee)]
        for c in arm.get("calls", [])
        for callee in c["callees"]
        if (c["caller"], callee) in prepared.missing
    )
    blind = False
    try:
        for root in arm["roots"]:
            sub, sub_notes, sub_route = image.chain_total(root)
            notes.extend(sub_notes)
            blind = blind or any("emitted this body as data" in n for n in sub_notes)
            if sub > walked or not route:
                walked, route = sub, sub_route
        for frame in arm.get("frames", []):
            add, sub_notes = image.own_frame(frame)
            notes.extend(sub_notes)
            frames.append((frame, add))
            walked += add
    except KeyError as exc:
        missing.append(str(exc.args[0]))

    if missing:
        return Walk(walked, route, frames, notes,
                    f"{name}: symbol(s) absent from the image: {', '.join(missing)}"
                    " -- the recorded recipe no longer describes this build"
                    " (renamed, or inlined away); re-record it",
                    "symbol absent")
    if blind:
        return Walk(walked, route, frames, notes,
                    f"{name}: a root's body is emitted as data in {image.env}, so the"
                    " recorded walk cannot be reproduced from the image this recipe"
                    " names; re-record the arm against an image where it decodes",
                    "root body is data in this image")
    return Walk(walked, route, frames, notes, None, None)


def format_route(walk: Walk) -> str:
    steps = list(walk.frames) + [(n, size) for n, size in walk.route if not n.startswith("<")]
    names = [n for n, _ in steps]
    text = " -> ".join(names[:ROUTE_HEAD])
    if len(names) > ROUTE_HEAD:
        text += f" -> ({len(names) - ROUTE_HEAD} more)"
    widest = sorted(steps, key=lambda step: -step[1])[:ROUTE_WIDEST]
    return text + "; widest frames: " + ", ".join(f"{n} {size} B" for n, size in widest)


# -- check mode ----------------------------------------------------------------

def run_check(env: str, no_rom: bool) -> int:
    recipes = load_recipes()
    image = ImageChains(env, no_rom)
    chip = image.platform
    print(f"check_task_stack_chains  env={env}  chip={chip}  arch={image.arch}")
    prepared = prepare_image(image, recipes, chip)

    rows: list[tuple[str, str, str]] = []
    failures: list[str] = list(prepared.failures)
    notes: list[str] = []
    covered = 0

    for task in recipes["tasks"]:
        name = task["task"]
        arm = task["chips"].get(chip)
        if arm is None:
            rows.append((name, "-", f"absent on {chip} (not built into this image)"))
            continue
        if arm["env"] != env:
            rows.append((name, "-", f"not covered: recorded on {arm['env']}"))
            continue
        walk = walk_arm(image, name, arm, prepared)
        notes.extend(f"{name}: {n}" for n in walk.notes)
        if walk.failure is not None:
            failures.append(walk.failure)
            rows.append((name, "?" if walk.detail == "symbol absent" else str(walk.walked),
                         walk.detail))
            continue

        covered += 1
        verdict = judge_arm(chip, walk.walked, task["chain_constant"], arm["chain_bytes"],
                            task["stack_constant"], arm["stack_bytes"])
        if verdict.failure is not None:
            failures.append(f"{name}: {verdict.failure}")
        if verdict.note is not None:
            notes.append(f"{name}: {verdict.note}")
        rows.append((name, str(walk.walked), verdict.detail))

    width = max(len(r[0]) for r in rows) if rows else 4
    for task_name, walked, detail in rows:
        print(f"  {task_name:<{width}}  {walked:>7}  {detail}")

    if notes:
        print()
        for note in notes:
            print(f"  note: {note}")

    print()
    print(f"{covered} of {len(recipes['tasks'])} recipes re-walked on {env}")
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


# -- rewrite mode --------------------------------------------------------------

class Rerecorded(NamedTuple):
    results: list[str]   # what happened to the arm, for the row
    warnings: list[str]  # what the author must still do before committing
    changed: bool        # the arm's recipe entry was modified
    placeholder: bool    # its note is now the placeholder
    stack_moved: int     # bytes the stack moved by (0 unless accepted)


def rerecord_arm(arm: dict, walked: int, accepted: bool,
                 stack_constant: str, name: str) -> Rerecorded:
    """Record one walked chain in its arm, in place; move the stack only if accepted.

    The chain is always recorded: it is what the image walks. The stack is the
    operator's decision (ADR 0040, amended 2026-09-27), so without `accepted`
    it stays where it is even when the rule now wants more - the warning says
    what that costs, and config.h's static_assert refuses a stack left below its
    chain. A moved chain replaces the note, which described the old figure.
    """
    old_chain, old_stack = arm["chain_bytes"], arm["stack_bytes"]
    want = rule_stack(walked)
    results: list[str] = []
    warnings: list[str] = []
    placeholder = False
    if walked != old_chain:
        arm["chain_bytes"] = walked
        arm["note"] = NOTE_PLACEHOLDER
        placeholder = True
        results.append(f"chain {old_chain} -> {walked}")
    moved = 0
    if accepted and want != old_stack:
        arm["stack_bytes"] = want
        moved = want - old_stack
        results.append(f"stack {old_stack} -> {want} (accepted)")
    stack = arm["stack_bytes"]
    state = rule_state(walked, stack)
    if accepted and "reason" in arm and state == "applied":
        # On the rule now, so the reason for being off it no longer applies.
        del arm["reason"]
        results.append("reason dropped")
    changed = bool(results)
    if state == "applied":
        results.append("on the rule")
    elif state == "short":
        results.append("STACK BELOW CHAIN")
        warnings.append(
            f"{name}: {stack_constant} = {stack} no longer covers the chain "
            f"{walked}; include/config.h's static_assert fails the build until the "
            f"stack is raised - rerun with --accept {name} to take the rule, {want}")
    else:
        results.append(f"{state} the rule")
        if not arm.get("reason", "").strip():
            warnings.append(
                f"{name}: {stack} is {state} the rule for {walked} ({want}) and the "
                "arm records no 'reason'; write one, or --accept the rule - the "
                "recipe test fails without either")
    return Rerecorded(results, warnings, changed, placeholder, moved)


def accepted_tasks(recipes: dict, chip: str, product: str,
                   accept: list[str], accept_all: bool) -> set[str]:
    """The task names whose stack this run may move. Refuses a name it cannot use.

    Checked before anything is built, so a typo costs a second rather than a
    build and a walk.
    """
    eligible = [
        task["task"] for task in recipes["tasks"]
        if chip in task["chips"] and task["chips"][chip]["env"] == product
    ]
    if accept_all:
        return set(eligible)
    chosen: set[str] = set()
    for item in accept:
        name, _, named_chip = item.partition(":")
        if named_chip and named_chip != chip:
            raise Fatal(f"--accept {item}: this run rewrites {chip}, not {named_chip}")
        if name not in eligible:
            raise Fatal(f"--accept {item}: no task by that name is walked on {chip}; "
                        f"the tasks are {', '.join(eligible)}")
        chosen.add(name)
    return chosen


def build_product(env: str) -> None:
    """Build through make, which selects the chip's toolchain and takes the build lock."""
    print(f"building {env}: make build BUILD_ENV={env}", flush=True)
    result = subprocess.run(["make", "build", f"BUILD_ENV={env}"], cwd=ROOT, check=False)
    if result.returncode != 0:
        raise Fatal(f"make build BUILD_ENV={env} exited {result.returncode}; nothing walked")


def run_rewrite(chip: str, accept: list[str], accept_all: bool,
                build: bool, no_rom: bool) -> int:
    recipes = load_recipes()
    try:
        product = recipes["metadata"]["envs"][chip]["product"]
    except KeyError as exc:
        raise Fatal(f"{rel(RECIPES)} names no product image for {chip} "
                    "(metadata.envs.<chip>.product)") from exc
    accepted = accepted_tasks(recipes, chip, product, accept, accept_all)
    if build:
        build_product(product)
    image = ImageChains(product, no_rom)
    if image.platform != chip:
        raise Fatal(f"{product} is a {image.platform} image, not {chip}")

    print(f"check_task_stack_chains --rewrite  env={product}  chip={chip}"
          f"  arch={image.arch}")
    prepared = prepare_image(image, recipes, chip)

    failures: list[str] = list(prepared.failures)
    walks: dict[str, Walk] = {}
    skipped: list[str] = []
    for task in recipes["tasks"]:
        arm = task["chips"].get(chip)
        if arm is None:
            continue
        if arm["env"] != product:
            skipped.append(f"{task['task']}: recorded on {arm['env']}, not re-derived here")
            continue
        walk = walk_arm(image, task["task"], arm, prepared)
        walks[task["task"]] = walk
        if walk.failure is not None:
            failures.append(walk.failure)
    if failures:
        for failure in failures:
            print(f"FAIL {failure}")
        print()
        print(f"nothing written: {rel(RECIPES)} and {rel(FIGURES_H)} are as they were")
        return 2

    header = (f"  {'task':<14} {'recorded':>8} {'walked':>7} {'stack':>6} "
              f"{'rule wants':>10} {'RAM to take it':>15}  result")
    print(header)
    changed: list[str] = []
    placeholders: list[str] = []
    warnings: list[str] = []
    take_all = 0
    taken = 0
    for task in recipes["tasks"]:
        name = task["task"]
        walk = walks.get(name)
        if walk is None:
            continue
        arm = task["chips"][chip]
        old_chain = arm["chain_bytes"]
        want = rule_stack(walk.walked)
        take_all += want - arm["stack_bytes"]
        outcome = rerecord_arm(arm, walk.walked, name in accepted,
                               task["stack_constant"], name)
        chain, stack = walk.walked, arm["stack_bytes"]
        taken += outcome.stack_moved
        if outcome.changed:
            changed.append(name)
        if outcome.placeholder:
            placeholders.append(name)
        warnings.extend(outcome.warnings)
        results = outcome.results
        delta = want - stack
        cost = "-" if delta == 0 else f"{delta:+d} B"
        print(f"  {name:<14} {old_chain:>8} {chain:>7} {stack:>6} {want:>10} "
              f"{cost:>15}  {', '.join(results)}")

    print()
    print("deepest route per task (write the note from this):")
    for name, walk in walks.items():
        print(f"  {name}: {format_route(walk)}")
    notes = [f"{name}: {n}" for name, walk in walks.items() for n in walk.notes]
    for line in notes + skipped:
        print(f"  note: {line}")

    print()
    print(f"RAM if every task took the rule: {take_all:+d} B of stack on {chip}")
    if accepted:
        print(f"accepted: {', '.join(sorted(accepted))} ({taken:+d} B)")
    for warning in warnings:
        print(f"WARN {warning}")
    print()
    if changed:
        save_recipes(recipes)
        print(f"wrote {rel(RECIPES)}: {', '.join(changed)}")
    else:
        print(f"no change: every walked chain equals its recorded figure and no stack"
              f" was accepted; {rel(RECIPES)} untouched")
    print(f"{'regenerated' if write_header(recipes) else 'unchanged'}: {rel(FIGURES_H)}")
    if placeholders:
        print()
        print("next:")
        print(f"  1. replace the '{NOTE_PLACEHOLDER_PREFIX}' note on "
              f"{', '.join(placeholders)} with why the chain is as deep as it is"
              " today; the recipe test fails while a placeholder is committed")
        print("  2. make build, then make test and "
              "python3 -m unittest discover -s test/test_tools -q")
        print(f"  3. commit {rel(RECIPES)} and {rel(FIGURES_H)} together")
    return 0


# -- entry point ---------------------------------------------------------------

EPILOG = """\
examples:
  %(prog)s
      check the artoo_esp32 image already built in .pio/build (the gate row)
  %(prog)s --rewrite --chip esp32
      build artoo_esp32, walk it and record every chain; stacks are untouched
  %(prog)s --rewrite --chip esp32p4 --accept DomeLinkTask
      the same on the ESP32-P4, and move DomeLinkTask's stack onto the rule
  %(prog)s --generate
      regenerate include/task_stack_figures.h from the recipe, walking nothing

In rewrite mode a changed chain replaces that task's note with a placeholder:
write the real one - why the chain is as deep as it is today - before
committing. The recipe test refuses the placeholder.
"""


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(
        description="Walk every task's Measured Chain recipe "
                    f"({rel(RECIPES)}): check it against its chip's rule, record "
                    f"it, or generate {rel(FIGURES_H)} from it. ADR 0040.",
        epilog=EPILOG,
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    mode = parser.add_mutually_exclusive_group()
    mode.add_argument(
        "--rewrite", action="store_true",
        help="build the chip's product image, walk it and record every walked chain "
             "in the recipe, then regenerate the header (needs --chip)")
    mode.add_argument(
        "--generate", action="store_true",
        help="regenerate the header from the recipe as it stands; walks nothing")
    parser.add_argument(
        "--env",
        help="check mode: the PlatformIO env whose built image to re-walk "
             "(default: artoo_esp32)")
    parser.add_argument(
        "--chip", choices=[chip for chip, _ in CHIP_MACROS],
        help="rewrite mode: the chip whose arms to re-record; its product image is "
             "the recipe's metadata.envs.<chip>.product")
    accept = parser.add_mutually_exclusive_group()
    accept.add_argument(
        "--accept", action="append", default=[], metavar="TASK",
        help="rewrite mode: set TASK's stack to what the rule wants for its walked "
             "chain - a raise or a cut, the operator's decision (repeatable; "
             "TASK or TASK:chip)")
    accept.add_argument(
        "--accept-all", action="store_true",
        help="rewrite mode: --accept every task walked on the chip")
    parser.add_argument(
        "--no-build", action="store_true",
        help="rewrite mode: walk the image already in .pio/build/<env> instead of "
             "building it first")
    parser.add_argument(
        "--no-rom", action="store_true",
        help="do not load the chip ROM elf (chains into ROM stop early)")
    args = parser.parse_args(argv)

    rewrite_only = [flag for flag, used in (
        ("--chip", args.chip is not None), ("--accept", bool(args.accept)),
        ("--accept-all", args.accept_all), ("--no-build", args.no_build)) if used]
    if not args.rewrite and rewrite_only:
        parser.error(f"{', '.join(rewrite_only)} only apply with --rewrite")
    if args.env is not None and (args.rewrite or args.generate):
        parser.error("--env is check mode's; --rewrite takes --chip")
    if args.generate and args.no_rom:
        parser.error("--generate walks nothing; --no-rom does not apply")

    if args.generate:
        wrote = write_header(load_recipes())
        print(f"{'regenerated' if wrote else 'unchanged'}: {rel(FIGURES_H)}")
        return 0
    if args.rewrite:
        if args.chip is None:
            parser.error("--rewrite needs --chip (esp32 or esp32p4)")
        return run_rewrite(args.chip, args.accept, args.accept_all,
                           not args.no_build, args.no_rom)
    return run_check(args.env or "artoo_esp32", args.no_rom)


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (Fatal, sur.Fatal) as exc:
        print(f"check_task_stack_chains: {exc}", file=sys.stderr)
        sys.exit(1)
