"""Pinned behavior for the epic worktree setup tool.

The tool exists because `gh issue develop` branches from the REMOTE ref while an
epic's integration branch advances locally, so every worktree it makes starts at
the wrong commit. Two things must not regress: the verdict has to fail on a
mismatch rather than warn about it, and the unpushed gap has to be reported on
every run so the divergence stays visible instead of becoming background noise.

Run via `make test-tools` or `python3 -m unittest discover -s test/test_tools`.
"""

import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "tools"))

import epic_worktree  # noqa: E402


class Verdict(unittest.TestCase):
    def test_matching_head_passes(self):
        ok, line = epic_worktree.verdict("a" * 40, "a" * 40)
        self.assertTrue(ok)
        self.assertIn("OK", line)

    def test_mismatched_head_fails_rather_than_warns(self):
        ok, line = epic_worktree.verdict("a" * 40, "b" * 40)
        self.assertFalse(ok)
        self.assertIn("FAIL", line)
        # Both shas belong in the line: the reader has to see which is which.
        self.assertIn("aaaaaaaa", line)
        self.assertIn("bbbbbbbb", line)


class DivergenceNote(unittest.TestCase):
    def test_behind_says_how_far_and_why_it_matters(self):
        note = epic_worktree.divergence_note("epic/x", 2)
        self.assertIn("2 commits behind", note)

    def test_one_commit_is_singular(self):
        self.assertIn("1 commit behind", epic_worktree.divergence_note("epic/x", 1))

    def test_level_says_gh_would_have_been_right(self):
        note = epic_worktree.divergence_note("epic/x", 0)
        self.assertIn("level", note)

    def test_absent_remote_is_reported_not_crashed_on(self):
        self.assertIn("no epic/x", epic_worktree.divergence_note("epic/x", None))


class CountAheadAgainstRealGit(unittest.TestCase):
    """The count is the evidence the note is built from, so drive real git."""

    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.repo = Path(self.tmp.name)
        run = lambda *a: subprocess.run(a, cwd=self.repo, check=True, capture_output=True)
        run("git", "init", "-q", "-b", "main")
        run("git", "config", "user.email", "t@example.invalid")
        run("git", "config", "user.name", "t")
        (self.repo / "f").write_text("1\n")
        run("git", "add", "f")
        run("git", "commit", "-qm", "one")
        # Stand in for the remote-tracking ref without needing a remote.
        run("git", "branch", "origin/main")
        (self.repo / "f").write_text("2\n")
        run("git", "commit", "-qam", "two")

    def tearDown(self):
        self.tmp.cleanup()

    def test_counts_the_commits_the_remote_lacks(self):
        self.assertEqual(
            epic_worktree.count_ahead("main", "origin/main", cwd=self.repo), 1
        )

    def test_missing_ref_returns_none_rather_than_raising(self):
        self.assertIsNone(
            epic_worktree.count_ahead("main", "origin/nope", cwd=self.repo)
        )


class SyncBaseAgainstRealGit(unittest.TestCase):
    """origin ahead of the local base: a real remote, a real second pusher."""

    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        top = Path(self.tmp.name)
        self.origin, self.mine, self.theirs = top / "origin.git", top / "mine", top / "theirs"
        self.git(top, "init", "-q", "--bare", "-b", "epic/x", str(self.origin))
        self.git(top, "clone", "-q", str(self.origin), str(self.mine))
        self.commit(self.mine, "one")
        self.git(self.mine, "push", "-q", "origin", "epic/x")
        self.git(top, "clone", "-q", str(self.origin), str(self.theirs))

    def tearDown(self):
        self.tmp.cleanup()

    def git(self, cwd, *args):
        return subprocess.run(["git", "-c", "user.email=t@example.invalid", "-c", "user.name=t", *args],
                              cwd=cwd, check=True, capture_output=True, text=True).stdout.strip()

    def commit(self, repo, name):
        (repo / name).write_text(name)
        self.git(repo, "add", name)
        self.git(repo, "commit", "-qm", name)

    def push_from_another_session(self):
        self.commit(self.theirs, "theirs")
        self.git(self.theirs, "push", "-q", "origin", "epic/x")
        return self.git(self.theirs, "rev-parse", "HEAD")

    def test_a_clean_checkout_is_fast_forwarded_to_the_remote_tip(self):
        tip = self.push_from_another_session()
        ok, line = epic_worktree.sync_base("epic/x", cwd=self.mine)
        self.assertTrue(ok, line)
        self.assertIn("fast-forwarded epic/x by 1 commit", line)
        self.assertEqual(self.git(self.mine, "rev-parse", "epic/x"), tip)

    def test_a_dirty_checkout_is_refused_with_the_command(self):
        before = self.git(self.mine, "rev-parse", "epic/x")
        self.push_from_another_session()
        (self.mine / "one").write_text("edited")
        ok, line = epic_worktree.sync_base("epic/x", cwd=self.mine)
        self.assertFalse(ok)
        self.assertIn("merge --ff-only origin/epic/x", line)
        self.assertEqual(self.git(self.mine, "rev-parse", "epic/x"), before)

    def test_a_base_checked_out_nowhere_moves_by_ref(self):
        tip = self.push_from_another_session()
        self.git(self.mine, "checkout", "-q", "--detach")
        ok, line = epic_worktree.sync_base("epic/x", cwd=self.mine)
        self.assertTrue(ok, line)
        self.assertIn("checked out nowhere", line)
        self.assertEqual(self.git(self.mine, "rev-parse", "epic/x"), tip)

    def test_diverged_is_refused(self):
        self.push_from_another_session()
        self.commit(self.mine, "mine")
        ok, line = epic_worktree.sync_base("epic/x", cwd=self.mine)
        self.assertFalse(ok)
        self.assertIn("diverged (1 local-only, 1 remote-only", line)

    def test_level_or_local_ahead_changes_nothing(self):
        self.commit(self.mine, "mine")
        before = self.git(self.mine, "rev-parse", "epic/x")
        ok, line = epic_worktree.sync_base("epic/x", cwd=self.mine)
        self.assertTrue(ok, line)
        self.assertEqual(self.git(self.mine, "rev-parse", "epic/x"), before)

    def test_dry_run_moves_nothing(self):
        before = self.git(self.mine, "rev-parse", "epic/x")
        self.push_from_another_session()
        ok, line = epic_worktree.sync_base("epic/x", cwd=self.mine, dry_run=True)
        self.assertTrue(ok, line)
        self.assertIn("dry run", line)
        self.assertEqual(self.git(self.mine, "rev-parse", "epic/x"), before)


class Cli(unittest.TestCase):
    def test_create_requires_a_branch_name(self):
        self.assertEqual(epic_worktree.main(["338", "--base", "epic/x"]), 1)

    def test_push_is_the_default(self):
        args = epic_worktree.build_parser().parse_args(
            ["338", "--base", "epic/x", "--name", "feat/338-x"]
        )
        self.assertTrue(args.push)

    def test_default_path_follows_the_wt_convention(self):
        self.assertEqual(epic_worktree.default_path(338), "../wt-338")


if __name__ == "__main__":
    unittest.main()
