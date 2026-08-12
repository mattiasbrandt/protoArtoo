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
    TEST_ASSERT_TRUE(servo_arm_enabled(0, true, false, false, false, false, AUX_LED_PIN_DISABLED));
}

void test_arm0_disabled_when_arm1_flag_false() {
    TEST_ASSERT_FALSE(servo_arm_enabled(0, false, true, true, true, true, AUX_LED_PIN_DISABLED));
}

void test_arm1_enabled_when_arm2_flag_true() {
    TEST_ASSERT_TRUE(servo_arm_enabled(1, false, true, false, false, false, AUX_LED_PIN_DISABLED));
}

void test_arm1_disabled_when_arm2_flag_false() {
    TEST_ASSERT_FALSE(servo_arm_enabled(1, true, false, true, true, true, AUX_LED_PIN_DISABLED));
}

void test_arm2_aux1_enabled_when_aux1_flag_true() {
    TEST_ASSERT_TRUE(servo_arm_enabled(2, false, false, true, false, false, AUX_LED_PIN_DISABLED));
}

void test_arm3_aux2_enabled_when_aux2_flag_true() {
    TEST_ASSERT_TRUE(servo_arm_enabled(3, false, false, false, true, false, AUX_LED_PIN_DISABLED));
}

void test_arm4_aux3_enabled_when_aux3_flag_true() {
    TEST_ASSERT_TRUE(servo_arm_enabled(4, false, false, false, false, true, AUX_LED_PIN_DISABLED));
}

void test_broadcast_255_enabled_when_both_arm1_arm2_true() {
    TEST_ASSERT_TRUE(servo_arm_enabled(255, true, true, false, false, false, AUX_LED_PIN_DISABLED));
}

void test_broadcast_255_disabled_when_arm1_false() {
    TEST_ASSERT_FALSE(servo_arm_enabled(255, false, true, true, true, true, AUX_LED_PIN_DISABLED));
}

void test_broadcast_255_disabled_when_arm2_false() {
    TEST_ASSERT_FALSE(servo_arm_enabled(255, true, false, true, true, true, AUX_LED_PIN_DISABLED));
}

void test_broadcast_255_disabled_when_both_false() {
    TEST_ASSERT_FALSE(servo_arm_enabled(255, false, false, true, true, true, AUX_LED_PIN_DISABLED));
}

void test_unknown_arm_id_returns_false() {
    TEST_ASSERT_FALSE(servo_arm_enabled(5, true, true, true, true, true, AUX_LED_PIN_DISABLED));
    TEST_ASSERT_FALSE(servo_arm_enabled(100, true, true, true, true, true, AUX_LED_PIN_DISABLED));
}

// AUX LED pin reservation must disable the matching AUX servo arm only.
void test_aux1_reserved_blocks_arm2_servo() {
    TEST_ASSERT_FALSE(servo_arm_enabled(2, false, false, true, true, true, AUX_LED_PIN_AUX1));
    TEST_ASSERT_TRUE(servo_arm_enabled(3, false, false, true, true, true, AUX_LED_PIN_AUX1));
    TEST_ASSERT_TRUE(servo_arm_enabled(4, false, false, true, true, true, AUX_LED_PIN_AUX1));
}

void test_aux2_reserved_blocks_arm3_servo() {
    TEST_ASSERT_TRUE(servo_arm_enabled(2, false, false, true, true, true, AUX_LED_PIN_AUX2));
    TEST_ASSERT_FALSE(servo_arm_enabled(3, false, false, true, true, true, AUX_LED_PIN_AUX2));
    TEST_ASSERT_TRUE(servo_arm_enabled(4, false, false, true, true, true, AUX_LED_PIN_AUX2));
}

void test_aux3_reserved_blocks_arm4_servo() {
    TEST_ASSERT_TRUE(servo_arm_enabled(2, false, false, true, true, true, AUX_LED_PIN_AUX3));
    TEST_ASSERT_TRUE(servo_arm_enabled(3, false, false, true, true, true, AUX_LED_PIN_AUX3));
    TEST_ASSERT_FALSE(servo_arm_enabled(4, false, false, true, true, true, AUX_LED_PIN_AUX3));
}

// --- servo_enabled_ledc_mask -------------------------------------------------

void test_ledc_mask_all_on_no_reservation() {
    // All channels enabled, no AUX LED reservation.
    // Expected mask: bits 0-5 set (0x3F)
    uint8_t mask = servo_enabled_ledc_mask(true, true, true, true, true, true, AUX_LED_PIN_DISABLED);
    TEST_ASSERT_EQUAL_UINT8(0x3F, mask);
}

void test_ledc_mask_all_off() {
    // All channels disabled.
    // Expected mask: 0x00
    uint8_t mask = servo_enabled_ledc_mask(false, false, false, false, false, false, AUX_LED_PIN_DISABLED);
    TEST_ASSERT_EQUAL_UINT8(0x00, mask);
}

void test_ledc_mask_dome_only() {
    // Only dome enabled.
    // Expected mask: bit 2 set (0x04)
    uint8_t mask = servo_enabled_ledc_mask(false, false, false, false, false, true, AUX_LED_PIN_DISABLED);
    TEST_ASSERT_EQUAL_UINT8(0x04, mask);
}

void test_ledc_mask_aux1_reserved_excluded() {
    // All AUX channels enabled, but AUX1 reserved for LED.
    // arm1=false, arm2=true, aux1=true (reserved), aux2=true, aux3=true, dome=false, LED=AUX1
    // Expected: bit 1 (ARM2), bit 4 (AUX2), bit 5 (AUX3) = 0b110010 = 0x32 = 50
    uint8_t mask = servo_enabled_ledc_mask(false, true, true, true, true, false, AUX_LED_PIN_AUX1);
    TEST_ASSERT_EQUAL_UINT8(0x32, mask);
}

void test_ledc_mask_aux2_reserved_excluded() {
    // All AUX channels enabled, AUX2 reserved for LED.
    // arm1=true, arm2=false, aux1=true, aux2=true (reserved), aux3=true, dome=false
    // Expected: 0b101001 = 0x29
    uint8_t mask = servo_enabled_ledc_mask(true, false, true, true, true, false, AUX_LED_PIN_AUX2);
    TEST_ASSERT_EQUAL_UINT8(0x29, mask);
}

void test_ledc_mask_aux3_reserved_excluded() {
    // All AUX channels enabled, AUX3 reserved for LED.
    // arm1=true, arm2=true, aux1=true, aux2=true, aux3=true (reserved), dome=false
    // Expected: bits 0,1,3,4 set = 0b011011 = 0x1B = 27
    uint8_t mask = servo_enabled_ledc_mask(true, true, true, true, true, false, AUX_LED_PIN_AUX3);
    TEST_ASSERT_EQUAL_UINT8(0x1B, mask);
}

void test_ledc_mask_servo_and_dome() {
    // ARM1, ARM2, and DOME enabled, no AUX, no LED reservation.
    // Expected: bits 0,1,2 set = 0x07
    uint8_t mask = servo_enabled_ledc_mask(true, true, false, false, false, true, AUX_LED_PIN_DISABLED);
    TEST_ASSERT_EQUAL_UINT8(0x07, mask);
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
    RUN_TEST(test_aux1_reserved_blocks_arm2_servo);
    RUN_TEST(test_aux2_reserved_blocks_arm3_servo);
    RUN_TEST(test_aux3_reserved_blocks_arm4_servo);

    RUN_TEST(test_ledc_mask_all_on_no_reservation);
    RUN_TEST(test_ledc_mask_all_off);
    RUN_TEST(test_ledc_mask_dome_only);
    RUN_TEST(test_ledc_mask_aux1_reserved_excluded);
    RUN_TEST(test_ledc_mask_aux2_reserved_excluded);
    RUN_TEST(test_ledc_mask_aux3_reserved_excluded);
    RUN_TEST(test_ledc_mask_servo_and_dome);

    return UNITY_END();
}
