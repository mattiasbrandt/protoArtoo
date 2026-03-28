// =============================================================================
// test/test_native/test_audio_status_json/test_audio_status_json.cpp
//
// Native tests for formatAudioStatusJson().
// Verifies content correctness and that the output fits within the 192-byte
// buffer used by GET /api/audio.
// =============================================================================

#include <string.h>
#include <unity.h>

#include "api_helpers.h"

static constexpr uint8_t AUDIO_CAP_STATUS_QUERY = 0x01;
static constexpr uint8_t AUDIO_CAP_DEVICE_TYPE = 0x02;
static constexpr uint8_t AUDIO_CAP_TRACK_COUNT = 0x04;
static constexpr uint8_t AUDIO_CAP_CURRENT_TRACK = 0x08;
static constexpr uint8_t AUDIO_CAP_QUERY_SAFE_PLAYING = 0x10;
static constexpr uint8_t CAPS_DY_SV5W = 0x0F;
static constexpr uint8_t CAPS_CHIRP   = 0x1F;

void setUp() {
}
void tearDown() {
}

// Worst-case response: driver = "DY-SV5W", play_state = "unknown",
// device = "unknown", max uint16 values.
// {"driver":"DY-SV5W","link_ok":false,"active":false,
//  "play_state":"unknown","device":"unknown",
//  "total_tracks":65535,"current_track":65535}
// ~137 bytes + null = 138 — well within 192.

void test_buffer_fits_with_capabilities() {
    char buf[192];
    formatAudioStatusJson(buf, sizeof(buf), "DY-SV5W", 0xFF, true, true, 0xFF, 0xFF, 65535, 65535);
    size_t len = strlen(buf);
    TEST_ASSERT_LESS_THAN(192u, len);
    TEST_ASSERT_GREATER_THAN(0u, len);
}

void test_typical_case_link_ok_sd_playing() {
    char buf[192];
    // link_ok=true active=true play_state=1(playing) device=1(SD/TF) tracks=50 current=7
    formatAudioStatusJson(buf, sizeof(buf), "DY-SV5W", CAPS_DY_SV5W, true, true, 0x01, 0x01, 50, 7);
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"link_ok\":true"));
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"active\":true"));
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"play_state\":\"playing\""));
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"device\":\"SD/TF\""));
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"total_tracks\":50"));
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"current_track\":7"));
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"driver\":\"DY-SV5W\""));
}

void test_link_not_ok_shows_unknown_device() {
    char buf[192];
    // link_ok=false, device=0xFF (no device / unknown until query responds)
    formatAudioStatusJson(buf, sizeof(buf), "DY-SV5W", CAPS_DY_SV5W, false, false, 0xFF, 0xFF, 0, 0);
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"link_ok\":false"));
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"play_state\":\"unknown\""));
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"device\":\"none\""));
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"total_tracks\":0"));
}

void test_play_state_stop() {
    char buf[192];
    formatAudioStatusJson(buf, sizeof(buf), "DY-SV5W", CAPS_DY_SV5W, true, false, 0x00, 0x01, 10, 3);
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"play_state\":\"stop\""));
}

void test_play_state_paused() {
    char buf[192];
    formatAudioStatusJson(buf, sizeof(buf), "DY-SV5W", CAPS_DY_SV5W, true, false, 0x02, 0x01, 10, 3);
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"play_state\":\"paused\""));
}

void test_device_usb() {
    char buf[192];
    formatAudioStatusJson(buf, sizeof(buf), "DY-SV5W", CAPS_DY_SV5W, true, false, 0x00, 0x00, 5, 1);
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"device\":\"USB\""));
}

void test_device_flash() {
    char buf[192];
    formatAudioStatusJson(buf, sizeof(buf), "DY-SV5W", CAPS_DY_SV5W, true, false, 0x00, 0x02, 5, 1);
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"device\":\"FLASH\""));
}

void test_capabilities_field_present() {
    char buf[192];
    TEST_ASSERT_EQUAL_UINT8(CAPS_DY_SV5W,
        (uint8_t)(AUDIO_CAP_STATUS_QUERY | AUDIO_CAP_DEVICE_TYPE | AUDIO_CAP_TRACK_COUNT | AUDIO_CAP_CURRENT_TRACK));
    formatAudioStatusJson(buf, sizeof(buf), "DY-SV5W", CAPS_DY_SV5W, true, true, 0x01, 0x01, 50, 7);
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"capabilities\":15"));
}

void test_chirp_capabilities_field() {
    char buf[192];
    // 0x1F = all five bits: STATUS_QUERY | DEVICE_TYPE | TRACK_COUNT | CURRENT_TRACK | QUERY_SAFE_PLAYING
    TEST_ASSERT_EQUAL_UINT8(CAPS_CHIRP,
        (uint8_t)(AUDIO_CAP_STATUS_QUERY | AUDIO_CAP_DEVICE_TYPE | AUDIO_CAP_TRACK_COUNT |
                  AUDIO_CAP_CURRENT_TRACK | AUDIO_CAP_QUERY_SAFE_PLAYING));
    formatAudioStatusJson(buf, sizeof(buf), "CHIRP", CAPS_CHIRP, true, false, 0x00, 0x03, 61, 5);
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"capabilities\":31"));
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"driver\":\"CHIRP\""));
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"device\":\"Flash+SD\""));
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"total_tracks\":61"));
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"current_track\":5"));
}

void test_device_flash_sd() {
    char buf[192];
    formatAudioStatusJson(buf, sizeof(buf), "CHIRP", CAPS_CHIRP, true, false, 0x00, 0x03, 61, 0);
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"device\":\"Flash+SD\""));
}

void test_capabilities_zero_driver() {
    char buf[192];
    formatAudioStatusJson(buf, sizeof(buf), "UNKNOWN", 0, false, false, 0xFF, 0xFF, 0, 0);
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"capabilities\":0"));
}


int main(int argc, char** argv) {
    UNITY_BEGIN();

    RUN_TEST(test_buffer_fits_with_capabilities);
    RUN_TEST(test_typical_case_link_ok_sd_playing);
    RUN_TEST(test_link_not_ok_shows_unknown_device);
    RUN_TEST(test_play_state_stop);
    RUN_TEST(test_play_state_paused);
    RUN_TEST(test_device_usb);
    RUN_TEST(test_device_flash);
    RUN_TEST(test_capabilities_field_present);
    RUN_TEST(test_chirp_capabilities_field);
    RUN_TEST(test_capabilities_zero_driver);
    RUN_TEST(test_device_flash_sd);

    return UNITY_END();
}
