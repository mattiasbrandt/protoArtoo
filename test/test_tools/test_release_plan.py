"""Pin the release decision that tools/release_plan.py makes (issue #285).

This script decides, unattended, whether a merge to `main` publishes a release
and what version it carries. Nobody reviews that decision before it happens, so
the rules it applies are pinned here rather than left to a reading of the code.

Three regressions live in this file:

* `test_last_commit_in_range_is_not_lost` -- the log format delimits records
  with the ASCII separators \\x1e and \\x1f, and Python's str.strip() counts
  both as whitespace. Stripping git's output therefore ate the final record's
  terminators and dropped the oldest commit of every range, which would have
  silently under-reported a bump.
* `test_notes_exclude_the_version_sync_bot` -- version-sync.yml commits once
  per push to main, and 87 of the 174 non-fix commits between v1.2.0 and
  2026-09-11 were that bot. Listing them makes generated notes unreadable.
* `test_range_start_follows_to_not_head` -- `decide --to <ref>` measured the
  range from HEAD's last tag rather than from <ref>'s, so a preview run from a
  stale branch named a version that was already published.
"""

import json
import os
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parents[2] / "tools"))

import release_plan as rp  # noqa: E402

SCRIPT = Path(__file__).parents[2] / "tools" / "release_plan.py"

BOT = rp.VERSION_SYNC_BOT_EMAIL
HUMAN = "maker@example.invalid"


def _git(repo, *args, email=HUMAN):
    subprocess.check_output(
        ["git", "-c", f"user.email={email}", "-c", "user.name=t", *args],
        cwd=repo, stderr=subprocess.STDOUT,
    )


def _commit(repo, subject, body="", email=HUMAN):
    """Empty commit with a given subject/body, so tests read as history."""
    args = ["commit", "-q", "--allow-empty", "-m", subject]
    if body:
        args += ["-m", body]
    _git(repo, *args, email=email)


def _make_repo(tmp):
    """Scratch repo with one tagged base commit on main."""
    repo = Path(tmp)
    _git(repo, "init", "-q", "-b", "main")
    _commit(repo, "chore(test): base")
    _git(repo, "tag", "-a", "v1.2.0", "-m", "v1.2.0")
    return repo


def _run(repo, *args):
    """Run the script as a subprocess; return (returncode, stdout, stderr)."""
    proc = subprocess.run(
        [sys.executable, str(SCRIPT), "--repo", str(repo), *args],
        capture_output=True, text=True,
    )
    return proc.returncode, proc.stdout, proc.stderr


class ParseSubjectTest(unittest.TestCase):
    """CONTRIBUTING.md's commit format, as this script reads it."""

    def test_type_and_scope(self):
        parsed = rp.parse_subject("feat(drive): add the CH8 dial")
        self.assertEqual(parsed["type"], "feat")
        self.assertEqual(parsed["scope"], "drive")
        self.assertEqual(parsed["summary"], "add the CH8 dial")
        self.assertFalse(parsed["breaking"])

    def test_scope_is_optional(self):
        parsed = rp.parse_subject("fix: stop the thing")
        self.assertEqual(parsed["type"], "fix")
        self.assertIsNone(parsed["scope"])

    def test_bang_before_the_scope(self):
        # CONTRIBUTING.md spells it this way in its prose: `feat!(drive): ...`
        parsed = rp.parse_subject("feat!(drive): rename speed field to spd_raw")
        self.assertTrue(parsed["breaking"])

    def test_bang_after_the_scope(self):
        # ...and this way in its example block: `feat(hw)!: ...`. Both are the
        # project's own, so both are accepted.
        parsed = rp.parse_subject("feat(hw)!: confirm UART1 drive pins")
        self.assertTrue(parsed["breaking"])

    def test_non_conventional_subject_is_none(self):
        for subject in [
            "Merge A5 (#343): say which droid you built",
            "wip",
            "Revert \"feat(drive): add the CH8 dial\"",
        ]:
            with self.subTest(subject=subject):
                self.assertIsNone(rp.parse_subject(subject))


class BumpTest(unittest.TestCase):
    """The type-to-version-effect table from CONTRIBUTING.md."""

    @staticmethod
    def _commits(*subjects, body=""):
        return [{"subject": s, "body": body} for s in subjects]

    def test_no_commits_is_no_release(self):
        self.assertIsNone(rp.bump_for([]))

    def test_docs_and_chore_only_is_no_release(self):
        commits = self._commits(
            "docs(plan): record ADR 0047",
            "chore(ci): sync version JSON to abc1234",
            "refactor(web): move the handler",
            "test(test): add a mutation patch",
            "style(web): clang-format pass",
            "perf(drive): tighten the loop",
        )
        self.assertIsNone(rp.bump_for(commits))

    def test_fix_is_a_patch(self):
        self.assertEqual(rp.bump_for(self._commits("fix(web): stop it")), "patch")

    def test_feat_is_a_minor(self):
        self.assertEqual(rp.bump_for(self._commits("feat(web): start it")), "minor")

    def test_bang_is_a_major(self):
        self.assertEqual(rp.bump_for(self._commits("fix(nvs)!: rename a key")), "major")

    def test_breaking_change_footer_is_a_major(self):
        commits = [{
            "subject": "feat(drive): rename the speed field",
            "body": "BREAKING CHANGE: spd_raw replaces speed.\n",
        }]
        self.assertEqual(rp.bump_for(commits), "major")

    def test_breaking_change_hyphenated_footer_is_a_major(self):
        commits = [{
            "subject": "feat(drive): rename the speed field",
            "body": "BREAKING-CHANGE: spd_raw replaces speed.\n",
        }]
        self.assertEqual(rp.bump_for(commits), "major")

    def test_breaking_change_mid_sentence_is_not_a_footer(self):
        # The footer has to start its own line. Prose about a breaking change
        # must not silently publish a major release.
        commits = [{
            "subject": "docs(plan): explain what a BREAKING CHANGE: footer is",
            "body": "A BREAKING CHANGE: footer declares one.\n",
        }]
        self.assertIsNone(rp.bump_for(commits))

    def test_strongest_bump_in_the_range_wins(self):
        self.assertEqual(
            rp.bump_for(self._commits(
                "fix(web): stop it", "feat(web): start it", "docs(plan): note it",
            )),
            "minor",
        )


class VersionTest(unittest.TestCase):
    def test_parse_accepts_both_spellings(self):
        self.assertEqual(rp.parse_version("v1.2.3"), (1, 2, 3, None))
        self.assertEqual(rp.parse_version("1.2.3"), (1, 2, 3, None))

    def test_parse_keeps_the_prerelease_and_drops_build_metadata(self):
        self.assertEqual(rp.parse_version("v1.3.0-rc.1"), (1, 3, 0, "rc.1"))
        self.assertEqual(rp.parse_version("v1.3.0+epic-x"), (1, 3, 0, None))

    def test_parse_rejects_a_non_version(self):
        for tag in ["safepoint/asyncwebserver-2026-07-11", "v1.2", "banana"]:
            with self.subTest(tag=tag):
                with self.assertRaises(rp.ReleasePlanError):
                    rp.parse_version(tag)

    def test_next_version(self):
        self.assertEqual(rp.next_version("v1.2.0", "patch"), "1.2.1")
        self.assertEqual(rp.next_version("v1.2.3", "minor"), "1.3.0")
        self.assertEqual(rp.next_version("v1.2.3", "major"), "2.0.0")

    def test_next_version_drops_a_prerelease(self):
        self.assertEqual(rp.next_version("v1.0.0-alpha.1", "patch"), "1.0.1")


class TierTest(unittest.TestCase):
    """A patch release is the only kind with a non-zero Z."""

    def test_patch_tier(self):
        self.assertEqual(rp.tier_for_tag("v1.2.1"), "patch")
        self.assertEqual(rp.tier_for_tag("1.0.9"), "patch")

    def test_full_tier(self):
        self.assertEqual(rp.tier_for_tag("v1.3.0"), "full")
        self.assertEqual(rp.tier_for_tag("v2.0.0"), "full")

    def test_a_prerelease_keeps_its_core_tier(self):
        self.assertEqual(rp.tier_for_tag("v1.3.0-rc.1"), "full")
        self.assertEqual(rp.tier_for_tag("v1.3.1-rc.1"), "patch")


class GitRangeTest(unittest.TestCase):
    def test_last_commit_in_range_is_not_lost(self):
        """Regression: str.strip() treats \\x1e and \\x1f as whitespace.

        Stripping git log's output removed the final record's terminators, so
        the oldest commit in every range failed to parse. With a fix as that
        oldest commit, the range would have decided "no release".
        """
        with tempfile.TemporaryDirectory() as tmp:
            repo = _make_repo(tmp)
            # Oldest first: the fix is the one the bug used to drop, and it has
            # an empty body, which is what shortened the record.
            _commit(repo, "fix(web): the oldest commit in the range")
            _commit(repo, "docs(plan): a later commit", body="With a body.\n")
            commits = rp.commits_in(repo, "v1.2.0", "HEAD")
            subjects = [c["subject"] for c in commits]
            self.assertEqual(len(commits), 2, subjects)
            self.assertIn("fix(web): the oldest commit in the range", subjects)
            self.assertEqual(rp.bump_for(commits), "patch")

    def test_bodies_survive_their_blank_lines(self):
        with tempfile.TemporaryDirectory() as tmp:
            repo = _make_repo(tmp)
            _commit(
                repo,
                "feat(drive): rename the speed field",
                body="Why it changed.\n\nBREAKING CHANGE: spd_raw replaces speed.\n",
            )
            commits = rp.commits_in(repo, "v1.2.0", "HEAD")
            self.assertEqual(rp.bump_for(commits), "major")

    def test_latest_release_tag_ignores_non_release_tags(self):
        """A safepoint marker is not a version, here or in extract_version.py."""
        with tempfile.TemporaryDirectory() as tmp:
            repo = _make_repo(tmp)
            _commit(repo, "fix(web): something")
            _git(repo, "tag", "safepoint/scratch-2026-09-11")
            self.assertEqual(rp.latest_release_tag(repo), "v1.2.0")

    def test_latest_full_release_tag_skips_patches(self):
        with tempfile.TemporaryDirectory() as tmp:
            repo = _make_repo(tmp)
            _commit(repo, "fix(web): something")
            _git(repo, "tag", "-a", "v1.2.1", "-m", "v1.2.1")
            self.assertEqual(rp.latest_full_release_tag(repo), "v1.2.0")


class DecideCliTest(unittest.TestCase):
    def test_docs_only_range_decides_no_release_and_exits_green(self):
        """Acceptance criterion: a docs/chore-only merge is not a failed run."""
        with tempfile.TemporaryDirectory() as tmp:
            repo = _make_repo(tmp)
            _commit(repo, "docs(plan): write something down")
            _commit(repo, "chore(ci): sync version JSON to abc1234", email=BOT)
            code, out, err = _run(repo, "decide")
            self.assertEqual(code, 0, err)
            plan = json.loads(out)
            self.assertEqual(plan["bump"], "none")
            self.assertEqual(plan["next"], "")
            self.assertEqual(plan["tag"], "")

    def test_a_fix_decides_a_patch_release(self):
        with tempfile.TemporaryDirectory() as tmp:
            repo = _make_repo(tmp)
            _commit(repo, "fix(web): stop the thing")
            code, out, err = _run(repo, "decide")
            self.assertEqual(code, 0, err)
            plan = json.loads(out)
            self.assertEqual(plan["bump"], "patch")
            self.assertEqual(plan["tag"], "v1.2.1")
            self.assertEqual(plan["tier"], "patch")

    def test_a_feat_decides_a_full_release(self):
        with tempfile.TemporaryDirectory() as tmp:
            repo = _make_repo(tmp)
            _commit(repo, "feat(web): add the thing")
            code, out, err = _run(repo, "decide")
            self.assertEqual(code, 0, err)
            plan = json.loads(out)
            self.assertEqual(plan["bump"], "minor")
            self.assertEqual(plan["tag"], "v1.3.0")
            self.assertEqual(plan["tier"], "full")

    def test_decide_writes_github_outputs(self):
        with tempfile.TemporaryDirectory() as tmp:
            repo = _make_repo(tmp)
            _commit(repo, "fix(web): stop the thing")
            output = Path(tmp) / "gh-output"
            output.write_text("")
            env = dict(os.environ, GITHUB_OUTPUT=str(output))
            proc = subprocess.run(
                [sys.executable, str(SCRIPT), "--repo", str(repo),
                 "decide", "--github-output"],
                capture_output=True, text=True, env=env,
            )
            self.assertEqual(proc.returncode, 0, proc.stderr)
            written = dict(
                line.split("=", 1) for line in output.read_text().splitlines() if line
            )
            self.assertEqual(written["bump"], "patch")
            self.assertEqual(written["tag"], "v1.2.1")
            self.assertEqual(written["tier"], "patch")

    def test_range_start_follows_to_not_head(self):
        """Regression: `--to <ref>` measured the range from HEAD's last tag.

        Previewing a release for another ref from a stale branch reported a
        version that was already published -- observed on 2026-09-11 against
        origin/main from a pre-rebase branch, which answered v1.2.1 when
        v1.2.1 already existed. CI never hit it (it checks out main and lets
        --to default to HEAD), which is exactly why it needed a test.
        """
        with tempfile.TemporaryDirectory() as tmp:
            repo = _make_repo(tmp)
            # A released line: v1.2.1 exists and carries the fix.
            _commit(repo, "fix(web): the released fix")
            _git(repo, "tag", "-a", "v1.2.1", "-m", "v1.2.1")
            _git(repo, "branch", "released")
            # A stale side branch that forked before v1.2.1 was cut.
            _git(repo, "checkout", "-q", "-b", "stale", "v1.2.0")
            _commit(repo, "docs(plan): something on the side")

            # HEAD is `stale`, whose last tag is v1.2.0; the answer must come
            # from the ref being asked about, not from where we are standing.
            code, out, err = _run(repo, "decide", "--to", "released")
            self.assertEqual(code, 0, err)
            plan = json.loads(out)
            self.assertEqual(plan["current"], "v1.2.1")
            self.assertEqual(plan["bump"], "none")
            self.assertEqual(plan["commits"], 0)

    def test_no_release_tag_at_all_is_a_clear_failure(self):
        with tempfile.TemporaryDirectory() as tmp:
            repo = Path(tmp)
            _git(repo, "init", "-q", "-b", "main")
            _commit(repo, "fix(web): stop the thing")
            code, _, err = _run(repo, "decide")
            self.assertEqual(code, 1)
            self.assertIn("no release tag", err)


class NotesTest(unittest.TestCase):
    def _notes(self, repo, tag="v1.2.1"):
        code, out, err = _run(repo, "notes", tag)
        self.assertEqual(code, 0, err)
        return out

    def test_notes_exclude_the_version_sync_bot(self):
        """Regression: the bot commits once per push and drowns the notes."""
        with tempfile.TemporaryDirectory() as tmp:
            repo = _make_repo(tmp)
            _commit(repo, "fix(web): stop the thing")
            _commit(repo, "chore(ci): sync version JSON to abc1234", email=BOT)
            _git(repo, "tag", "-a", "v1.2.1", "-m", "v1.2.1")
            notes = self._notes(repo)
            self.assertIn("stop the thing", notes)
            self.assertNotIn("sync version JSON", notes)

    def test_notes_list_fixes_under_their_own_heading(self):
        with tempfile.TemporaryDirectory() as tmp:
            repo = _make_repo(tmp)
            _commit(repo, "fix(web): stop the thing")
            _commit(repo, "docs(plan): write it down")
            _git(repo, "tag", "-a", "v1.2.1", "-m", "v1.2.1")
            notes = self._notes(repo)
            self.assertIn("### Fixed", notes)
            self.assertIn("- `fix(web)` stop the thing", notes)
            # Everything that is not a fix is present but folded away.
            self.assertIn("<details>", notes)
            self.assertIn("write it down", notes)

    def test_notes_say_where_the_images_are(self):
        """An empty release reads as a broken one unless the notes explain."""
        with tempfile.TemporaryDirectory() as tmp:
            repo = _make_repo(tmp)
            _commit(repo, "fix(web): stop the thing")
            _git(repo, "tag", "-a", "v1.2.1", "-m", "v1.2.1")
            notes = self._notes(repo)
            self.assertIn("no files to download here", notes)
            self.assertIn("v1.2.0", notes)

    def test_notes_do_not_claim_the_old_images_carry_this_fix(self):
        with tempfile.TemporaryDirectory() as tmp:
            repo = _make_repo(tmp)
            _commit(repo, "fix(web): stop the thing")
            _git(repo, "tag", "-a", "v1.2.1", "-m", "v1.2.1")
            notes = self._notes(repo)
            self.assertIn("built before this fix", notes)


if __name__ == "__main__":
    unittest.main()
