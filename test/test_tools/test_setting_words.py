"""Tests for tools/check_setting_words.py.

Each case writes a small declaration file and a small words table and asks
the check about them, so it is proven able to fail; the last class runs it
over the real tree, which is what keeps the slice gate honest.
"""

import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "tools"))

import check_setting_words as words  # noqa: E402

SETTINGS = """
const ConfigSetting kConfigSettings[] = {
    PA_RANGE("speedLimitMax", "drive.speedLimitMax", "spd_max", Immediate, Drive, DriveConfig, speedLimitMax,
             0, SPEED_LIMIT_MAX, SPEED_LIMIT_MAX),
    PA_BOOL("enableArm1", nullptr, "en_arm1", AtReboot, System, SystemConfig, enable_arm1, false),
    PA_BOOL("enableDomeEsc", "components.domeEsc.enabled", "en_dome_esc", AtReboot, System,
            SystemConfig, enable_dome_esc, false),
};

const ConfigSetting kAudioSettings[] = {
    PA_TRACK("scream", "snd_scream", Immediate, snd_scream, AUDIO_TRACK_SCREAM),
};

const ConfigSetting kCatalogBindingSettings[] = {
    {"bank", nullptr, nullptr, ApplyTiming::Immediate, SettingSection::Audio, 0, SettingStorage::U8, 1, SettingRule::Range, 1,
     6, 1, nullptr, 0, nullptr, SettingDoor::AudioTracks, false},
};

const OutputRowSetting kOutputRowSettings[] = {
    {"throwMs", ApplyTiming::Immediate, RowSettingStore::Row, RowSettingOn::Every, PA_ROW_FIELD(throw_ms),
     SERVO_FIELD_THROW_MS, SettingRule::Range, SERVO_THROW_MS_MIN, SERVO_THROW_MS_MAX, nullptr},
};
"""


# One Record module and the act fields, and the words every case carries for
# them unless it is about them.
RECORD = """
const ConfigRecordField kFields[FieldCount] = {
    {"domeDesign", "droidBuild.domeDesign", ApplyTiming::Immediate, "mk41"},
};
"""

ACTS = """
constexpr ConfigActField kActFields[ActFieldCount] = {
    {"captureUs", "500..2500"},
};
"""

RECORD_AND_ACT_WORDS = (
    '    domeDesign: { word: "dome design", path: "droidBuild.domeDesign", applies: "immediate" },\n'
    '    captureUs: { word: "captured width", on: "captureOutput" },\n'
    '    enableDomeEsc: { label: "Dome ESC", word: "Dome ESC", path: "components.domeEsc.enabled",'
    ' applies: "at-reboot" },'
)


def web_api(droid: str, row: str, records_and_acts: str = RECORD_AND_ACT_WORDS) -> str:
    return (
        "  const SETTING_WORDS = Object.freeze({\n" + droid + "\n" + records_and_acts + "\n  });\n"
        "  const ROW_SETTING_WORDS = Object.freeze({\n" + row + "\n  });\n"
    )


def fixtures(tmp: str) -> dict:
    record = Path(tmp) / "config_record_droid_build.cpp"
    record.write_text(RECORD)
    acts = Path(tmp) / "api_config_apply.cpp"
    acts.write_text(ACTS)
    return {"records": [record], "acts": acts, "pages": []}


class Check(unittest.TestCase):
    def run_check(self, web_text: str) -> list[str]:
        with tempfile.TemporaryDirectory() as tmp:
            settings = Path(tmp) / "config_settings.cpp"
            settings.write_text(SETTINGS)
            api = Path(tmp) / "web_api.js"
            api.write_text(web_text)
            errors: list[str] = []
            words.check(errors, settings=settings, web_api=api, **fixtures(tmp))
        return errors

    def test_words_for_every_setting_pass(self):
        errors = self.run_check(web_api(
            '    speedLimitMax: { word: "top speed", path: "drive.speedLimitMax", applies: "immediate" },\n'
            '    scream: { word: "Scream track", applies: "immediate" },\n    bank: { word: "b" },',
            '    throwMs: { word: "time to full throw", unit: MS, applies: "immediate" },',
        ))
        self.assertEqual([], errors)

    def test_a_droid_setting_with_no_words_is_reported(self):
        errors = self.run_check(web_api('    scream: { word: "Scream track", applies: "immediate" },\n    bank: { word: "b" },',
                                        '    throwMs: { word: "t", applies: "immediate" },'))
        self.assertEqual(1, len(errors), errors)
        self.assertIn("speedLimitMax", errors[0])

    def test_an_audio_setting_with_no_words_is_reported(self):
        errors = self.run_check(web_api(
            '    speedLimitMax: { word: "top speed", path: "drive.speedLimitMax", applies: "immediate" },\n'
            '    bank: { word: "b" },',
            '    throwMs: { word: "t", applies: "immediate" },',
        ))
        self.assertEqual(1, len(errors), errors)
        self.assertIn("scream", errors[0])

    def test_a_binding_part_with_no_words_is_reported(self):
        errors = self.run_check(web_api(
            '    speedLimitMax: { word: "top speed", path: "drive.speedLimitMax", applies: "immediate" },\n'
            '    scream: { word: "Scream track", applies: "immediate" },',
            '    throwMs: { word: "t", applies: "immediate" },',
        ))
        self.assertEqual(1, len(errors), errors)
        self.assertIn("bank", errors[0])

    def test_words_naming_another_get_path_are_reported(self):
        errors = self.run_check(web_api(
            '    speedLimitMax: { word: "top speed", path: "drive.topSpeed", applies: "immediate" },\n'
            '    scream: { word: "Scream track", applies: "immediate" },\n    bank: { word: "b" },',
            '    throwMs: { word: "t", applies: "immediate" },',
        ))
        self.assertEqual(1, len(errors), errors)
        self.assertIn("drive.speedLimitMax", errors[0])

    def test_a_row_setting_with_no_words_is_reported(self):
        errors = self.run_check(web_api(
            '    speedLimitMax: { word: "top speed", path: "drive.speedLimitMax", applies: "immediate" },\n'
            '    scream: { word: "Scream track", applies: "immediate" },\n    bank: { word: "b" },', ""))
        self.assertEqual(1, len(errors), errors)
        self.assertIn("throwMs", errors[0])


    def test_a_key_past_fifteen_characters_is_reported(self):
        with tempfile.TemporaryDirectory() as tmp:
            settings = Path(tmp) / "config_settings.cpp"
            settings.write_text(SETTINGS.replace('"snd_scream"', '"snd_scream_longer"'))
            api = Path(tmp) / "web_api.js"
            api.write_text(web_api('    speedLimitMax: { word: "t", path: "drive.speedLimitMax", applies: "immediate" },\n'
                                   '    scream: { word: "s", applies: "immediate" },\n    bank: { word: "b" },', '    throwMs: { word: "t", applies: "immediate" },'))
            errors: list[str] = []
            words.check(errors, settings=settings, web_api=api, **fixtures(tmp))
        self.assertEqual(1, len(errors), errors)
        self.assertIn("17 characters", errors[0])

    def test_a_key_declared_twice_is_reported(self):
        with tempfile.TemporaryDirectory() as tmp:
            settings = Path(tmp) / "config_settings.cpp"
            settings.write_text(SETTINGS.replace('"snd_scream"', '"spd_max"'))
            api = Path(tmp) / "web_api.js"
            api.write_text(web_api('    speedLimitMax: { word: "t", path: "drive.speedLimitMax", applies: "immediate" },\n'
                                   '    scream: { word: "s", applies: "immediate" },\n    bank: { word: "b" },', '    throwMs: { word: "t", applies: "immediate" },'))
            errors: list[str] = []
            words.check(errors, settings=settings, web_api=api, **fixtures(tmp))
        self.assertEqual(1, len(errors), errors)
        self.assertIn("both declare", errors[0])

    def test_a_record_field_with_no_words_is_reported(self):
        errors = self.run_check(web_api(
            '    speedLimitMax: { word: "top speed", path: "drive.speedLimitMax", applies: "immediate" },\n'
            '    scream: { word: "Scream track", applies: "immediate" },\n    bank: { word: "b" },',
            '    throwMs: { word: "t", applies: "immediate" },',
            '    captureUs: { word: "captured width" },\n'
            '    enableDomeEsc: { label: "Dome ESC", path: "components.domeEsc.enabled", applies: "at-reboot" },',
        ))
        self.assertEqual(1, len(errors), errors)
        self.assertIn("domeDesign is a declared Record field", errors[0])

    def test_a_record_field_named_at_another_get_path_is_reported(self):
        errors = self.run_check(web_api(
            '    speedLimitMax: { word: "top speed", path: "drive.speedLimitMax", applies: "immediate" },\n'
            '    scream: { word: "Scream track", applies: "immediate" },\n    bank: { word: "b" },',
            '    throwMs: { word: "t", applies: "immediate" },',
            '    domeDesign: { word: "dome design", path: "droidBuild.dome", applies: "immediate" },\n'
            '    captureUs: { word: "captured width" },\n'
            '    enableDomeEsc: { label: "Dome ESC", path: "components.domeEsc.enabled", applies: "at-reboot" },',
        ))
        self.assertEqual(1, len(errors), errors)
        self.assertIn("droidBuild.domeDesign", errors[0])

    def test_an_act_field_with_no_words_is_reported(self):
        errors = self.run_check(web_api(
            '    speedLimitMax: { word: "top speed", path: "drive.speedLimitMax", applies: "immediate" },\n'
            '    scream: { word: "Scream track", applies: "immediate" },\n    bank: { word: "b" },',
            '    throwMs: { word: "t", applies: "immediate" },',
            '    domeDesign: { word: "dome design", path: "droidBuild.domeDesign", applies: "immediate" },\n'
            '    enableDomeEsc: { label: "Dome ESC", path: "components.domeEsc.enabled", applies: "at-reboot" },',
        ))
        self.assertEqual(1, len(errors), errors)
        self.assertIn("captureUs is a declared act field", errors[0])

    def test_a_timing_that_is_not_the_firmware_s_is_reported(self):
        errors = self.run_check(web_api(
            '    speedLimitMax: { word: "top speed", path: "drive.speedLimitMax", applies: "at-reboot" },\n'
            '    scream: { word: "Scream track", applies: "immediate" },\n    bank: { word: "b" },',
            '    throwMs: { word: "t", applies: "immediate" },',
        ))
        self.assertEqual(1, len(errors), errors)
        self.assertIn("speedLimitMax's words say it takes effect at-reboot", errors[0])

    def test_an_entry_that_does_not_say_when_is_reported(self):
        errors = self.run_check(web_api(
            '    speedLimitMax: { word: "top speed", path: "drive.speedLimitMax", applies: "immediate" },\n'
            '    scream: { word: "Scream track", applies: "immediate" },\n    bank: { word: "b" },',
            '    throwMs: { word: "t" },',
        ))
        self.assertEqual(1, len(errors), errors)
        self.assertIn("throwMs's words do not say when it takes effect", errors[0])

    def test_a_page_naming_a_toggle_s_label_beside_it_is_reported(self):
        with tempfile.TemporaryDirectory() as tmp:
            settings = Path(tmp) / "config_settings.cpp"
            settings.write_text(SETTINGS)
            api = Path(tmp) / "web_api.js"
            api.write_text(web_api(
                '    speedLimitMax: { word: "t", path: "drive.speedLimitMax", applies: "immediate" },\n'
                '    scream: { label: "Scream", word: "s", applies: "immediate" },\n    bank: { word: "b" },',
                '    throwMs: { word: "t", applies: "immediate" },'))
            page = Path(tmp) / "app.js"
            # The toggle beside its label is flagged; a sequence called Scream is not.
            page.write_text('const LABELS = [["domeEsc", "Dome ESC"]];\n'
                            '{ token: "droid_seq_scream", label: "Scream" }\n')
            errors: list[str] = []
            words.check(errors, settings=settings, web_api=api, **{**fixtures(tmp), "pages": [page]})
        self.assertEqual(1, len(errors), errors)
        self.assertIn("app.js:1 names enableDomeEsc's label 'Dome ESC'", errors[0])


class RealTree(unittest.TestCase):
    def test_every_declared_setting_has_words(self):
        errors: list[str] = []
        words.check(errors)
        self.assertEqual([], errors)


if __name__ == "__main__":
    unittest.main()
