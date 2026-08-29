"""Tests for tools/check_framework_envelope.py.

The check exists to catch one specific silent failure: the framework libs being
pristine (every override OFF) while the project stamp claims they were rebuilt.
So the test that matters is the FAILING one - a check that cannot go red on a
pristine sdkconfig would be worse than no check at all, because it would report
PASS on exactly the state it was written to detect.
"""

import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent.parent
sys.path.insert(0, str(ROOT / "tools"))

import check_framework_envelope as cfe  # noqa: E402


def references_shared_envelope() -> bool:
    """Does artoo_esp32 reach any override through `${section.custom_sdkconfig}`?

    Branch-dependent, and deliberately not asserted as a universal fact. The
    shared [artoo_envelope] section lives on the branch that owns the Framework
    Envelope work; a branch without it declares the env's overrides inline.
    Expansion is a real property worth testing, so the test below asserts it
    wherever a reference exists and skips where there is none.
    """
    return "custom_sdkconfig}" in cfe.section_body(cfe.read_ini(), "env:artoo_esp32")


class DeclaredOverridesTest(unittest.TestCase):
    def test_env_overrides_include_the_expanded_envelope(self):
        """A `${section.custom_sdkconfig}` reference must be EXPANDED, not skipped
        as an unresolved token."""
        if not references_shared_envelope():
            self.skipTest("artoo_esp32 declares its overrides inline on this branch")
        overrides = cfe.declared_overrides(cfe.read_ini(), "artoo_esp32")
        self.assertIn("CONFIG_BT_ENABLED", overrides, "envelope reference was not expanded")
        self.assertEqual("n", overrides["CONFIG_BT_ENABLED"])
        # ...alongside the env's own board-specific overrides.
        self.assertIn("CONFIG_LWIP_TCP_MSL", overrides)

    def test_comments_are_not_parsed_as_overrides(self):
        overrides = cfe.declared_overrides(cfe.read_ini(), "artoo_esp32")
        self.assertTrue(all(k.startswith("CONFIG_") for k in overrides))


class ActualValueTest(unittest.TestCase):
    def test_disabled_bool_is_read_from_the_comment_form(self):
        """Kconfig writes a disabled bool as `# KEY is not set`, never `KEY=n`.
        Missing this is how a correct config reads as an absent one."""
        self.assertEqual("n", cfe.actual_value("# CONFIG_BT_ENABLED is not set\n", "CONFIG_BT_ENABLED"))

    def test_enabled_and_absent_are_distinguished(self):
        self.assertEqual("y", cfe.actual_value("CONFIG_BT_ENABLED=y\n", "CONFIG_BT_ENABLED"))
        self.assertEqual("ABSENT", cfe.actual_value("", "CONFIG_BT_ENABLED"))


class CheckTest(unittest.TestCase):
    """The red/green pair this tool is for."""

    def _write(self, name: str, body: str) -> Path:
        import tempfile

        path = Path(tempfile.mkdtemp()) / name
        path.write_text(body)
        return path

    def test_fails_on_a_pristine_sdkconfig(self):
        """The regression this tool exists to catch: envelope keys back to =y."""
        overrides = cfe.declared_overrides(cfe.read_ini(), "artoo_esp32")
        pristine = "".join(f"{key}=y\n" for key in overrides)
        self.assertEqual(1, cfe.check("artoo_esp32", quiet=True, sdkconfig=self._write("sdkconfig", pristine)))

    def test_fails_when_a_single_override_is_flipped(self):
        """One silently re-selected key is still a failure - the 2026-08-29
        incident lost several at once, but a Kconfig `select` can lose just one."""
        overrides = cfe.declared_overrides(cfe.read_ini(), "artoo_esp32")
        self.assertTrue(overrides, "artoo_esp32 declares no overrides to flip")
        # Flip whichever key comes first rather than naming one: the declared set
        # differs per branch, and a test that hardcodes a key present in only one
        # platformio.ini layout goes red everywhere else without finding anything.
        flipped = next(iter(overrides))
        lines = []
        for key, want in overrides.items():
            if key == flipped:
                # A value the checker cannot read as satisfied, whatever `want` is.
                lines.append(f"{key}={'y' if want != 'y' else 'n'}\n")
            elif want == "n":
                lines.append(f"# {key} is not set\n")
            else:
                lines.append(f"{key}={want}\n")
        self.assertEqual(
            1, cfe.check("artoo_esp32", quiet=True, sdkconfig=self._write("sdkconfig", "".join(lines)))
        )

    def test_passes_when_every_override_held(self):
        overrides = cfe.declared_overrides(cfe.read_ini(), "artoo_esp32")
        good = "".join(
            (f"# {key} is not set\n" if want == "n" else f"{key}={want}\n")
            for key, want in overrides.items()
        )
        self.assertEqual(0, cfe.check("artoo_esp32", quiet=True, sdkconfig=self._write("sdkconfig", good)))

    def test_missing_sdkconfig_fails_rather_than_passing_vacuously(self):
        self.assertEqual(
            1, cfe.check("artoo_esp32", quiet=True, sdkconfig=Path("/nonexistent/sdkconfig"))
        )


class ResolvedPathTest(unittest.TestCase):
    def test_p4_env_resolves_to_its_own_core_dir(self):
        """Each chip target has its own PLATFORMIO_CORE_DIR; reading the artoo
        core dir while checking a P4 env would check the wrong libs entirely."""
        artoo = cfe.resolved_sdkconfig_path("artoo_esp32")
        p4 = cfe.resolved_sdkconfig_path("firebeetle2")
        self.assertNotEqual(artoo, p4)
        self.assertIn("esp32p4", str(p4))
        self.assertIn(".platformio-p4", str(p4))


if __name__ == "__main__":
    unittest.main()
