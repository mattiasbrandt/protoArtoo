// =============================================================================
// test/test_native/test_sbus_flags/test_sbus_flags.cpp
//
// Native unit tests for SBUS flags-byte decoding.
// Tests: correct bit positions for CH17, CH18, lost_frame, failsafe.
// Regression guard against the bit-shift bug (was: lost_frame=bit1, failsafe=bit2).
// =============================================================================
#include <unity.h>

#include "sbus_flags.h"

void setUp() {
}
void tearDown() {
}

void test_all_flags_clear_on_zero_byte() {
    SbusFlags f = parseSbusFlags(0x00);
    TEST_ASSERT_FALSE(f.ch17);
    TEST_ASSERT_FALSE(f.ch18);
    TEST_ASSERT_FALSE(f.lost_frame);
    TEST_ASSERT_FALSE(f.failsafe);
}

void test_ch17_is_bit0() {
    SbusFlags f = parseSbusFlags(0x01);
    TEST_ASSERT_TRUE(f.ch17);
    TEST_ASSERT_FALSE(f.ch18);
    TEST_ASSERT_FALSE(f.lost_frame);
    TEST_ASSERT_FALSE(f.failsafe);
}

void test_ch18_is_bit1() {
    SbusFlags f = parseSbusFlags(0x02);
    TEST_ASSERT_FALSE(f.ch17);
    TEST_ASSERT_TRUE(f.ch18);
    TEST_ASSERT_FALSE(f.lost_frame);
    TEST_ASSERT_FALSE(f.failsafe);
}

void test_lost_frame_is_bit2() {
    SbusFlags f = parseSbusFlags(0x04);
    TEST_ASSERT_FALSE(f.ch17);
    TEST_ASSERT_FALSE(f.ch18);
    TEST_ASSERT_TRUE(f.lost_frame);
    TEST_ASSERT_FALSE(f.failsafe);
}

void test_failsafe_is_bit3() {
    SbusFlags f = parseSbusFlags(0x08);
    TEST_ASSERT_FALSE(f.ch17);
    TEST_ASSERT_FALSE(f.ch18);
    TEST_ASSERT_FALSE(f.lost_frame);
    TEST_ASSERT_TRUE(f.failsafe);
}

void test_lost_frame_does_not_set_failsafe() {
    SbusFlags f = parseSbusFlags(0x04);
    TEST_ASSERT_TRUE(f.lost_frame);
    TEST_ASSERT_FALSE(f.failsafe);
}

void test_failsafe_does_not_set_lost_frame() {
    SbusFlags f = parseSbusFlags(0x08);
    TEST_ASSERT_FALSE(f.lost_frame);
    TEST_ASSERT_TRUE(f.failsafe);
}

void test_all_flags_set() {
    SbusFlags f = parseSbusFlags(0x0F);
    TEST_ASSERT_TRUE(f.ch17);
    TEST_ASSERT_TRUE(f.ch18);
    TEST_ASSERT_TRUE(f.lost_frame);
    TEST_ASSERT_TRUE(f.failsafe);
}

void test_upper_nibble_ignored() {
    SbusFlags f = parseSbusFlags(0xF0);
    TEST_ASSERT_FALSE(f.ch17);
    TEST_ASSERT_FALSE(f.ch18);
    TEST_ASSERT_FALSE(f.lost_frame);
    TEST_ASSERT_FALSE(f.failsafe);
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_all_flags_clear_on_zero_byte);
    RUN_TEST(test_ch17_is_bit0);
    RUN_TEST(test_ch18_is_bit1);
    RUN_TEST(test_lost_frame_is_bit2);
    RUN_TEST(test_failsafe_is_bit3);
    RUN_TEST(test_lost_frame_does_not_set_failsafe);
    RUN_TEST(test_failsafe_does_not_set_lost_frame);
    RUN_TEST(test_all_flags_set);
    RUN_TEST(test_upper_nibble_ignored);
    return UNITY_END();
}
