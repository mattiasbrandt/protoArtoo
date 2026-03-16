// =============================================================================
// test/test_native/test_sbus_unpack/test_sbus_unpack.cpp
//
// Native unit tests for sbusUnpackChannels() — 16 × 11-bit SBUS channel
// unpacking from a packed byte payload.
//
// Safety relevance: channel values drive the robot. Bit-shift errors here
// corrupt all 16 channels silently. These tests guard against off-by-one
// errors in the bit cursor and incorrect masking.
//
// Note on buffer size: sbusUnpackChannels() reads data[byteIdx+2] for all
// channels. For ch15 (bitPos=165, byteIdx=20) this reads data[22], which is
// one byte past the 22-byte payload. The caller must supply at least 23 bytes.
// In production, frame+1 points into a 25-byte frame so data[22]=frame[23]
// (the flags byte) is always valid. Tests use a 23-byte array accordingly.
// =============================================================================
#include <unity.h>

#include "sbus_unpack.h"

void setUp() {
}
void tearDown() {
}

// Build a 23-byte payload with a single channel set to `value`, all others 0.
// Returns the payload via the `out` parameter.
static void make_single_channel_payload(int channel, int16_t value, uint8_t out[23]) {
    for (int i = 0; i < 23; i++) {
        out[i] = 0;
    }
    int bitPos = channel * 11;
    int byteIdx = bitPos / 8;
    int bitOff = bitPos % 8;
    uint32_t v = static_cast<uint32_t>(value & 0x7FF) << bitOff;
    out[byteIdx] |= static_cast<uint8_t>(v & 0xFF);
    out[byteIdx + 1] |= static_cast<uint8_t>((v >> 8) & 0xFF);
    out[byteIdx + 2] |= static_cast<uint8_t>((v >> 16) & 0xFF);
}

// --- Baseline: all-zero payload ---

void test_all_zero_payload_gives_all_zero_channels() {
    uint8_t data[23] = {};
    int16_t ch[16] = {};
    sbusUnpackChannels(data, ch);
    for (int i = 0; i < 16; i++) {
        TEST_ASSERT_EQUAL_INT16(0, ch[i]);
    }
}

// --- Single-channel isolation: verify bit cursor for first, middle, last ---

void test_ch0_value_1() {
    uint8_t data[23];
    int16_t ch[16];
    make_single_channel_payload(0, 1, data);
    sbusUnpackChannels(data, ch);
    TEST_ASSERT_EQUAL_INT16(1, ch[0]);
    for (int i = 1; i < 16; i++) {
        TEST_ASSERT_EQUAL_INT16(0, ch[i]);
    }
}

void test_ch1_value_1() {
    uint8_t data[23];
    int16_t ch[16];
    make_single_channel_payload(1, 1, data);
    sbusUnpackChannels(data, ch);
    TEST_ASSERT_EQUAL_INT16(1, ch[1]);
    TEST_ASSERT_EQUAL_INT16(0, ch[0]);
    TEST_ASSERT_EQUAL_INT16(0, ch[2]);
}

void test_ch7_value_1() {
    uint8_t data[23];
    int16_t ch[16];
    make_single_channel_payload(7, 1, data);
    sbusUnpackChannels(data, ch);
    TEST_ASSERT_EQUAL_INT16(1, ch[7]);
    for (int i = 0; i < 16; i++) {
        if (i != 7) {
            TEST_ASSERT_EQUAL_INT16(0, ch[i]);
        }
    }
}

void test_ch15_value_1() {
    uint8_t data[23];
    int16_t ch[16];
    make_single_channel_payload(15, 1, data);
    sbusUnpackChannels(data, ch);
    TEST_ASSERT_EQUAL_INT16(1, ch[15]);
    for (int i = 0; i < 15; i++) {
        TEST_ASSERT_EQUAL_INT16(0, ch[i]);
    }
}

// --- Upper-bound 11-bit values (0x7FF = 2047) ---

void test_ch0_max_value() {
    uint8_t data[23];
    int16_t ch[16];
    make_single_channel_payload(0, 0x7FF, data);
    sbusUnpackChannels(data, ch);
    TEST_ASSERT_EQUAL_INT16(0x7FF, ch[0]);
    for (int i = 1; i < 16; i++) {
        TEST_ASSERT_EQUAL_INT16(0, ch[i]);
    }
}

void test_ch7_max_value() {
    uint8_t data[23];
    int16_t ch[16];
    make_single_channel_payload(7, 0x7FF, data);
    sbusUnpackChannels(data, ch);
    TEST_ASSERT_EQUAL_INT16(0x7FF, ch[7]);
    for (int i = 0; i < 16; i++) {
        if (i != 7) {
            TEST_ASSERT_EQUAL_INT16(0, ch[i]);
        }
    }
}

void test_ch15_max_value() {
    uint8_t data[23];
    int16_t ch[16];
    make_single_channel_payload(15, 0x7FF, data);
    sbusUnpackChannels(data, ch);
    TEST_ASSERT_EQUAL_INT16(0x7FF, ch[15]);
    for (int i = 0; i < 15; i++) {
        TEST_ASSERT_EQUAL_INT16(0, ch[i]);
    }
}

// --- Mixed known values across several channel indices ---
// ch0=172 (SBUS_MIN), ch3=992 (SBUS center), ch8=1811 (SBUS_MAX), ch15=500

void test_mixed_known_channel_values() {
    uint8_t data[23] = {};
    int16_t ch[16] = {};

    // Pack each channel value into the payload
    const int16_t expected[16] = {172, 0, 0, 992, 0, 0, 0, 0, 1811, 0, 0, 0, 0, 0, 0, 500};
    for (int i = 0; i < 16; i++) {
        if (expected[i] == 0) {
            continue;
        }
        int bitPos = i * 11;
        int byteIdx = bitPos / 8;
        int bitOff = bitPos % 8;
        uint32_t v = static_cast<uint32_t>(expected[i] & 0x7FF) << bitOff;
        data[byteIdx] |= static_cast<uint8_t>(v & 0xFF);
        data[byteIdx + 1] |= static_cast<uint8_t>((v >> 8) & 0xFF);
        data[byteIdx + 2] |= static_cast<uint8_t>((v >> 16) & 0xFF);
    }

    sbusUnpackChannels(data, ch);

    for (int i = 0; i < 16; i++) {
        TEST_ASSERT_EQUAL_INT16(expected[i], ch[i]);
    }
}

// --- All channels simultaneously at max (0x7FF) ---

void test_all_channels_max() {
    // All 176 bits set to 1 → every channel reads 0x7FF
    uint8_t data[23];
    for (int i = 0; i < 22; i++) {
        data[i] = 0xFF;
    }
    data[22] = 0xFF;  // guard byte — value doesn't affect ch[15] after masking

    int16_t ch[16] = {};
    sbusUnpackChannels(data, ch);
    for (int i = 0; i < 16; i++) {
        TEST_ASSERT_EQUAL_INT16(0x7FF, ch[i]);
    }
}

// --- Verify 11-bit mask: bits above bit 10 in a channel window are discarded ---

void test_mask_discards_bits_above_11() {
    // Set data[0]=0xFF, data[1]=0xFF — ch0 window has all bits set.
    // ch0 should still be 0x7FF (11 bits), not 0xFFF or larger.
    uint8_t data[23] = {};
    data[0] = 0xFF;
    data[1] = 0xFF;
    int16_t ch[16] = {};
    sbusUnpackChannels(data, ch);
    TEST_ASSERT_EQUAL_INT16(0x7FF, ch[0]);
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_all_zero_payload_gives_all_zero_channels);
    RUN_TEST(test_ch0_value_1);
    RUN_TEST(test_ch1_value_1);
    RUN_TEST(test_ch7_value_1);
    RUN_TEST(test_ch15_value_1);
    RUN_TEST(test_ch0_max_value);
    RUN_TEST(test_ch7_max_value);
    RUN_TEST(test_ch15_max_value);
    RUN_TEST(test_mixed_known_channel_values);
    RUN_TEST(test_all_channels_max);
    RUN_TEST(test_mask_discards_bits_above_11);
    return UNITY_END();
}
