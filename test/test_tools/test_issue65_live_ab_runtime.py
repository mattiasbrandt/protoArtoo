import importlib.util
import json
import os
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path
from types import SimpleNamespace


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

    def test_clean_worktree_keeps_locked_identity_during_generated_file_churn(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            worktree = root / "repo"
            scratch = root / "scratch"
            generated = worktree / "data" / "fw-version.json"
            generated.parent.mkdir(parents=True)
            scratch.mkdir()
            generated.write_text('{"version":"old"}\n', encoding="utf-8")
            subprocess.run(["git", "init", "-q", worktree], check=True)
            subprocess.run(
                ["git", "-C", worktree, "config", "user.email", "test@example.com"],
                check=True,
            )
            subprocess.run(
                ["git", "-C", worktree, "config", "user.name", "Test"],
                check=True,
            )
            subprocess.run(
                ["git", "-C", worktree, "add", "data/fw-version.json"],
                check=True,
            )
            subprocess.run(
                ["git", "-C", worktree, "commit", "-qm", "fixture"],
                check=True,
            )
            clean = subprocess.check_output(
                ["git", "describe", "--tags", "--always", "--long", "--dirty"],
                cwd=worktree,
                text=True,
            ).strip()

            with RUNTIME._temporary_git_identity_wrapper(
                worktree,
                scratch_directory=scratch,
                expected_descriptor=clean,
                expected_actual_descriptor=clean,
            ) as environment:
                generated.write_text('{"version":"generated"}\n', encoding="utf-8")
                described = subprocess.check_output(
                    ["git", "describe", "--tags", "--always", "--long", "--dirty"],
                    cwd=worktree,
                    env=environment,
                    text=True,
                ).strip()
                delegated_status = subprocess.check_output(
                    ["git", "status", "--porcelain"],
                    cwd=worktree,
                    env=environment,
                    text=True,
                ).strip()

                self.assertEqual(described, clean)
                self.assertIn("data/fw-version.json", delegated_status)

            self.assertFalse((scratch / "git-identity-bin").exists())


class PostRollbackRunTest(unittest.TestCase):
    def test_r1_build_environment_masks_only_generated_identity_churn(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            worktree = root / "repo"
            bundle_root = root / "bundle"
            generated = worktree / "data" / "fw-version.json"
            generated.parent.mkdir(parents=True)
            bundle_root.mkdir()
            generated.write_text('{"version":"old"}\n', encoding="utf-8")
            subprocess.run(["git", "init", "-q", worktree], check=True)
            subprocess.run(
                ["git", "-C", worktree, "config", "user.email", "test@example.com"],
                check=True,
            )
            subprocess.run(
                ["git", "-C", worktree, "config", "user.name", "Test"],
                check=True,
            )
            subprocess.run(
                ["git", "-C", worktree, "add", "data/fw-version.json"],
                check=True,
            )
            subprocess.run(
                ["git", "-C", worktree, "commit", "-qm", "fixture"],
                check=True,
            )
            report = {
                "plannerPlan": {
                    "run": {"role": "R"},
                    "paths": {"worktree": str(worktree)},
                },
            }
            bundle = SimpleNamespace(
                root=bundle_root,
                manifest={},
                events=[],
            )

            with RUNTIME._temporary_build_environment(
                report,
                bundle,
                before_event="firmware-build",
            ) as environment:
                generated.write_text('{"version":"generated"}\n', encoding="utf-8")
                described = subprocess.check_output(
                    ["git", "describe", "--tags", "--always", "--long", "--dirty"],
                    cwd=worktree,
                    env=environment,
                    text=True,
                ).strip()
                clean = subprocess.check_output(
                    ["git", "describe", "--tags", "--always", "--long"],
                    cwd=worktree,
                    text=True,
                ).strip()
                self.assertEqual(described, clean)

            records = bundle.manifest["temporaryBuildIdentityWrappers"]
            self.assertEqual(len(records), 1)
            self.assertTrue(records[0]["removed"])
            self.assertFalse((bundle_root / "git-identity-bin").exists())

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


class PingLossStopTest(unittest.TestCase):
    """ICMP is the least reliable liveness probe here, so it needs corroboration.

    The regression case is `test_single_dropped_echo_does_not_stop`: one lost
    reply used to end a run as "Unexpected controller failure" while HTTP kept
    serving and serial showed no panic or reset.
    """

    def _stop(self, **kwargs):
        base = dict(armed=True, success=False, loss_duration=99.0, status_recent=False)
        base.update(kwargs)
        return RUNTIME.should_stop_on_ping_loss(**base)

    def test_single_dropped_echo_does_not_stop(self):
        self.assertFalse(self._stop(loss_duration=RUNTIME.PING_INTERVAL_SECONDS))

    def test_sustained_loss_stops(self):
        self.assertTrue(self._stop(loss_duration=RUNTIME.PING_LOSS_STOP_SECONDS))

    def test_recent_http_success_vetoes_the_stop(self):
        self.assertFalse(self._stop(status_recent=True))

    def test_a_reply_never_stops(self):
        self.assertFalse(self._stop(success=True))

    def test_unarmed_never_stops(self):
        self.assertFalse(self._stop(armed=False))


if __name__ == "__main__":
    unittest.main()
