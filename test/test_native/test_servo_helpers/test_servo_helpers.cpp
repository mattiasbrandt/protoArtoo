// =============================================================================
// test/test_native/test_servo_helpers/test_servo_helpers.cpp
//
// Native unit tests for servo arm ID mapping and enable-flag helpers.
// Tests: armId→LEDC channel mapping, servo_arm_enabled() with feature toggles.
//
// All helpers are inline in servo_helpers.h — no hardware or framework deps.
// =============================================================================
#include <unity.h>

#include "servo_helpers.h"

void setUp() {
}
void tearDown() {
}

// --- servo_arm_id_to_ledc_channel --------------------------------------------

void test_arm0_maps_to_ledc_arm1() {
    TEST_ASSERT_EQUAL_UINT8(LEDC_CH_ARM1, servo_arm_id_to_ledc_channel(0));
}

void test_arm1_maps_to_ledc_arm2() {
    TEST_ASSERT_EQUAL_UINT8(LEDC_CH_ARM2, servo_arm_id_to_ledc_channel(1));
}

void test_arm2_maps_to_ledc_aux1() {
    TEST_ASSERT_EQUAL_UINT8(LEDC_CH_AUX1, servo_arm_id_to_ledc_channel(2));
}

void test_arm3_maps_to_ledc_aux2() {
    TEST_ASSERT_EQUAL_UINT8(LEDC_CH_AUX2, servo_arm_id_to_ledc_channel(3));
}

void test_arm4_maps_to_ledc_aux3() {
    TEST_ASSERT_EQUAL_UINT8(LEDC_CH_AUX3, servo_arm_id_to_ledc_channel(4));
}

void test_arm5_is_invalid_returns_ledc_max() {
    TEST_ASSERT_EQUAL_UINT8(LEDC_CH_MAX, servo_arm_id_to_ledc_channel(5));
}

void test_arm255_broadcast_is_invalid_returns_ledc_max() {
    TEST_ASSERT_EQUAL_UINT8(LEDC_CH_MAX, servo_arm_id_to_ledc_channel(255));
}

void test_arm_invalid_large_returns_ledc_max() {
    TEST_ASSERT_EQUAL_UINT8(LEDC_CH_MAX, servo_arm_id_to_ledc_channel(100));
}

// --- servo_arm_enabled -------------------------------------------------------

void test_arm0_enabled_when_arm1_flag_true() {
    TEST_ASSERT_TRUE(servo_arm_enabled(0, true, false, false, false, false));
}

void test_arm0_disabled_when_arm1_flag_false() {
    TEST_ASSERT_FALSE(servo_arm_enabled(0, false, true, true, true, true));
}

void test_arm1_enabled_when_arm2_flag_true() {
    TEST_ASSERT_TRUE(servo_arm_enabled(1, false, true, false, false, false));
}

void test_arm1_disabled_when_arm2_flag_false() {
    TEST_ASSERT_FALSE(servo_arm_enabled(1, true, false, true, true, true));
}

void test_arm2_aux1_enabled_when_aux1_flag_true() {
    TEST_ASSERT_TRUE(servo_arm_enabled(2, false, false, true, false, false));
}

void test_arm3_aux2_enabled_when_aux2_flag_true() {
    TEST_ASSERT_TRUE(servo_arm_enabled(3, false, false, false, true, false));
}

void test_arm4_aux3_enabled_when_aux3_flag_true() {
    TEST_ASSERT_TRUE(servo_arm_enabled(4, false, false, false, false, true));
}

void test_broadcast_255_enabled_when_both_arm1_arm2_true() {
    TEST_ASSERT_TRUE(servo_arm_enabled(255, true, true, false, false, false));
}

void test_broadcast_255_disabled_when_arm1_false() {
    TEST_ASSERT_FALSE(servo_arm_enabled(255, false, true, true, true, true));
}

void test_broadcast_255_disabled_when_arm2_false() {
    TEST_ASSERT_FALSE(servo_arm_enabled(255, true, false, true, true, true));
}

void test_broadcast_255_disabled_when_both_false() {
    TEST_ASSERT_FALSE(servo_arm_enabled(255, false, false, true, true, true));
}

void test_unknown_arm_id_returns_false() {
    TEST_ASSERT_FALSE(servo_arm_enabled(5, true, true, true, true, true));
    TEST_ASSERT_FALSE(servo_arm_enabled(100, true, true, true, true, true));
}

int main() {
    UNITY_BEGIN();

    RUN_TEST(test_arm0_maps_to_ledc_arm1);
    RUN_TEST(test_arm1_maps_to_ledc_arm2);
    RUN_TEST(test_arm2_maps_to_ledc_aux1);
    RUN_TEST(test_arm3_maps_to_ledc_aux2);
    RUN_TEST(test_arm4_maps_to_ledc_aux3);
    RUN_TEST(test_arm5_is_invalid_returns_ledc_max);
    RUN_TEST(test_arm255_broadcast_is_invalid_returns_ledc_max);
    RUN_TEST(test_arm_invalid_large_returns_ledc_max);

    RUN_TEST(test_arm0_enabled_when_arm1_flag_true);
    RUN_TEST(test_arm0_disabled_when_arm1_flag_false);
    RUN_TEST(test_arm1_enabled_when_arm2_flag_true);
    RUN_TEST(test_arm1_disabled_when_arm2_flag_false);
    RUN_TEST(test_arm2_aux1_enabled_when_aux1_flag_true);
    RUN_TEST(test_arm3_aux2_enabled_when_aux2_flag_true);
    RUN_TEST(test_arm4_aux3_enabled_when_aux3_flag_true);
    RUN_TEST(test_broadcast_255_enabled_when_both_arm1_arm2_true);
    RUN_TEST(test_broadcast_255_disabled_when_arm1_false);
    RUN_TEST(test_broadcast_255_disabled_when_arm2_false);
    RUN_TEST(test_broadcast_255_disabled_when_both_false);
    RUN_TEST(test_unknown_arm_id_returns_false);

    return UNITY_END();
}
