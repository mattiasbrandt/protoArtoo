"""The generated task-stack header against the recipe it came from (ADR 0040, #381).

include/task_stack_figures.h carries every *_MEASURED_CHAIN_BYTES and
*_STACK_BYTES constant the firmware compiles, and is generated from
tools/task_stack_recipes.json, the one home of those figures. The generator
cannot catch the one failure that matters most: nobody running it. A recipe
edited by hand with the header left behind builds firmware from figures the
recipe no longer holds - and the gate row judges chains against the recipe, so
the two would disagree silently.

So the assertions here are about the committed header going stale, and about
the two properties that make the check trustworthy -

  it never writes, so corrupting the header and checking twice fails twice. A
  check that regenerated in place would repair the drift it was meant to report
  and then pass on evidence it had manufactured itself;

  it runs the real generator (check_task_stack_chains.render_header) rather
  than a copy of its rules, so a generator change cannot leave the check
  agreeing with a version of the header nobody ships.

Every case but the first runs against a scratch copy. The check addresses the
recipe and the header through the tool's own path constants, which is what
lets it be aimed at a scratch tree by patching them - and what keeps a test
that goes wrong from touching the committed files.
"""

import contextlib
import json
import shutil
import sys
import tempfile
import unittest
import unittest.mock
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))

import check_task_stack_chains as checker  # noqa: E402


class TaskStackFiguresDrift(unittest.TestCase):
    def test_the_committed_header_is_what_the_recipe_generates(self):
        self.assertIsNone(
            checker.header_drift(),
            "regenerate it: python3 tools/check_task_stack_chains.py --generate "
            "(or re-record the figures with --rewrite, which regenerates it too)",
        )

    @contextlib.contextmanager
    def scratch(self):
        """A copy of the recipe and the header, with the tool aimed at it."""
        with tempfile.TemporaryDirectory() as tmp:
            recipes = Path(tmp) / "task_stack_recipes.json"
            header = Path(tmp) / "task_stack_figures.h"
            shutil.copy(checker.RECIPES, recipes)
            shutil.copy(checker.FIGURES_H, header)
            with unittest.mock.patch.object(checker, "RECIPES", recipes), \
                    unittest.mock.patch.object(checker, "FIGURES_H", header):
                yield recipes, header

    def test_a_stale_header_is_reported_every_time_and_never_repaired(self):
        with self.scratch() as (_, header):
            header.write_text(
                header.read_text(encoding="utf-8").replace(
                    "CONSOLE_TASK_STACK_BYTES = 11264", "CONSOLE_TASK_STACK_BYTES = 11776"),
                encoding="utf-8")
            corrupted = header.read_bytes()
            first = checker.header_drift()
            second = checker.header_drift()
            self.assertIsNotNone(first)
            self.assertIsNotNone(second)
            self.assertEqual(header.read_bytes(), corrupted, "the check wrote the header")

    def test_a_recipe_edited_without_regenerating_is_caught(self):
        with self.scratch() as (recipes, _):
            doc = json.loads(recipes.read_text(encoding="utf-8"))
            arm = next(t for t in doc["tasks"] if t["task"] == "DriveTask")["chips"]["esp32"]
            arm["stack_bytes"] += 512
            recipes.write_text(json.dumps(doc, indent=2) + "\n", encoding="utf-8")
            self.assertIsNotNone(checker.header_drift())

    def test_generating_is_what_clears_it(self):
        with self.scratch() as (recipes, header):
            header.write_text("// hand-edited\n", encoding="utf-8")
            self.assertTrue(checker.write_header(checker.load_recipes()))
            self.assertIsNone(checker.header_drift())
            self.assertFalse(checker.write_header(checker.load_recipes()),
                             "a second run rewrote an up-to-date header")

    def test_a_missing_header_is_reported(self):
        with self.scratch() as (_, header):
            header.unlink()
            self.assertIn("missing", checker.header_drift())
            self.assertFalse(header.exists())


if __name__ == "__main__":
    unittest.main()
