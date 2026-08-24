// =============================================================================
// test/test_native/test_api_helpers/test_api_helpers.cpp
//
// Native unit tests for web API parsing helpers and format functions.
// Tests: parseDriveValue, parseUint32Value, parseBoolValue,
//        formatSleepControlJson, formatIdentityJson, formatSpeedPresetResponseJson,
//        formatAuxLedStateJson
// =============================================================================
#include <unity.h>

#include <string.h>
#include "api_helpers.h"
#include "api_system.h"
#include "api_identity.h"
#include "api_drive.h"
#include "api_aux_led.h"

void setUp() {
}
void tearDown() {
}

// --- parseDriveValue() tests ---

void test_parseDrive_zero() {
    int16_t out = 99;
    TEST_ASSERT_TRUE(parseDriveValue("0", &out));
    TEST_ASSERT_EQUAL_INT16(0, out);
}

void test_parseDrive_positive() {
    int16_t out = 0;
    TEST_ASSERT_TRUE(parseDriveValue("500", &out));
    TEST_ASSERT_EQUAL_INT16(500, out);
}

void test_parseDrive_negative() {
    int16_t out = 0;
    TEST_ASSERT_TRUE(parseDriveValue("-300", &out));
    TEST_ASSERT_EQUAL_INT16(-300, out);
}

void test_parseDrive_max_positive() {
    int16_t out = 0;
    TEST_ASSERT_TRUE(parseDriveValue("600", &out));
    TEST_ASSERT_EQUAL_INT16(600, out);
}

void test_parseDrive_max_negative() {
    int16_t out = 0;
    TEST_ASSERT_TRUE(parseDriveValue("-600", &out));
    TEST_ASSERT_EQUAL_INT16(-600, out);
}

void test_parseDrive_empty_fails() {
    int16_t out = 99;
    TEST_ASSERT_FALSE(parseDriveValue("", &out));
}

void test_parseDrive_null_fails() {
    int16_t out = 99;
    TEST_ASSERT_FALSE(parseDriveValue(nullptr, &out));
}

void test_parseDrive_alpha_fails() {
    int16_t out = 99;
    TEST_ASSERT_FALSE(parseDriveValue("abc", &out));
}

void test_parseDrive_trailing_chars_fails() {
    int16_t out = 99;
    TEST_ASSERT_FALSE(parseDriveValue("100x", &out));
}

void test_parseDrive_leading_space_accepted() {
    int16_t out = 0;
    TEST_ASSERT_TRUE(parseDriveValue(" 100", &out));
    TEST_ASSERT_EQUAL_INT16(100, out);
}

// --- parseUint32Value() tests ---

void test_parseUint32_zero() {
    uint32_t out = 99;
    TEST_ASSERT_TRUE(parseUint32Value("0", &out));
    TEST_ASSERT_EQUAL_UINT32(0, out);
}

void test_parseUint32_positive() {
    uint32_t out = 0;
    TEST_ASSERT_TRUE(parseUint32Value("500", &out));
    TEST_ASSERT_EQUAL_UINT32(500, out);
}

void test_parseUint32_large() {
    uint32_t out = 0;
    TEST_ASSERT_TRUE(parseUint32Value("5000", &out));
    TEST_ASSERT_EQUAL_UINT32(5000, out);
}

void test_parseUint32_empty_fails() {
    uint32_t out = 99;
    TEST_ASSERT_FALSE(parseUint32Value("", &out));
}

void test_parseUint32_null_fails() {
    uint32_t out = 99;
    TEST_ASSERT_FALSE(parseUint32Value(nullptr, &out));
}

void test_parseUint32_negative_fails() {
    uint32_t out = 99;
    TEST_ASSERT_FALSE(parseUint32Value("-1", &out));
}

void test_parseUint32_alpha_fails() {
    uint32_t out = 99;
    TEST_ASSERT_FALSE(parseUint32Value("abc", &out));
}

void test_parseUint32_trailing_chars_fails() {
    uint32_t out = 99;
    TEST_ASSERT_FALSE(parseUint32Value("100ms", &out));
}

// --- parseBoolValue() tests ---

void test_parseBool_true_string() {
    bool out = false;
    TEST_ASSERT_TRUE(parseBoolValue("true", &out));
    TEST_ASSERT_TRUE(out);
}

void test_parseBool_false_string() {
    bool out = true;
    TEST_ASSERT_TRUE(parseBoolValue("false", &out));
    TEST_ASSERT_FALSE(out);
}

void test_parseBool_one() {
    bool out = false;
    TEST_ASSERT_TRUE(parseBoolValue("1", &out));
    TEST_ASSERT_TRUE(out);
}

void test_parseBool_zero() {
    bool out = true;
    TEST_ASSERT_TRUE(parseBoolValue("0", &out));
    TEST_ASSERT_FALSE(out);
}

void test_parseBool_empty_fails() {
    bool out = true;
    TEST_ASSERT_FALSE(parseBoolValue("", &out));
}

void test_parseBool_null_fails() {
    bool out = true;
    TEST_ASSERT_FALSE(parseBoolValue(nullptr, &out));
}

void test_parseBool_uppercase_fails() {
    bool out = false;
    TEST_ASSERT_FALSE(parseBoolValue("True", &out));
}

void test_parseBool_yes_fails() {
    bool out = false;
    TEST_ASSERT_FALSE(parseBoolValue("yes", &out));
}

// --- normalizeDroidName() tests ---

void test_normalizeDroidName_accepts_lowercase() {
    char out[33] = {};
    TEST_ASSERT_TRUE(normalizeDroidName("r2-unit07", out, sizeof(out)));
    TEST_ASSERT_EQUAL_STRING("r2-unit07", out);
}

void test_normalizeDroidName_rejects_uppercase() {
    char out[33] = {};
    TEST_ASSERT_FALSE(normalizeDroidName("R2-unit07", out, sizeof(out)));
}

void test_normalizeDroidName_rejects_spaces() {
    char out[33] = {};
    TEST_ASSERT_FALSE(normalizeDroidName("Artoo Body", out, sizeof(out)));
}

void test_normalizeDroidName_rejects_invalid_chars() {
    char out[33] = {};
    TEST_ASSERT_FALSE(normalizeDroidName("Artoo!Body", out, sizeof(out)));
}

void test_normalizeDroidName_empty_fails() {
    char out[33] = {};
    TEST_ASSERT_FALSE(normalizeDroidName("", out, sizeof(out)));
}

void test_normalizeDroidName_too_long_fails() {
    char out[33] = {};
    TEST_ASSERT_FALSE(normalizeDroidName("abcdefghijklmnopqrstuvwxyz1234567", out, sizeof(out)));
}

// --- formatSleepControlJson() tests ---

void test_formatSleepControlJson_sleep_changed() {
    char out[96] = {};
    TEST_ASSERT_TRUE(formatSleepControlJson(out, sizeof(out), true, true));
    TEST_ASSERT_EQUAL_STRING("{\"ok\":true,\"sleepMode\":true,\"changed\":true}", out);
}

void test_formatSleepControlJson_wake_unchanged() {
    char out[96] = {};
    TEST_ASSERT_TRUE(formatSleepControlJson(out, sizeof(out), false, false));
    TEST_ASSERT_EQUAL_STRING("{\"ok\":true,\"sleepMode\":false,\"changed\":false}", out);
}

void test_formatSleepControlJson_null_buffer_fails() {
    TEST_ASSERT_FALSE(formatSleepControlJson(nullptr, 32, true, true));
}

void test_formatSleepControlJson_small_buffer_fails() {
    char out[16] = {};
    TEST_ASSERT_FALSE(formatSleepControlJson(out, sizeof(out), true, true));
}

// --- formatIdentityJson() tests ---

void test_formatIdentityJson_valid_payload() {
    char out[IDENTITY_JSON_MAX_BYTES] = {};
    TEST_ASSERT_TRUE(formatIdentityJson(out, sizeof(out), "r2-unit", true));
    TEST_ASSERT_NOT_NULL(strstr(out, "{\"droidName\":\"r2-unit\",\"mdnsUseName\":true"));
    TEST_ASSERT_NOT_NULL(strstr(out, "\"board\":\"artoo_esp32\""));
    TEST_ASSERT_NOT_NULL(strstr(out, "\"board_capabilities\""));
    TEST_ASSERT_NOT_NULL(strstr(out, "\"build_flags\""));
}

void test_formatIdentityJson_small_buffer_fails() {
    char out[20] = {};
    TEST_ASSERT_FALSE(formatIdentityJson(out, sizeof(out), "r2-unit", false));
}

// --- formatSpeedPresetResponseJson() tests ---

void test_formatSpeedPresetResponseJson_valid_payload() {
    char out[96] = {};
    TEST_ASSERT_TRUE(formatSpeedPresetResponseJson(out, sizeof(out), SpeedPresetId::Turbo, 600));
    TEST_ASSERT_EQUAL_STRING("{\"ok\":true,\"preset\":\"turbo\",\"speedLimitMax\":600}", out);
}

void test_formatSpeedPresetResponseJson_null_buffer_fails() {
    TEST_ASSERT_FALSE(formatSpeedPresetResponseJson(nullptr, 32, SpeedPresetId::Slow, 200));
}

void test_formatSpeedPresetResponseJson_small_buffer_fails() {
    char out[24] = {};
    TEST_ASSERT_FALSE(formatSpeedPresetResponseJson(out, sizeof(out), SpeedPresetId::Normal, 350));
}

void test_formatSpeedPresetResponseJson_out_of_range_fails() {
    char out[96] = {};
    TEST_ASSERT_FALSE(formatSpeedPresetResponseJson(out, sizeof(out), SpeedPresetId::Slow, 601));
}

void test_formatSpeedPresetResponseJson_invalid_preset_fails() {
    char out[96] = {};
    TEST_ASSERT_FALSE(
        formatSpeedPresetResponseJson(out, sizeof(out), (SpeedPresetId)99, 200));
}


// --- formatAuxLedStateJson() tests ---

void test_formatAuxLedStateJson_valid_payload() {
    char out[128] = {};
    TEST_ASSERT_TRUE(formatAuxLedStateJson(out, sizeof(out), 19, 10, 20, 30, "pulse"));
    TEST_ASSERT_EQUAL_STRING(
        "{\"ok\":true,\"auxLed\":{\"pin\":19,\"r\":10,\"g\":20,\"b\":30,\"effect\":\"pulse\"}}",
        out);
}

void test_formatAuxLedStateJson_null_effect_fails() {
    char out[128] = {};
    TEST_ASSERT_FALSE(formatAuxLedStateJson(out, sizeof(out), 19, 10, 20, 30, nullptr));
}

void test_formatAuxLedStateJson_small_buffer_fails() {
    char out[16] = {};
    TEST_ASSERT_FALSE(formatAuxLedStateJson(out, sizeof(out), 19, 10, 20, 30, "solid"));
}

// trimAsciiWhitespace() replaced the Arduino String::trim() that the sequence
// and action-test routes relied on before they took copied-out C strings across
// the WebRequest seam. An operator who pastes a name with a trailing space has
// to keep getting the same answer they did before.
void test_trimAsciiWhitespace_strips_both_ends() {
    char s[32] = "  DM:ROCKMARCH\t\r\n";
    trimAsciiWhitespace(s);
    TEST_ASSERT_EQUAL_STRING("DM:ROCKMARCH", s);
}

void test_trimAsciiWhitespace_leaves_inner_spaces() {
    char s[32] = "  a b  ";
    trimAsciiWhitespace(s);
    TEST_ASSERT_EQUAL_STRING("a b", s);
}

void test_trimAsciiWhitespace_all_whitespace_becomes_empty() {
    char s[32] = " \t\r\n ";
    trimAsciiWhitespace(s);
    TEST_ASSERT_EQUAL_STRING("", s);
}

void test_trimAsciiWhitespace_untouched_string_is_unchanged() {
    char s[32] = "DM:ROCKMARCH";
    trimAsciiWhitespace(s);
    TEST_ASSERT_EQUAL_STRING("DM:ROCKMARCH", s);
}

int main() {
    UNITY_BEGIN();

    RUN_TEST(test_parseDrive_zero);
    RUN_TEST(test_parseDrive_positive);
    RUN_TEST(test_parseDrive_negative);
    RUN_TEST(test_parseDrive_max_positive);
    RUN_TEST(test_parseDrive_max_negative);
    RUN_TEST(test_parseDrive_empty_fails);
    RUN_TEST(test_parseDrive_null_fails);
    RUN_TEST(test_parseDrive_alpha_fails);
    RUN_TEST(test_parseDrive_trailing_chars_fails);
    RUN_TEST(test_parseDrive_leading_space_accepted);

    RUN_TEST(test_parseUint32_zero);
    RUN_TEST(test_parseUint32_positive);
    RUN_TEST(test_parseUint32_large);
    RUN_TEST(test_parseUint32_empty_fails);
    RUN_TEST(test_parseUint32_null_fails);
    RUN_TEST(test_parseUint32_negative_fails);
    RUN_TEST(test_parseUint32_alpha_fails);
    RUN_TEST(test_parseUint32_trailing_chars_fails);

    RUN_TEST(test_parseBool_true_string);
    RUN_TEST(test_parseBool_false_string);
    RUN_TEST(test_parseBool_one);
    RUN_TEST(test_parseBool_zero);
    RUN_TEST(test_parseBool_empty_fails);
    RUN_TEST(test_parseBool_null_fails);
    RUN_TEST(test_parseBool_uppercase_fails);
    RUN_TEST(test_parseBool_yes_fails);
    RUN_TEST(test_normalizeDroidName_accepts_lowercase);
    RUN_TEST(test_normalizeDroidName_rejects_uppercase);
    RUN_TEST(test_normalizeDroidName_rejects_spaces);
    RUN_TEST(test_normalizeDroidName_rejects_invalid_chars);
    RUN_TEST(test_normalizeDroidName_empty_fails);
    RUN_TEST(test_normalizeDroidName_too_long_fails);
    RUN_TEST(test_formatSleepControlJson_sleep_changed);
    RUN_TEST(test_formatSleepControlJson_wake_unchanged);
    RUN_TEST(test_formatSleepControlJson_null_buffer_fails);
    RUN_TEST(test_formatSleepControlJson_small_buffer_fails);
    RUN_TEST(test_formatIdentityJson_valid_payload);
    RUN_TEST(test_formatIdentityJson_small_buffer_fails);
    RUN_TEST(test_formatSpeedPresetResponseJson_valid_payload);
    RUN_TEST(test_formatSpeedPresetResponseJson_null_buffer_fails);
    RUN_TEST(test_formatSpeedPresetResponseJson_small_buffer_fails);
    RUN_TEST(test_formatSpeedPresetResponseJson_out_of_range_fails);
    RUN_TEST(test_formatSpeedPresetResponseJson_invalid_preset_fails);
    RUN_TEST(test_formatAuxLedStateJson_valid_payload);
    RUN_TEST(test_formatAuxLedStateJson_null_effect_fails);
    RUN_TEST(test_formatAuxLedStateJson_small_buffer_fails);
    RUN_TEST(test_trimAsciiWhitespace_strips_both_ends);
    RUN_TEST(test_trimAsciiWhitespace_leaves_inner_spaces);
    RUN_TEST(test_trimAsciiWhitespace_all_whitespace_becomes_empty);
    RUN_TEST(test_trimAsciiWhitespace_untouched_string_is_unchanged);

    return UNITY_END();
}
