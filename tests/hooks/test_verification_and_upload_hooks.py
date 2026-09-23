#!/usr/bin/env python3
import importlib.util
import json
import subprocess
import sys
import unittest
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[2]
HOOKS = REPO_ROOT / ".claude" / "hooks"


def load_hook(name):
    spec = importlib.util.spec_from_file_location(name, HOOKS / f"{name}.py")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def run_hook(name, payload):
    return subprocess.run(
        [sys.executable, str(HOOKS / f"{name}.py")],
        input=json.dumps(payload),
        text=True,
        capture_output=True,
        check=False,
    )


class BackendVerificationTrackerTests(unittest.TestCase):
    def setUp(self):
        self.classify = load_hook("backend_verification_tracker")._classify_command

    def test_make_targets_count_as_the_checks_they_run(self):
        self.assertEqual(self.classify("make build"), "firmware_build")
        self.assertEqual(self.classify("make build BUILD_ENV=artoo_esp32"), "firmware_build")
        self.assertEqual(self.classify("make test"), "native_tests")
        self.assertEqual(self.classify("make check"), "static_check")

    def test_other_envs_and_neighbouring_targets_do_not_count(self):
        self.assertEqual(self.classify("make build BUILD_ENV=firebeetle2"), "")
        self.assertEqual(self.classify("make build BUILD_ENV=artoo_esp32_chirp"), "")
        self.assertEqual(self.classify("make test-web"), "")
        self.assertEqual(self.classify("make check-action-drift"), "")

    def test_locked_pio_build_still_counts(self):
        self.assertEqual(
            self.classify("python3 tools/pio_lock.py pio run -e artoo_esp32"), "firmware_build"
        )


class PreUploadGuardTests(unittest.TestCase):
    def decision(self, command):
        proc = run_hook("pre_upload_guard", {"tool_name": "Bash", "tool_input": {"command": command}})
        self.assertEqual(proc.returncode, 0)
        if not proc.stdout.strip():
            return None
        return json.loads(proc.stdout)["hookSpecificOutput"]

    def test_make_upload_targets_ask_first(self):
        for command, kind in (
            ("make flash UPLOAD_PORT=/dev/ttyUSB0", "USB firmware upload"),
            ("make flash-monitor", "USB firmware upload"),
            ("make ota OTA_IP=artoo.local", "OTA firmware upload"),
            ("make BUILD_ENV=artoo_esp32_chirp ota-chirp", "OTA firmware upload"),
            ("make uploadfs", "UploadFS"),
        ):
            with self.subTest(command=command):
                out = self.decision(command)
                self.assertEqual(out["permissionDecision"], "ask")
                self.assertIn(kind, out["permissionDecisionReason"])

    def test_non_upload_make_targets_pass_silently(self):
        for command in ("make build", "make test", "make check-build-budgets", "make monitor"):
            with self.subTest(command=command):
                self.assertIsNone(self.decision(command))


class PostToolFailureDiagnosticsQuietTests(unittest.TestCase):
    def test_ordinary_nonzero_exit_emits_nothing(self):
        proc = run_hook(
            "post_tool_failure_diagnostics",
            {
                "hook_event_name": "PostToolUseFailure",
                "tool_name": "Bash",
                "tool_input": {"command": "git grep -n missing_symbol"},
                "error": "Exit code 1",
            },
        )
        self.assertEqual(proc.returncode, 0)
        self.assertEqual(proc.stdout.strip(), "")


if __name__ == "__main__":
    unittest.main()
