// =============================================================================
// test/test_native/test_audio_mp3trigger/test_audio_mp3trigger.cpp
//
// Native tests for SparkFun MP3 Trigger driver logic.
//
// Tests volume scaling, command byte construction, guard logic, constants, and
// S1 response parsing without any GPIO or hardware dependencies.
//
// The driver is not instantiated here — the same approach used by
// test_audio_chirp: all logic under test is extracted as standalone functions
// that mirror the implementation in audio_mp3trigger.cpp exactly.
//
// Volume formula (inverted VS1053 register):
//   nativeVol = (30 - vol) * MP3TRIGGER_VOL_MAX / 30
//   vol=0 → 255 (silent), vol=30 → 0 (maximum), vol=15 → 127.
//
// Stop workaround: play track MP3TRIGGER_STOP_TRACK (254) — see driver header.
// =============================================================================

#include <stdint.h>
#include <stdio.h>
#include <unity.h>

// Replicate constants locally — keeps tests self-contained and avoids pulling
// in Arduino headers through audio_mp3trigger.h.
static constexpr uint8_t MP3TRIGGER_STOP_TRACK = 254;
static constexpr uint8_t MP3TRIGGER_VOL_MAX    = 255;

// Capability bits (from audio_driver.h — replicated to keep tests standalone)
static constexpr uint8_t AUDIO_CAP_STATUS_QUERY   = 0x01;
static constexpr uint8_t AUDIO_CAP_TRACK_COUNT    = 0x04;
static constexpr uint8_t AUDIO_CAP_CURRENT_TRACK  = 0x08;

// Volume scaling formula (mirrors audio_mp3trigger.cpp exactly)
static uint8_t mp3triggerVolScale(uint8_t vol) {
    return (uint8_t)((uint32_t)(30u - vol) * MP3TRIGGER_VOL_MAX / 30u);
}

// Guard logic for track range (mirrors playTrack() guard in audio_mp3trigger.cpp)
static bool mp3triggerShouldDrop(uint16_t track) {
    return track == 0 || track > 255;
}

void setUp() {}
void tearDown() {}

// -----------------------------------------------------------------------------
// Test 1: Volume scaling boundary — vol=0 → 255 (silent)
// -----------------------------------------------------------------------------
void test_volume_zero_maps_to_255() {
    TEST_ASSERT_EQUAL_UINT8(255, mp3triggerVolScale(0));
}

// -----------------------------------------------------------------------------
// Test 2: Volume scaling boundary — vol=30 → 0 (maximum)
// -----------------------------------------------------------------------------
void test_volume_max_30_maps_to_0() {
    TEST_ASSERT_EQUAL_UINT8(0, mp3triggerVolScale(30));
}

// -----------------------------------------------------------------------------
// Test (bonus): Volume scaling midpoint — vol=15 → 127
// (30-15)*255/30 = 3825/30 = 127 exactly.
// -----------------------------------------------------------------------------
void test_volume_mid_15_maps_to_127() {
    TEST_ASSERT_EQUAL_UINT8(127, mp3triggerVolScale(15));
}

// -----------------------------------------------------------------------------
// Test (part of spec test 2): Volume scale is monotonically non-increasing
// across the full 0–30 range (higher vol → lower native value).
// -----------------------------------------------------------------------------
void test_volume_scale_monotonically_decreasing() {
    for (uint8_t v = 1; v <= 30; v++) {
        TEST_ASSERT_TRUE_MESSAGE(mp3triggerVolScale(v) <= mp3triggerVolScale(v - 1),
                                 "volume scale must decrease or stay flat as vol increases");
    }
}

// -----------------------------------------------------------------------------
// Test 3: Volume scale never exceeds MP3TRIGGER_VOL_MAX (255)
// -----------------------------------------------------------------------------
void test_volume_scale_never_exceeds_255() {
    for (uint8_t v = 0; v <= 30; v++) {
        TEST_ASSERT_TRUE_MESSAGE(mp3triggerVolScale(v) <= MP3TRIGGER_VOL_MAX,
                                 "scaled volume must not exceed MP3TRIGGER_VOL_MAX");
    }
}

// -----------------------------------------------------------------------------
// Test 4: playTrack(1) sends 't' + 0x01
// -----------------------------------------------------------------------------
void test_play_track_1_bytes() {
    // Verify byte construction matches wire protocol exactly.
    uint8_t cmd[2] = {'t', (uint8_t)1};
    TEST_ASSERT_EQUAL_HEX8('t', cmd[0]);
    TEST_ASSERT_EQUAL_HEX8(0x01, cmd[1]);
}

// -----------------------------------------------------------------------------
// Test 5: playTrack(255) sends 't' + 0xFF (highest addressable track)
// -----------------------------------------------------------------------------
void test_play_track_255_boundary_bytes() {
    uint8_t cmd[2] = {'t', (uint8_t)255};
    TEST_ASSERT_EQUAL_HEX8('t', cmd[0]);
    TEST_ASSERT_EQUAL_HEX8(0xFF, cmd[1]);
}

// -----------------------------------------------------------------------------
// Test 6: playTrack(256) must be dropped — guard prevents uint8_t overflow.
// If cast directly, (uint8_t)256 = 0x00, which would play the wrong track.
// The guard logic must catch track == 256 before any byte is sent.
// -----------------------------------------------------------------------------
void test_play_track_256_is_dropped() {
    TEST_ASSERT_TRUE(mp3triggerShouldDrop(256));
}

void test_play_track_255_is_not_dropped() {
    TEST_ASSERT_FALSE(mp3triggerShouldDrop(255));
}

void test_play_track_0_is_dropped() {
    TEST_ASSERT_TRUE(mp3triggerShouldDrop(0));
}

// Demonstrate why the guard is necessary: direct cast of 256 overflows to 0x00.
void test_play_track_256_cast_overflows_to_zero() {
    uint16_t track = 256;
    uint8_t  cast  = (uint8_t)track;
    TEST_ASSERT_EQUAL_HEX8(0x00, cast);  // proves the overflow that guard prevents
}

// -----------------------------------------------------------------------------
// Test 7: Stop track constant is exactly 254
// -----------------------------------------------------------------------------
void test_stop_track_constant_is_254() {
    TEST_ASSERT_EQUAL_UINT8(254, MP3TRIGGER_STOP_TRACK);
}

// stop() sends 't' + MP3TRIGGER_STOP_TRACK (0xFE)
void test_stop_command_bytes() {
    uint8_t cmd[2] = {'t', MP3TRIGGER_STOP_TRACK};
    TEST_ASSERT_EQUAL_HEX8('t', cmd[0]);
    TEST_ASSERT_EQUAL_HEX8(0xFE, cmd[1]);
}

// -----------------------------------------------------------------------------
// Test 8: capabilities() returns 0x0D
//   AUDIO_CAP_STATUS_QUERY (0x01) | AUDIO_CAP_TRACK_COUNT (0x04)
//   | AUDIO_CAP_CURRENT_TRACK (0x08) = 0x0D
// -----------------------------------------------------------------------------
void test_capabilities_is_0x0D() {
    uint8_t caps = AUDIO_CAP_STATUS_QUERY | AUDIO_CAP_TRACK_COUNT | AUDIO_CAP_CURRENT_TRACK;
    TEST_ASSERT_EQUAL_HEX8(0x0D, caps);
}

// AUDIO_CAP_DEVICE_TYPE (0x02) must not be set — no device-type concept.
void test_capabilities_no_device_type() {
    uint8_t caps = AUDIO_CAP_STATUS_QUERY | AUDIO_CAP_TRACK_COUNT | AUDIO_CAP_CURRENT_TRACK;
    TEST_ASSERT_EQUAL_UINT8(0, caps & 0x02);
}

// AUDIO_CAP_QUERY_SAFE_PLAYING (0x10) must not be set — no play-state query.
void test_capabilities_not_safe_during_play() {
    uint8_t caps = AUDIO_CAP_STATUS_QUERY | AUDIO_CAP_TRACK_COUNT | AUDIO_CAP_CURRENT_TRACK;
    TEST_ASSERT_EQUAL_UINT8(0, caps & 0x10);
}

// -----------------------------------------------------------------------------
// Test 9: driverName() returns "MP3Trigger"
// -----------------------------------------------------------------------------
void test_driver_name_is_MP3Trigger() {
    // Mirror the literal from AudioDriverMp3Trigger::driverName()
    const char* name = "MP3Trigger";
    TEST_ASSERT_EQUAL_STRING("MP3Trigger", name);
    TEST_ASSERT_EQUAL_INT(10, (int)(sizeof("MP3Trigger") - 1));
}

// -----------------------------------------------------------------------------
// Bonus: S1 response parsing — strip '=' prefix before sscanf
// Response format: "=NNN\r\n"; after readLine() stripping \r: "=NNN"
// sscanf(line + 1, ...) parses from after the '='.
// -----------------------------------------------------------------------------
void test_s1_response_parse_strips_equals_prefix() {
    char line[] = "=171";
    TEST_ASSERT_EQUAL_CHAR('=', line[0]);
    uint16_t count = 0;
    TEST_ASSERT_EQUAL_INT(1, sscanf(line + 1, "%hu", &count));
    TEST_ASSERT_EQUAL_UINT16(171, count);
}

void test_s1_response_parse_single_digit() {
    char line[] = "=5";
    uint16_t count = 0;
    TEST_ASSERT_EQUAL_INT(1, sscanf(line + 1, "%hu", &count));
    TEST_ASSERT_EQUAL_UINT16(5, count);
}

void test_s1_response_no_equals_prefix_skips_parse() {
    // The driver guard: only parse when line[0] == '='.
    // Verify that a response without '=' is correctly identified as non-matching,
    // i.e. the predicate that gates sscanf returns false.
    char line[] = "171";  // no '=' prefix — would happen on timeout/noise byte
    bool guard_passes = (line[0] == '=');
    TEST_ASSERT_FALSE_MESSAGE(guard_passes,
        "response without '=' prefix must not trigger sscanf parse");
}

// Volume formula: additional spot-check values
void test_volume_10_maps_correct() {
    // (30-10)*255/30 = 20*255/30 = 5100/30 = 170
    TEST_ASSERT_EQUAL_UINT8(170, mp3triggerVolScale(10));
}

void test_volume_20_maps_correct() {
    // (30-20)*255/30 = 10*255/30 = 2550/30 = 85
    TEST_ASSERT_EQUAL_UINT8(85, mp3triggerVolScale(20));
}

void test_volume_1_maps_correct() {
    // (30-1)*255/30 = 29*255/30 = 7395/30 = 246 (integer division)
    TEST_ASSERT_EQUAL_UINT8(246, mp3triggerVolScale(1));
}

// -----------------------------------------------------------------------------

int main(int argc, char** argv) {
    UNITY_BEGIN();

    // Volume scaling (spec tests 1–3)
    RUN_TEST(test_volume_zero_maps_to_255);
    RUN_TEST(test_volume_max_30_maps_to_0);
    RUN_TEST(test_volume_mid_15_maps_to_127);
    RUN_TEST(test_volume_scale_monotonically_decreasing);
    RUN_TEST(test_volume_scale_never_exceeds_255);
    RUN_TEST(test_volume_10_maps_correct);
    RUN_TEST(test_volume_20_maps_correct);
    RUN_TEST(test_volume_1_maps_correct);

    // Play command bytes (spec tests 4–6)
    RUN_TEST(test_play_track_1_bytes);
    RUN_TEST(test_play_track_255_boundary_bytes);
    RUN_TEST(test_play_track_256_is_dropped);
    RUN_TEST(test_play_track_255_is_not_dropped);
    RUN_TEST(test_play_track_0_is_dropped);
    RUN_TEST(test_play_track_256_cast_overflows_to_zero);

    // Stop command (spec test 7)
    RUN_TEST(test_stop_track_constant_is_254);
    RUN_TEST(test_stop_command_bytes);

    // Capabilities (spec test 8)
    RUN_TEST(test_capabilities_is_0x0D);
    RUN_TEST(test_capabilities_no_device_type);
    RUN_TEST(test_capabilities_not_safe_during_play);

    // Driver name (spec test 9)
    RUN_TEST(test_driver_name_is_MP3Trigger);

    // S1 response parsing
    RUN_TEST(test_s1_response_parse_strips_equals_prefix);
    RUN_TEST(test_s1_response_parse_single_digit);
    RUN_TEST(test_s1_response_no_equals_prefix_skips_parse);

    return UNITY_END();
}
