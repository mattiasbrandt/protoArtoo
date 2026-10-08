// =============================================================================
// test/test_native/test_servo_component_type/test_servo_component_type.cpp
//
// Native unit tests for servo component type system.
// Tests: type string conversion and round-trip.
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

    return UNITY_END();
}
