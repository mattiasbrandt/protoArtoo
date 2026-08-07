"""Pin the version-string behavior of tools/extract_version.py (issue #36).

The script runs at import time and writes data/*.json into its working
directory, so every case executes it as a subprocess inside a scratch git
repo — never against the real working tree.

The regression case is `test_stamp_rewrite_alone_stays_clean`: the script's
own stamp writes used to flip `git describe --dirty` for every later
invocation in the same build, so firmware built from a pristine CI checkout
reported itself `-dirty` (observed in the release-workflow dry run for #35).
"""

import json
import os
import re
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

SCRIPT = Path(__file__).parents[2] / "tools" / "extract_version.py"


def _git(repo, *args):
    subprocess.check_output(
        ["git", "-c", "user.email=t@t", "-c", "user.name=t", *args],
        cwd=repo, stderr=subprocess.STDOUT,
    )


def _make_repo(tmp):
    """Scratch repo shaped like the project: tracked stamps, a source file,
    a CHANGELOG, one commit on main, tagged v9.9.9."""
    repo = Path(tmp)
    (repo / "data").mkdir()
    (repo / "data" / "fw-version.json").write_text('{"firmwareVersion": "stale"}\n')
    (repo / "data" / "fs-version.json").write_text('{"fsVersion": "stale"}\n')
    (repo / "src.cpp").write_text("// source\n")
    (repo / "CHANGELOG.md").write_text("## [9.9.9] - test\n")
    _git(repo, "init", "-q", "-b", "main")
    _git(repo, "add", "-A")
    _git(repo, "commit", "-q", "-m", "chore(test): scratch repo")
    _git(repo, "tag", "v9.9.9")
    return repo


def _run_script(repo, extra_env=None):
    """Run the script standalone; return the PA_FIRMWARE_VERSION it printed."""
    env = {k: v for k, v in os.environ.items() if not k.startswith("GITHUB_")}
    env.update(extra_env or {})
    out = subprocess.check_output(
        [sys.executable, str(SCRIPT)], cwd=repo, env=env,
    ).decode()
    match = re.search(r"PA_FIRMWARE_VERSION=(\S+)", out)
    assert match, f"no version line in output: {out!r}"
    return match.group(1)


class ExtractVersionTest(unittest.TestCase):
    def setUp(self):
        self._tmp = tempfile.TemporaryDirectory()
        self.repo = _make_repo(self._tmp.name)
        self.addCleanup(self._tmp.cleanup)

    def test_clean_tagged_main_is_bare_tag(self):
        self.assertEqual(_run_script(self.repo), "v9.9.9")

    def test_real_source_edit_reports_dirty(self):
        (self.repo / "src.cpp").write_text("// edited\n")
        version = _run_script(self.repo)
        self.assertTrue(version.endswith("-dirty"), version)
        # Dirty never collapses to a clean-looking bare tag.
        self.assertIn("-0-g", version)

    def test_non_release_tag_is_ignored(self):
        # A marker tag closer to HEAD than the release tag (the repo carries
        # safepoint/* tags) must not become the version.
        (self.repo / "src.cpp").write_text("// later work\n")
        _git(self.repo, "add", "-A")
        _git(self.repo, "commit", "-q", "-m", "chore(test): later work")
        _git(self.repo, "tag", "safepoint/scratch-2026-01-01")
        version = _run_script(self.repo)
        self.assertNotIn("safepoint", version)
        self.assertTrue(version.startswith("v9.9.9-1-g"), version)

    def test_stamp_rewrite_alone_stays_clean(self):
        # First run rewrites the tracked stamps; a second run must not read
        # its own output back as a dirty tree.
        self.assertEqual(_run_script(self.repo), "v9.9.9")
        self.assertEqual(_run_script(self.repo), "v9.9.9")

    def test_source_edit_still_dirty_after_stamp_rewrite(self):
        # The stamp exclusion must not swallow real modifications.
        _run_script(self.repo)
        (self.repo / "src.cpp").write_text("// edited\n")
        self.assertTrue(_run_script(self.repo).endswith("-dirty"))

    def test_non_main_branch_gets_build_metadata_suffix(self):
        _git(self.repo, "checkout", "-q", "-b", "feature/dome-fix")
        self.assertEqual(_run_script(self.repo), "v9.9.9+feature-dome-fix")

    def test_ci_branch_env_wins_over_detached_head(self):
        head = subprocess.check_output(
            ["git", "rev-parse", "HEAD"], cwd=self.repo,
        ).decode().strip()
        _git(self.repo, "checkout", "-q", "--detach", head)
        version = _run_script(
            self.repo,
            {"GITHUB_REF_TYPE": "branch", "GITHUB_REF_NAME": "feature/x"},
        )
        self.assertEqual(version, "v9.9.9+feature-x")

    def test_stamp_files_written_and_consistent(self):
        _run_script(self.repo)
        fw = json.loads((self.repo / "data" / "fw-version.json").read_text())
        fs = json.loads((self.repo / "data" / "fs-version.json").read_text())
        self.assertEqual(fw["firmwareVersion"], "v9.9.9")
        self.assertEqual(fs["fsVersion"], "fs-v9.9.9")


if __name__ == "__main__":
    unittest.main()
