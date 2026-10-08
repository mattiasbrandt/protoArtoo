"""The small navigation tools from #465: CSS lookup and bench-sheet check."""

import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

import importlib.util

ROOT = Path(__file__).resolve().parents[2]
CSS = ROOT / "tools" / "css_where.py"
CONSOLE = ROOT / "tools" / "console_client.py"
WAIT = ROOT / "tools" / "wait_worker.py"


def _run(args, **kwargs):
    return subprocess.run(
        [sys.executable, *args],
        cwd=ROOT, capture_output=True, text=True, **kwargs,
    )


class CssWhere(unittest.TestCase):
    def test_btn_is_found_and_does_not_mean_btn_sm(self):
        r = _run([str(CSS), ".btn"])
        self.assertEqual(r.returncode, 0, r.stderr)
        self.assertIn(".btn", r.stdout)
        # The .btn-sm rule is a different selector. A line that is only .btn-sm
        # must not be the only hit; .btn itself has its own lines.
        self.assertGreater(r.stdout.count(".btn"), 0)
        sm = _run([str(CSS), ".btn-sm"])
        self.assertEqual(sm.returncode, 0, sm.stderr)
        self.assertIn(".btn-sm", sm.stdout)

    def test_missing_selector_exits_nonzero(self):
        r = _run([str(CSS), ".no-such-class-465"])
        self.assertEqual(r.returncode, 1)
        self.assertIn("(no line)", r.stdout)


class CheckSheet(unittest.TestCase):
    def test_unique_labels_pass_and_a_duplicate_or_unknown_fails(self):
        with tempfile.TemporaryDirectory() as tmp:
            ok = Path(tmp) / "ok.txt"
            ok.write_text("@row 1 alpha\nsend status\n@row 1 beta\nhttp GET /api/status\n")
            good = _run([str(CONSOLE), "--check-sheet", str(ok)])
            self.assertEqual(good.returncode, 0, good.stderr)
            self.assertIn("ok ", good.stdout)

            dup = Path(tmp) / "dup.txt"
            dup.write_text("@row 1 alpha\nsend status\n@row 2 alpha\nsend status\n")
            bad = _run([str(CONSOLE), "--check-sheet", str(dup)])
            self.assertEqual(bad.returncode, 1)
            self.assertIn("alpha", bad.stderr)

            missing = Path(tmp) / "missing.txt"
            missing.write_text("@row 1\nsend status\n")
            bare = _run([str(CONSOLE), "--check-sheet", str(missing)])
            self.assertEqual(bare.returncode, 1)
            self.assertIn("no label", bare.stderr)

            unknown = Path(tmp) / "unknown.txt"
            unknown.write_text("@row 1 alpha\nexpect 1\n")
            nope = _run([str(CONSOLE), "--check-sheet", str(unknown)])
            self.assertEqual(nope.returncode, 1)
            self.assertIn("unknown directive", nope.stderr)


class WaitWorker(unittest.TestCase):
    def verdict(self, body):
        spec = importlib.util.spec_from_file_location("wait_worker", WAIT)
        mod = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(mod)
        return mod._verdict_from_text(body)

    def test_with_both_signals_ok_needs_both_and_blocked_needs_either(self):
        spec = importlib.util.spec_from_file_location("wait_worker", WAIT)
        mod = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(mod)
        # The mid-slice gate run: the file says ok, the comment is not done yet.
        self.assertIsNone(mod._combined([None, "ok"]))
        self.assertEqual(mod._combined(["ok", "ok"]), "ok")
        self.assertEqual(mod._combined(["blocked", None]), "blocked")
        self.assertEqual(mod._combined([None, "blocked"]), "blocked")
        self.assertEqual(mod._combined(["ok"]), "ok")

    def test_token_is_the_last_line_and_a_signature_does_not_count(self):
        self.assertEqual(self.verdict("WORKER_DONE: ok\n//Grok Build Grok 4.7\n"), "ok")
        self.assertEqual(self.verdict("WORKER_DONE: blocked\n"), "blocked")
        self.assertIsNone(self.verdict("WORKER_DONE: ok\n\nGate restarted; still running.\n"))
        self.assertIsNone(self.verdict("WORKER_DONE: ok\nGate restarted; still running.\n//sig\n"))

    def test_since_ignores_a_comment_last_updated_before_the_rework(self):
        spec = importlib.util.spec_from_file_location("wait_worker", WAIT)
        mod = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(mod)
        marker = "<!-- worker-status-1-s -->"
        updated = "2026-10-08T15:00:00Z"
        mod._comment_bodies = lambda issue: [(f"{marker}\nWORKER_DONE: ok\n", updated)]
        args = ["--issue", "1", "--marker", marker, "--timeout", "0", "--interval", "0"]
        self.assertEqual(mod.main([*args, "--since", "2026-10-08T15:00:00Z"]), 1)
        self.assertEqual(mod.main([*args, "--since", "2026-10-08T14:59:59Z"]), 0)
        self.assertEqual(mod.main(args), 0)

    def test_new_head_counts_ok_only_after_a_commit_and_never_blocks(self):
        spec = importlib.util.spec_from_file_location("wait_worker", WAIT)
        mod = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(mod)
        with tempfile.TemporaryDirectory() as tmp:
            def commit():
                subprocess.run(["git", "-C", tmp, "-c", "user.name=t", "-c", "user.email=t@t",
                                "commit", "-q", "--allow-empty", "-m", "c"], check=True)
            subprocess.run(["git", "-C", tmp, "init", "-q"], check=True)
            commit()
            start = mod._head(Path(tmp))
            self.assertIsNone(mod._verdict_from_head(Path(tmp), start))
            commit()
            self.assertEqual(mod._verdict_from_head(Path(tmp), start), "ok")


if __name__ == "__main__":
    unittest.main()
