"""The profiler's task list must name every task main.cpp creates (#250).

`src/web/api_profiler.cpp` carries a hardcoded `s_taskNames[]` and a comment
saying it "must match xTaskCreatePinnedToCore() calls in main.cpp". Nothing
enforced that, and it drifted: `SeqDisp` was absent, so `/api/profiler`
reported nine of the ten tasks for an unknown length of time.

That drift is worse than a missing endpoint. The response still looks complete,
and an absent task is indistinguishable from one whose Component Toggle is off
-- so the gap reads as normal. It cost #250 its primary acceptance criterion:
the ticket asked for SeqDisp's runtime high-water mark, and no bench session
could ever have produced it.

This test is the enforcement the comment always implied.
"""

import re
import unittest
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[2]
MAIN_CPP = REPO_ROOT / "src" / "main.cpp"
API_PROFILER = REPO_ROOT / "src" / "web" / "api_profiler.cpp"

# Second argument of xTaskCreatePinnedToCore(fn, "Name", ...) is the task name
# FreeRTOS registers, and it is the name xTaskGetHandle() looks up.
TASK_CREATE_RE = re.compile(
    r"xTaskCreatePinnedToCore\s*\(\s*[^,]+,\s*\"([^\"]+)\"", re.MULTILINE
)
TASK_NAMES_BLOCK_RE = re.compile(
    r"s_taskNames\s*\[\s*PROF_TASK_MAX\s*\]\s*=\s*\{(.*?)\}\s*;", re.DOTALL
)
PROF_TASK_MAX_RE = re.compile(r"#define\s+PROF_TASK_MAX\s+(\d+)")

# Tasks the profiler legitimately watches that main.cpp does not create itself.
# loopTask is spawned by arduino-esp32's core, which calls our setup()/loop();
# we size it through ARDUINO_LOOP_STACK_SIZE in platformio.ini rather than with
# xTaskCreatePinnedToCore, so it will never appear in the scan above.
EXTERNALLY_CREATED = {"loopTask"}


def created_task_names() -> set:
    return set(TASK_CREATE_RE.findall(MAIN_CPP.read_text()))


def profiled_task_names() -> list:
    block = TASK_NAMES_BLOCK_RE.search(API_PROFILER.read_text())
    assert block, "s_taskNames[] not found in api_profiler.cpp"
    return re.findall(r"\"([^\"]+)\"", block.group(1))


class ProfilerTaskListTest(unittest.TestCase):
    def test_every_created_task_is_profiled(self):
        created = created_task_names()
        profiled = set(profiled_task_names())
        missing = sorted(created - profiled)
        self.assertEqual(
            missing,
            [],
            f"main.cpp creates {missing} but api_profiler.cpp does not list them, so "
            f"/api/profiler will silently omit them. Add them to s_taskNames[] and "
            f"raise PROF_TASK_MAX.",
        )

    def test_no_phantom_tasks_are_profiled(self):
        """A name here that nothing creates always reports 'not found'.

        Harmless at runtime, but it means a rename went half-done, so catch it.
        """
        created = created_task_names() | EXTERNALLY_CREATED
        phantom = sorted(set(profiled_task_names()) - created)
        self.assertEqual(
            phantom,
            [],
            f"api_profiler.cpp lists {phantom}, which main.cpp does not create.",
        )

    def test_prof_task_max_matches_the_list(self):
        """Array size and declared bound must agree, or the tail reads garbage."""
        declared = PROF_TASK_MAX_RE.search(API_PROFILER.read_text())
        self.assertIsNotNone(declared, "PROF_TASK_MAX not found")
        self.assertEqual(
            int(declared.group(1)),
            len(profiled_task_names()),
            "PROF_TASK_MAX does not equal the number of entries in s_taskNames[]",
        )

    def test_seqdisp_specifically(self):
        """The task whose absence cost #250 its acceptance criterion."""
        self.assertIn("SeqDisp", profiled_task_names())


if __name__ == "__main__":
    unittest.main()
