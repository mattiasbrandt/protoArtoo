"""Pin how tools/promote_changelog.py turns [Unreleased] into a release (#285).

auto-release.yml runs this unattended between a merge and a tag, so the two
behaviours the release flow leans on are pinned here:

* an empty [Unreleased] fails the release rather than publishing a bare
  heading -- the ticket's acceptance criterion;
* promoting twice is a no-op, so a run that dies between the CHANGELOG push
  and the tag push is fixed by re-running rather than wedging main.

The last case checks the contract with tools/extract_changelog_section.py:
release.yml reads the promoted section back, and a heading these two spell
differently would publish empty notes.
"""

import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

TOOLS = Path(__file__).parents[2] / "tools"
sys.path.insert(0, str(TOOLS))

import extract_changelog_section as extract  # noqa: E402
import promote_changelog as promote  # noqa: E402

SCRIPT = TOOLS / "promote_changelog.py"

HEADER = """# Changelog

All notable changes to `protoArtoo` are documented here.

"""

RELEASED = """## [1.2.0] - 2026-09-06

A command console for the droid.

### Added
- The Controller Console.
"""


def _changelog(unreleased_body=""):
    body = f"\n{unreleased_body}\n" if unreleased_body else "\n"
    return f"{HEADER}## [Unreleased]\n{body}\n{RELEASED}"


def _run(path, *args):
    proc = subprocess.run(
        [sys.executable, str(SCRIPT), *args, "--path", str(path)],
        capture_output=True, text=True,
    )
    return proc.returncode, proc.stdout, proc.stderr


class PromoteTest(unittest.TestCase):
    def test_empty_unreleased_refuses(self):
        """A feature reached main with nothing written about it: stop."""
        with self.assertRaises(promote.PromoteError) as caught:
            promote.promote(_changelog(), "1.3.0", "2026-09-11")
        self.assertIn("is empty", str(caught.exception))

    def test_whitespace_only_unreleased_refuses(self):
        with self.assertRaises(promote.PromoteError):
            promote.promote(_changelog("   \n\n  "), "1.3.0", "2026-09-11")

    def test_missing_unreleased_heading_refuses(self):
        with self.assertRaises(promote.PromoteError) as caught:
            promote.promote(f"{HEADER}{RELEASED}", "1.3.0", "2026-09-11")
        self.assertIn("no ## [Unreleased]", str(caught.exception))

    def test_promotion_moves_the_body_under_the_new_heading(self):
        content = _changelog("### Added\n- A thing a builder can see.")
        result = promote.promote(content, "1.3.0", "2026-09-11")
        self.assertIn("## [Unreleased]\n\n## [1.3.0] - 2026-09-11\n", result)
        self.assertIn("- A thing a builder can see.", result)
        # The new section sits above the previous release, not below it.
        self.assertLess(result.index("## [1.3.0]"), result.index("## [1.2.0]"))

    def test_promotion_leaves_a_fresh_empty_unreleased(self):
        content = _changelog("### Added\n- A thing.")
        result = promote.promote(content, "1.3.0", "2026-09-11")
        # Promoting the result again must refuse: the new Unreleased is empty.
        with self.assertRaises(promote.PromoteError):
            promote.promote(result, "1.4.0", "2026-09-12")

    def test_earlier_releases_are_untouched(self):
        content = _changelog("### Added\n- A thing.")
        result = promote.promote(content, "1.3.0", "2026-09-11")
        self.assertIn(RELEASED.rstrip("\n"), result)

    def test_has_section_sees_the_promoted_version(self):
        result = promote.promote(_changelog("- A thing."), "1.3.0", "2026-09-11")
        self.assertTrue(promote.has_section(result, "1.3.0"))
        self.assertFalse(promote.has_section(result, "1.4.0"))


class PromoteCliTest(unittest.TestCase):
    def _file(self, tmp, unreleased_body=""):
        path = Path(tmp) / "CHANGELOG.md"
        path.write_text(_changelog(unreleased_body))
        return path

    def test_empty_unreleased_exits_non_zero(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = self._file(tmp)
            code, _, err = _run(path, "1.3.0")
            self.assertEqual(code, 1)
            self.assertIn("is empty", err)
            # Nothing was written.
            self.assertEqual(path.read_text(), _changelog())

    def test_promote_then_promote_again_is_a_no_op(self):
        """A run that dies after pushing must be fixable by re-running."""
        with tempfile.TemporaryDirectory() as tmp:
            path = self._file(tmp, "### Added\n- A thing.")
            code, _, err = _run(path, "1.3.0", "--date", "2026-09-11")
            self.assertEqual(code, 0, err)
            after_first = path.read_text()

            code, out, err = _run(path, "1.3.0", "--date", "2026-09-11")
            self.assertEqual(code, 0, err)
            self.assertIn("already has a [1.3.0] section", out)
            self.assertEqual(path.read_text(), after_first)

    def test_check_mode_writes_nothing(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = self._file(tmp, "### Added\n- A thing.")
            before = path.read_text()
            code, out, err = _run(path, "1.3.0", "--check")
            self.assertEqual(code, 0, err)
            self.assertEqual(path.read_text(), before)
            self.assertIn("ready to become", out)

    def test_release_yml_can_read_the_promoted_section_back(self):
        """The contract with extract_changelog_section.py, which release.yml
        uses to turn the promoted section into the release notes."""
        with tempfile.TemporaryDirectory() as tmp:
            path = self._file(tmp, "### Added\n- A thing a builder can see.")
            code, _, err = _run(path, "1.3.0", "--date", "2026-09-11")
            self.assertEqual(code, 0, err)
            section = extract.extract("1.3.0", str(path))
            self.assertIn("- A thing a builder can see.", section)
            # ...and it stops at the previous release rather than swallowing it.
            self.assertNotIn("Controller Console", section)


if __name__ == "__main__":
    unittest.main()
