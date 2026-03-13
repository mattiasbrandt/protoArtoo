// =============================================================================
// test/test_native/test_api_helpers/test_api_helpers.cpp
//
// Native unit tests for web API parsing helpers.
// Tests: parseDriveValue, parseUint32Value, parseBoolValue, resolveManualCommand.
// =============================================================================
#include <unity.h>

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

// --- resolveManualCommand() tests ---

void test_resolve_estop() {
    TEST_ASSERT_EQUAL_INT(MC_ESTOP, (int)resolveManualCommand("estop"));
}

void test_resolve_clear_estop() {
    TEST_ASSERT_EQUAL_INT(MC_CLEAR_ESTOP, (int)resolveManualCommand("clear_estop"));
}

void test_resolve_enable_web_control() {
    TEST_ASSERT_EQUAL_INT(MC_ENABLE_WEB_CONTROL, (int)resolveManualCommand("enable_web_control"));
}

void test_resolve_disable_web_control() {
    TEST_ASSERT_EQUAL_INT(MC_DISABLE_WEB_CONTROL, (int)resolveManualCommand("disable_web_control"));
}

void test_resolve_reboot() {
    TEST_ASSERT_EQUAL_INT(MC_REBOOT, (int)resolveManualCommand("reboot"));
}

void test_resolve_unknown() {
    TEST_ASSERT_EQUAL_INT(MC_UNKNOWN, (int)resolveManualCommand("unknown_cmd"));
}

void test_resolve_empty_unknown() {
    TEST_ASSERT_EQUAL_INT(MC_UNKNOWN, (int)resolveManualCommand(""));
}

void test_resolve_null_unknown() {
    TEST_ASSERT_EQUAL_INT(MC_UNKNOWN, (int)resolveManualCommand(nullptr));
}

void test_resolve_uppercase_unknown() {
    TEST_ASSERT_EQUAL_INT(MC_UNKNOWN, (int)resolveManualCommand("ESTOP"));
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

    RUN_TEST(test_resolve_estop);
    RUN_TEST(test_resolve_clear_estop);
    RUN_TEST(test_resolve_enable_web_control);
    RUN_TEST(test_resolve_disable_web_control);
    RUN_TEST(test_resolve_reboot);
    RUN_TEST(test_resolve_unknown);
    RUN_TEST(test_resolve_empty_unknown);
    RUN_TEST(test_resolve_null_unknown);
    RUN_TEST(test_resolve_uppercase_unknown);

    return UNITY_END();
}
