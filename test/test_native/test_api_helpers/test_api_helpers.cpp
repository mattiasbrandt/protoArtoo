// =============================================================================
// test/test_native/test_api_helpers/test_api_helpers.cpp
//
// Native unit tests for web API parsing helpers.
// Tests: parseDriveValue, parseUint32Value, parseBoolValue.
// =============================================================================
#include <unity.h>

#include <string.h>
#include "api_helpers.h"

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
    RUN_TEST(test_formatSleepControlJson_sleep_changed);
    RUN_TEST(test_formatSleepControlJson_wake_unchanged);
    RUN_TEST(test_formatSleepControlJson_null_buffer_fails);
    RUN_TEST(test_formatSleepControlJson_small_buffer_fails);
    RUN_TEST(test_formatSpeedPresetResponseJson_valid_payload);
    RUN_TEST(test_formatSpeedPresetResponseJson_null_buffer_fails);
    RUN_TEST(test_formatSpeedPresetResponseJson_small_buffer_fails);
    RUN_TEST(test_formatSpeedPresetResponseJson_out_of_range_fails);
    RUN_TEST(test_formatSpeedPresetResponseJson_invalid_preset_fails);
    RUN_TEST(test_formatAuxLedStateJson_valid_payload);
    RUN_TEST(test_formatAuxLedStateJson_null_effect_fails);
    RUN_TEST(test_formatAuxLedStateJson_small_buffer_fails);

    return UNITY_END();
}
