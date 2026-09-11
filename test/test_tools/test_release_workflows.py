"""Pin the release workflows' loop safety and tier wiring (issue #285).

A merge to `main` now tags itself, and a deploy-key push DOES trigger
workflows. That makes a release loop a live possibility rather than a
theoretical one, and a self-sustaining loop here would be public: tags,
releases and notifications, on a repeat. The ticket asked for that safety to
be demonstrated rather than argued, so the guards are asserted here instead of
being left to a reading of the YAML.

The three properties that together close the loop:

1. `auto-release.yml` triggers on a push to `main` and on nothing else, so the
   tag it pushes cannot re-enter it -- a tag is not a branch.
2. Its job skips any commit authored by github-actions[bot], which is both
   `version-sync.yml`'s version-JSON commit and `auto-release.yml`'s own
   CHANGELOG promotion. All three workflows must spell that bot identically,
   so the address is compared across the files rather than trusted.
3. `release.yml` triggers only on a tag and pushes nothing back to `main`.

The tier wiring is asserted for the same reason: `build` is skipped for a
patch tag, and in GitHub Actions a skipped dependency skips its dependants
unless the dependant says otherwise. Losing that `if` would silently stop
publishing patch releases.
"""

import re
import unittest
from pathlib import Path

import yaml

ROOT = Path(__file__).parents[2]
WORKFLOWS = ROOT / ".github" / "workflows"

BOT_EMAIL = "41898282+github-actions[bot]@users.noreply.github.com"


def _load(name):
    return yaml.safe_load((WORKFLOWS / name).read_text())


def _triggers(workflow):
    """The `on:` block. YAML 1.1 reads a bare `on` as the boolean True, which
    is why this is not simply workflow["on"]."""
    return workflow.get("on", workflow.get(True))


class AutoReleaseTriggerTest(unittest.TestCase):
    def setUp(self):
        self.workflow = _load("auto-release.yml")
        self.triggers = _triggers(self.workflow)

    def test_triggers_on_a_push_to_main_only(self):
        self.assertEqual(list(self.triggers), ["push"])
        self.assertEqual(self.triggers["push"]["branches"], ["main"])

    def test_does_not_trigger_on_a_tag(self):
        """The tag this workflow pushes must not re-enter it."""
        self.assertNotIn("tags", self.triggers["push"])

    def test_has_no_path_filter(self):
        """A docs-only merge must still run and decide "no release".

        Filtering it out would look equivalent, but the range is computed from
        the last release tag, and a skipped run is one that never reports.
        """
        self.assertNotIn("paths", self.triggers["push"])
        self.assertNotIn("paths-ignore", self.triggers["push"])

    def test_serialises_and_never_cancels(self):
        concurrency = self.workflow["concurrency"]
        self.assertFalse(concurrency["cancel-in-progress"])

    def test_the_job_skips_bot_authored_commits(self):
        job = self.workflow["jobs"]["release"]
        self.assertIn(BOT_EMAIL, job["if"])
        self.assertIn("head_commit.author.email", job["if"])
        self.assertIn("!=", job["if"])


class BotIdentityTest(unittest.TestCase):
    """Every guard must name the same bot, or one of them stops guarding."""

    def test_version_sync_and_auto_release_agree(self):
        auto = _load("auto-release.yml")["jobs"]["release"]["if"]
        sync = _load("version-sync.yml")["jobs"]["sync-version"]["if"]
        self.assertIn(BOT_EMAIL, auto)
        self.assertIn(BOT_EMAIL, sync)

    def test_verification_agrees(self):
        guard = _load("verification.yml")["jobs"]["verification"]["if"]
        self.assertIn(BOT_EMAIL, guard)

    def test_auto_release_commits_as_that_same_bot(self):
        """The commit it makes must be the commit its own guard skips."""
        text = (WORKFLOWS / "auto-release.yml").read_text()
        self.assertIn(f'git config user.email "{BOT_EMAIL}"', text)

    def test_release_plan_agrees(self):
        """The notes filter names the same bot as the workflow guards."""
        import sys
        sys.path.insert(0, str(ROOT / "tools"))
        import release_plan

        self.assertEqual(release_plan.VERSION_SYNC_BOT_EMAIL, BOT_EMAIL)


class ReleaseWorkflowTest(unittest.TestCase):
    def setUp(self):
        self.workflow = _load("release.yml")
        self.jobs = self.workflow["jobs"]

    def test_triggers_on_a_tag_only(self):
        triggers = _triggers(self.workflow)
        self.assertEqual(list(triggers), ["push"])
        self.assertIn("tags", triggers["push"])
        self.assertNotIn("branches", triggers["push"])

    def test_pushes_nothing(self):
        """Closing the loop: a release must not write back to main."""
        text = (WORKFLOWS / "release.yml").read_text()
        self.assertIsNone(re.search(r"^\s*git push", text, re.MULTILINE), text)

    def test_builds_run_for_a_full_release_only(self):
        self.assertEqual(self.jobs["build"]["needs"], "classify")
        self.assertIn("tier == 'full'", self.jobs["build"]["if"])

    def test_publish_survives_a_skipped_build(self):
        """A patch tag skips `build`, and a skipped need skips its dependants
        by default -- so publish has to state the condition itself."""
        publish = self.jobs["publish"]
        self.assertEqual(publish["needs"], ["classify", "build"])
        self.assertIn("always()", publish["if"])
        self.assertIn("needs.build.result == 'skipped'", publish["if"])
        self.assertIn("needs.classify.result == 'success'", publish["if"])

    def test_publish_does_not_run_on_a_failed_build(self):
        publish_if = self.jobs["publish"]["if"]
        self.assertNotIn("needs.build.result == 'failure'", publish_if)
        self.assertIn("needs.build.result == 'success'", publish_if)

    def test_each_tier_takes_its_own_notes_path(self):
        steps = {step.get("name"): step for step in self.jobs["publish"]["steps"]}
        curated = steps["📝 Extract CHANGELOG section for this tag"]
        generated = steps["📝 Generate notes for this patch tag"]
        self.assertIn("tier == 'full'", curated["if"])
        self.assertIn("tier == 'patch'", generated["if"])
        self.assertIn("extract_changelog_section.py", curated["run"])
        self.assertIn("release_plan.py notes", generated["run"])

    def test_images_and_checksums_are_full_tier_only(self):
        steps = {step.get("name"): step for step in self.jobs["publish"]["steps"]}
        for name in [
            "⬇️ Download all build artifacts",
            "🔏 Generate SHA256 checksums",
        ]:
            with self.subTest(step=name):
                self.assertIn("tier == 'full'", steps[name]["if"])

    def test_publish_checks_out_full_history(self):
        """Generated patch notes read a commit range; a shallow clone has none."""
        steps = self.jobs["publish"]["steps"]
        checkout = next(s for s in steps if str(s.get("uses", "")).startswith("actions/checkout"))
        self.assertEqual(checkout["with"]["fetch-depth"], 0)


if __name__ == "__main__":
    unittest.main()
