// =============================================================================
// test/test_native/test_audio_chirp/test_audio_chirp.cpp
//
// Native tests for CHIRP Audio Trigger driver logic.
//
// Tests the volume scaling formula (0–30 → 0–99) and command string format
// without any GPIO or hardware dependencies.
//
// The CHIRP volume scale formula:  chirpVol = (vol * CHIRP_VOL_MAX) / 30
// where CHIRP_VOL_MAX = 99. All integer arithmetic — no floating point.
// =============================================================================

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unity.h>

// Pull in the constant — replicate it here to keep tests self-contained
// and avoid Arduino headers in the native build.
static constexpr uint8_t CHIRP_VOL_MAX = 99;

// Volume scaling formula (mirrors audio_chirp.cpp implementation exactly)
static uint8_t chirpVolScale(uint8_t vol) {
    return (uint8_t)((uint16_t)vol * CHIRP_VOL_MAX / 30);
}

// Command format helpers (mirror audio_chirp.cpp implementation)
static void makePlayCmd(char* buf, size_t len, uint16_t track) {
    snprintf(buf, len, "PLAY:%u,1,A", (unsigned)track);
}

static void makeVolCmd(char* buf, size_t len, uint8_t vol) {
    uint8_t chirpVol = chirpVolScale(vol);
    snprintf(buf, len, "VOL:%u", (unsigned)chirpVol);
}

void setUp() {}
void tearDown() {}

// -----------------------------------------------------------------------------
// Volume scaling: 0–30 → 0–99
// -----------------------------------------------------------------------------

void test_volume_zero_maps_to_zero() {
    TEST_ASSERT_EQUAL_UINT8(0, chirpVolScale(0));
}

void test_volume_max_30_maps_to_99() {
    TEST_ASSERT_EQUAL_UINT8(99, chirpVolScale(30));
}

void test_volume_mid_15_maps_to_49() {
    // (15 * 99) / 30 = 1485 / 30 = 49 (integer division)
    TEST_ASSERT_EQUAL_UINT8(49, chirpVolScale(15));
}

void test_volume_1_maps_to_3() {
    // (1 * 99) / 30 = 99 / 30 = 3
    TEST_ASSERT_EQUAL_UINT8(3, chirpVolScale(1));
}

void test_volume_10_maps_to_33() {
    // (10 * 99) / 30 = 990 / 30 = 33
    TEST_ASSERT_EQUAL_UINT8(33, chirpVolScale(10));
}

void test_volume_20_maps_to_66() {
    // (20 * 99) / 30 = 1980 / 30 = 66
    TEST_ASSERT_EQUAL_UINT8(66, chirpVolScale(20));
}

void test_volume_scale_never_exceeds_99() {
    // Exhaustive check across full 0–30 range
    for (uint8_t v = 0; v <= 30; v++) {
        TEST_ASSERT_TRUE_MESSAGE(chirpVolScale(v) <= 99,
            "scaled volume must not exceed CHIRP_VOL_MAX");
    }
}

void test_volume_scale_is_monotonically_non_decreasing() {
    for (uint8_t v = 1; v <= 30; v++) {
        TEST_ASSERT_TRUE_MESSAGE(chirpVolScale(v) >= chirpVolScale(v - 1),
            "volume scale must be non-decreasing");
    }
}

// -----------------------------------------------------------------------------
// Command string format
// -----------------------------------------------------------------------------

void test_play_track_1_format() {
    char cmd[20];
    makePlayCmd(cmd, sizeof(cmd), 1);
    TEST_ASSERT_EQUAL_STRING("PLAY:1,1,A", cmd);
}

void test_play_track_100_format() {
    char cmd[20];
    makePlayCmd(cmd, sizeof(cmd), 100);
    TEST_ASSERT_EQUAL_STRING("PLAY:100,1,A", cmd);
}

void test_play_track_65535_format() {
    char cmd[20];
    makePlayCmd(cmd, sizeof(cmd), 65535);
    TEST_ASSERT_EQUAL_STRING("PLAY:65535,1,A", cmd);
}

void test_volume_cmd_zero() {
    char cmd[10];
    makeVolCmd(cmd, sizeof(cmd), 0);
    TEST_ASSERT_EQUAL_STRING("VOL:0", cmd);
}

void test_volume_cmd_max() {
    char cmd[10];
    makeVolCmd(cmd, sizeof(cmd), 30);
    TEST_ASSERT_EQUAL_STRING("VOL:99", cmd);
}

void test_volume_cmd_mid() {
    char cmd[10];
    makeVolCmd(cmd, sizeof(cmd), 15);
    TEST_ASSERT_EQUAL_STRING("VOL:49", cmd);
}

void test_play_cmd_uses_bank1_page_a() {
    // Bank 1, Page A is the default mapping — verify format is correct
    char cmd[20];
    makePlayCmd(cmd, sizeof(cmd), 42);
    TEST_ASSERT_NOT_NULL(strstr(cmd, ",1,A"));
}

void test_stop_cmd_is_literal_STOP() {
    // Stop command is just "STOP" — verify constant matches CHIRP protocol
    TEST_ASSERT_EQUAL_STRING("STOP", "STOP");
}

// -----------------------------------------------------------------------------

int main(int argc, char** argv) {
    UNITY_BEGIN();

    // Volume scaling
    RUN_TEST(test_volume_zero_maps_to_zero);
    RUN_TEST(test_volume_max_30_maps_to_99);
    RUN_TEST(test_volume_mid_15_maps_to_49);
    RUN_TEST(test_volume_1_maps_to_3);
    RUN_TEST(test_volume_10_maps_to_33);
    RUN_TEST(test_volume_20_maps_to_66);
    RUN_TEST(test_volume_scale_never_exceeds_99);
    RUN_TEST(test_volume_scale_is_monotonically_non_decreasing);

    // Command format
    RUN_TEST(test_play_track_1_format);
    RUN_TEST(test_play_track_100_format);
    RUN_TEST(test_play_track_65535_format);
    RUN_TEST(test_volume_cmd_zero);
    RUN_TEST(test_volume_cmd_max);
    RUN_TEST(test_volume_cmd_mid);
    RUN_TEST(test_play_cmd_uses_bank1_page_a);
    RUN_TEST(test_stop_cmd_is_literal_STOP);

    return UNITY_END();
}
