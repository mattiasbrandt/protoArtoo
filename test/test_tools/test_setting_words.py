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
    PA_RANGE("speedLimitMax", "drive.speedLimitMax", "spd_max", Drive, DriveConfig, speedLimitMax,
             0, SPEED_LIMIT_MAX, SPEED_LIMIT_MAX),
    PA_BOOL("enableArm1", nullptr, "en_arm1", System, SystemConfig, enable_arm1, false),
};

const ConfigSetting kAudioSettings[] = {
    PA_TRACK("scream", "snd_scream", snd_scream, AUDIO_TRACK_SCREAM),
};

const ConfigSetting kCatalogBindingSettings[] = {
    {"bank", nullptr, nullptr, SettingSection::Audio, 0, SettingStorage::U8, 1, SettingRule::Range, 1,
     6, 1, nullptr, 0, nullptr, SettingDoor::AudioTracks, false},
};

const OutputRowSetting kOutputRowSettings[] = {
    {"throwMs", RowSettingStore::Row, RowSettingOn::Every, PA_ROW_FIELD(throw_ms),
     SERVO_FIELD_THROW_MS, SettingRule::Range, SERVO_THROW_MS_MIN, SERVO_THROW_MS_MAX, nullptr},
};
"""


# One Record module and the act fields, and the words every case carries for
# them unless it is about them.
RECORD = """
const ConfigRecordField kFields[FieldCount] = {
    {"domeDesign", "droidBuild.domeDesign", "mk41"},
};
"""

ACTS = """
constexpr ConfigActField kActFields[ActFieldCount] = {
    {"captureUs", "500..2500"},
};
"""

RECORD_AND_ACT_WORDS = (
    '    domeDesign: { word: "dome design", path: "droidBuild.domeDesign" },\n'
    '    captureUs: { word: "captured width", on: "captureOutput" },'
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
    return {"records": [record], "acts": acts}


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
            '    speedLimitMax: { word: "top speed", path: "drive.speedLimitMax" },\n'
            '    scream: { word: "Scream track" },\n    bank: { word: "b" },',
            '    throwMs: { word: "time to full throw", unit: MS },',
        ))
        self.assertEqual([], errors)

    def test_a_droid_setting_with_no_words_is_reported(self):
        errors = self.run_check(web_api('    scream: { word: "Scream track" },\n    bank: { word: "b" },',
                                        '    throwMs: { word: "t" },'))
        self.assertEqual(1, len(errors), errors)
        self.assertIn("speedLimitMax", errors[0])

    def test_an_audio_setting_with_no_words_is_reported(self):
        errors = self.run_check(web_api(
            '    speedLimitMax: { word: "top speed", path: "drive.speedLimitMax" },\n'
            '    bank: { word: "b" },',
            '    throwMs: { word: "t" },',
        ))
        self.assertEqual(1, len(errors), errors)
        self.assertIn("scream", errors[0])

    def test_a_binding_part_with_no_words_is_reported(self):
        errors = self.run_check(web_api(
            '    speedLimitMax: { word: "top speed", path: "drive.speedLimitMax" },\n'
            '    scream: { word: "Scream track" },',
            '    throwMs: { word: "t" },',
        ))
        self.assertEqual(1, len(errors), errors)
        self.assertIn("bank", errors[0])

    def test_words_naming_another_get_path_are_reported(self):
        errors = self.run_check(web_api(
            '    speedLimitMax: { word: "top speed", path: "drive.topSpeed" },\n'
            '    scream: { word: "Scream track" },\n    bank: { word: "b" },',
            '    throwMs: { word: "t" },',
        ))
        self.assertEqual(1, len(errors), errors)
        self.assertIn("drive.speedLimitMax", errors[0])

    def test_a_row_setting_with_no_words_is_reported(self):
        errors = self.run_check(web_api(
            '    speedLimitMax: { word: "top speed", path: "drive.speedLimitMax" },\n'
            '    scream: { word: "Scream track" },\n    bank: { word: "b" },', ""))
        self.assertEqual(1, len(errors), errors)
        self.assertIn("throwMs", errors[0])


    def test_a_key_past_fifteen_characters_is_reported(self):
        with tempfile.TemporaryDirectory() as tmp:
            settings = Path(tmp) / "config_settings.cpp"
            settings.write_text(SETTINGS.replace('"snd_scream"', '"snd_scream_longer"'))
            api = Path(tmp) / "web_api.js"
            api.write_text(web_api('    speedLimitMax: { word: "t", path: "drive.speedLimitMax" },\n'
                                   '    scream: { word: "s" },\n    bank: { word: "b" },', '    throwMs: { word: "t" },'))
            errors: list[str] = []
            words.check(errors, settings=settings, web_api=api, **fixtures(tmp))
        self.assertEqual(1, len(errors), errors)
        self.assertIn("17 characters", errors[0])

    def test_a_key_declared_twice_is_reported(self):
        with tempfile.TemporaryDirectory() as tmp:
            settings = Path(tmp) / "config_settings.cpp"
            settings.write_text(SETTINGS.replace('"snd_scream"', '"spd_max"'))
            api = Path(tmp) / "web_api.js"
            api.write_text(web_api('    speedLimitMax: { word: "t", path: "drive.speedLimitMax" },\n'
                                   '    scream: { word: "s" },\n    bank: { word: "b" },', '    throwMs: { word: "t" },'))
            errors: list[str] = []
            words.check(errors, settings=settings, web_api=api, **fixtures(tmp))
        self.assertEqual(1, len(errors), errors)
        self.assertIn("both declare", errors[0])

    def test_a_record_field_with_no_words_is_reported(self):
        errors = self.run_check(web_api(
            '    speedLimitMax: { word: "top speed", path: "drive.speedLimitMax" },\n'
            '    scream: { word: "Scream track" },\n    bank: { word: "b" },',
            '    throwMs: { word: "t" },',
            '    captureUs: { word: "captured width" },',
        ))
        self.assertEqual(1, len(errors), errors)
        self.assertIn("domeDesign is a declared Record field", errors[0])

    def test_a_record_field_named_at_another_get_path_is_reported(self):
        errors = self.run_check(web_api(
            '    speedLimitMax: { word: "top speed", path: "drive.speedLimitMax" },\n'
            '    scream: { word: "Scream track" },\n    bank: { word: "b" },',
            '    throwMs: { word: "t" },',
            '    domeDesign: { word: "dome design", path: "droidBuild.dome" },\n'
            '    captureUs: { word: "captured width" },',
        ))
        self.assertEqual(1, len(errors), errors)
        self.assertIn("droidBuild.domeDesign", errors[0])

    def test_an_act_field_with_no_words_is_reported(self):
        errors = self.run_check(web_api(
            '    speedLimitMax: { word: "top speed", path: "drive.speedLimitMax" },\n'
            '    scream: { word: "Scream track" },\n    bank: { word: "b" },',
            '    throwMs: { word: "t" },',
            '    domeDesign: { word: "dome design", path: "droidBuild.domeDesign" },',
        ))
        self.assertEqual(1, len(errors), errors)
        self.assertIn("captureUs is a declared act field", errors[0])


class RealTree(unittest.TestCase):
    def test_every_declared_setting_has_words(self):
        errors: list[str] = []
        words.check(errors)
        self.assertEqual([], errors)


if __name__ == "__main__":
    unittest.main()
