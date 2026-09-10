"""What the parts catalog generator refuses, and what it promises (#356).

The generator is the only thing standing between a typo in docs/droid-parts.yaml
and an entry nothing can resolve - a Part id firmware cannot store against an
output, a control path the firmware does not drive, a complement that quietly
seeds an empty droid. Those refusals are asserted here against a scratch copy of
the real catalog, one broken field at a time.

Two promises are asserted beside them, because they are what downstream work
rests on: reordering rows in the catalog changes nothing in either output, and
generating into a scratch tree produces exactly the bytes that are committed -
which is what makes #358's byte-compare a check rather than a coin toss.

Nothing here writes into the tree it is checking. A test that regenerates the
committed artefact in place repairs the staleness it was meant to report and can
never fail twice (tools/check-studio.js:24), so every run goes to a temporary
directory and the committed files are only ever read.
"""

import contextlib
import io
import json
import re
import shutil
import sys
import tempfile
import unittest
import unittest.mock
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))

import generate_droid_parts_catalog as gen  # noqa: E402


class Scratch:
    """A throwaway tree the generator can be aimed at.

    Only the three inputs are copied; the outputs are written beside them.
    """

    def __init__(self, stack):
        self.dir = Path(stack.enter_context(tempfile.TemporaryDirectory()))
        self.catalog = self.dir / "droid-parts.yaml"
        self.control = self.dir / "droid_part_control.inc"
        self.row = self.dir / "servo_output_row.h"
        shutil.copy(gen.CATALOG_PATH, self.catalog)
        shutil.copy(gen.CONTROL_MANIFEST_PATH, self.control)
        shutil.copy(gen.SERVO_OUTPUT_ROW_PATH, self.row)
        self.firmware = self.dir / "droid_parts.h"
        self.browser = self.dir / "droid_parts.js"

    def generate(self):
        with contextlib.redirect_stdout(io.StringIO()):
            gen.generate(
                catalog_path=self.catalog,
                firmware_path=self.firmware,
                browser_path=self.browser,
                control_path=self.control,
                id_limit_path=self.row,
            )
        return (
            self.firmware.read_text(encoding="utf-8"),
            self.browser.read_text(encoding="utf-8"),
        )

    def edit(self, before, after):
        """Break one thing in the scratch catalog, at a data line.

        Several of these strings also appear in the file's header prose, so the
        replacement is anchored on the leading newline and indentation of the
        real row and asserted to have changed the file.
        """
        text = self.catalog.read_text(encoding="utf-8")
        assert before in text, f"scratch catalog does not carry {before!r}"
        edited = text.replace(before, after, 1)
        assert edited != text, f"replacing {before!r} changed nothing"
        self.catalog.write_text(edited, encoding="utf-8")


def browser_payload(text):
    """The data the browser would actually see, parsed out of the module."""
    match = re.search(r"window\.DroidParts = (\{.*\});\n\}\)\(\);", text, re.S)
    assert match, "browser module does not assign window.DroidParts"
    return json.loads(match.group(1))


class GeneratorRefusals(unittest.TestCase):
    def setUp(self):
        stack = contextlib.ExitStack()
        self.addCleanup(stack.close)
        self.scratch = Scratch(stack)

    def assertRefused(self, fragment):
        with self.assertRaises(gen.CatalogError) as caught:
            self.scratch.generate()
        joined = "\n".join(caught.exception.problems)
        self.assertIn(fragment, joined)
        return joined

    def test_a_control_path_the_firmware_does_not_define(self):
        """The firmware declares the control paths; the catalog cannot invent one."""
        self.scratch.edit("position: front, control: body-ledc,",
                          "position: front, control: i2c-expander,")
        self.assertRefused("is not a path the firmware defines")

    def test_an_escape_hatch_on_a_path_the_firmware_does_not_define(self):
        self.scratch.edit("\n  control: body-ledc", "\n  control: dome-link")
        # dome-link is a path the firmware defines but does not drive, so the
        # slots stop reaching firmware rather than being refused - and then a
        # real output has no id, which is what the emitted table shows.
        firmware, _ = self.scratch.generate()
        self.assertNotIn('"other1"', firmware)
        self.assertIn("DROID_PART_COUNT = 2", firmware)

    def test_an_id_too_long_for_the_part_field_on_an_output_row(self):
        """An id no Output can store is an id no Output can ever claim."""
        self.scratch.edit("id: utilUp,", "id: upperUtilityArmOne,")
        self.assertRefused("a Servo Output row holds")

    def test_an_id_that_is_not_an_identifier(self):
        self.scratch.edit("id: doorFL,", "id: door-FL,")
        self.assertRefused("is not an unquoted identifier")

    def test_a_seed_list_naming_a_part_no_row_declares(self):
        self.scratch.edit("seeds: [pie1,", "seeds: [pie0,")
        self.assertRefused("seeds ids no part row declares")

    def test_a_scalar_seeds_value_that_is_not_the_declared_unknown(self):
        """`TBD` is the one permitted non-list; anything else is a typo."""
        self.scratch.edit("\n        seeds: TBD", "\n        seeds: soon")
        self.assertRefused("neither a list nor TBD")

    def test_a_design_with_variants_and_no_default(self):
        self.scratch.edit("\n    default_variant: complex", "")
        self.assertRefused("declares variants but not which one a builder starts on")

    def test_a_default_naming_a_variant_that_does_not_exist(self):
        self.scratch.edit("\n    default_variant: complex", "\n    default_variant: deluxe")
        self.assertRefused("is not one of")

    def test_a_default_on_a_design_with_no_variants(self):
        self.scratch.edit(
            "  - id: own\n    label: My own build",
            "  - id: own\n    default_variant: complex\n    label: My own build",
        )
        self.assertRefused("declares a default_variant but no variants")

    def test_a_misspelled_part_field(self):
        """A key nobody reads generates an entry silently missing a field."""
        self.scratch.edit("position: rear-right,  control: dome-link",
                          "postion: rear-right,  control: dome-link")
        self.assertRefused("unknown field(s)")

    def test_an_escape_hatch_with_no_name(self):
        self.scratch.edit("\n  label_prefix: Other part", "")
        self.assertRefused("the slots have no name")

    def test_every_problem_is_reported_in_one_pass(self):
        """A broken catalog is fixed once, not one message per run."""
        self.scratch.edit("id: doorFL,", "id: door-FL,")
        self.scratch.edit("\n        seeds: TBD", "\n        seeds: soon")
        problems = self.assertRefused("is not an unquoted identifier")
        self.assertIn("neither a list nor TBD", problems)

    def test_a_bad_catalog_writes_nothing(self):
        """Refusal happens before any output is written (tools/build.js:31)."""
        self.scratch.edit("\n        seeds: TBD", "\n        seeds: soon")
        with self.assertRaises(gen.CatalogError):
            self.scratch.generate()
        self.assertFalse(self.scratch.firmware.exists())
        self.assertFalse(self.scratch.browser.exists())

    def test_a_control_manifest_row_it_cannot_read(self):
        """The firmware's own declaration is parsed, never guessed around."""
        text = self.scratch.control.read_text(encoding="utf-8")
        self.scratch.control.write_text(
            text.replace('PA_PART_CONTROL(DROID_PART_CONTROL_NONE, "none", 0)',
                         'PA_PART_CONTROL(DROID_PART_CONTROL_NONE, none, 0)'),
            encoding="utf-8",
        )
        self.assertRefused("not a PA_PART_CONTROL row")


class GeneratorPromises(unittest.TestCase):
    def setUp(self):
        stack = contextlib.ExitStack()
        self.addCleanup(stack.close)
        self.scratch = Scratch(stack)

    def test_the_committed_outputs_are_what_the_generator_produces(self):
        firmware, browser = self.scratch.generate()
        self.assertEqual(firmware, gen.FIRMWARE_OUTPUT_PATH.read_text(encoding="utf-8"))
        self.assertEqual(browser, gen.BROWSER_OUTPUT_PATH.read_text(encoding="utf-8"))

    def test_a_scratch_run_never_names_the_scratch_tree(self):
        """Generated text says where a file belongs, not where it was written."""
        firmware, browser = self.scratch.generate()
        for text in (firmware, browser):
            self.assertNotIn(str(self.scratch.dir), text)

    def test_reordering_the_catalog_renumbers_nothing(self):
        """Emission order is the generator's, so a saved sequence cannot move."""
        before_firmware, before_browser = self.scratch.generate()
        text = self.scratch.catalog.read_text(encoding="utf-8")
        rows = [
            line for line in text.splitlines(keepends=True)
            if line.startswith("  - { id: door")
        ]
        self.assertEqual(len(rows), 4)
        shuffled = text.replace("".join(rows), "".join(reversed(rows)))
        self.assertNotEqual(shuffled, text)
        self.scratch.catalog.write_text(shuffled, encoding="utf-8")

        after_firmware, after_browser = self.scratch.generate()
        # The digest is of the catalog bytes, which did change; everything the
        # outputs say about parts did not.
        self.assertEqual(
            browser_payload(after_browser)["parts"],
            browser_payload(before_browser)["parts"],
        )
        self.assertEqual(
            [line for line in after_firmware.splitlines() if "Source digest" not in line],
            [line for line in before_firmware.splitlines() if "Source digest" not in line],
        )

    def test_the_declared_unknown_never_reaches_the_browser_as_a_value(self):
        _, browser = self.scratch.generate()
        payload = browser_payload(browser)
        self.assertNotIn("TBD", json.dumps(payload))
        simple = payload["designs"][0]["variants"][0]
        self.assertEqual(simple["id"], "simple")
        self.assertIsNone(simple["seeds"], "an unknown complement must not be an empty one")

    def test_both_outputs_carry_the_stamp_and_their_provenance(self):
        firmware, browser = self.scratch.generate()
        for text in (firmware, browser):
            self.assertIn("DO NOT EDIT MANUALLY", text)
            self.assertIn("docs/droid-parts.yaml", text)
            self.assertIn("tools/generate_droid_parts_catalog.py", text)
            self.assertRegex(text, r"Source digest: sha256 [0-9a-f]{64}")

    def test_the_firmware_output_carries_ids_and_no_operator_copy(self):
        firmware, _ = self.scratch.generate()
        table = firmware.split("DROID_PART_IDS[DROID_PART_COUNT] = {", 1)[1].split("};", 1)[0]
        self.assertIn('"utilUp"', table)
        for browser_only in ("Upper utility arm", "PP1", "rear-right", "Other part"):
            self.assertNotIn(browser_only, table)

    def test_the_regeneration_note_names_both_outputs(self):
        with contextlib.redirect_stdout(io.StringIO()) as out:
            with self.assertRaises(SystemExit):
                gen.main(["--help"])
        help_text = out.getvalue()
        self.assertIn("include/droid_parts.h", help_text)
        self.assertIn("data/droid_parts.js", help_text)

    def test_a_refusal_exits_one_and_says_every_problem_on_stderr(self):
        self.scratch.edit("\n        seeds: TBD", "\n        seeds: soon")
        # Every path the entry point resolves is pointed at the scratch tree, so
        # a run that unexpectedly succeeded still could not touch the repo.
        with contextlib.ExitStack() as stack:
            for attribute, value in (
                ("CATALOG_PATH", self.scratch.catalog),
                ("CONTROL_MANIFEST_PATH", self.scratch.control),
                ("SERVO_OUTPUT_ROW_PATH", self.scratch.row),
                ("FIRMWARE_OUTPUT_PATH", self.scratch.firmware),
                ("BROWSER_OUTPUT_PATH", self.scratch.browser),
            ):
                stack.enter_context(unittest.mock.patch.object(gen, attribute, value))
            err = stack.enter_context(contextlib.redirect_stderr(io.StringIO()))
            code = gen.main([])
        self.assertEqual(code, 1)
        self.assertIn("neither a list nor TBD", err.getvalue())
        self.assertFalse(self.scratch.firmware.exists())


if __name__ == "__main__":
    unittest.main()
