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

const OutputRowSetting kOutputRowSettings[] = {
    {"throwMs", RowSettingStore::Row, RowSettingOn::Every, PA_ROW_FIELD(throw_ms),
     SERVO_FIELD_THROW_MS, SettingRule::Range, SERVO_THROW_MS_MIN, SERVO_THROW_MS_MAX, nullptr},
};
"""


def web_api(droid: str, row: str) -> str:
    return (
        "  const SETTING_WORDS = Object.freeze({\n" + droid + "\n  });\n"
        "  const ROW_SETTING_WORDS = Object.freeze({\n" + row + "\n  });\n"
    )


class Check(unittest.TestCase):
    def run_check(self, web_text: str) -> list[str]:
        with tempfile.TemporaryDirectory() as tmp:
            settings = Path(tmp) / "config_settings.cpp"
            settings.write_text(SETTINGS)
            api = Path(tmp) / "web_api.js"
            api.write_text(web_text)
            errors: list[str] = []
            words.check(errors, settings=settings, web_api=api)
        return errors

    def test_words_for_every_setting_pass(self):
        errors = self.run_check(web_api(
            '    speedLimitMax: { word: "top speed", path: "drive.speedLimitMax" },\n'
            '    scream: { word: "Scream track" },',
            '    throwMs: { word: "time to full throw", unit: MS },',
        ))
        self.assertEqual([], errors)

    def test_a_droid_setting_with_no_words_is_reported(self):
        errors = self.run_check(web_api('    scream: { word: "Scream track" },',
                                        '    throwMs: { word: "t" },'))
        self.assertEqual(1, len(errors), errors)
        self.assertIn("speedLimitMax", errors[0])

    def test_an_audio_setting_with_no_words_is_reported(self):
        errors = self.run_check(web_api(
            '    speedLimitMax: { word: "top speed", path: "drive.speedLimitMax" },',
            '    throwMs: { word: "t" },',
        ))
        self.assertEqual(1, len(errors), errors)
        self.assertIn("scream", errors[0])

    def test_words_naming_another_get_path_are_reported(self):
        errors = self.run_check(web_api(
            '    speedLimitMax: { word: "top speed", path: "drive.topSpeed" },\n'
            '    scream: { word: "Scream track" },',
            '    throwMs: { word: "t" },',
        ))
        self.assertEqual(1, len(errors), errors)
        self.assertIn("drive.speedLimitMax", errors[0])

    def test_a_row_setting_with_no_words_is_reported(self):
        errors = self.run_check(web_api(
            '    speedLimitMax: { word: "top speed", path: "drive.speedLimitMax" },\n'
            '    scream: { word: "Scream track" },', ""))
        self.assertEqual(1, len(errors), errors)
        self.assertIn("throwMs", errors[0])


class RealTree(unittest.TestCase):
    def test_every_declared_setting_has_words(self):
        errors: list[str] = []
        words.check(errors)
        self.assertEqual([], errors)


if __name__ == "__main__":
    unittest.main()
