"""Unit coverage for tools/check_vocabulary_drift.py and tools/operator_copy.py (#353).

The fixtures are synthetic: a made-up glossary and a made-up surface. Copying
real copy in would write the shipped strings down a second time, which is the
thing the checker exists to stop.

`test_blanking_leaves_every_shipped_script_parseable` is the one that is not
synthetic, and it is the load-bearing one: the extractor's whole claim is that
it can tell a comment from code, and a `/` read as a comment opener instead of
a regular-expression literal silently eats the rest of a line. Handing the
blanked text to `node --check` is what proves it - `data/wifi.js:371` is the
line that failed that check while this was written.

The `RealTree` case keeps the slice gate covering the repository: the gate runs
this directory and does not run `make check-vocabulary-drift`.
"""

import io
import subprocess
import sys
import tempfile
import unittest
from contextlib import redirect_stderr, redirect_stdout
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPO / "tools"))

import check_vocabulary_drift as vocabulary  # noqa: E402
import operator_copy  # noqa: E402

GLOSSARY = """# Fixture glossary

**Body Controller**:
The board running this firmware.
_Avoid_: controller (unqualified, anywhere in operator copy)

**Controller Console**:
The command surface.
_Avoid_: terminal
"""

REGISTRY = """entries:
- name: fixture.action.park
  display_name: Park
  description: Park the Body Controller.
  domain: system
"""

BARE_REGISTRY = """entries:
- name: fixture.action.park
  display_name: Park
  description: Park the Body Controller.
  domain: system
- name: fixture.action.reboot
  display_name: Reboot
  description: Restart the controller.
  domain: system
"""


class Tree:
    """A throwaway repository fragment: a glossary, a surface, a registry."""

    def __init__(self, stack, *, surface="", html="", glossary=GLOSSARY, registry=REGISTRY):
        root = Path(stack.enter_context(tempfile.TemporaryDirectory()))
        self.glossary = root / "CONTEXT.md"
        self.glossary.write_text(glossary, encoding="utf-8")
        self.data = root / "data"
        self.data.mkdir()
        if surface:
            (self.data / "surface.js").write_text(surface, encoding="utf-8")
        if html:
            (self.data / "surface.html").write_text(html, encoding="utf-8")
        self.registry = root / "action-registry.yaml"
        self.registry.write_text(registry, encoding="utf-8")

    def findings(self):
        found, _, _ = vocabulary.check(
            context=self.glossary, data=self.data, registry=self.registry
        )
        return found


class VocabularyCheck(unittest.TestCase):
    def setUp(self):
        self.stack = self.enterContext(__import__("contextlib").ExitStack())

    def test_a_bare_controller_in_a_string_is_caught(self):
        tree = Tree(self.stack, surface='const NOTE = "Restart the controller.";\n')
        found = tree.findings()
        self.assertEqual(1, len(found), found)
        self.assertIn("surface.js:1", found[0])
        self.assertIn("which controller?", found[0])

    def test_the_same_words_in_a_comment_are_not(self):
        # A note to a maintainer is not copy. `#293` in a comment looking like a
        # color literal is the same false positive one check over.
        tree = Tree(
            self.stack,
            surface='// Restart the controller when the log stops.\n/* the controller */\nconst A = 1;\n',
        )
        self.assertEqual([], tree.findings())

    def test_a_qualified_controller_passes(self):
        tree = Tree(
            self.stack,
            surface='const A = "Nothing from the wheel controller yet.";\n',
        )
        self.assertEqual([], tree.findings())

    def test_an_identifier_is_not_copy(self):
        tree = Tree(self.stack, surface='const ICON = "controller-classic-outline";\n')
        self.assertEqual([], tree.findings())

    def test_the_qualified_forms_come_from_the_glossary(self):
        # A term is accepted because the glossary declares it, not because this
        # file lists it: "the Controller Console" has a bare word in it by every
        # other rule here, and passes only while CONTEXT.md carries the term.
        surface = 'const A = "Open the Controller Console.";\n'
        self.assertEqual([], Tree(self.stack, surface=surface).findings())
        without = GLOSSARY.split("**Controller Console**")[0]
        found = Tree(self.stack, surface=surface, glossary=without).findings()
        self.assertEqual(1, len(found), found)

    def test_html_text_is_copy_and_html_comments_are_not(self):
        tree = Tree(
            self.stack,
            html="<!-- the controller sheds connections -->\n<p>Restart the controller.</p>\n",
        )
        found = tree.findings()
        self.assertEqual(1, len(found), found)
        self.assertIn("surface.html:2", found[0])

    def test_a_registry_description_is_caught_at_its_own_line(self):
        # Line 8, not line 6: the description, not the head of its entry. A
        # finding that points two lines off is a finding an editor distrusts.
        found = Tree(self.stack, registry=BARE_REGISTRY).findings()
        self.assertEqual(1, len(found), found)
        self.assertIn("action-registry.yaml:8", found[0])
        self.assertIn("fixture.action.reboot", found[0])

    def test_a_glossary_with_no_controller_term_refuses_to_pass(self):
        # Silence would be the wrong answer: with no qualified form to accept,
        # the check cannot tell a correct string from a bare one.
        tree = Tree(self.stack, glossary="# Fixture glossary\n")
        found = tree.findings()
        self.assertEqual(1, len(found), found)
        self.assertIn("no `**... Controller**:` term", found[0])


class Blanking(unittest.TestCase):
    def test_blanking_leaves_every_shipped_script_parseable(self):
        broken = []
        for path in sorted((REPO / "data").rglob("*.js")):
            source = path.read_text(encoding="utf-8")
            blanked = operator_copy.blank_js_comments(source)
            self.assertEqual(len(source), len(blanked), path)
            with tempfile.NamedTemporaryFile("w", suffix=".js", delete=False) as handle:
                handle.write(blanked)
                scratch = handle.name
            result = subprocess.run(
                ["node", "--check", scratch], capture_output=True, text=True, check=False
            )
            Path(scratch).unlink()
            if result.returncode:
                broken.append(f"{path.relative_to(REPO)}: {result.stderr.strip().splitlines()[:2]}")
        self.assertEqual([], broken, "blanking comments broke the script")


class RealTree(unittest.TestCase):
    def test_the_repository_passes(self):
        out, err = io.StringIO(), io.StringIO()
        with redirect_stdout(out), redirect_stderr(err):
            status = vocabulary.main()
        self.assertEqual(0, status, err.getvalue())
        self.assertIn("Vocabulary check passed", out.getvalue())


if __name__ == "__main__":
    unittest.main()
