// =============================================================================
// test/test_native/test_hoverboard_feedback/test_hoverboard_feedback.cpp
//
// Native unit tests for hoverboard UART feedback-frame parsing.
// Safety relevance: correct frame parsing ensures dashboard battery voltage,
// board temperature, speed, and current telemetry reflect controller reality.
// =============================================================================
#include <unity.h>

#include <string.h>

#include "hoverboard_uart.h"

void setUp() {}
void tearDown() {}

static uint16_t xorChecksum(const uint16_t* fields, int n) {
    uint16_t x = 0;
    for (int i = 0; i < n; i++) {
        x ^= fields[i];
    }
    return x;
}

static void writeFrame(uint8_t* buf, const uint16_t* fields, int n) {
    for (int i = 0; i < n; i++) {
        memcpy(buf + i * 2, &fields[i], 2);
    }
}

static void buildValidFocFrame(uint8_t* buf) {
    uint16_t fields[9] = {
        (uint16_t)0xABCD,      // start
        (uint16_t)0,           // cmd1
        (uint16_t)0,           // cmd2
        (uint16_t)100,         // speedR
        (uint16_t)(int16_t)-50,  // speedL
        (uint16_t)3650,        // batVoltage
        (uint16_t)280,         // boardTemp
        (uint16_t)0,           // cmdLed
        (uint16_t)0            // checksum (filled below)
    };
    fields[8] = xorChecksum(fields, 8);
    writeFrame(buf, fields, 9);
}

static void buildValidGen2xFrame(uint8_t* buf) {
    uint16_t fields[13] = {
        (uint16_t)0xABCD,  // start
        (uint16_t)0,       // cmd1
        (uint16_t)0,       // cmd2
        (uint16_t)200,     // speedR
        (uint16_t)195,     // speedL
        (uint16_t)100,     // wheelR
        (uint16_t)98,      // wheelL
        (uint16_t)230,     // currL
        (uint16_t)225,     // currR
        (uint16_t)3700,    // batVoltage
        (uint16_t)310,     // boardTemp
        (uint16_t)0,       // cmdLed
        (uint16_t)0        // checksum (filled below)
    };
    fields[12] = xorChecksum(fields, 12);
    writeFrame(buf, fields, 13);
}

void test_foc_valid_frame() {
    uint8_t frame[kHoverFocFrameLen] = {};
    HoverboardFeedback out = {};

    buildValidFocFrame(frame);

    TEST_ASSERT_TRUE(parseHoverboardFeedbackFrame(frame, kHoverFocFrameLen, &out));
    TEST_ASSERT_EQUAL_INT16(3650, out.batteryRaw);
    TEST_ASSERT_EQUAL_INT16(280, out.boardTempRaw);
    TEST_ASSERT_EQUAL_INT16(100, out.speedR);
    TEST_ASSERT_EQUAL_INT16(-50, out.speedL);
    TEST_ASSERT_EQUAL_INT16(0, out.currentL);
    TEST_ASSERT_EQUAL_INT16(0, out.currentR);
}

void test_gen2x_valid_frame() {
    uint8_t frame[kHoverGen2xFrameLen] = {};
    HoverboardFeedback out = {};

    buildValidGen2xFrame(frame);

    TEST_ASSERT_TRUE(parseHoverboardFeedbackFrame(frame, kHoverGen2xFrameLen, &out));
    TEST_ASSERT_EQUAL_INT16(3700, out.batteryRaw);
    TEST_ASSERT_EQUAL_INT16(310, out.boardTempRaw);
    TEST_ASSERT_EQUAL_INT16(200, out.speedR);
    TEST_ASSERT_EQUAL_INT16(195, out.speedL);
    TEST_ASSERT_EQUAL_INT16(230, out.currentL);
    TEST_ASSERT_EQUAL_INT16(225, out.currentR);
}

void test_foc_bad_checksum() {
    uint8_t frame[kHoverFocFrameLen] = {};
    HoverboardFeedback out = {};

    buildValidFocFrame(frame);
    frame[kHoverFocFrameLen - 2] ^= 0x01;  // corrupt checksum LSB

    TEST_ASSERT_FALSE(parseHoverboardFeedbackFrame(frame, kHoverFocFrameLen, &out));
}

void test_gen2x_bad_checksum() {
    uint8_t frame[kHoverGen2xFrameLen] = {};
    HoverboardFeedback out = {};

    buildValidGen2xFrame(frame);
    frame[kHoverGen2xFrameLen - 2] ^= 0x01;  // corrupt checksum LSB

    TEST_ASSERT_FALSE(parseHoverboardFeedbackFrame(frame, kHoverGen2xFrameLen, &out));
}

void test_unrecognised_length() {
    uint8_t frame[kHoverGen2xFrameLen] = {};
    HoverboardFeedback out = {};

    buildValidFocFrame(frame);

    TEST_ASSERT_FALSE(parseHoverboardFeedbackFrame(frame, 20, &out));
}

void test_null_out_returns_false() {
    uint8_t frame[kHoverFocFrameLen] = {};

    buildValidFocFrame(frame);

    TEST_ASSERT_FALSE(parseHoverboardFeedbackFrame(frame, kHoverFocFrameLen, nullptr));
}

void test_foc_neutral_values() {
    uint8_t frame[kHoverFocFrameLen] = {};
    HoverboardFeedback out = {};
    uint16_t fields[9] = {
        (uint16_t)0xABCD,
        (uint16_t)0,
        (uint16_t)0,
        (uint16_t)0,     // speedR
        (uint16_t)0,     // speedL
        (uint16_t)3600,  // batVoltage
        (uint16_t)250,   // boardTemp
        (uint16_t)0,
        (uint16_t)0
    };

    fields[8] = xorChecksum(fields, 8);
    writeFrame(frame, fields, 9);

    TEST_ASSERT_TRUE(parseHoverboardFeedbackFrame(frame, kHoverFocFrameLen, &out));
    TEST_ASSERT_EQUAL_INT16(3600, out.batteryRaw);
    TEST_ASSERT_EQUAL_INT16(250, out.boardTempRaw);
    TEST_ASSERT_EQUAL_INT16(0, out.speedR);
    TEST_ASSERT_EQUAL_INT16(0, out.speedL);
    TEST_ASSERT_EQUAL_INT16(0, out.currentL);
    TEST_ASSERT_EQUAL_INT16(0, out.currentR);
}

void test_gen2x_negative_speed() {
    uint8_t frame[kHoverGen2xFrameLen] = {};
    HoverboardFeedback out = {};
    uint16_t fields[13] = {
        (uint16_t)0xABCD,
        (uint16_t)0,
        (uint16_t)0,
        (uint16_t)(int16_t)-150,  // speedR
        (uint16_t)(int16_t)-145,  // speedL
        (uint16_t)100,
        (uint16_t)98,
        (uint16_t)230,
        (uint16_t)225,
        (uint16_t)3700,
        (uint16_t)310,
        (uint16_t)0,
        (uint16_t)0
    };

    fields[12] = xorChecksum(fields, 12);
    writeFrame(frame, fields, 13);

    TEST_ASSERT_TRUE(parseHoverboardFeedbackFrame(frame, kHoverGen2xFrameLen, &out));
    TEST_ASSERT_EQUAL_INT16(-150, out.speedR);
    TEST_ASSERT_EQUAL_INT16(-145, out.speedL);
    TEST_ASSERT_EQUAL_INT16(230, out.currentL);
    TEST_ASSERT_EQUAL_INT16(225, out.currentR);
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_foc_valid_frame);
    RUN_TEST(test_gen2x_valid_frame);
    RUN_TEST(test_foc_bad_checksum);
    RUN_TEST(test_gen2x_bad_checksum);
    RUN_TEST(test_unrecognised_length);
    RUN_TEST(test_null_out_returns_false);
    RUN_TEST(test_foc_neutral_values);
    RUN_TEST(test_gen2x_negative_speed);
    return UNITY_END();
}
