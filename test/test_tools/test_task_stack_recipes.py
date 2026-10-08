"""Every project-created task has a Measured Chain recipe, on every chip (#271).

`tools/task_stack_recipes.json` is the recipe table ADR 0040 asks for, and the
one home of every Recorded Chain (ADR 0040, amended 2026-09-27): per task and
per chip, the environment walked, the root symbols, the frames stitched by hand
across an indirect call, the chain that walk recorded, the task's stack, the
reason wherever that stack is not what the rule gives, and why the chain is as
deep as it is today. `include/task_stack_figures.h` is generated from it. Three
separate things can rot, and each has a class of test here:

1. **The table can miss a task.** That is how `/api/profiler` came to report
   nine of ten tasks (#250), and it is why the criterion is the task, not the
   file it happens to be created in. Every `xTaskCreate*` call site in `src/` is
   scanned, and every registered name must have a recipe.

2. **A figure can drift from its derivation.** The rule state of an arm is
   derived from its two figures, never stored, so what is asserted is what must
   hold for every arm: the stack covers the chain, stacks move in 512 B steps,
   an arm off the rule says why and an arm on it carries no stale reason, and
   the ESP32-P4 pays the rule everywhere. None of it restates a figure.

3. **The build can compile something else.** The header is generated and
   committed, and config.h includes it per chip. A compiler probe includes the
   real headers under each `PA_BOARD` value and reads back what each board
   actually compiles - the only way a host test sees the ESP32-P4 arm at all
   (platformio.ini env:native always builds PA_BOARD_ARTOO_ESP32) - and the
   rule's C++ copy is checked against its Python copy there. Byte drift between
   the recipe and the committed header is test_task_stack_figures_drift.py's.

What this file does NOT do is re-walk the chains. That needs a linked image and
is `tools/check_task_stack_chains.py`, run as a gate row against the artoo build
the gate already produces.
"""

import json
import os
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

from task_create_sites import created_task_contexts, created_task_sites, is_once_guard

REPO_ROOT = Path(__file__).resolve().parents[2]
INCLUDE_DIR = REPO_ROOT / "include"
RECIPES_PATH = REPO_ROOT / "tools" / "task_stack_recipes.json"

sys.path.insert(0, str(REPO_ROOT / "tools"))
import check_task_stack_chains as checker  # noqa: E402

# Everything the probed headers pull in, staged into the probe's include path.
PROBE_HEADER_SET = (
    "config.h",
    "board_capabilities.inc",
    "board_lanes.inc",
    "build_flags.inc",
    "firebeetle_required_pins.inc",
    "task_stack_figures.h",
)

BOARD_TO_CHIP = {
    "PA_BOARD_ARTOO_ESP32": "esp32",
    "PA_BOARD_FIREBEETLE2": "esp32p4",
}

# Chains the rule's two copies are compared on: the step boundaries either side
# of a rounding, and every recorded chain (added in the test).
RULE_PROBE_CHAINS = (1, 409, 410, 511, 512, 3152, 4095, 4096, 4097, 8601, 8602)


class TaskStackRecipes(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.recipes = json.loads(RECIPES_PATH.read_text(encoding="utf-8"))
        cls.by_task = {entry["task"]: entry for entry in cls.recipes["tasks"]}
        compiler_name = os.environ.get("CXX", "c++")
        cls.compiler = shutil.which(compiler_name)
        if cls.compiler is None:
            raise RuntimeError(f"C++ compiler not found: {compiler_name}")
        cls._probe_cache = {}

    # -- the compiler probe ---------------------------------------------------

    def _probe_source(self, board_macro, names, chains):
        emit = "\n".join(
            f'    std::printf("const {name} %u\\n", (unsigned){name});' for name in names
        )
        rule = "\n".join(
            f'    std::printf("rule {chain} %u\\n", (unsigned)taskStackByTheRule({chain}U));'
            for chain in chains
        )
        return "\n".join(
            [
                f"#define PA_BOARD {board_macro}",
                # config.h #errors without these per-env macros (#244); a probe
                # stands in for a build environment, so it declares what any
                # environment must.
                "#define PA_LOG_LEVEL 2",
                "#define PA_HEAP_PROFILE 0",
                '#include "config.h"',
                "#include <cstdio>",
                "int main() {",
                emit,
                rule,
                "    for (const TaskStackFigure& row : TASK_STACK_FIGURES) {",
                '        std::printf("row %s %u %u\\n", row.task, (unsigned)row.chainBytes,',
                "                    (unsigned)row.stackBytes);",
                "    }",
                "    return 0;",
                "}",
                "",
            ]
        )

    def _probe(self, chip):
        """What the real headers compile to under this chip's board.

        {"const": {CONSTANT: value}, "rule": {chain: stack}, "rows": [(task,
        chain, stack)]} - the named constants for every task the recipe gives
        this chip, the C++ rule on RULE_PROBE_CHAINS plus every recorded chain,
        and the generated figure table in its order.
        """
        if chip in self._probe_cache:
            return self._probe_cache[chip]
        board = next(b for b, c in BOARD_TO_CHIP.items() if c == chip)
        names = []
        for entry in self.recipes["tasks"]:
            if chip in entry["chips"]:
                names.extend((entry["chain_constant"], entry["stack_constant"]))
        chains = sorted(set(RULE_PROBE_CHAINS) | {
            arm["chain_bytes"] for entry in self.recipes["tasks"]
            for arm in entry["chips"].values()})
        with tempfile.TemporaryDirectory() as tmp:
            staged = Path(tmp) / "include"
            staged.mkdir()
            for header in PROBE_HEADER_SET:
                shutil.copyfile(INCLUDE_DIR / header, staged / header)
            source = Path(tmp) / "task_stack_probe.cpp"
            source.write_text(self._probe_source(board, names, chains), encoding="utf-8")
            binary = Path(tmp) / "task_stack_probe"
            compiled = subprocess.run(
                [self.compiler, "-std=c++17", "-I", str(staged), str(source),
                 "-o", str(binary)],
                capture_output=True, text=True, check=False,
            )
            self.assertEqual(compiled.returncode, 0, compiled.stderr)
            ran = subprocess.run([str(binary)], capture_output=True, text=True,
                                 check=False, timeout=10)
            self.assertEqual(ran.returncode, 0, ran.stderr)
        values = {"const": {}, "rule": {}, "rows": []}
        for line in ran.stdout.splitlines():
            kind, *fields = line.split()
            if kind == "const":
                values["const"][fields[0]] = int(fields[1])
            elif kind == "rule":
                values["rule"][int(fields[0])] = int(fields[1])
            else:
                values["rows"].append((fields[0], int(fields[1]), int(fields[2])))
        self._probe_cache[chip] = values
        return values

    def _arms(self):
        for name, entry in self.by_task.items():
            for chip, arm in entry["chips"].items():
                yield name, entry, chip, arm

    # -- 1. the table names every task the tree creates ------------------------

    def test_every_created_task_has_a_recipe(self):
        created = created_task_sites()
        self.assertTrue(created, "the xTaskCreate scan found nothing; it is broken")
        missing = sorted(set(created) - set(self.by_task))
        self.assertEqual(
            missing, [],
            f"{missing} are created in src/ with no entry in "
            f"{RECIPES_PATH.relative_to(REPO_ROOT)}; a task with no recipe has no "
            "chain, no floor and nothing re-walking it",
        )

    def test_no_recipe_names_a_task_nothing_creates(self):
        created = created_task_sites()
        phantom = sorted(set(self.by_task) - set(created))
        self.assertEqual(
            phantom, [],
            f"{phantom} have recipes but nothing in src/ creates them; a rename "
            "went half-done",
        )

    def test_each_recipe_records_where_its_task_is_created(self):
        created = created_task_sites()
        for name, entry in self.by_task.items():
            with self.subTest(task=name):
                self.assertEqual(entry["created_in"], created[name])

    def test_the_three_tasks_outside_main_cpp_are_covered(self):
        """The criterion is the task, not the file (ADR 0040's rejected option).

        The profiler task list had exactly this blind spot: it scanned
        src/main.cpp only, so WebEvents, the ArduinoOTA task and HostedRecovery
        were invisible to it.
        """
        for name in ("WebEvents", "ArduinoOTA", "HostedRecovery"):
            with self.subTest(task=name):
                self.assertIn(name, self.by_task)
                self.assertNotEqual(self.by_task[name]["created_in"], "src/main.cpp")

    # -- 1b. which tasks every boot creates (the boot heap figure, #468) -------

    def test_each_recipe_records_the_function_and_guard_around_its_call(self):
        """A call moved, or wrapped in a new if, changes what every boot creates."""
        contexts = created_task_contexts()
        for name, entry in self.by_task.items():
            with self.subTest(task=name):
                self.assertEqual(entry["create_site"], contexts[name])

    def test_every_arm_says_whether_every_boot_creates_its_task(self):
        for name, _entry, chip, arm in self._arms():
            with self.subTest(task=name, chip=chip):
                self.assertIsInstance(arm.get("created"), str)
                self.assertTrue(arm["created"].strip())

    def test_a_task_under_a_real_guard_is_not_always_created(self):
        """`if (x) xTaskCreate...` is created only when x; the arm must say so.

        A `!flag` guard whose flag is set to true beside the call only stops a
        second creation, so it is no evidence either way.
        """
        for name, entry, chip, arm in self._arms():
            guard = entry["create_site"]["guard"]
            if guard is None or is_once_guard(guard, REPO_ROOT / entry["created_in"]):
                continue
            with self.subTest(task=name, chip=chip):
                self.assertNotEqual(arm["created"], "always")
                self.assertIn(guard, arm["created"])

    def test_the_tcb_figure_covers_every_chip_an_arm_names(self):
        tcb = self.recipes["metadata"]["tcb_bytes"]
        chips = {chip for _n, _e, chip, _a in self._arms()}
        self.assertEqual(sorted(tcb), sorted(chips))
        for chip, size in tcb.items():
            with self.subTest(chip=chip):
                self.assertIsInstance(size, int)
                self.assertGreater(size, 0)

    # -- 2. properties of the one source ---------------------------------------

    def test_every_arm_carries_both_figures_and_a_note(self):
        """The shape the header generator refuses to work without."""
        self.assertEqual(checker.recipe_shape_problems(self.recipes), [])

    def test_every_stack_covers_its_chain(self):
        """The floor, which config.h's static_assert also enforces per chip."""
        for name, _, chip, arm in self._arms():
            with self.subTest(chip=chip, task=name):
                self.assertGreaterEqual(arm["stack_bytes"], arm["chain_bytes"])

    def test_every_stack_is_a_whole_512_byte_step(self):
        """A value between steps means somebody typed a number instead of a size."""
        for name, _, chip, arm in self._arms():
            with self.subTest(chip=chip, task=name):
                self.assertEqual(arm["stack_bytes"] % 512, 0)

    def test_every_departure_from_the_rule_carries_its_reason(self):
        """#248's argument is what licenses a stack off the rule, so it is recorded.

        The state is derived from the two figures, so a stack edited off the rule
        without a reason is red here, and so is a re-derivation that moved an arm
        off it silently. The other direction holds too: an arm the rule sizes
        exactly carries no reason, because one left behind after the arm moved
        onto the rule would explain a departure that no longer exists.
        """
        for name, _, chip, arm in self._arms():
            with self.subTest(chip=chip, task=name):
                state = checker.rule_state(arm["chain_bytes"], arm["stack_bytes"])
                if state == "applied":
                    self.assertNotIn(
                        "reason", arm,
                        f"{name} on {chip} follows the rule but still carries a "
                        "reason for departing from it")
                else:
                    self.assertTrue(
                        arm.get("reason", "").strip(),
                        f"{name} on {chip} is {state} the rule for its chain "
                        f"({checker.rule_stack(arm['chain_bytes'])}) with no "
                        "recorded reason",
                    )

    def test_the_p4_arm_pays_the_rule_everywhere(self):
        """The chip that can afford the margin buys it on every task.

        ADR 0040 applies the rule per chip where affordable and declines it on
        #248's reason where not. artoo-esp32 has declines; the ESP32-P4 has
        none, and a new task must not quietly introduce the first one.
        """
        for name, entry in self.by_task.items():
            arm = entry["chips"].get("esp32p4")
            if arm is None:
                continue
            with self.subTest(task=name):
                self.assertEqual(
                    checker.rule_state(arm["chain_bytes"], arm["stack_bytes"]),
                    "applied",
                    f"{name}'s ESP32-P4 stack is not the rule for its chain, which "
                    "the free heap there pays for; #248's reason is an artoo-esp32 "
                    "argument",
                )

    def test_no_note_is_the_rewrite_placeholder(self):
        """--rewrite replaces a moved chain's note with a placeholder; it must not ship.

        The old note explained the old figure. Committing the placeholder would
        leave a figure nobody has explained.
        """
        for name, _, chip, arm in self._arms():
            with self.subTest(chip=chip, task=name):
                self.assertTrue(arm["note"].strip())
                self.assertFalse(
                    arm["note"].startswith(checker.NOTE_PLACEHOLDER_PREFIX),
                    f"{name} on {chip} still carries the --rewrite placeholder note",
                )

    # -- 3. what the build compiles --------------------------------------------

    def test_each_board_compiles_the_recipes_figures(self):
        """config.h, through the generated header, declares the recipe's figures.

        Per chip, so an arm generated under the wrong chip macro, or a config.h
        that stopped including the header, is red on the board it breaks.
        """
        for chip in ("esp32", "esp32p4"):
            compiled = self._probe(chip)["const"]
            for name, entry in self.by_task.items():
                arm = entry["chips"].get(chip)
                if arm is None:
                    continue
                with self.subTest(chip=chip, task=name):
                    self.assertEqual(compiled[entry["chain_constant"]], arm["chain_bytes"])
                    self.assertEqual(compiled[entry["stack_constant"]], arm["stack_bytes"])

    def test_each_boards_figure_table_is_its_recipe_arms_in_order(self):
        """TASK_STACK_FIGURES is what the native floors test walks; it must be whole.

        Also where HostedRecovery's absence on artoo-esp32 is proven: the task
        is in no artoo image (src/web/web_network_manager_hosted.cpp is
        whole-file guarded on PA_CAP_HOSTED_WIFI), so a figure for it there
        would be made up.
        """
        for chip in ("esp32", "esp32p4"):
            with self.subTest(chip=chip):
                expected = [
                    (entry["task"], entry["chips"][chip]["chain_bytes"],
                     entry["chips"][chip]["stack_bytes"])
                    for entry in self.recipes["tasks"] if chip in entry["chips"]
                ]
                self.assertEqual(self._probe(chip)["rows"], expected)
        self.assertNotIn("HostedRecovery", [row[0] for row in self._probe("esp32")["rows"]])

    def test_the_rules_two_copies_agree(self):
        """The rule is written once in Python and once in C++; they must be one rule.

        Compared on the step boundaries and on every recorded chain, so a
        rounding change in either copy is red wherever it would move a stack.
        """
        compiled = self._probe("esp32")["rule"]
        self.assertTrue(compiled)
        for chain, stack in compiled.items():
            with self.subTest(chain=chain):
                self.assertEqual(stack, checker.rule_stack(chain))

    # -- the recipe's own internal consistency --------------------------------

    def test_each_arm_is_walked_from_its_chips_product_image(self):
        """The gate re-walks the product image, so every arm must name it.

        An arm recorded against another image would be skipped by the row and
        silently never re-walked. The Recorded Chain is the product image's walk
        (ADR 0040, amended 2026-09-27).
        """
        product = {
            chip: spec["product"]
            for chip, spec in self.recipes["metadata"]["envs"].items()
        }
        for name, _, chip, arm in self._arms():
            with self.subTest(chip=chip, task=name):
                self.assertEqual(arm["env"], product[chip])

    def test_a_stitched_frame_carries_the_reason_it_is_stitched(self):
        """A hand-added frame or table is a hole in the walk, so it must say which one."""
        for name, _, chip, arm in self._arms():
            with self.subTest(chip=chip, task=name):
                stitches = arm.get("frames", []) + arm.get("tables", [])
                if not stitches:
                    continue
                self.assertTrue(
                    arm.get("stitch_reason", "").strip(),
                    f"{name} on {chip} stitches {stitches} into its "
                    "chain without saying what the walker could not follow",
                )

    def test_the_table_covers_fifteen_tasks_fourteen_of_them_on_artoo(self):
        self.assertEqual(len(self.recipes["tasks"]), 15)
        self.assertEqual(
            sum(1 for e in self.recipes["tasks"] if "esp32" in e["chips"]), 14)
        self.assertEqual(
            sum(1 for e in self.recipes["tasks"] if "esp32p4" in e["chips"]), 15)


class ChainJudgementIsPerChip(unittest.TestCase):
    """ADR 0040's 2026-09-25 amendment (#429): the gate row judges each chip
    its own way. artoo-esp32 fails on any byte past the recorded chain; an
    ESP32-P4 arm fails only once the rule on the walk no longer fits its stack,
    and a figure that merely drifted is a note.

    The figures are the Console's on each chip. On the P4 its 10752 B stack
    holds a chain of up to 8601 B by the rule (8601 -> 10752), and 8602 needs
    11264.
    """

    def test_artoo_fails_on_one_byte_past_the_recorded_chain(self):
        # 7472 -> 9340 -> 9728 still fits the stack; byte-exact fails it anyway.
        verdict = checker.judge_arm(
            "esp32", 7472, "CONSOLE_TASK_MEASURED_CHAIN_BYTES", 7456,
            "CONSOLE_TASK_STACK_BYTES", 9728)
        self.assertIsNotNone(verdict.failure)
        self.assertIn("by 16 B", verdict.failure)
        self.assertIsNone(checker.judge_arm(
            "esp32", 7456, "CONSOLE_TASK_MEASURED_CHAIN_BYTES", 7456,
            "CONSOLE_TASK_STACK_BYTES", 9728).failure)

    def test_p4_drift_inside_the_allocation_is_a_note(self):
        for walked in (8464 + 16, 8601, 8464 - 32):
            with self.subTest(walked=walked):
                verdict = checker.judge_arm(
                    "esp32p4", walked, "CONSOLE_TASK_MEASURED_CHAIN_BYTES", 8464,
                    "CONSOLE_TASK_STACK_BYTES", 10752)
                self.assertIsNone(verdict.failure)
                self.assertIsNotNone(verdict.note, "drift must still be printed")
        self.assertIsNone(checker.judge_arm(
            "esp32p4", 8464, "CONSOLE_TASK_MEASURED_CHAIN_BYTES", 8464,
            "CONSOLE_TASK_STACK_BYTES", 10752).note)

    def test_p4_fails_once_the_rule_outgrows_the_stack(self):
        verdict = checker.judge_arm(
            "esp32p4", 8602, "CONSOLE_TASK_MEASURED_CHAIN_BYTES", 8464,
            "CONSOLE_TASK_STACK_BYTES", 10752)
        self.assertIsNotNone(verdict.failure)
        self.assertIn("8602 -> 11264", verdict.failure)

    def test_a_chip_with_no_recorded_judgement_is_refused(self):
        with self.assertRaises(checker.Fatal):
            checker.judge_arm("esp32s3", 1000, "X_CHAIN", 1000, "X_STACK", 2048)


class RewriteMovesAStackOnlyWhenAccepted(unittest.TestCase):
    """--rewrite records every walked chain, and a stack only when told to.

    A raise is the operator's decision (ADR 0040, amended 2026-09-27), so
    re-walking must never move a stack as a side effect - including the case
    where the chain has grown past it and the build will refuse to compile.
    """

    def arm(self, chain, stack, **extra):
        return {"env": "artoo_esp32", "roots": ["t"], "chain_bytes": chain,
                "stack_bytes": stack, "note": "why it is this deep", **extra}

    def test_a_grown_chain_is_recorded_and_the_stack_is_left(self):
        arm = self.arm(4000, 5120)
        outcome = checker.rerecord_arm(arm, 4400, False, "T_STACK_BYTES", "T")
        self.assertEqual((arm["chain_bytes"], arm["stack_bytes"]), (4400, 5120))
        self.assertEqual(arm["note"], checker.NOTE_PLACEHOLDER)
        self.assertTrue(outcome.changed and outcome.placeholder)
        self.assertEqual(outcome.stack_moved, 0)
        # 4400 -> 5500 -> 5632: the stack is now below the rule, with no reason.
        self.assertEqual(len(outcome.warnings), 1)
        self.assertIn("no 'reason'", outcome.warnings[0])

    def test_a_chain_past_its_stack_is_recorded_and_said_to_break_the_build(self):
        arm = self.arm(4000, 5120)
        outcome = checker.rerecord_arm(arm, 5200, False, "T_STACK_BYTES", "T")
        self.assertEqual(arm["stack_bytes"], 5120)
        self.assertIn("static_assert", outcome.warnings[0])
        self.assertIn("--accept T", outcome.warnings[0])

    def test_accepting_takes_the_rule_and_drops_a_reason_that_no_longer_applies(self):
        arm = self.arm(4000, 6144, reason="an earlier raise")
        outcome = checker.rerecord_arm(arm, 4000, True, "T_STACK_BYTES", "T")
        self.assertEqual(arm["stack_bytes"], checker.rule_stack(4000))
        self.assertEqual(outcome.stack_moved, 5120 - 6144)
        self.assertNotIn("reason", arm)
        # The chain did not move, so the note still describes it.
        self.assertEqual(arm["note"], "why it is this deep")
        self.assertFalse(outcome.placeholder)
        self.assertEqual(outcome.warnings, [])

    def test_an_unchanged_arm_is_not_touched(self):
        arm = self.arm(4000, 5120)
        before = dict(arm)
        outcome = checker.rerecord_arm(arm, 4000, False, "T_STACK_BYTES", "T")
        self.assertEqual(arm, before)
        self.assertFalse(outcome.changed)

    def test_accept_names_are_checked_before_anything_is_built(self):
        recipes = {"tasks": [
            {"task": "A", "chips": {"esp32": {"env": "artoo_esp32"}}},
            {"task": "B", "chips": {"esp32p4": {"env": "firebeetle2"}}},
        ]}
        self.assertEqual(
            checker.accepted_tasks(recipes, "esp32", "artoo_esp32", ["A:esp32"], False),
            {"A"})
        for bad in ("B", "A:esp32p4", "Nope"):
            with self.subTest(accept=bad):
                with self.assertRaises(checker.Fatal):
                    checker.accepted_tasks(recipes, "esp32", "artoo_esp32", [bad], False)
        self.assertEqual(
            checker.accepted_tasks(recipes, "esp32", "artoo_esp32", [], True), {"A"})

    def test_accept_all_leaves_a_stack_held_off_the_rule_on_purpose(self):
        # A recorded reason is a decision to be off the rule, above it (#250) or
        # below it (#248). A sweep must not undo it in either direction; only
        # naming the task does.
        recipes = {"tasks": [
            {"task": "OnRule", "chips": {"esp32": {"env": "artoo_esp32"}}},
            {"task": "Above", "chips": {"esp32": {"env": "artoo_esp32",
                                                  "reason": "raised on purpose"}}},
            {"task": "Declined", "chips": {"esp32": {"env": "artoo_esp32",
                                                     "reason": "heap is scarce"}}},
        ]}
        self.assertEqual(
            checker.accepted_tasks(recipes, "esp32", "artoo_esp32", [], True), {"OnRule"})
        self.assertEqual(
            checker.accepted_tasks(recipes, "esp32", "artoo_esp32", ["Above"], False),
            {"Above"})

    def test_accept_all_on_the_real_recipe_moves_no_reasoned_arm(self):
        recipes = checker.load_recipes()
        for chip, product in (("esp32", "artoo_esp32"), ("esp32p4", "firebeetle2")):
            chosen = checker.accepted_tasks(recipes, chip, product, [], True)
            for task in recipes["tasks"]:
                arm = task["chips"].get(chip)
                if arm and arm.get("reason", "").strip():
                    with self.subTest(chip=chip, task=task["task"]):
                        self.assertNotIn(task["task"], chosen)


if __name__ == "__main__":
    unittest.main()
