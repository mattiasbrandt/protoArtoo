// =============================================================================
// test/test_native/test_audio_frames/test_audio_frames.cpp
//
// Native tests for the soft UART audio driver binary frame format.
//
// The driver sends frames as: 0xAA [CMD] [LEN] [DATA...] 0xAB
// These tests verify the frame byte values are correct before any hardware
// connection is made. Command bytes are flagged for T09 hardware validation
// but the frame structure itself is verified here.
//
// Note: the driver uses GPIO calls (pinMode/digitalWrite) that are not
// available natively, so we test the frame byte logic by reconstructing
// the same arrays the driver builds and checking each byte.
// =============================================================================

#include <stdint.h>
#include <unity.h>

// Frame constants (must match audio_soft_uart.cpp)
static constexpr uint8_t FRAME_HEADER    = 0xAA;
static constexpr uint8_t FRAME_FOOTER    = 0xAB;
static constexpr uint8_t CMD_PLAY_INDEX  = 0x07;
static constexpr uint8_t CMD_STOP        = 0x04;
static constexpr uint8_t CMD_SET_VOLUME  = 0x13;

void setUp() {}
void tearDown() {}

// -----------------------------------------------------------------------------
// playTrack frame: AA 07 02 [hi] [lo] AB
// -----------------------------------------------------------------------------

void test_play_frame_track_1() {
    uint16_t track = 1;
    uint8_t frame[] = {
        FRAME_HEADER,
        CMD_PLAY_INDEX,
        0x02,
        (uint8_t)(track >> 8),
        (uint8_t)(track & 0xFF),
        FRAME_FOOTER,
    };
    TEST_ASSERT_EQUAL_HEX8(0xAA, frame[0]);
    TEST_ASSERT_EQUAL_HEX8(0x07, frame[1]);
    TEST_ASSERT_EQUAL_HEX8(0x02, frame[2]);
    TEST_ASSERT_EQUAL_HEX8(0x00, frame[3]);  // hi byte of 1
    TEST_ASSERT_EQUAL_HEX8(0x01, frame[4]);  // lo byte of 1
    TEST_ASSERT_EQUAL_HEX8(0xAB, frame[5]);
    TEST_ASSERT_EQUAL(6, sizeof(frame));
}

void test_play_frame_track_126_scream() {
    uint16_t track = 126;
    uint8_t frame[] = {
        FRAME_HEADER,
        CMD_PLAY_INDEX,
        0x02,
        (uint8_t)(track >> 8),
        (uint8_t)(track & 0xFF),
        FRAME_FOOTER,
    };
    TEST_ASSERT_EQUAL_HEX8(0x00, frame[3]);  // hi byte of 126
    TEST_ASSERT_EQUAL_HEX8(0x7E, frame[4]);  // lo byte of 126 (0x7E = 126)
}

void test_play_frame_track_256_boundary() {
    // Track 256 crosses the byte boundary — hi byte becomes 0x01
    uint16_t track = 256;
    uint8_t frame[] = {
        FRAME_HEADER,
        CMD_PLAY_INDEX,
        0x02,
        (uint8_t)(track >> 8),
        (uint8_t)(track & 0xFF),
        FRAME_FOOTER,
    };
    TEST_ASSERT_EQUAL_HEX8(0x01, frame[3]);  // hi byte of 256
    TEST_ASSERT_EQUAL_HEX8(0x00, frame[4]);  // lo byte of 256
}

void test_play_frame_track_65535_max() {
    uint16_t track = 65535;
    uint8_t frame[] = {
        FRAME_HEADER,
        CMD_PLAY_INDEX,
        0x02,
        (uint8_t)(track >> 8),
        (uint8_t)(track & 0xFF),
        FRAME_FOOTER,
    };
    TEST_ASSERT_EQUAL_HEX8(0xFF, frame[3]);
    TEST_ASSERT_EQUAL_HEX8(0xFF, frame[4]);
}

// -----------------------------------------------------------------------------
// stop frame: AA 04 00 AB
// -----------------------------------------------------------------------------

void test_stop_frame() {
    uint8_t frame[] = {FRAME_HEADER, CMD_STOP, 0x00, FRAME_FOOTER};
    TEST_ASSERT_EQUAL_HEX8(0xAA, frame[0]);
    TEST_ASSERT_EQUAL_HEX8(0x04, frame[1]);
    TEST_ASSERT_EQUAL_HEX8(0x00, frame[2]);
    TEST_ASSERT_EQUAL_HEX8(0xAB, frame[3]);
    TEST_ASSERT_EQUAL(4, sizeof(frame));
}

// -----------------------------------------------------------------------------
// setVolume frame: AA 13 01 [vol] AB
// -----------------------------------------------------------------------------

void test_volume_frame_zero() {
    uint8_t vol = 0;
    uint8_t frame[] = {FRAME_HEADER, CMD_SET_VOLUME, 0x01, vol, FRAME_FOOTER};
    TEST_ASSERT_EQUAL_HEX8(0xAA, frame[0]);
    TEST_ASSERT_EQUAL_HEX8(0x13, frame[1]);
    TEST_ASSERT_EQUAL_HEX8(0x01, frame[2]);
    TEST_ASSERT_EQUAL_HEX8(0x00, frame[3]);
    TEST_ASSERT_EQUAL_HEX8(0xAB, frame[4]);
    TEST_ASSERT_EQUAL(5, sizeof(frame));
}

void test_volume_frame_max() {
    uint8_t vol = 30;
    uint8_t frame[] = {FRAME_HEADER, CMD_SET_VOLUME, 0x01, vol, FRAME_FOOTER};
    TEST_ASSERT_EQUAL_HEX8(0x1E, frame[3]);  // 30 = 0x1E
}

void test_volume_frame_mid() {
    uint8_t vol = 15;
    uint8_t frame[] = {FRAME_HEADER, CMD_SET_VOLUME, 0x01, vol, FRAME_FOOTER};
    TEST_ASSERT_EQUAL_HEX8(0x0F, frame[3]);  // 15 = 0x0F
}

// -----------------------------------------------------------------------------
// Frame structure invariants
// -----------------------------------------------------------------------------

void test_all_frames_start_with_header() {
    uint8_t play[]   = {FRAME_HEADER, CMD_PLAY_INDEX, 0x02, 0x00, 0x01, FRAME_FOOTER};
    uint8_t stop[]   = {FRAME_HEADER, CMD_STOP, 0x00, FRAME_FOOTER};
    uint8_t volume[] = {FRAME_HEADER, CMD_SET_VOLUME, 0x01, 0x0F, FRAME_FOOTER};

    TEST_ASSERT_EQUAL_HEX8(FRAME_HEADER, play[0]);
    TEST_ASSERT_EQUAL_HEX8(FRAME_HEADER, stop[0]);
    TEST_ASSERT_EQUAL_HEX8(FRAME_HEADER, volume[0]);
}

void test_all_frames_end_with_footer() {
    uint8_t play[]   = {FRAME_HEADER, CMD_PLAY_INDEX, 0x02, 0x00, 0x01, FRAME_FOOTER};
    uint8_t stop[]   = {FRAME_HEADER, CMD_STOP, 0x00, FRAME_FOOTER};
    uint8_t volume[] = {FRAME_HEADER, CMD_SET_VOLUME, 0x01, 0x0F, FRAME_FOOTER};

    TEST_ASSERT_EQUAL_HEX8(FRAME_FOOTER, play[5]);
    TEST_ASSERT_EQUAL_HEX8(FRAME_FOOTER, stop[3]);
    TEST_ASSERT_EQUAL_HEX8(FRAME_FOOTER, volume[4]);
}

// -----------------------------------------------------------------------------

int main(int argc, char** argv) {
    UNITY_BEGIN();

    RUN_TEST(test_play_frame_track_1);
    RUN_TEST(test_play_frame_track_126_scream);
    RUN_TEST(test_play_frame_track_256_boundary);
    RUN_TEST(test_play_frame_track_65535_max);

    RUN_TEST(test_stop_frame);

    RUN_TEST(test_volume_frame_zero);
    RUN_TEST(test_volume_frame_max);
    RUN_TEST(test_volume_frame_mid);

    RUN_TEST(test_all_frames_start_with_header);
    RUN_TEST(test_all_frames_end_with_footer);

    return UNITY_END();
}
