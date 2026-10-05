#!/usr/bin/env python3
"""tools/make_brief.py renders the real worker-brief template with both markers (#470)."""

import contextlib
import io
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))

import make_brief  # noqa: E402


class RenderTemplate(unittest.TestCase):
    def test_no_placeholder_survives_and_both_markers_appear(self):
        with tempfile.TemporaryDirectory() as tmp:
            out = Path(tmp) / "brief.md"
            stdout = io.StringIO()
            with contextlib.redirect_stdout(stdout):
                rc = make_brief.main(["--issue", "355", "--slug", "355rr",
                                      "--worktree", "../wt-355rr",
                                      "--base", "epic/operator-experience",
                                      "--out", str(out)])
            self.assertEqual(rc, 0)
            self.assertEqual(stdout.getvalue().strip(), str(out))
            brief = out.read_text()

        self.assertEqual(make_brief.leftover_placeholders(brief), [])
        self.assertIn("<!-- coordinator-pin-355rr -->", brief)
        self.assertIn("<!-- worker-status-355-355rr -->", brief)
        self.assertIn("--json /tmp/slice-355rr.json", brief)
        # The coordinator's header above the rule is not part of the prompt.
        self.assertNotIn("make_brief.py", brief)

    def test_refuses_to_write_a_brief_with_a_leftover_placeholder(self):
        with tempfile.TemporaryDirectory() as tmp:
            template = Path(tmp) / "t.md"
            template.write_text("header\n\n---\n\n#{ISSUE} {Issue}\n")
            out = Path(tmp) / "brief.md"
            with contextlib.redirect_stderr(io.StringIO()):
                rc = make_brief.main(["--issue", "1", "--slug", "a", "--worktree", "w",
                                      "--base", "b", "--out", str(out),
                                      "--template", str(template)])
            self.assertEqual(rc, 1)
            self.assertFalse(out.exists())

    def test_refuses_a_numbered_placeholder_in_an_argument(self):
        for flag, value in (("--worktree", "{WORKTREE_2}"), ("--base", "{BASE2}")):
            args = {"--worktree": "../wt-1", "--base": "main", flag: value}
            with self.subTest(flag=flag), tempfile.TemporaryDirectory() as tmp:
                out = Path(tmp) / "brief.md"
                with contextlib.redirect_stderr(io.StringIO()), \
                        self.assertRaises(SystemExit) as raised:
                    make_brief.main(["--issue", "1", "--slug", "a",
                                     "--worktree", args["--worktree"],
                                     "--base", args["--base"], "--out", str(out)])
                self.assertEqual(raised.exception.code, 2)
                self.assertFalse(out.exists())

    def test_finds_a_numbered_placeholder_left_in_a_brief(self):
        self.assertEqual(make_brief.leftover_placeholders("{BASE2} {owner}/{repo}"), ["{BASE2}"])


if __name__ == "__main__":
    unittest.main()
