"""Every project-created task has a Measured Chain recipe, on every chip (#271).

`tools/task_stack_recipes.json` is the recipe table ADR 0038 asks for: per task
and per chip, the environment walked, the root symbols, the frames stitched by
hand across an indirect call, and the profiler-image substitution where the
product image's chain is not the deeper one. Three separate things can rot, and
each has a class of test here:

1. **The table can miss a task.** That is how `/api/profiler` came to report
   nine of ten tasks (#250), and it is why the criterion is the task, not the
   file it happens to be created in. Every `xTaskCreate*` call site in `src/` is
   scanned, and every registered name must have a recipe.

2. **A constant can drift from its derivation.** `include/config.h` declares the
   chain and the stack; the recipe records which of them the sizing rule
   produced and, where the rule was declined, why. So each arm is re-derived
   here: `applied` must equal the rule for its OWN chain, `above` must exceed
   it, `declined` must sit below the rule and still cover the chain, with a
   written reason. A hand-edited value that clears its chain but not the rule is
   red, which is what "a constant cannot drift from its derivation" means.

3. **The gate row can read the header wrongly.**
   `tools/check_task_stack_chains.py` parses `include/config.h` rather than
   compiling it, because it runs as a slice-gate row where a C++ probe per
   environment is cost the row does not need. That parse is proven here against
   a compiler probe that includes the real headers under each `PA_BOARD` value -
   the same technique test_board_chip_sized_constants.py uses, and the only way
   a host test sees the ESP32-P4 arm at all (platformio.ini env:native always
   builds PA_BOARD_ARTOO_ESP32).

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

from task_create_sites import created_task_sites

REPO_ROOT = Path(__file__).resolve().parents[2]
INCLUDE_DIR = REPO_ROOT / "include"
RECIPES_PATH = REPO_ROOT / "tools" / "task_stack_recipes.json"

sys.path.insert(0, str(REPO_ROOT / "tools"))
import check_task_stack_chains as checker  # noqa: E402

# Everything the probed headers pull in, staged into the probe's include path.
PROBE_HEADER_SET = (
    "config.h",
    "board_capabilities.inc",
    "build_flags.inc",
    "firebeetle_required_pins.inc",
)

BOARD_TO_CHIP = {
    "PA_BOARD_ARTOO_ESP32": "esp32",
    "PA_BOARD_FIREBEETLE2": "esp32p4",
}

def rule_stack_for_chain(chain_bytes: int) -> int:
    """The #248 sizing rule: chain + 25%, rounded up to the next 512 bytes."""
    need = (chain_bytes * 5 + 3) // 4
    return ((need + 511) // 512) * 512


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

    def _probe_source(self, board_macro, names):
        emit = "\n".join(
            f'    std::printf("{name} %u\\n", (unsigned){name});' for name in names
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
                "    return 0;",
                "}",
                "",
            ]
        )

    def _probe(self, board_macro, names):
        key = (board_macro, tuple(names))
        if key in self._probe_cache:
            return self._probe_cache[key]
        with tempfile.TemporaryDirectory() as tmp:
            staged = Path(tmp) / "include"
            staged.mkdir()
            for header in PROBE_HEADER_SET:
                shutil.copyfile(INCLUDE_DIR / header, staged / header)
            source = Path(tmp) / "task_stack_probe.cpp"
            source.write_text(self._probe_source(board_macro, names), encoding="utf-8")
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
        values = {}
        for line in ran.stdout.split("\n"):
            if line.strip():
                name, value = line.split()
                values[name] = int(value)
        self._probe_cache[key] = values
        return values

    def _constants_for(self, chip):
        """{CONSTANT: value} for one chip, from the real headers via the probe."""
        board = next(b for b, c in BOARD_TO_CHIP.items() if c == chip)
        names = []
        for entry in self.recipes["tasks"]:
            if chip in entry["chips"]:
                names.extend((entry["chain_constant"], entry["stack_constant"]))
        return self._probe(board, names)

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
        """The criterion is the task, not the file (ADR 0038's rejected option).

        The profiler task list had exactly this blind spot: it scanned
        src/main.cpp only, so WebEvents, the ArduinoOTA task and HostedRecovery
        were invisible to it.
        """
        for name in ("WebEvents", "ArduinoOTA", "HostedRecovery"):
            with self.subTest(task=name):
                self.assertIn(name, self.by_task)
                self.assertNotEqual(self.by_task[name]["created_in"], "src/main.cpp")

    # -- 2. constants are re-derived from their chains -------------------------

    def test_every_arm_covers_its_own_chain(self):
        """The floor, on both chips. This is what the static_asserts enforce."""
        for chip in ("esp32", "esp32p4"):
            values = self._constants_for(chip)
            for name, entry in self.by_task.items():
                arm = entry["chips"].get(chip)
                if arm is None:
                    continue
                with self.subTest(chip=chip, task=name):
                    chain = values[entry["chain_constant"]]
                    stack = values[entry["stack_constant"]]
                    self.assertGreaterEqual(
                        stack, chain,
                        f"{entry['stack_constant']} is below "
                        f"{entry['chain_constant']} on {chip}",
                    )

    def test_every_arm_matches_the_recorded_chain(self):
        """config.h and the recipe table must not be able to disagree.

        The gate row compares a fresh walk against config.h's constant; the
        recipe records the figure that walk produced when it was written. If the
        two drift apart, the row is enforcing a number nothing measured.
        """
        for chip in ("esp32", "esp32p4"):
            values = self._constants_for(chip)
            for name, entry in self.by_task.items():
                arm = entry["chips"].get(chip)
                if arm is None:
                    continue
                with self.subTest(chip=chip, task=name):
                    self.assertEqual(
                        values[entry["chain_constant"]], arm["chain_bytes"],
                        f"{entry['chain_constant']} on {chip} does not equal the "
                        "chain recorded in the recipe table",
                    )

    def test_every_arm_follows_its_recorded_derivation(self):
        """`applied` is the rule exactly; `above` exceeds it; `declined` is below.

        This is the assertion that stops a constant drifting from the derivation
        it claims. A value hand-edited to something that still clears its chain
        but no longer follows the rule it says it follows is red here.
        """
        for chip in ("esp32", "esp32p4"):
            values = self._constants_for(chip)
            for name, entry in self.by_task.items():
                arm = entry["chips"].get(chip)
                if arm is None:
                    continue
                with self.subTest(chip=chip, task=name):
                    chain = values[entry["chain_constant"]]
                    stack = values[entry["stack_constant"]]
                    by_rule = rule_stack_for_chain(chain)
                    state = arm["rule"]
                    if state == "applied":
                        self.assertEqual(
                            stack, by_rule,
                            f"{entry['stack_constant']} on {chip} claims the rule "
                            f"but {chain} by the rule is {by_rule}",
                        )
                    elif state == "above":
                        self.assertGreater(
                            stack, by_rule,
                            f"{entry['stack_constant']} on {chip} claims to sit "
                            "above the rule but does not",
                        )
                    elif state == "declined":
                        self.assertLess(
                            stack, by_rule,
                            f"{entry['stack_constant']} on {chip} claims the rule "
                            "was declined but it is at or above it - relabel it",
                        )
                    else:
                        self.fail(f"unknown rule state {state!r}")
                    self.assertEqual(stack % 512, 0, "stacks are 512-byte steps")

    def test_every_departure_from_the_rule_carries_its_reason(self):
        """#248's argument is the only thing that licenses declining the rule.

        ADR 0038 requires the decline to be recorded beside the constant. Beside
        it in config.h is prose; here it is checked, so a later arm cannot be
        moved off the rule silently.
        """
        for name, entry in self.by_task.items():
            for chip, arm in entry["chips"].items():
                with self.subTest(chip=chip, task=name):
                    if arm["rule"] == "applied":
                        continue
                    self.assertTrue(
                        arm.get("reason", "").strip(),
                        f"{name} on {chip} is {arm['rule']} the rule with no "
                        "recorded reason",
                    )

    def test_the_p4_arm_pays_the_rule_everywhere(self):
        """The chip that can afford the margin buys it on every task.

        ADR 0038 applies the rule per chip where affordable and declines it on
        #248's reason where not. artoo-esp32 has declines; the ESP32-P4 has
        none, and a new task must not quietly introduce the first one.
        """
        for name, entry in self.by_task.items():
            arm = entry["chips"].get("esp32p4")
            if arm is None:
                continue
            with self.subTest(task=name):
                self.assertEqual(
                    arm["rule"], "applied",
                    f"{name} declines the sizing rule on the ESP32-P4, which has "
                    "the free heap to pay it; #248's reason is an artoo-esp32 "
                    "argument",
                )

    # -- 3. the gate row's parser reads what the compiler reads ----------------

    def test_the_gate_rows_parser_agrees_with_the_compiler(self):
        """tools/check_task_stack_chains.py parses config.h; prove the parse.

        A parser that silently returned half a ladder would make the gate row
        pass on tasks it never checked, which is the failure mode this whole
        ticket exists to remove.
        """
        parsed = checker.parse_chip_constants()
        for chip in ("esp32", "esp32p4"):
            compiled = self._constants_for(chip)
            for constant, value in compiled.items():
                with self.subTest(chip=chip, constant=constant):
                    self.assertIn(
                        constant, parsed[chip],
                        f"{constant} is compiled into the {chip} arm but the gate "
                        "row's parser does not see it",
                    )
                    self.assertEqual(parsed[chip][constant], value)

    # -- the recipe's own internal consistency --------------------------------

    def test_each_arm_is_walked_from_its_chips_product_image(self):
        """The gate re-walks the product image, so every arm must name it.

        An arm recorded against a profiler image would be skipped by the row and
        silently never re-walked. The profiler figure belongs in
        `profiler_bytes`, and the substitution in `deeper_image`.
        """
        product = {
            chip: spec["product"]
            for chip, spec in self.recipes["metadata"]["envs"].items()
        }
        for name, entry in self.by_task.items():
            for chip, arm in entry["chips"].items():
                with self.subTest(chip=chip, task=name):
                    self.assertEqual(arm["env"], product[chip])

    def test_the_recorded_chain_is_the_deeper_of_the_two_images(self):
        """config.h is compiled into the profiler image too, so it must cover it.

        #245 sized SafetyMonitor from the profiler image for this reason, and at
        #271 the artoo arm of that same task was 16 B under it.
        """
        for name, entry in self.by_task.items():
            for chip, arm in entry["chips"].items():
                with self.subTest(chip=chip, task=name):
                    self.assertEqual(
                        arm["chain_bytes"],
                        max(arm["product_bytes"], arm["profiler_bytes"]),
                    )
                    if arm["product_bytes"] != arm["profiler_bytes"]:
                        self.assertTrue(
                            arm.get("deeper_image", "").strip(),
                            f"{name} on {chip} takes its chain from the deeper "
                            "image with no recorded substitution",
                        )

    def test_a_stitched_frame_carries_the_reason_it_is_stitched(self):
        """A hand-added frame is a hole in the walk, so it must say which one."""
        for name, entry in self.by_task.items():
            for chip, arm in entry["chips"].items():
                with self.subTest(chip=chip, task=name):
                    if not arm.get("frames"):
                        continue
                    self.assertTrue(
                        arm.get("stitch_reason", "").strip(),
                        f"{name} on {chip} stitches {arm['frames']} into its "
                        "chain without saying what the walker could not follow",
                    )

    def test_hosted_recovery_is_declared_on_the_p4_arm_only(self):
        """The task is in no artoo image, so a chain for it there would be made up.

        src/web/web_network_manager_hosted.cpp is whole-file guarded on
        PA_CAP_HOSTED_WIFI. Twelve tasks on artoo-esp32, thirteen on the P4.
        """
        entry = self.by_task["HostedRecovery"]
        self.assertEqual(sorted(entry["chips"]), ["esp32p4"])
        parsed = checker.parse_chip_constants()
        self.assertIn(entry["stack_constant"], parsed["esp32p4"])
        self.assertNotIn(
            entry["stack_constant"], parsed["esp32"],
            "an artoo-esp32 arm declares a stack for a task that board never "
            "creates",
        )

    def test_the_table_covers_thirteen_tasks_twelve_of_them_on_artoo(self):
        self.assertEqual(len(self.recipes["tasks"]), 13)
        self.assertEqual(
            sum(1 for e in self.recipes["tasks"] if "esp32" in e["chips"]), 12)
        self.assertEqual(
            sum(1 for e in self.recipes["tasks"] if "esp32p4" in e["chips"]), 13)


if __name__ == "__main__":
    unittest.main()
