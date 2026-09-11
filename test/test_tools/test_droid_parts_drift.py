"""What the parts catalog drift check catches, and what it refuses to repair (#358).

The check exists because the generator cannot catch the one failure that matters
most: nobody running it. So the assertions here are about the committed artefact
going stale, and about the two properties that make the check trustworthy -

  it never writes, so corrupting an output and running twice fails twice. A
  check that rebuilds in place repairs the drift it was meant to report and
  then passes on evidence it manufactured itself;

  it runs the real generator rather than a copy of its rules, so a generator
  change cannot leave the check agreeing with a version of the rules nobody
  ships.

Every case runs against a scratch tree. The check addresses every file it reads
through the generator's own path constants, which is what lets the whole thing
be aimed somewhere else by patching them - and what keeps a test that goes wrong
from touching the committed outputs it is about.
"""

import contextlib
import hashlib
import io
import shutil
import sys
import tempfile
import unittest
import unittest.mock
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))

import check_droid_parts_drift as check  # noqa: E402
import generate_droid_parts_catalog as gen  # noqa: E402


class Scratch:
    """A copy of the catalog, its inputs, its outputs and its browser consumer.

    Generated once from the real catalog, so the tree starts in the state the
    repository is supposed to be in: outputs that match their source exactly.
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
        # The one hand-written browser module that reads a Part Kind. It sits
        # beside the generated catalog in data/, and the check looks for a
        # consumer in exactly that neighbourhood.
        self.consumer = self.dir / "droid_part_kind.js"
        shutil.copy(gen.BROWSER_OUTPUT_PATH.parent / "droid_part_kind.js", self.consumer)

        stack.enter_context(self.aimed())
        with contextlib.redirect_stdout(io.StringIO()):
            gen.generate(quiet=True)

    def aimed(self):
        """Point the generator - and therefore the check - at this tree."""
        stack = contextlib.ExitStack()
        for attribute, value in (
            ("CATALOG_PATH", self.catalog),
            ("CONTROL_MANIFEST_PATH", self.control),
            ("SERVO_OUTPUT_ROW_PATH", self.row),
            ("FIRMWARE_OUTPUT_PATH", self.firmware),
            ("BROWSER_OUTPUT_PATH", self.browser),
        ):
            stack.enter_context(unittest.mock.patch.object(gen, attribute, value))
        return stack

    def edit(self, path, before, after):
        text = path.read_text(encoding="utf-8")
        assert text.count(before) == 1, f"{before!r} appears {text.count(before)} times"
        path.write_text(text.replace(before, after), encoding="utf-8")

    def digests(self):
        return {
            path: hashlib.sha256(path.read_bytes()).hexdigest()
            for path in (self.firmware, self.browser)
            if path.exists()
        }


def run_check():
    """The check as an operator runs it: an exit code and what it printed."""
    out, err = io.StringIO(), io.StringIO()
    with contextlib.redirect_stdout(out), contextlib.redirect_stderr(err):
        code = check.main()
    return code, out.getvalue() + err.getvalue()


class TheCommittedTree(unittest.TestCase):
    def test_the_committed_outputs_match_the_catalog_they_came_from(self):
        """The check over the repository as it stands, which is what
        `make check-parts-drift` runs."""
        code, output = run_check()
        self.assertEqual(code, 0, output)
        self.assertIn("drift check passed", output)
        # The Kind that costs six Parts their treatment if its reader goes.
        self.assertIn("light -> data/droid_part_kind.js", output)


class DriftIsReported(unittest.TestCase):
    def setUp(self):
        stack = contextlib.ExitStack()
        self.addCleanup(stack.close)
        self.scratch = Scratch(stack)

    def assertFails(self, fragment):
        code, output = run_check()
        self.assertEqual(code, 1, output)
        self.assertIn(fragment, output)
        return output

    def test_a_clean_scratch_tree_passes(self):
        """The baseline every case below breaks one thing from."""
        code, output = run_check()
        self.assertEqual(code, 0, output)

    def test_a_corrupted_output_fails_and_fails_again(self):
        """The double failure. A check that rebuilt in place would pass here on
        its second run, having quietly written the answer it then read back."""
        self.scratch.edit(self.scratch.browser, '"name": "Dome pie 1"',
                          '"name": "Dome pie one"')
        corrupted = self.scratch.digests()

        self.assertFails("is not what")
        self.assertFails("is not what")

        self.assertEqual(self.scratch.digests(), corrupted,
                         "the check repaired what it was meant to report")
        self.assertIn("Dome pie one", self.scratch.browser.read_text(encoding="utf-8"))

    def test_an_edited_catalog_with_stale_outputs_beside_it(self):
        """The failure this whole check exists for: the catalog moved and
        nobody regenerated. The digest says so by name, so a reader is sent to
        the generator rather than to a diff."""
        self.scratch.edit(self.scratch.catalog, '"PP1", "Dome pie 1"',
                          '"PP1", "Dome pie one"')
        output = self.assertFails("is a catalog behind")
        self.assertIn("is not what", output)

    def test_an_output_with_its_stamp_removed(self):
        self.scratch.edit(self.scratch.firmware, "// DO NOT EDIT MANUALLY\n", "")
        self.assertFails("carries no 'DO NOT EDIT MANUALLY' stamp")

    def test_an_output_that_stopped_naming_where_it_came_from(self):
        self.scratch.edit(self.scratch.firmware,
                          "// Auto-generated from docs/droid-parts.yaml by "
                          "tools/generate_droid_parts_catalog.py\n", "")
        output = self.assertFails("does not name docs/droid-parts.yaml")
        self.assertIn("does not name tools/generate_droid_parts_catalog.py", output)

    def test_a_generated_id_that_traces_back_to_no_catalog_row(self):
        """tools/build.js:48's direction: what is emitted must be declared."""
        self.scratch.edit(self.scratch.firmware, '    "pie1",  // dome_pies',
                          '    "pie1",  // dome_pies\n    "pie0",  // dome_pies')
        self.assertFails("carries pie0, which traces back to no row")

    def test_a_catalog_row_that_reaches_an_output_not_at_all(self):
        """tools/build.js:31's direction, and the one that was a warning there
        until four modules had gone missing. One renamed id is both directions
        at once: the row now reaches nothing, and the output carries a name
        nothing declares."""
        self.scratch.edit(self.scratch.browser, '"id": "doorFL"', '"id": "doorFX"')
        output = self.assertFails(
            "declares doorFL, which reaches data/droid_parts.js not at all"
        )
        self.assertIn("carries doorFX, which traces back to no row", output)

    def test_a_committed_output_damaged_past_reading(self):
        """A file a hand can edit is a file a hand can break. A part that lost
        its id is reported, not raised three functions later where an operator
        gets a traceback instead of a drift report."""
        self.scratch.edit(
            self.scratch.browser,
            '        "id": "doorFL",\n        "section": "body_doors",\n',
            '        "section": "body_doors",\n',
        )
        self.assertFails("parts[36] carries no id")

    def test_a_table_that_outran_its_own_count(self):
        """The one shape of hand damage that still compiles: an id appended past
        the count a consumer sizes its buffer against."""
        self.scratch.edit(self.scratch.firmware, "DROID_PART_COUNT = 58;",
                          "DROID_PART_COUNT = 57;")
        self.assertFails("DROID_PART_COUNT is 57 and the table holds 58 ids")

    def test_a_part_kind_no_browser_module_reads(self):
        """`light` with nothing consulting it gives six Parts the treatment of
        something that moves - travel and throw offered for a device with
        neither (#357)."""
        self.scratch.consumer.unlink()
        self.assertFails("Part Kind 'light' reaches data/droid_parts.js and no "
                         "browser module reads it")

    def test_a_consumer_that_renamed_the_token_out_from_under_the_catalog(self):
        """Deleting the module is the loud case; this is the quiet one."""
        self.scratch.edit(self.scratch.consumer, 'const LIGHT = "light";',
                          'const LIGHT = "lit";')
        self.assertFails("Part Kind 'light' reaches")

    def test_a_broken_catalog_is_reported_rather_than_raised(self):
        """An operator running this has a catalog to fix either way, and a
        traceback is a worse way to be told."""
        self.scratch.edit(self.scratch.catalog, "\n        seeds: TBD",
                          "\n        seeds: soon")
        self.assertFails("neither a list nor TBD")


class TheCheckWritesNothing(unittest.TestCase):
    def setUp(self):
        stack = contextlib.ExitStack()
        self.addCleanup(stack.close)
        self.scratch = Scratch(stack)

    def test_a_passing_run_leaves_every_file_byte_for_byte(self):
        """Not only the outputs: the generator is run with its writes
        intercepted, so nothing in the tree moves at all."""
        before = {
            path: hashlib.sha256(path.read_bytes()).hexdigest()
            for path in sorted(self.scratch.dir.iterdir())
        }
        code, output = run_check()
        self.assertEqual(code, 0, output)
        after = {
            path: hashlib.sha256(path.read_bytes()).hexdigest()
            for path in sorted(self.scratch.dir.iterdir())
        }
        self.assertEqual(before, after)

    def test_a_generator_write_the_check_does_not_expect_is_refused(self):
        """The interception refuses an unknown target rather than falling
        through to the real filesystem. A generator that grows a third output
        has to be taught to this check, not silently written from inside it."""
        stray = self.scratch.dir / "somewhere_else.txt"
        with check.writes_captured({self.scratch.firmware.resolve()}):
            with self.assertRaises(check.Wrote):
                stray.write_text("no", encoding="utf-8")
        self.assertFalse(stray.exists())
        # And the swap is undone whatever happened inside it.
        stray.write_text("yes", encoding="utf-8")
        self.assertEqual(stray.read_text(encoding="utf-8"), "yes")


if __name__ == "__main__":
    unittest.main()
