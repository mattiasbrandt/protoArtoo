"""Unit coverage for tools/check_component_registry_drift.py (#340).

Two jobs, the same split `test_action_registry_drift.py` uses.

**Fixtures prove each check can fail.** Every check is driven against a
hand-written manifest with the defect planted in it, so "this check would catch
that" is demonstrated rather than asserted. Before the checker moved into
`tools/` the only way to show this was to mutate the real tree and put it back;
these do it without touching a shipped file.

**Live-tree assertions keep the slice gate covering the repo.** The gate runs
`python3 -m unittest discover -s test/test_tools` as its `gate self-tests`
stage and does not run `make check-component-drift`, so without the three
`RealTree` cases below, moving the checker out of here would have taken the
repo's own drift out of the gate. `make check-component-drift` and the CI step
are the operator-facing entry points; these are what make the gate fail on a
real drift.
"""

import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "tools"))

import check_component_registry_drift as drift  # noqa: E402


SUPPORTED = "COMPONENT_STATUS_SUPPORTED"
ROADMAP = "COMPONENT_STATUS_ROADMAP"


def manifest(categories: str = "", parts: str = "") -> str:
    return f"{categories}\n{parts}\n"


def write(tmp: str, name: str, text: str) -> Path:
    path = Path(tmp) / name
    path.write_text(text, encoding="utf-8")
    return path


class ManifestParsing(unittest.TestCase):
    def test_a_row_spanning_several_lines_is_one_row(self):
        # The Sound rows wrap, because a capability expression is long. A
        # line-based parser would cut them in half and see columns that are
        # not there.
        text = manifest(parts='''PA_COMPONENT_PART(18, "dy_sv5w", "DY-SV5W", COMPONENT_CATEGORY_SOUND, "soft_uart_binary", COMPONENT_STATUS_SUPPORTED,
                  AudioDriver::AUDIO_CAP_STATUS_QUERY | AudioDriver::AUDIO_CAP_DEVICE_TYPE,
                  nullptr, 1)''')
        rows = drift.read_invocations(text, "PA_COMPONENT_PART")
        self.assertEqual(1, len(rows))
        self.assertEqual(drift.PART_COLUMNS, len(rows[0]))
        self.assertEqual("dy_sv5w", drift.unquote(rows[0][1]))

    def test_a_parenthesised_included_expression_is_one_column(self):
        text = manifest(parts='PA_COMPONENT_PART(1, "a", "A", C, "p", COMPONENT_STATUS_SUPPORTED, 0, nullptr, (PA_BOARD == PA_BOARD_ARTOO_ESP32))')
        rows = drift.read_invocations(text, "PA_COMPONENT_PART")
        self.assertEqual(drift.PART_COLUMNS, len(rows[0]))
        self.assertEqual("(PA_BOARD == PA_BOARD_ARTOO_ESP32)", rows[0][8])

    def test_a_comma_inside_a_string_does_not_split_a_row(self):
        # "Hoverboard, hacked firmware" is a real row's name.
        text = manifest(parts='PA_COMPONENT_PART(15, "hoverboard", "Hoverboard, hacked firmware", C, "p", COMPONENT_STATUS_SUPPORTED, 0, nullptr, 1)')
        rows = drift.read_invocations(text, "PA_COMPONENT_PART")
        self.assertEqual(drift.PART_COLUMNS, len(rows[0]))
        self.assertEqual("Hoverboard, hacked firmware", drift.unquote(rows[0][2]))

    def test_a_manifest_it_cannot_read_is_reported_not_raised(self):
        # A checker that crashes on the file it reports on tells whoever broke
        # it nothing.
        with tempfile.TemporaryDirectory() as tmp:
            path = write(tmp, "component_registry.inc", "// nothing here\n")
            errors: list[str] = []
            categories, parts = drift.load_manifest(path, errors)
        self.assertEqual(([], []), (categories, parts))
        self.assertTrue(any("produced no rows" in e for e in errors), errors)

    def test_a_row_with_the_wrong_column_count_is_reported(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = write(tmp, "component_registry.inc",
                         'PA_COMPONENT_CATEGORY(C, "c", "C", nullptr)\n'
                         'PA_COMPONENT_PART(1, "a", "A", C, "p", COMPONENT_STATUS_SUPPORTED, 0)\n')
            errors: list[str] = []
            drift.load_manifest(path, errors)
        self.assertTrue(any("columns, expected 9" in e for e in errors), errors)


class CapabilityConsumers(unittest.TestCase):
    """The check ADR 0042 scoped to supported rows."""

    VOCABULARY = {"AUDIO_CAP_STATUS_QUERY": "0x01", "AUDIO_CAP_TRACK_COUNT": "0x04"}

    def part(self, part_id="dy_sv5w", status=SUPPORTED, capabilities="AudioDriver::AUDIO_CAP_TRACK_COUNT"):
        return [f"18", f'"{part_id}"', '"DY-SV5W"', "COMPONENT_CATEGORY_SOUND",
                '"soft_uart_binary"', status, capabilities, "nullptr", "1"]

    def run_check(self, parts, consumer_text):
        with tempfile.TemporaryDirectory() as tmp:
            files = [write(tmp, "consumer.js", consumer_text)]
            errors: list[str] = []
            drift.check_capability_consumers(parts, errors, files=files,
                                             vocabulary=self.VOCABULARY)
        return errors

    def test_a_bit_with_a_real_consumer_passes(self):
        errors = self.run_check(
            [self.part()],
            "const AUDIO_CAP_TRACK_COUNT = 0x04;\n"
            "const supports = (caps & AUDIO_CAP_TRACK_COUNT) !== 0;\n",
        )
        self.assertEqual([], errors)

    def test_a_bit_nothing_mentions_is_reported(self):
        # The pre-#340 state: three drivers declared AUDIO_CAP_TRACK_COUNT and
        # data/sound.js did not even mirror the constant.
        errors = self.run_check([self.part()], "const unrelated = 1;\n")
        self.assertEqual(1, len(errors), errors)
        self.assertIn("AUDIO_CAP_TRACK_COUNT", errors[0])
        self.assertIn("consulted by nothing", errors[0])

    def test_a_mirror_that_nothing_branches_on_is_still_reported(self):
        # The stealth case. A file that only DEFINES the constant would
        # otherwise vouch for itself, which is what makes this check more than
        # a grep.
        errors = self.run_check([self.part()], "const AUDIO_CAP_TRACK_COUNT = 0x04;\n")
        self.assertEqual(1, len(errors), errors)
        self.assertIn("AUDIO_CAP_TRACK_COUNT", errors[0])

    def test_the_bit_value_alone_is_not_a_consumer(self):
        # An earlier draft accepted the VALUE as evidence of a consumer, which
        # made this check pass with the bit consulted by nothing: 0x04 appears
        # all over a firmware tree.
        errors = self.run_check([self.part()], "const somethingElse = 0x04;\n")
        self.assertEqual(1, len(errors), errors)

    def test_a_roadmap_row_is_exempt(self):
        # A roadmap row's capabilities have no driver to consume them by
        # construction (ADR 0042), so scoping this to supported rows is the
        # whole point. Declared alongside a supported row that IS consulted, so
        # the run has something to pass on.
        errors = self.run_check(
            [self.part(),
             self.part(part_id="dfplayer_mini", status=ROADMAP,
                       capabilities="AudioDriver::AUDIO_CAP_STATUS_QUERY")],
            "const supports = (caps & AUDIO_CAP_TRACK_COUNT) !== 0;\n",
        )
        self.assertEqual([], errors)

    def test_a_bit_the_interface_header_does_not_define_is_reported(self):
        errors = self.run_check(
            [self.part(capabilities="AudioDriver::AUDIO_CAP_INVENTED")],
            "const supports = (caps & AUDIO_CAP_INVENTED) !== 0;\n",
        )
        self.assertTrue(any("AUDIO_CAP_INVENTED" in e and "does not define it" in e
                            for e in errors), errors)

    def test_a_registry_with_no_declared_capability_at_all_is_reported(self):
        errors = self.run_check([self.part(capabilities="0")], "")
        self.assertTrue(any("no supported row declares a capability" in e for e in errors),
                        errors)


class BoardCapabilityGates(unittest.TestCase):
    GATES = {"PA_CAP_DRIVE_BACKEND_HOVERBOARD", "PA_CAP_NATIVE_WIFI"}

    def part(self, gate, included):
        return ["15", '"hoverboard"', '"Hoverboard"', "COMPONENT_CATEGORY_FOOT_DRIVE",
                '"hoverboard_gen2_uart"', SUPPORTED, "0", gate, included]

    def run_check(self, parts):
        errors: list[str] = []
        drift.check_board_capability_gates(parts, errors, gates=self.GATES)
        return errors

    def test_a_row_that_reports_the_gate_it_consults_passes(self):
        errors = self.run_check([self.part('"PA_CAP_DRIVE_BACKEND_HOVERBOARD"',
                                           "PA_CAP_DRIVE_BACKEND_HOVERBOARD")])
        self.assertEqual([], errors)

    def test_a_universal_row_passes(self):
        errors = self.run_check([self.part("nullptr", "1")])
        self.assertEqual([], errors)

    def test_a_gate_no_manifest_declares_is_reported(self):
        errors = self.run_check([self.part('"PA_CAP_INVENTED"', "PA_CAP_INVENTED")])
        self.assertEqual(1, len(errors), errors)
        self.assertIn("PA_CAP_INVENTED", errors[0])

    def test_reporting_one_gate_and_consulting_another_is_reported(self):
        # The defect a builder feels: they are told to check the wrong board
        # fact for a part that is missing.
        errors = self.run_check([self.part('"PA_CAP_NATIVE_WIFI"',
                                           "PA_CAP_DRIVE_BACKEND_HOVERBOARD")])
        self.assertEqual(1, len(errors), errors)
        self.assertIn("does not consult it", errors[0])

    def test_consulting_a_gate_while_reporting_none_is_reported(self):
        errors = self.run_check([self.part("nullptr", "PA_CAP_DRIVE_BACKEND_HOVERBOARD")])
        self.assertEqual(1, len(errors), errors)
        self.assertIn("reports no gate", errors[0])


class MemberKeys(unittest.TestCase):
    def category(self, member_key):
        return ["COMPONENT_CATEGORY_SOUND", '"sound"', '"Sound"', member_key]

    def run_check(self, categories, serializer_text):
        with tempfile.TemporaryDirectory() as tmp:
            serializer = write(tmp, "config_serializer.cpp", serializer_text)
            errors: list[str] = []
            drift.check_member_keys(categories, errors, serializer=serializer)
        return errors

    def test_a_key_both_read_and_written_passes(self):
        errors = self.run_check(
            [self.category('"snd_member"')],
            'r.readU8("snd_member", def.sound_member);\n'
            'w.writeU8("snd_member", cfg.sound_member);\n',
        )
        self.assertEqual([], errors)

    def test_a_key_only_written_is_reported(self):
        # Rename either half alone and the member silently stops surviving a
        # reboot, with nothing else failing.
        errors = self.run_check(
            [self.category('"snd_member"')],
            'w.writeU8("snd_member", cfg.sound_member);\n',
        )
        self.assertEqual(1, len(errors), errors)
        self.assertIn("snd_member", errors[0])
        self.assertIn("1 time(s)", errors[0])

    def test_a_key_the_serializer_never_mentions_is_reported(self):
        errors = self.run_check([self.category('"snd_member"')], "// nothing\n")
        self.assertEqual(1, len(errors), errors)

    def test_a_family_with_no_member_setting_is_not_asked_for_one(self):
        errors = self.run_check([self.category("nullptr")], "// nothing\n")
        self.assertEqual([], errors)


class RealTree(unittest.TestCase):
    """The repo's own state, so the slice gate still fails on a real drift.

    The gate runs this directory, not `make check-component-drift`. Without
    these three, moving the checker into `tools/` would have taken the repo's
    own Component Registry out of the gate entirely.
    """

    @classmethod
    def setUpClass(cls):
        cls.errors: list[str] = []
        cls.categories, cls.parts = drift.load_manifest(drift.MANIFEST, cls.errors)
        if cls.errors:
            raise AssertionError(f"the shipped manifest does not parse: {cls.errors}")

    def test_every_capability_a_supported_row_declares_has_a_consumer(self):
        errors: list[str] = []
        drift.check_capability_consumers(self.parts, errors)
        self.assertEqual([], errors)

    def test_every_named_gate_exists_and_is_the_one_the_row_consults(self):
        errors: list[str] = []
        drift.check_board_capability_gates(self.parts, errors)
        self.assertEqual([], errors)

    def test_every_declared_member_key_is_read_and_written(self):
        errors: list[str] = []
        drift.check_member_keys(self.categories, errors)
        self.assertEqual([], errors)


if __name__ == "__main__":
    unittest.main()
