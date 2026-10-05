#!/usr/bin/env python3
"""tools/accept_slice.py rejects a gate block that is not of the branch (#469).

Each case builds a throwaway repo: a `base` branch carrying the three verifier
scripts, a `slice` branch with one commit on top, and a gate block written the
way slice_verify --json writes it. Then one thing is made to disagree.
"""

import contextlib
import io
import json
import os
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))

import accept_slice  # noqa: E402

GIT_ENV = {
    **os.environ,
    "GIT_CONFIG_GLOBAL": os.devnull,
    "GIT_CONFIG_NOSYSTEM": "1",
    "GIT_AUTHOR_NAME": "t", "GIT_AUTHOR_EMAIL": "t@t",
    "GIT_COMMITTER_NAME": "t", "GIT_COMMITTER_EMAIL": "t@t",
}


class AcceptSlice(unittest.TestCase):
    def setUp(self):
        self._tmp = tempfile.TemporaryDirectory()
        self.repo = Path(self._tmp.name)
        self.git("init", "-q", "-b", "base")
        for path in ("tools/slice_verify.py", "tools/mutation_verify.py",
                     "tools/web_load_trace.cjs", "src/a.cpp", "src/b.cpp",
                     "data/fw-version.json"):
            self.write(path, f"{path}\n")
        self.commit("base")
        self.git("checkout", "-q", "-b", "slice")
        self.write("src/a.cpp", "slice change\n")
        self.commit("slice")
        self.block = self.gate_block()

    def tearDown(self):
        self._tmp.cleanup()

    def git(self, *args):
        return subprocess.run(["git", "-C", str(self.repo), *args], env=GIT_ENV,
                              check=True, capture_output=True, text=True).stdout.strip()

    def write(self, path, text):
        target = self.repo / path
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_text(text)

    def commit(self, message):
        self.git("add", "-A")
        self.git("commit", "-q", "-m", message)

    def on_base(self, path, text):
        """Land a commit on `base` without touching the slice checkout."""
        self.git("checkout", "-q", "base")
        self.write(path, text)
        self.commit(f"base: {path}")
        self.git("checkout", "-q", "slice")

    def gate_block(self):
        """What slice_verify --json records on the slice's HEAD right now."""
        def blob(path):
            return self.git("hash-object", path)[:12]
        return {
            "gate": {
                "script_hash": blob("tools/slice_verify.py"),
                "mutation_script_hash": blob("tools/mutation_verify.py"),
                "trace_script_hash": blob("tools/web_load_trace.cjs"),
                "head": self.git("rev-parse", "HEAD"),
                "dirty": False,
                "base_ref": "base",
                "merge_base": self.git("merge-base", "base", "HEAD"),
            },
            "ok": True,
        }

    def run_tool(self, *extra):
        path = self.repo.parent / f"{self.repo.name}-block.json"
        path.write_text(json.dumps(self.block))
        self.addCleanup(path.unlink)
        out = io.StringIO()
        with contextlib.redirect_stdout(out):
            rc = accept_slice.main(["--json", str(path), "--worktree", str(self.repo),
                                    "--base", "base", *extra])
        return rc, out.getvalue()

    def assert_fails(self, label, *extra):
        rc, out = self.run_tool(*extra)
        self.assertEqual(rc, 1, out)
        # Exactly the one row under test fails: "FAIL  <label padded to 16><detail>".
        failed = [line[6:22].strip() for line in out.splitlines() if line.startswith("FAIL")]
        self.assertEqual(failed, [label], out)
        self.assertIn("next:", out)
        return out

    def test_a_block_of_this_branch_is_accepted(self):
        self.write("data/fw-version.json", "stamped by a build\n")
        rc, out = self.run_tool()
        self.assertEqual(rc, 0, out)
        self.assertNotIn("FAIL", out)

    def test_head_mismatch_fails(self):
        self.write("src/b.cpp", "a later commit\n")
        self.commit("after the gate ran")
        self.assert_fails("head")

    def test_moved_base_without_overlap_passes_with_a_note(self):
        self.on_base("src/b.cpp", "landed on the base\n")
        rc, out = self.run_tool()
        self.assertEqual(rc, 0, out)
        self.assertIn("moved, no overlap", out)

    def test_moved_base_with_overlap_fails_and_names_the_file(self):
        self.on_base("src/a.cpp", "the base changed the same file\n")
        out = self.assert_fails("base tip")
        self.assertIn("overlap: src/a.cpp", out)

    def test_verifier_changed_on_the_base_fails(self):
        self.on_base("tools/slice_verify.py", "a side session edited the gate\n")
        out = self.assert_fails("verifier hashes")
        self.assertIn("tools/slice_verify.py", out)

    def test_block_without_a_trace_hash_fails_when_the_base_has_the_script(self):
        del self.block["gate"]["trace_script_hash"]
        out = self.assert_fails("verifier hashes")
        self.assertIn("tools/web_load_trace.cjs: block none", out)

    def edit_gate_on_the_slice(self):
        self.write("tools/slice_verify.py", "sanctioned gate work\n")
        self.commit("slice edits the gate")
        self.block = self.gate_block()

    def test_a_verifier_the_branch_edits_fails_without_the_grant(self):
        self.edit_gate_on_the_slice()
        out = self.assert_fails("verifier hashes")
        self.assertIn("tools/slice_verify.py: edited on this branch", out)
        self.assertIn("--allow-gate-edit", out)

    def test_a_granted_verifier_edit_is_held_to_head(self):
        self.edit_gate_on_the_slice()
        rc, out = self.run_tool("--allow-gate-edit")
        self.assertEqual(rc, 0, out)
        self.assertIn("held to HEAD", out)

    def test_the_grant_does_not_cover_a_verifier_that_moved_on_the_base(self):
        self.edit_gate_on_the_slice()
        self.on_base("tools/mutation_verify.py", "a side session edited the mutation gate\n")
        out = self.assert_fails("verifier hashes", "--allow-gate-edit")
        self.assertIn("tools/mutation_verify.py: block", out)

    def test_dirty_tree_fails(self):
        self.write("src/b.cpp", "uncommitted\n")
        out = self.assert_fails("worktree clean")
        self.assertIn("dirty: src/b.cpp", out)

    def test_a_failed_or_dirty_gate_run_fails(self):
        self.block["gate"]["dirty"] = True
        self.assert_fails("gate result")


if __name__ == "__main__":
    unittest.main()
