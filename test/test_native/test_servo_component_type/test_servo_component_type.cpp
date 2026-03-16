// =============================================================================
// test/test_native/test_servo_component_type/test_servo_component_type.cpp
//
// Native unit tests for servo component type system.
// Tests: type string conversion, default calibration values, validation.
//
// All helpers are inline in servo_component_helpers.h — no hardware deps.
// =============================================================================
#include <unity.h>

#include "servo_component_helpers.h"

void setUp() {
}
void tearDown() {
}

// --- servoCompTypeToString ---------------------------------------------------

void test_mg996r_converts_to_string() {
    TEST_ASSERT_EQUAL_STRING("mg996r", servoCompTypeToString(SERVO_COMP_MG996R));
}

void test_mg90s_converts_to_string() {
    TEST_ASSERT_EQUAL_STRING("mg90s", servoCompTypeToString(SERVO_COMP_MG90S));
}

void test_rgb_converts_to_string() {
    TEST_ASSERT_EQUAL_STRING("rgb", servoCompTypeToString(SERVO_COMP_RGB));
}

void test_none_converts_to_string() {
    TEST_ASSERT_EQUAL_STRING("none", servoCompTypeToString(SERVO_COMP_NONE));
}

// --- parseServoCompType ------------------------------------------------------

void test_parse_mg996r_string() {
    TEST_ASSERT_EQUAL_UINT8(SERVO_COMP_MG996R, parseServoCompType("mg996r"));
}

void test_parse_mg90s_string() {
    TEST_ASSERT_EQUAL_UINT8(SERVO_COMP_MG90S, parseServoCompType("mg90s"));
}

void test_parse_rgb_string() {
    TEST_ASSERT_EQUAL_UINT8(SERVO_COMP_RGB, parseServoCompType("rgb"));
}

void test_parse_none_string() {
    TEST_ASSERT_EQUAL_UINT8(SERVO_COMP_NONE, parseServoCompType("none"));
}

void test_parse_null_returns_none() {
    TEST_ASSERT_EQUAL_UINT8(SERVO_COMP_NONE, parseServoCompType(nullptr));
}

void test_parse_unknown_returns_none() {
    TEST_ASSERT_EQUAL_UINT8(SERVO_COMP_NONE, parseServoCompType("unknown"));
    TEST_ASSERT_EQUAL_UINT8(SERVO_COMP_NONE, parseServoCompType("mg995"));
    TEST_ASSERT_EQUAL_UINT8(SERVO_COMP_NONE, parseServoCompType(""));
}

void test_parse_case_sensitive() {
    // Servo type strings are lowercase
    TEST_ASSERT_EQUAL_UINT8(SERVO_COMP_NONE, parseServoCompType("MG996R"));
    TEST_ASSERT_EQUAL_UINT8(SERVO_COMP_NONE, parseServoCompType("Mg996r"));
}

// --- Round-trip conversion ---------------------------------------------------

void test_round_trip_preserves_type() {
    // All valid types should round-trip through string and back
    ServoComponentType types[] = {SERVO_COMP_NONE, SERVO_COMP_MG996R, SERVO_COMP_MG90S,
                                  SERVO_COMP_RGB};

    for (size_t i = 0; i < sizeof(types) / sizeof(types[0]); i++) {
        const char* str = servoCompTypeToString(types[i]);
        ServoComponentType parsed = parseServoCompType(str);
        TEST_ASSERT_EQUAL_UINT8(types[i], parsed);
    }
}

// --- servoTypeDefaultOpen ----------------------------------------------------

void test_mg996r_default_open_is_2000us() {
    TEST_ASSERT_EQUAL_UINT16(2000, servoTypeDefaultOpen(SERVO_COMP_MG996R));
}

void test_mg90s_default_open_is_2500us() {
    TEST_ASSERT_EQUAL_UINT16(2500, servoTypeDefaultOpen(SERVO_COMP_MG90S));
}

void test_rgb_default_open_is_neutral() {
    TEST_ASSERT_EQUAL_UINT16(1500, servoTypeDefaultOpen(SERVO_COMP_RGB));
}

void test_none_default_open_is_neutral() {
    TEST_ASSERT_EQUAL_UINT16(1500, servoTypeDefaultOpen(SERVO_COMP_NONE));
}

// --- servoTypeDefaultClose ---------------------------------------------------

void test_mg996r_default_close_is_1000us() {
    TEST_ASSERT_EQUAL_UINT16(1000, servoTypeDefaultClose(SERVO_COMP_MG996R));
}

void test_mg90s_default_close_is_500us() {
    TEST_ASSERT_EQUAL_UINT16(500, servoTypeDefaultClose(SERVO_COMP_MG90S));
}

void test_rgb_default_close_is_neutral() {
    TEST_ASSERT_EQUAL_UINT16(1500, servoTypeDefaultClose(SERVO_COMP_RGB));
}

void test_none_default_close_is_neutral() {
    TEST_ASSERT_EQUAL_UINT16(1500, servoTypeDefaultClose(SERVO_COMP_NONE));
}

// --- Default range validation ------------------------------------------------

void test_mg996r_range_is_1000us() {
    uint16_t open = servoTypeDefaultOpen(SERVO_COMP_MG996R);
    uint16_t close = servoTypeDefaultClose(SERVO_COMP_MG996R);
    TEST_ASSERT_EQUAL_UINT16(1000, open - close);
}

void test_mg90s_range_is_2000us() {
    uint16_t open = servoTypeDefaultOpen(SERVO_COMP_MG90S);
    uint16_t close = servoTypeDefaultClose(SERVO_COMP_MG90S);
    TEST_ASSERT_EQUAL_UINT16(2000, open - close);
}

void test_mg90s_wider_range_than_mg996r() {
    // MG90S has full 500-2500 range, MG996R has limited 1000-2000 range
    uint16_t mg90sRange =
        servoTypeDefaultOpen(SERVO_COMP_MG90S) - servoTypeDefaultClose(SERVO_COMP_MG90S);
    uint16_t mg996rRange =
        servoTypeDefaultOpen(SERVO_COMP_MG996R) - servoTypeDefaultClose(SERVO_COMP_MG996R);
    TEST_ASSERT_TRUE(mg90sRange > mg996rRange);
}

// --- isValidServoCompType ----------------------------------------------------

void test_valid_types_return_true() {
    TEST_ASSERT_TRUE(isValidServoCompType(0));  // NONE
    TEST_ASSERT_TRUE(isValidServoCompType(1));  // MG996R
    TEST_ASSERT_TRUE(isValidServoCompType(2));  // MG90S
    TEST_ASSERT_TRUE(isValidServoCompType(3));  // RGB
}

void test_invalid_types_return_false() {
    TEST_ASSERT_FALSE(isValidServoCompType(4));
    TEST_ASSERT_FALSE(isValidServoCompType(5));
    TEST_ASSERT_FALSE(isValidServoCompType(255));
}

// --- clampServoCompType ------------------------------------------------------

void test_clamp_valid_types_preserved() {
    TEST_ASSERT_EQUAL_UINT8(SERVO_COMP_NONE, clampServoCompType(0));
    TEST_ASSERT_EQUAL_UINT8(SERVO_COMP_MG996R, clampServoCompType(1));
    TEST_ASSERT_EQUAL_UINT8(SERVO_COMP_MG90S, clampServoCompType(2));
    TEST_ASSERT_EQUAL_UINT8(SERVO_COMP_RGB, clampServoCompType(3));
}

void test_clamp_invalid_to_none() {
    TEST_ASSERT_EQUAL_UINT8(SERVO_COMP_NONE, clampServoCompType(4));
    TEST_ASSERT_EQUAL_UINT8(SERVO_COMP_NONE, clampServoCompType(5));
    TEST_ASSERT_EQUAL_UINT8(SERVO_COMP_NONE, clampServoCompType(100));
    TEST_ASSERT_EQUAL_UINT8(SERVO_COMP_NONE, clampServoCompType(255));
}

// --- Integration: clamp + defaults -------------------------------------------

void test_clamped_value_gives_valid_defaults() {
    // Simulate reading a corrupted NVS value
    uint8_t corruptedValue = 200;
    ServoComponentType clamped = clampServoCompType(corruptedValue);

    // Should clamp to NONE, which has neutral defaults
    TEST_ASSERT_EQUAL_UINT8(SERVO_COMP_NONE, clamped);
    TEST_ASSERT_EQUAL_UINT16(1500, servoTypeDefaultOpen(clamped));
    TEST_ASSERT_EQUAL_UINT16(1500, servoTypeDefaultClose(clamped));
}

int main() {
    UNITY_BEGIN();

    // Type to string
    RUN_TEST(test_mg996r_converts_to_string);
    RUN_TEST(test_mg90s_converts_to_string);
    RUN_TEST(test_rgb_converts_to_string);
    RUN_TEST(test_none_converts_to_string);

    // String to type
    RUN_TEST(test_parse_mg996r_string);
    RUN_TEST(test_parse_mg90s_string);
    RUN_TEST(test_parse_rgb_string);
    RUN_TEST(test_parse_none_string);
    RUN_TEST(test_parse_null_returns_none);
    RUN_TEST(test_parse_unknown_returns_none);
    RUN_TEST(test_parse_case_sensitive);

    // Round-trip
    RUN_TEST(test_round_trip_preserves_type);

    // Default open values
    RUN_TEST(test_mg996r_default_open_is_2000us);
    RUN_TEST(test_mg90s_default_open_is_2500us);
    RUN_TEST(test_rgb_default_open_is_neutral);
    RUN_TEST(test_none_default_open_is_neutral);

    // Default close values
    RUN_TEST(test_mg996r_default_close_is_1000us);
    RUN_TEST(test_mg90s_default_close_is_500us);
    RUN_TEST(test_rgb_default_close_is_neutral);
    RUN_TEST(test_none_default_close_is_neutral);

    // Range validation
    RUN_TEST(test_mg996r_range_is_1000us);
    RUN_TEST(test_mg90s_range_is_2000us);
    RUN_TEST(test_mg90s_wider_range_than_mg996r);

    // Validation
    RUN_TEST(test_valid_types_return_true);
    RUN_TEST(test_invalid_types_return_false);

    // Clamping
    RUN_TEST(test_clamp_valid_types_preserved);
    RUN_TEST(test_clamp_invalid_to_none);

    // Integration
    RUN_TEST(test_clamped_value_gives_valid_defaults);

    return UNITY_END();
}
