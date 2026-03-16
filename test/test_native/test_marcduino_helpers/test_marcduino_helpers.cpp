// =============================================================================
// test/test_native/test_marcduino_helpers/test_marcduino_helpers.cpp
//
// Native unit tests for Marcduino pure-math helpers.
// Tests: panel→armId mapping, direct `:MV` numeric conversion, sequence ID validation.
//
// All helpers are inline in marcduino_helpers.h — no hardware or framework deps.
// =============================================================================
#include <unity.h>

#include "../../../include/marcduino_helpers.h"

#ifndef TEST_ASSERT_EQUAL_UINT8
#include <assert.h>
#define TEST_ASSERT_EQUAL_UINT8(expected, actual) assert((uint8_t)(expected) == (uint8_t)(actual))
#define TEST_ASSERT_EQUAL_UINT16(expected, actual) \
    assert((uint16_t)(expected) == (uint16_t)(actual))
#define TEST_ASSERT_TRUE(condition) assert(condition)
#define TEST_ASSERT_FALSE(condition) assert(!(condition))
#define UNITY_BEGIN() 0
#define RUN_TEST(func) func()
#define UNITY_END() 0
#endif

void setUp() {
}
void tearDown() {
}

// --- marcduino_panel_to_arm_id (:OP/:CL) -------------------------------------

void test_panel1_maps_to_arm0() {
    TEST_ASSERT_EQUAL_UINT8(0, marcduino_panel_to_arm_id(1));
}

void test_panel2_maps_to_arm1() {
    TEST_ASSERT_EQUAL_UINT8(1, marcduino_panel_to_arm_id(2));
}

void test_panel3_maps_to_arm2_aux1() {
    TEST_ASSERT_EQUAL_UINT8(2, marcduino_panel_to_arm_id(3));
}

void test_panel4_maps_to_arm3_aux2() {
    TEST_ASSERT_EQUAL_UINT8(3, marcduino_panel_to_arm_id(4));
}

void test_panel5_maps_to_arm4_aux3() {
    TEST_ASSERT_EQUAL_UINT8(4, marcduino_panel_to_arm_id(5));
}

void test_panel0_maps_to_broadcast() {
    TEST_ASSERT_EQUAL_UINT8(255, marcduino_panel_to_arm_id(0));
}

void test_panel99_maps_to_broadcast() {
    TEST_ASSERT_EQUAL_UINT8(255, marcduino_panel_to_arm_id(99));
}

void test_panel6_is_invalid() {
    TEST_ASSERT_EQUAL_UINT8(254, marcduino_panel_to_arm_id(6));
}

void test_panel_negative_is_invalid() {
    TEST_ASSERT_EQUAL_UINT8(254, marcduino_panel_to_arm_id(-1));
}

void test_panel_large_is_invalid() {
    TEST_ASSERT_EQUAL_UINT8(254, marcduino_panel_to_arm_id(100));
}

// --- marcduino_panel_to_arm_id_mv (:MV) --------------------------------------

void test_mv_panel1_maps_to_arm0() {
    TEST_ASSERT_EQUAL_UINT8(0, marcduino_panel_to_arm_id_mv(1));
}

void test_mv_panel5_maps_to_arm4() {
    TEST_ASSERT_EQUAL_UINT8(4, marcduino_panel_to_arm_id_mv(5));
}

void test_mv_panel0_is_invalid() {
    TEST_ASSERT_EQUAL_UINT8(254, marcduino_panel_to_arm_id_mv(0));
}

void test_mv_panel99_is_invalid() {
    TEST_ASSERT_EQUAL_UINT8(254, marcduino_panel_to_arm_id_mv(99));
}

void test_mv_panel6_is_invalid() {
    TEST_ASSERT_EQUAL_UINT8(254, marcduino_panel_to_arm_id_mv(6));
}

// --- marcduino_mv_value_to_pulse_us ------------------------------------------

void test_mv_value_0_degrees_gives_min_pulse() {
    TEST_ASSERT_EQUAL_UINT16(SERVO_PULSE_MIN_US, marcduino_mv_value_to_pulse_us(0));
}

void test_mv_value_180_degrees_gives_max_pulse() {
    TEST_ASSERT_EQUAL_UINT16(SERVO_PULSE_MAX_US, marcduino_mv_value_to_pulse_us(180));
}

void test_mv_value_90_degrees_gives_midpoint() {
    uint16_t expected = SERVO_PULSE_MIN_US + (SERVO_PULSE_MAX_US - SERVO_PULSE_MIN_US) / 2;
    TEST_ASSERT_EQUAL_UINT16(expected, marcduino_mv_value_to_pulse_us(90));
}

void test_mv_degree_values_monotonically_increase() {
    TEST_ASSERT_TRUE(marcduino_mv_value_to_pulse_us(0) < marcduino_mv_value_to_pulse_us(45));
    TEST_ASSERT_TRUE(marcduino_mv_value_to_pulse_us(45) < marcduino_mv_value_to_pulse_us(90));
    TEST_ASSERT_TRUE(marcduino_mv_value_to_pulse_us(90) < marcduino_mv_value_to_pulse_us(135));
    TEST_ASSERT_TRUE(marcduino_mv_value_to_pulse_us(135) < marcduino_mv_value_to_pulse_us(180));
}

void test_mv_value_545_is_direct_microseconds() {
    TEST_ASSERT_EQUAL_UINT16(545, marcduino_mv_value_to_pulse_us(545));
}

void test_mv_value_1500_is_direct_microseconds() {
    TEST_ASSERT_EQUAL_UINT16(1500, marcduino_mv_value_to_pulse_us(1500));
}

// --- marcduino_sequence_id_valid ---------------------------------------------

void test_sequence_30_is_valid() {
    TEST_ASSERT_TRUE(marcduino_sequence_id_valid(30));
}

void test_sequence_36_is_valid() {
    TEST_ASSERT_TRUE(marcduino_sequence_id_valid(36));
}

void test_sequence_33_is_valid() {
    TEST_ASSERT_TRUE(marcduino_sequence_id_valid(33));
}

void test_sequence_29_is_invalid() {
    TEST_ASSERT_FALSE(marcduino_sequence_id_valid(29));
}

void test_sequence_37_is_invalid() {
    TEST_ASSERT_FALSE(marcduino_sequence_id_valid(37));
}

void test_sequence_0_is_invalid() {
    TEST_ASSERT_FALSE(marcduino_sequence_id_valid(0));
}

void test_sequence_negative_is_invalid() {
    TEST_ASSERT_FALSE(marcduino_sequence_id_valid(-1));
}

// --- marcduino_full_sequence_to_body_sequence -------------------------------

void test_full_sequence_01_maps_to_body_sequence_30() {
    TEST_ASSERT_EQUAL_UINT8(30, (uint8_t)marcduino_full_sequence_to_body_sequence(1));
}

void test_full_sequence_02_has_no_direct_body_mapping() {
    TEST_ASSERT_TRUE(marcduino_full_sequence_to_body_sequence(2) < 0);
}

void test_full_sequence_16_has_no_direct_body_mapping() {
    TEST_ASSERT_TRUE(marcduino_full_sequence_to_body_sequence(16) < 0);
}

int main() {
    UNITY_BEGIN();

    RUN_TEST(test_panel1_maps_to_arm0);
    RUN_TEST(test_panel2_maps_to_arm1);
    RUN_TEST(test_panel3_maps_to_arm2_aux1);
    RUN_TEST(test_panel4_maps_to_arm3_aux2);
    RUN_TEST(test_panel5_maps_to_arm4_aux3);
    RUN_TEST(test_panel0_maps_to_broadcast);
    RUN_TEST(test_panel99_maps_to_broadcast);
    RUN_TEST(test_panel6_is_invalid);
    RUN_TEST(test_panel_negative_is_invalid);
    RUN_TEST(test_panel_large_is_invalid);

    RUN_TEST(test_mv_panel1_maps_to_arm0);
    RUN_TEST(test_mv_panel5_maps_to_arm4);
    RUN_TEST(test_mv_panel0_is_invalid);
    RUN_TEST(test_mv_panel99_is_invalid);
    RUN_TEST(test_mv_panel6_is_invalid);

    RUN_TEST(test_mv_value_0_degrees_gives_min_pulse);
    RUN_TEST(test_mv_value_180_degrees_gives_max_pulse);
    RUN_TEST(test_mv_value_90_degrees_gives_midpoint);
    RUN_TEST(test_mv_degree_values_monotonically_increase);
    RUN_TEST(test_mv_value_545_is_direct_microseconds);
    RUN_TEST(test_mv_value_1500_is_direct_microseconds);

    RUN_TEST(test_sequence_30_is_valid);
    RUN_TEST(test_sequence_36_is_valid);
    RUN_TEST(test_sequence_33_is_valid);
    RUN_TEST(test_sequence_29_is_invalid);
    RUN_TEST(test_sequence_37_is_invalid);
    RUN_TEST(test_sequence_0_is_invalid);
    RUN_TEST(test_sequence_negative_is_invalid);

    RUN_TEST(test_full_sequence_01_maps_to_body_sequence_30);
    RUN_TEST(test_full_sequence_02_has_no_direct_body_mapping);
    RUN_TEST(test_full_sequence_16_has_no_direct_body_mapping);

    return UNITY_END();
}
