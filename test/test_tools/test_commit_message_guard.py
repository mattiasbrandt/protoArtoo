"""The commit message guard enforces the format in this repo only.

Runs the hook as Claude Code does: JSON on stdin, CLAUDE_PROJECT_DIR in the
environment, a deny decision on stdout or nothing.
"""

import json
import os
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

HOOK = Path(__file__).resolve().parents[2] / ".claude" / "hooks" / "commit_message_guard.py"
GOOD = 'docs(plan): record the decision'
BAD = 'claude: another repo convention'


def _git(*args, cwd):
    subprocess.run(["git", *args], cwd=cwd, check=True, capture_output=True)


def _init_repo(path: Path) -> None:
    path.mkdir(parents=True)
    _git("init", "-q", cwd=path)
    _git("-c", "user.name=t", "-c", "user.email=t@t", "commit", "-q", "--allow-empty", "-m", "init", cwd=path)


class CommitMessageGuard(unittest.TestCase):
    def setUp(self):
        self._tmp = tempfile.TemporaryDirectory()
        root = Path(self._tmp.name)
        self.project = root / "project"
        self.other = root / "other"
        self.worktree = root / "wt-1"
        _init_repo(self.project)
        _init_repo(self.other)
        _git("worktree", "add", "-q", str(self.worktree), cwd=self.project)

    def tearDown(self):
        self._tmp.cleanup()

    def decide(self, command, cwd=None, project_dir="project"):
        env = dict(os.environ)
        env.pop("CLAUDE_PROJECT_DIR", None)
        if project_dir:
            env["CLAUDE_PROJECT_DIR"] = str(self.project)
        payload = {"tool_name": "Bash", "tool_input": {"command": command},
                   "cwd": str(cwd or self.project)}
        r = subprocess.run([sys.executable, str(HOOK)], input=json.dumps(payload),
                           capture_output=True, text=True, env=env, check=True)
        if not r.stdout.strip():
            return "allow"
        return json.loads(r.stdout)["hookSpecificOutput"]["permissionDecision"]

    # This repo: the format rule holds exactly as before.
    def test_project_commit_keeps_the_format_rule(self):
        self.assertEqual(self.decide(f'git commit -m "{GOOD}"'), "allow")
        self.assertEqual(self.decide(f'git commit -m "{BAD}"'), "deny")

    def test_project_worktree_keeps_the_format_rule(self):
        self.assertEqual(self.decide(f'cd {self.worktree} && git commit -m "{BAD}"'), "deny")
        self.assertEqual(self.decide(f'git commit -m "{BAD}"', cwd=self.worktree), "deny")

    def test_project_named_by_git_dash_c_keeps_the_format_rule(self):
        self.assertEqual(self.decide(f'git -C {self.project} commit -m "{BAD}"', cwd=self.other), "deny")

    def test_unparseable_message_is_still_denied_here(self):
        self.assertEqual(self.decide("git commit -F msg.txt"), "deny")

    # Another repo: its own convention passes.
    def test_other_repo_by_cd_is_not_held_to_this_format(self):
        self.assertEqual(self.decide(f'cd {self.other} && git commit -m "{BAD}"'), "allow")
        self.assertEqual(self.decide(f'(cd ../other; git commit -m "{BAD}")'), "allow")

    def test_other_repo_by_git_dash_c_is_not_held_to_this_format(self):
        self.assertEqual(self.decide(f'git -C {self.other} commit -m "{BAD}"'), "allow")
        self.assertEqual(self.decide(f'git -C ../other commit -m "{BAD}"'), "allow")

    def test_other_repo_as_session_cwd_is_not_held_to_this_format(self):
        self.assertEqual(self.decide(f'git commit -m "{BAD}"', cwd=self.other), "allow")

    def test_co_author_is_denied_in_every_repo(self):
        trailer = "Co-Authored-By: Someone <x@y>"
        self.assertEqual(self.decide(f'git -C {self.other} commit -m "{BAD}\n\n{trailer}"'), "deny")
        self.assertEqual(self.decide(f'git commit -m "{GOOD}\n\n{trailer}"'), "deny")

    # Anything the hook cannot resolve falls back to enforcing.
    def test_unresolved_target_keeps_the_format_rule(self):
        self.assertEqual(self.decide(f'cd $NO_SUCH_VAR_X/other && git commit -m "{BAD}"'), "deny")
        self.assertEqual(self.decide(f'git -C /no/such/dir commit -m "{BAD}"'), "deny")

    def test_without_a_project_dir_the_format_rule_applies_everywhere(self):
        self.assertEqual(self.decide(f'git -C {self.other} commit -m "{BAD}"', project_dir=None), "deny")

    def test_non_commit_commands_pass(self):
        self.assertEqual(self.decide("git status"), "allow")

    def test_short_cluster_ending_in_m_is_a_message(self):
        self.assertEqual(self.decide(f'git commit -qam "{GOOD}"'), "allow")
        self.assertEqual(self.decide(f'git commit -qam "{BAD}"'), "deny")

    def test_no_edit_needs_a_merge_in_progress(self):
        self.assertEqual(self.decide("git commit --no-edit"), "deny")
        _git("config", "user.email", "t@t", cwd=self.project)
        _git("config", "user.name", "t", cwd=self.project)
        side = self.project / "side.txt"
        mainf = self.project / "main.txt"
        side.write_text("side\n")
        _git("checkout", "-q", "-b", "side", cwd=self.project)
        _git("add", "side.txt", cwd=self.project)
        _git("commit", "-q", "-m", "side", cwd=self.project)
        _git("checkout", "-q", "-", cwd=self.project)
        mainf.write_text("main\n")
        _git("add", "main.txt", cwd=self.project)
        _git("commit", "-q", "-m", "main", cwd=self.project)
        subprocess.run(
            ["git", "merge", "--no-commit", "side"],
            cwd=self.project, check=True, capture_output=True,
        )
        self.assertEqual(self.decide("git commit --no-edit"), "allow")


if __name__ == "__main__":
    unittest.main()
