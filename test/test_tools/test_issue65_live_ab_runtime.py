import importlib.util
import json
import os
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path


TOOLS_DIR = Path(__file__).parents[2] / "tools"
MODULE_PATH = TOOLS_DIR / "issue65_live_ab_runtime.py"
sys.path.insert(0, str(TOOLS_DIR))
SPEC = importlib.util.spec_from_file_location("issue65_live_ab_runtime", MODULE_PATH)
RUNTIME = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = RUNTIME
SPEC.loader.exec_module(RUNTIME)


class TemporaryGitIdentityWrapperTest(unittest.TestCase):
    def test_shimmed_tracked_file_describes_locked_head_as_clean(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            worktree = root / "repo"
            scratch = root / "scratch"
            patcher = worktree / "tools" / "patcher.py"
            patcher.parent.mkdir(parents=True)
            scratch.mkdir()
            patcher.write_text("original\n", encoding="utf-8")
            subprocess.run(["git", "init", "-q", worktree], check=True)
            subprocess.run(
                ["git", "-C", worktree, "config", "user.email", "test@example.com"],
                check=True,
            )
            subprocess.run(
                ["git", "-C", worktree, "config", "user.name", "Test"],
                check=True,
            )
            subprocess.run(["git", "-C", worktree, "add", "tools/patcher.py"], check=True)
            subprocess.run(
                ["git", "-C", worktree, "commit", "-qm", "fixture"],
                check=True,
            )
            clean = subprocess.check_output(
                ["git", "describe", "--tags", "--always", "--long", "--dirty"],
                cwd=worktree,
                text=True,
            ).strip()
            patcher.write_text("authorized shim\n", encoding="utf-8")

            normal = subprocess.check_output(
                ["git", "describe", "--tags", "--always", "--long", "--dirty"],
                cwd=worktree,
                text=True,
            ).strip()
            self.assertTrue(normal.endswith("-dirty"))

            with RUNTIME._temporary_git_identity_wrapper(
                worktree,
                scratch_directory=scratch,
                expected_descriptor=clean,
            ) as environment:
                described = subprocess.check_output(
                    ["git", "describe", "--tags", "--always", "--long", "--dirty"],
                    cwd=worktree,
                    env=environment,
                    text=True,
                ).strip()
                self.assertFalse(described.endswith("-dirty"))
                self.assertEqual(described, clean)
                self.assertEqual(
                    Path(environment["PATH"].split(os.pathsep)[0]),
                    scratch / "git-identity-bin",
                )
                self.assertTrue((scratch / "git-identity-bin" / "git").is_file())
                delegated_status = subprocess.check_output(
                    ["git", "status", "--porcelain"],
                    cwd=worktree,
                    env=environment,
                    text=True,
                ).strip()
                self.assertTrue(delegated_status)

            self.assertFalse((scratch / "git-identity-bin").exists())
            self.assertTrue(
                subprocess.check_output(
                    ["git", "status", "--porcelain"],
                    cwd=worktree,
                    text=True,
                ).strip()
            )


class PostRollbackRunTest(unittest.TestCase):
    def test_r1_is_locked_to_verified_revert_and_complete_comparison(self):
        locked = RUNTIME.planner.RUNS["R1"]

        self.assertEqual(locked.role, "R")
        self.assertEqual(
            locked.commit,
            "128ab4581fa8ccf1112b13615ce219cff1cb463f",
        )
        self.assertEqual(locked.worktree, "/tmp/protoartoo-issue65-R")
        self.assertEqual(locked.required_completed_runs, ("A1", "B1", "A2"))

    def test_r1_browser_plan_uses_pre_bootstrap_resource_contract(self):
        result = subprocess.run(
            [
                "node",
                TOOLS_DIR / "issue65_browser_capture.js",
                "--dry-run",
                "--run",
                "R1",
                "--url",
                "http://10.0.0.22/wifi.html",
            ],
            check=False,
            capture_output=True,
            text=True,
        )

        self.assertEqual(result.returncode, 0, result.stderr)
        plan = json.loads(result.stdout)
        self.assertEqual(plan["role"], "R")
        self.assertEqual(
            plan["expectedCommit"],
            "128ab4581fa8ccf1112b13615ce219cff1cb463f",
        )
        self.assertIn("/page_loader.js", plan["requiredResources"])
        self.assertNotIn("/page_bootstrap.js", plan["requiredResources"])


if __name__ == "__main__":
    unittest.main()
