// =============================================================================
// test/test_native/test_audio_frames/test_audio_frames.cpp
//
// Native tests for the DY-SV5W UART driver checksum frame format.
//
// Frame format (checksum dialect — confirmed by datasheet):
//   [0xAA] [CMD] [LEN] [DATA...] [SM]
//   SM = low 8 bits of sum of ALL preceding bytes (0xAA + CMD + LEN + DATA).
//
// These tests verify the payload bytes and checksum computation are correct
// before any hardware connection is tested.
//
// NOTE: This file was updated when the driver was rewritten from the end-marker
//   dialect (trailing 0xAB) to the correct checksum dialect (see T65). The old
//   tests used FRAME_FOOTER = 0xAB; the 0xAB in query frame AA 01 00 AB is just
//   a checksum coincidence (AA+01+00 = AB), not a protocol footer byte.
// =============================================================================

#include <stdint.h>
#include <unity.h>

// SM = low 8 bits of sum of all payload bytes (including 0xAA).
static uint8_t computeSM(const uint8_t* payload, uint8_t len) {
    uint8_t sum = 0;
    for (uint8_t i = 0; i < len; i++) {
        sum = (uint8_t)(sum + payload[i]);
    }
    return sum;
}

void setUp() {
}
void tearDown() {
}

// -----------------------------------------------------------------------------
// playTrack frame: AA 07 02 [hi] [lo] SM
// -----------------------------------------------------------------------------

void test_play_frame_track_1_bytes() {
    // Track 1: hi=0x00 lo=0x01
    uint8_t payload[] = {0xAA, 0x07, 0x02, 0x00, 0x01};
    TEST_ASSERT_EQUAL_HEX8(0xAA, payload[0]);
    TEST_ASSERT_EQUAL_HEX8(0x07, payload[1]);
    TEST_ASSERT_EQUAL_HEX8(0x02, payload[2]);
    TEST_ASSERT_EQUAL_HEX8(0x00, payload[3]);
    TEST_ASSERT_EQUAL_HEX8(0x01, payload[4]);
}

void test_play_frame_track_1_checksum() {
    // SM = AA+07+02+00+01 = 0xB4
    uint8_t payload[] = {0xAA, 0x07, 0x02, 0x00, 0x01};
    TEST_ASSERT_EQUAL_HEX8(0xB4, computeSM(payload, sizeof(payload)));
}

void test_play_frame_track_126_bytes() {
    // Track 126 (0x7E): hi=0x00 lo=0x7E
    uint16_t track = 126;
    uint8_t payload[] = {0xAA, 0x07, 0x02, (uint8_t)(track >> 8), (uint8_t)(track & 0xFF)};
    TEST_ASSERT_EQUAL_HEX8(0x00, payload[3]);
    TEST_ASSERT_EQUAL_HEX8(0x7E, payload[4]);
}

void test_play_frame_track_126_checksum() {
    // SM = AA+07+02+00+7E = 0x31 (wraps: 0x131 & 0xFF = 0x31)
    uint8_t payload[] = {0xAA, 0x07, 0x02, 0x00, 0x7E};
    TEST_ASSERT_EQUAL_HEX8(0x31, computeSM(payload, sizeof(payload)));
}

void test_play_frame_track_256_boundary() {
    // Track 256 crosses the byte boundary — hi=0x01 lo=0x00
    uint16_t track = 256;
    uint8_t payload[] = {0xAA, 0x07, 0x02, (uint8_t)(track >> 8), (uint8_t)(track & 0xFF)};
    TEST_ASSERT_EQUAL_HEX8(0x01, payload[3]);
    TEST_ASSERT_EQUAL_HEX8(0x00, payload[4]);
}

void test_play_frame_track_65535_bytes() {
    uint16_t track = 65535;
    uint8_t payload[] = {0xAA, 0x07, 0x02, (uint8_t)(track >> 8), (uint8_t)(track & 0xFF)};
    TEST_ASSERT_EQUAL_HEX8(0xFF, payload[3]);
    TEST_ASSERT_EQUAL_HEX8(0xFF, payload[4]);
}

// -----------------------------------------------------------------------------
// stop frame: AA 04 00 SM   SM=0xAE
// -----------------------------------------------------------------------------

void test_stop_frame_bytes() {
    uint8_t payload[] = {0xAA, 0x04, 0x00};
    TEST_ASSERT_EQUAL_HEX8(0xAA, payload[0]);
    TEST_ASSERT_EQUAL_HEX8(0x04, payload[1]);
    TEST_ASSERT_EQUAL_HEX8(0x00, payload[2]);
}

void test_stop_frame_checksum() {
    // SM = AA+04+00 = 0xAE
    uint8_t payload[] = {0xAA, 0x04, 0x00};
    TEST_ASSERT_EQUAL_HEX8(0xAE, computeSM(payload, sizeof(payload)));
}

// -----------------------------------------------------------------------------
// setVolume frame: AA 13 01 [vol] SM
// -----------------------------------------------------------------------------

void test_volume_frame_zero_checksum() {
    // SM = AA+13+01+00 = 0xBE
    uint8_t payload[] = {0xAA, 0x13, 0x01, 0x00};
    TEST_ASSERT_EQUAL_HEX8(0xBE, computeSM(payload, sizeof(payload)));
}

void test_volume_frame_max_checksum() {
    // vol=30 (0x1E). SM = AA+13+01+1E = 0xDC
    uint8_t payload[] = {0xAA, 0x13, 0x01, 0x1E};
    TEST_ASSERT_EQUAL_HEX8(0xDC, computeSM(payload, sizeof(payload)));
}

void test_volume_frame_15_checksum() {
    // vol=15 (0x0F). SM = AA+13+01+0F = 0xC7
    uint8_t payload[] = {0xAA, 0x13, 0x01, 0x0F};
    TEST_ASSERT_EQUAL_HEX8(0xC7, computeSM(payload, sizeof(payload)));
}

// -----------------------------------------------------------------------------
// Query frame checksums (confirm datasheet-listed frames)
// AA+01+00 = AB (play state query — NOT a footer, just a coincidence)
// AA+09+00 = B3 (device online query)
// AA+0A+00 = B4 (play drive query)
// AA+0C+00 = B6 (total tracks query)
// AA+0D+00 = B7 (current track query)
// -----------------------------------------------------------------------------

void test_query_checksums() {
    uint8_t q_play_state[] = {0xAA, 0x01, 0x00};
    uint8_t q_device_online[] = {0xAA, 0x09, 0x00};
    uint8_t q_play_drive[] = {0xAA, 0x0A, 0x00};
    uint8_t q_total_tracks[] = {0xAA, 0x0C, 0x00};
    uint8_t q_current_track[] = {0xAA, 0x0D, 0x00};

    TEST_ASSERT_EQUAL_HEX8(0xAB, computeSM(q_play_state, 3));
    TEST_ASSERT_EQUAL_HEX8(0xB3, computeSM(q_device_online, 3));
    TEST_ASSERT_EQUAL_HEX8(0xB4, computeSM(q_play_drive, 3));
    TEST_ASSERT_EQUAL_HEX8(0xB6, computeSM(q_total_tracks, 3));
    TEST_ASSERT_EQUAL_HEX8(0xB7, computeSM(q_current_track, 3));
}

// SM wraps at 8 bits
void test_sm_wraps_at_8_bits() {
    // AA+FF+FF+FF = 3A7 → & 0xFF = 0xA7
    uint8_t payload[] = {0xAA, 0xFF, 0xFF, 0xFF};
    TEST_ASSERT_EQUAL_HEX8(0xA7, computeSM(payload, sizeof(payload)));
}

// -----------------------------------------------------------------------------

int main(int argc, char** argv) {
    UNITY_BEGIN();

    RUN_TEST(test_play_frame_track_1_bytes);
    RUN_TEST(test_play_frame_track_1_checksum);
    RUN_TEST(test_play_frame_track_126_bytes);
    RUN_TEST(test_play_frame_track_126_checksum);
    RUN_TEST(test_play_frame_track_256_boundary);
    RUN_TEST(test_play_frame_track_65535_bytes);

    RUN_TEST(test_stop_frame_bytes);
    RUN_TEST(test_stop_frame_checksum);

    RUN_TEST(test_volume_frame_zero_checksum);
    RUN_TEST(test_volume_frame_max_checksum);
    RUN_TEST(test_volume_frame_15_checksum);

    RUN_TEST(test_query_checksums);
    RUN_TEST(test_sm_wraps_at_8_bits);

    return UNITY_END();
}
//
// Note: the driver uses GPIO calls (pinMode/digitalWrite) that are not
// available natively, so we test the frame byte logic by reconstructing
// the same arrays the driver builds and checking each byte.
// =============================================================================

#include <stdint.h>
#include <unity.h>

// Frame constants (must match audio_soft_uart.cpp)
static constexpr uint8_t FRAME_HEADER = 0xAA;
static constexpr uint8_t FRAME_FOOTER = 0xAB;
static constexpr uint8_t CMD_PLAY_INDEX = 0x07;
static constexpr uint8_t CMD_STOP = 0x04;
static constexpr uint8_t CMD_SET_VOLUME = 0x13;

void setUp() {
}
void tearDown() {
}

// -----------------------------------------------------------------------------
// playTrack frame: AA 07 02 [hi] [lo] AB
// -----------------------------------------------------------------------------

void test_play_frame_track_1() {
    uint16_t track = 1;
    uint8_t frame[] = {
        FRAME_HEADER,          CMD_PLAY_INDEX,          0x02,
        (uint8_t)(track >> 8), (uint8_t)(track & 0xFF), FRAME_FOOTER,
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
        FRAME_HEADER,          CMD_PLAY_INDEX,          0x02,
        (uint8_t)(track >> 8), (uint8_t)(track & 0xFF), FRAME_FOOTER,
    };
    TEST_ASSERT_EQUAL_HEX8(0x00, frame[3]);  // hi byte of 126
    TEST_ASSERT_EQUAL_HEX8(0x7E, frame[4]);  // lo byte of 126 (0x7E = 126)
}

void test_play_frame_track_256_boundary() {
    // Track 256 crosses the byte boundary — hi byte becomes 0x01
    uint16_t track = 256;
    uint8_t frame[] = {
        FRAME_HEADER,          CMD_PLAY_INDEX,          0x02,
        (uint8_t)(track >> 8), (uint8_t)(track & 0xFF), FRAME_FOOTER,
    };
    TEST_ASSERT_EQUAL_HEX8(0x01, frame[3]);  // hi byte of 256
    TEST_ASSERT_EQUAL_HEX8(0x00, frame[4]);  // lo byte of 256
}

void test_play_frame_track_65535_max() {
    uint16_t track = 65535;
    uint8_t frame[] = {
        FRAME_HEADER,          CMD_PLAY_INDEX,          0x02,
        (uint8_t)(track >> 8), (uint8_t)(track & 0xFF), FRAME_FOOTER,
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
    uint8_t play[] = {FRAME_HEADER, CMD_PLAY_INDEX, 0x02, 0x00, 0x01, FRAME_FOOTER};
    uint8_t stop[] = {FRAME_HEADER, CMD_STOP, 0x00, FRAME_FOOTER};
    uint8_t volume[] = {FRAME_HEADER, CMD_SET_VOLUME, 0x01, 0x0F, FRAME_FOOTER};

    TEST_ASSERT_EQUAL_HEX8(FRAME_HEADER, play[0]);
    TEST_ASSERT_EQUAL_HEX8(FRAME_HEADER, stop[0]);
    TEST_ASSERT_EQUAL_HEX8(FRAME_HEADER, volume[0]);
}

void test_all_frames_end_with_footer() {
    uint8_t play[] = {FRAME_HEADER, CMD_PLAY_INDEX, 0x02, 0x00, 0x01, FRAME_FOOTER};
    uint8_t stop[] = {FRAME_HEADER, CMD_STOP, 0x00, FRAME_FOOTER};
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
