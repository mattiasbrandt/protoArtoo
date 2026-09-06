"""The profiler's task list must name every task the tree creates (#250, #271).

`src/web/api_profiler.cpp` carries a hardcoded `s_taskNames[]` and a comment
saying it "must match xTaskCreatePinnedToCore() calls". Nothing enforced that,
and it drifted: `SeqDisp` was absent, so `/api/profiler` reported nine of the
ten tasks for an unknown length of time.

That drift is worse than a missing endpoint. The response still looks complete,
and an absent task is indistinguishable from one whose Component Toggle is off
-- so the gap reads as normal. It cost #250 its primary acceptance criterion:
the ticket asked for SeqDisp's runtime high-water mark, and no bench session
could ever have produced it.

This test is the enforcement the comment always implied. It scans the whole
tree, not `src/main.cpp` (#271, ADR 0040): the criterion is the task, not the
file it happens to be created in, and the main.cpp-only scan was blind to all
three tasks created elsewhere -- WebEvents and the ArduinoOTA task in
src/web/web_server.cpp, HostedRecovery in
src/web/web_network_manager_hosted.cpp. The same blind spot, in the same shape,
as the one this file was written to close.
"""

import re
import unittest
from pathlib import Path

from task_create_sites import EXTERNALLY_CREATED, created_task_names


REPO_ROOT = Path(__file__).resolve().parents[2]
API_PROFILER = REPO_ROOT / "src" / "web" / "api_profiler.cpp"
# PROF_TASK_MAX moved to the header when #224 extracted ProfilerReading, which
# sizes its taskStacks[] with it: the bound is now part of what an adapter
# reads, not private to the JSON one. Both files are searched, and exactly one
# definition is required - the number existing in two places is the same drift
# class this whole guard exists to catch.
PROF_TASK_MAX_FILES = (
    REPO_ROOT / "include" / "api_profiler.h",
    API_PROFILER,
)

TASK_NAMES_BLOCK_RE = re.compile(
    r"s_taskNames\s*\[\s*PROF_TASK_MAX\s*\]\s*=\s*\{(.*?)\}\s*;", re.DOTALL
)
PROF_TASK_MAX_RE = re.compile(r"#define\s+PROF_TASK_MAX\s+(\d+)")


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
            f"src/ creates {missing} but api_profiler.cpp does not list them, so "
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
            f"api_profiler.cpp lists {phantom}, which src/ does not create.",
        )

    def test_prof_task_max_matches_the_list(self):
        """Array size and declared bound must agree, or the tail reads garbage."""
        declared = [
            match
            for path in PROF_TASK_MAX_FILES
            for match in PROF_TASK_MAX_RE.findall(path.read_text())
        ]
        self.assertEqual(
            len(declared),
            1,
            "PROF_TASK_MAX must be defined exactly once across "
            f"{[p.name for p in PROF_TASK_MAX_FILES]}; found {len(declared)}",
        )
        self.assertEqual(
            int(declared[0]),
            len(profiled_task_names()),
            "PROF_TASK_MAX does not equal the number of entries in s_taskNames[]",
        )

    def test_seqdisp_specifically(self):
        """The task whose absence cost #250 its acceptance criterion."""
        self.assertIn("SeqDisp", profiled_task_names())

    def test_the_three_tasks_created_outside_main_cpp_are_profiled(self):
        """The blind spot the main.cpp-only scan had (#271).

        All three were created, running and unlistable: an operator reading
        /api/profiler could not see the high-water mark of the task that
        services OTA, the one that pushes every SSE event, or the one that
        recovers the ESP32-P4's radio co-processor.
        """
        profiled = profiled_task_names()
        for name in ("WebEvents", "ArduinoOTA", "HostedRecovery"):
            with self.subTest(task=name):
                self.assertIn(name, profiled)

    def test_the_scan_actually_reaches_outside_main_cpp(self):
        """Guard the guard: a scan narrowed back to main.cpp must fail here.

        Without this, narrowing created_task_names() would make every test above
        pass vacuously -- the profiler list would simply be checked against a
        smaller set, which is precisely how the original blind spot read as
        healthy.
        """
        from task_create_sites import created_task_sites

        sites = created_task_sites()
        outside = {name: path for name, path in sites.items() if path != "src/main.cpp"}
        self.assertEqual(
            sorted(outside),
            ["ArduinoOTA", "HostedRecovery", "WebEvents"],
            f"the xTaskCreate scan found {sorted(outside)} outside src/main.cpp; "
            "if a task moved or was added, update this expectation deliberately",
        )


if __name__ == "__main__":
    unittest.main()
