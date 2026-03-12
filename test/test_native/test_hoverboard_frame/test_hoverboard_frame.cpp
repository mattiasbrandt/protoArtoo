// =============================================================================
// test/test_native/test_hoverboard_frame/test_hoverboard_frame.cpp
//
// Native unit tests for Gen2.x hoverboard frame builder.
// Tests: checksum calculation, frame byte layout, edge values.
// =============================================================================
#include <unity.h>
#include <string.h>
#include "hoverboard_uart.h"

void setUp() {}
void tearDown() {}

// Checksum = 0xABCD ^ steer ^ speed (all as uint16_t)
void test_checksum_zero_zero() {
    TEST_ASSERT_EQUAL_HEX16(0xABCD, calcHoverboardChecksum(0, 0));
}

void test_checksum_known_values() {
    // 0xABCD ^ 500 ^ 300 = 0xABCD ^ 0x01F4 ^ 0x012C
    uint16_t expected = (uint16_t)(0xABCD ^ 500 ^ 300);
    TEST_ASSERT_EQUAL_HEX16(expected, calcHoverboardChecksum(500, 300));
}

void test_checksum_max_values() {
    uint16_t expected = (uint16_t)(0xABCD ^ (uint16_t)1000 ^ (uint16_t)1000);
    TEST_ASSERT_EQUAL_HEX16(expected, calcHoverboardChecksum(1000, 1000));
}

void test_checksum_negative_values() {
    // Negative int16 cast to uint16 — XOR still works on bit patterns
    uint16_t expected = (uint16_t)(0xABCD ^ (uint16_t)(-500) ^ (uint16_t)(-300));
    TEST_ASSERT_EQUAL_HEX16(expected, calcHoverboardChecksum(-500, -300));
}

void test_frame_size() {
    TEST_ASSERT_EQUAL(8, (int)sizeof(HoverboardFrame));
}

void test_frame_start_word() {
    uint8_t buf[8] = {};
    buildHoverboardFrame(buf, 0, 0);
    // First two bytes = 0xABCD little-endian: buf[0]=0xCD, buf[1]=0xAB
    TEST_ASSERT_EQUAL_HEX8(0xCD, buf[0]);
    TEST_ASSERT_EQUAL_HEX8(0xAB, buf[1]);
}

void test_frame_steer_field() {
    uint8_t buf[8] = {};
    buildHoverboardFrame(buf, 500, 0);  // steer=500, speed=0
    // steer is at bytes 2-3, little-endian: 500 = 0x01F4
    TEST_ASSERT_EQUAL_HEX8(0xF4, buf[2]);
    TEST_ASSERT_EQUAL_HEX8(0x01, buf[3]);
}

void test_frame_speed_field() {
    uint8_t buf[8] = {};
    buildHoverboardFrame(buf, 0, 300);  // steer=0, speed=300
    // speed is at bytes 4-5, little-endian: 300 = 0x012C
    TEST_ASSERT_EQUAL_HEX8(0x2C, buf[4]);
    TEST_ASSERT_EQUAL_HEX8(0x01, buf[5]);
}

void test_frame_checksum_field() {
    uint8_t buf[8] = {};
    buildHoverboardFrame(buf, 500, 300);
    uint16_t expected = calcHoverboardChecksum(500, 300);
    uint16_t actual = (uint16_t)(buf[6] | (buf[7] << 8));
    TEST_ASSERT_EQUAL_HEX16(expected, actual);
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_checksum_zero_zero);
    RUN_TEST(test_checksum_known_values);
    RUN_TEST(test_checksum_max_values);
    RUN_TEST(test_checksum_negative_values);
    RUN_TEST(test_frame_size);
    RUN_TEST(test_frame_start_word);
    RUN_TEST(test_frame_steer_field);
    RUN_TEST(test_frame_speed_field);
    RUN_TEST(test_frame_checksum_field);
    return UNITY_END();
}
