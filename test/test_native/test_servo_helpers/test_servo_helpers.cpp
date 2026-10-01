// =============================================================================
// test/test_native/test_servo_helpers/test_servo_helpers.cpp
//
// Native unit tests for the slot mapping and the enable helpers.
// Tests: slot -> Output Address, servo_output_enabled() and
// servo_target_enabled() over the wired and lit masks, and the LEDC init mask.
//
// All helpers are inline in servo_helpers.h — no hardware or framework deps.
// =============================================================================
#include <unity.h>

#include "servo_helpers.h"

void setUp() {
}
void tearDown() {
}

// The wired mask ServoTask snapshots at start, one bit per slot, built from the
// five ticks in the board's order - the shape these cases were written in.
static uint32_t wired(bool arm1, bool arm2, bool aux1, bool aux2, bool aux3) {
    return (arm1 ? 1u << 0 : 0u) | (arm2 ? 1u << 1 : 0u) | (aux1 ? 1u << 2 : 0u) |
           (aux2 ? 1u << 3 : 0u) | (aux3 ? 1u << 4 : 0u);
}

// --- servoOutputSlotAddress ---------------------------------------------------

void test_slot0_drives_ledc_arm1() {
    TEST_ASSERT_EQUAL_UINT8(LEDC_CH_ARM1, servoOutputSlotAddress(0).channel);
}

void test_slot1_drives_ledc_arm2() {
    TEST_ASSERT_EQUAL_UINT8(LEDC_CH_ARM2, servoOutputSlotAddress(1).channel);
}

void test_slot2_drives_ledc_aux1() {
    TEST_ASSERT_EQUAL_UINT8(LEDC_CH_AUX1, servoOutputSlotAddress(2).channel);
}

void test_slot3_drives_ledc_aux2() {
    TEST_ASSERT_EQUAL_UINT8(LEDC_CH_AUX2, servoOutputSlotAddress(3).channel);
}

void test_slot4_drives_ledc_aux3() {
    TEST_ASSERT_EQUAL_UINT8(LEDC_CH_AUX3, servoOutputSlotAddress(4).channel);
}

void test_slot5_has_no_output() {
    TEST_ASSERT_TRUE(servoOutputSlotAddress(5) == SERVO_OUTPUT_NONE);
}

void test_slot255_has_no_output() {
    TEST_ASSERT_TRUE(servoOutputSlotAddress(255) == SERVO_OUTPUT_NONE);
}

void test_slot_invalid_large_has_no_output() {
    TEST_ASSERT_TRUE(servoOutputSlotAddress(100) == SERVO_OUTPUT_NONE);
}

// The lit mask servo_output_enabled() and servo_enabled_ledc_mask() take: one
// bit per slot whose wire carries a Light Type (ADR 0067). It replaced a
// single slot number, so unlike that number it can say "two of these at once" -
// which is the whole point of #413 and what kBothAuxLit below exercises.
static constexpr uint8_t kNoLight = 0;
static constexpr uint8_t kAux1Lit = 1u << 2;  // slot 2
static constexpr uint8_t kAux2Lit = 1u << 3;  // slot 3
static constexpr uint8_t kAux3Lit = 1u << 4;  // slot 4
static constexpr uint8_t kBothAuxLit = kAux1Lit | kAux3Lit;

// --- servo_output_enabled / servo_target_enabled ---------------------------------

void test_arm0_enabled_when_arm1_flag_true() {
    TEST_ASSERT_TRUE(servo_output_enabled(0, wired(true, false, false, false, false), kNoLight));
}

void test_arm0_disabled_when_arm1_flag_false() {
    TEST_ASSERT_FALSE(servo_output_enabled(0, wired(false, true, true, true, true), kNoLight));
}

void test_arm1_enabled_when_arm2_flag_true() {
    TEST_ASSERT_TRUE(servo_output_enabled(1, wired(false, true, false, false, false), kNoLight));
}

void test_arm1_disabled_when_arm2_flag_false() {
    TEST_ASSERT_FALSE(servo_output_enabled(1, wired(true, false, true, true, true), kNoLight));
}

void test_arm2_aux1_enabled_when_aux1_flag_true() {
    TEST_ASSERT_TRUE(servo_output_enabled(2, wired(false, false, true, false, false), kNoLight));
}

void test_arm3_aux2_enabled_when_aux2_flag_true() {
    TEST_ASSERT_TRUE(servo_output_enabled(3, wired(false, false, false, true, false), kNoLight));
}

void test_arm4_aux3_enabled_when_aux3_flag_true() {
    TEST_ASSERT_TRUE(servo_output_enabled(4, wired(false, false, false, false, true), kNoLight));
}

void test_both_arms_enabled_when_both_arm1_arm2_true() {
    TEST_ASSERT_TRUE(
        servo_target_enabled(SERVO_OUTPUT_BOTH_ARMS, wired(true, true, false, false, false), kNoLight));
    // A light on one arm's wire is that arm's own check when it is driven; it
    // does not stop `both` moving the other.
    TEST_ASSERT_TRUE(
        servo_target_enabled(SERVO_OUTPUT_BOTH_ARMS, wired(true, true, false, false, false), 1u << 0));
}

void test_both_arms_disabled_when_arm1_false() {
    TEST_ASSERT_FALSE(
        servo_target_enabled(SERVO_OUTPUT_BOTH_ARMS, wired(false, true, true, true, true), kNoLight));
}

void test_both_arms_disabled_when_arm2_false() {
    TEST_ASSERT_FALSE(
        servo_target_enabled(SERVO_OUTPUT_BOTH_ARMS, wired(true, false, true, true, true), kNoLight));
}

void test_both_arms_disabled_when_both_false() {
    TEST_ASSERT_FALSE(
        servo_target_enabled(SERVO_OUTPUT_BOTH_ARMS, wired(false, false, true, true, true), kNoLight));
}

void test_unknown_slot_returns_false() {
    TEST_ASSERT_FALSE(servo_output_enabled(5, wired(true, true, true, true, true), kNoLight));
    TEST_ASSERT_FALSE(servo_output_enabled(100, wired(true, true, true, true, true), kNoLight));
    TEST_ASSERT_FALSE(
        servo_target_enabled(SERVO_OUTPUT_NONE, wired(true, true, true, true, true), kNoLight));
}

// A lit wire must disable that Output's servo and no other.
void test_aux1_reserved_blocks_arm2_servo() {
    TEST_ASSERT_FALSE(servo_output_enabled(2, wired(false, false, true, true, true), kAux1Lit));
    TEST_ASSERT_TRUE(servo_output_enabled(3, wired(false, false, true, true, true), kAux1Lit));
    TEST_ASSERT_TRUE(servo_output_enabled(4, wired(false, false, true, true, true), kAux1Lit));
}

void test_aux2_reserved_blocks_arm3_servo() {
    TEST_ASSERT_TRUE(servo_output_enabled(2, wired(false, false, true, true, true), kAux2Lit));
    TEST_ASSERT_FALSE(servo_output_enabled(3, wired(false, false, true, true, true), kAux2Lit));
    TEST_ASSERT_TRUE(servo_output_enabled(4, wired(false, false, true, true, true), kAux2Lit));
}

void test_aux3_reserved_blocks_arm4_servo() {
    TEST_ASSERT_TRUE(servo_output_enabled(2, wired(false, false, true, true, true), kAux3Lit));
    TEST_ASSERT_TRUE(servo_output_enabled(3, wired(false, false, true, true, true), kAux3Lit));
    TEST_ASSERT_FALSE(servo_output_enabled(4, wired(false, false, true, true, true), kAux3Lit));
}

// --- servo_enabled_ledc_mask -------------------------------------------------

void test_ledc_mask_all_on_no_reservation() {
    // All channels enabled, no AUX LED reservation.
    // Expected mask: bits 0-5 set (0x3F)
    uint8_t mask = servo_enabled_ledc_mask(wired(true, true, true, true, true), kNoLight, true);
    TEST_ASSERT_EQUAL_UINT8(0x3F, mask);
}

void test_ledc_mask_all_off() {
    // All channels disabled.
    // Expected mask: 0x00
    uint8_t mask = servo_enabled_ledc_mask(wired(false, false, false, false, false), kNoLight, false);
    TEST_ASSERT_EQUAL_UINT8(0x00, mask);
}

void test_ledc_mask_dome_only() {
    // Only dome enabled.
    // Expected mask: bit 2 set (0x04)
    uint8_t mask = servo_enabled_ledc_mask(wired(false, false, false, false, false), kNoLight, true);
    TEST_ASSERT_EQUAL_UINT8(0x04, mask);
}

void test_ledc_mask_aux1_reserved_excluded() {
    // All AUX channels enabled, but AUX1 reserved for LED.
    // arm1=false, arm2=true, aux1=true (reserved), aux2=true, aux3=true, dome=false, LED=AUX1
    // Expected: bit 1 (ARM2), bit 4 (AUX2), bit 5 (AUX3) = 0b110010 = 0x32 = 50
    uint8_t mask = servo_enabled_ledc_mask(wired(false, true, true, true, true), kAux1Lit, false);
    TEST_ASSERT_EQUAL_UINT8(0x32, mask);
}

void test_ledc_mask_aux2_reserved_excluded() {
    // All AUX channels enabled, AUX2 reserved for LED.
    // arm1=true, arm2=false, aux1=true, aux2=true (reserved), aux3=true, dome=false
    // Expected: 0b101001 = 0x29
    uint8_t mask = servo_enabled_ledc_mask(wired(true, false, true, true, true), kAux2Lit, false);
    TEST_ASSERT_EQUAL_UINT8(0x29, mask);
}

void test_ledc_mask_aux3_reserved_excluded() {
    // All AUX channels enabled, AUX3 reserved for LED.
    // arm1=true, arm2=true, aux1=true, aux2=true, aux3=true (reserved), dome=false
    // Expected: bits 0,1,3,4 set = 0b011011 = 0x1B = 27
    uint8_t mask = servo_enabled_ledc_mask(wired(true, true, true, true, true), kAux3Lit, false);
    TEST_ASSERT_EQUAL_UINT8(0x1B, mask);
}

// The model the single slot number could not express: two wires lit at once,
// each taking its own channel out of the servo mask and leaving the third.
void test_ledc_mask_two_lit_wires_excluded() {
    // arm1..aux3 all enabled, dome off, AUX1 and AUX3 carrying lights.
    // Expected: bits 0,1 (ARM1/ARM2) and bit 4 (AUX2) = 0b010011 = 0x13
    uint8_t mask = servo_enabled_ledc_mask(wired(true, true, true, true, true), kBothAuxLit, false);
    TEST_ASSERT_EQUAL_UINT8(0x13, mask);
    TEST_ASSERT_FALSE(servo_output_enabled(2, wired(true, true, true, true, true), kBothAuxLit));
    TEST_ASSERT_TRUE(servo_output_enabled(3, wired(true, true, true, true, true), kBothAuxLit));
    TEST_ASSERT_FALSE(servo_output_enabled(4, wired(true, true, true, true, true), kBothAuxLit));
}

void test_ledc_mask_servo_and_dome() {
    // ARM1, ARM2, and DOME enabled, no AUX, no LED reservation.
    // Expected: bits 0,1,2 set = 0x07
    uint8_t mask = servo_enabled_ledc_mask(wired(true, true, false, false, false), kNoLight, true);
    TEST_ASSERT_EQUAL_UINT8(0x07, mask);
}

int main() {
    UNITY_BEGIN();

    RUN_TEST(test_slot0_drives_ledc_arm1);
    RUN_TEST(test_slot1_drives_ledc_arm2);
    RUN_TEST(test_slot2_drives_ledc_aux1);
    RUN_TEST(test_slot3_drives_ledc_aux2);
    RUN_TEST(test_slot4_drives_ledc_aux3);
    RUN_TEST(test_slot5_has_no_output);
    RUN_TEST(test_slot255_has_no_output);
    RUN_TEST(test_slot_invalid_large_has_no_output);

    RUN_TEST(test_arm0_enabled_when_arm1_flag_true);
    RUN_TEST(test_arm0_disabled_when_arm1_flag_false);
    RUN_TEST(test_arm1_enabled_when_arm2_flag_true);
    RUN_TEST(test_arm1_disabled_when_arm2_flag_false);
    RUN_TEST(test_arm2_aux1_enabled_when_aux1_flag_true);
    RUN_TEST(test_arm3_aux2_enabled_when_aux2_flag_true);
    RUN_TEST(test_arm4_aux3_enabled_when_aux3_flag_true);
    RUN_TEST(test_both_arms_enabled_when_both_arm1_arm2_true);
    RUN_TEST(test_both_arms_disabled_when_arm1_false);
    RUN_TEST(test_both_arms_disabled_when_arm2_false);
    RUN_TEST(test_both_arms_disabled_when_both_false);
    RUN_TEST(test_unknown_slot_returns_false);
    RUN_TEST(test_aux1_reserved_blocks_arm2_servo);
    RUN_TEST(test_aux2_reserved_blocks_arm3_servo);
    RUN_TEST(test_aux3_reserved_blocks_arm4_servo);

    RUN_TEST(test_ledc_mask_all_on_no_reservation);
    RUN_TEST(test_ledc_mask_all_off);
    RUN_TEST(test_ledc_mask_dome_only);
    RUN_TEST(test_ledc_mask_aux1_reserved_excluded);
    RUN_TEST(test_ledc_mask_aux2_reserved_excluded);
    RUN_TEST(test_ledc_mask_aux3_reserved_excluded);
    RUN_TEST(test_ledc_mask_two_lit_wires_excluded);
    RUN_TEST(test_ledc_mask_servo_and_dome);

    return UNITY_END();
}
