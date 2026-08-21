#!/usr/bin/env python3
import json
import os
import subprocess
import sys
import unittest
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[2]
HOOK = REPO_ROOT / ".claude" / "hooks" / "post_tool_failure_diagnostics.py"


class PostToolFailureDiagnosticsHookTests(unittest.TestCase):
    def run_hook(self, payload, extra_env=None):
        env = os.environ.copy()
        if extra_env:
            env.update(extra_env)

        proc = subprocess.run(
            [sys.executable, str(HOOK)],
            input=json.dumps(payload),
            text=True,
            capture_output=True,
            env=env,
            check=False,
        )
        return proc

    def test_permission_denied_bash_emits_structured_packet(self):
        payload = {
            "hook_event_name": "PostToolUseFailure",
            "tool_name": "Bash",
            "tool_input": {"command": "pio run -e protoArtoo -t upload"},
            "error": "Permission denied by project policy",
        }
        proc = self.run_hook(payload)
        self.assertEqual(proc.returncode, 0)
        self.assertTrue(proc.stdout.strip())

        out = json.loads(proc.stdout)
        context = out["hookSpecificOutput"]["additionalContext"]
        self.assertIn("Tool failure diagnostics [permission_denied]", context)
        self.assertIn("Failed tool call: Bash", context)

    def test_playwright_schema_crash_emits_fallback_guidance(self):
        payload = {
            "hook_event_name": "PostToolUseFailure",
            "tool_name": "mcp__playwright__browser_navigate",
            "tool_input": {"url": "http://127.0.0.1:4173/sound.html"},
            "error": "Failed to compile JSON schema for validation: no schema with key or ref",
        }
        proc = self.run_hook(payload)
        self.assertEqual(proc.returncode, 0)
        out = json.loads(proc.stdout)
        context = out["hookSpecificOutput"]["additionalContext"]
        self.assertIn("mcp_tooling_crash", context)
        self.assertIn("switch immediately to script-based fallback", context)

    def test_minimal_profile_is_quiet(self):
        payload = {
            "hook_event_name": "PostToolUseFailure",
            "tool_name": "Bash",
            "tool_input": {"command": "pio run -e protoArtoo -t upload"},
            "error": "Permission denied",
        }
        proc = self.run_hook(payload, extra_env={"PROTOARTOO_HOOK_PROFILE": "minimal"})
        self.assertEqual(proc.returncode, 0)
        self.assertEqual(proc.stdout.strip(), "")


if __name__ == "__main__":
    unittest.main()
