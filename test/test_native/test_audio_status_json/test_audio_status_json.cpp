// =============================================================================
// test/test_native/test_audio_status_json/test_audio_status_json.cpp
//
// Native tests for formatAudioStatusJson().
// Verifies content correctness and that the response GET /api/audio actually
// sends is a complete document within AUDIO_STATUS_JSON_BUF_SIZE -- the 256
// bytes it used to carry were not enough for a blocked-RX CHIRP answer.
// =============================================================================

#include <ArduinoJson.h>
#include <string.h>
#include <unity.h>

#include "api_audio.h"
#include "component_registry.h"

static constexpr uint8_t AUDIO_CAP_STATUS_QUERY = 0x01;
static constexpr uint8_t AUDIO_CAP_DEVICE_TYPE = 0x02;
static constexpr uint8_t AUDIO_CAP_TRACK_COUNT = 0x04;
static constexpr uint8_t AUDIO_CAP_CURRENT_TRACK = 0x08;
static constexpr uint8_t AUDIO_CAP_QUERY_SAFE_PLAYING = 0x10;
static constexpr uint8_t AUDIO_CAP_CATALOG = 0x20;
static constexpr uint8_t CAPS_DY_SV5W = 0x0F;
// Read from the product's Component Registry row rather than restated, the same
// way the driver reads it: a hand-copied 0x1F here is what let the test agree
// with itself while disagreeing with the registry's 0x3F.
static constexpr uint8_t CAPS_CHIRP = componentPartCapabilities("chirp");

// The longest values every field of this response can carry today. The blocked
// RX pair is the real one AudioTask sends (src/tasks/audio_task.cpp
// audioRxStatusToken/audioRxStatusDetail).
static const char* kFullDriverName = "CHIRP Audio Trigger";
static const char* kBlockedRxToken = "blocked_by_dome_uart";
static const char* kBlockedRxDetail = "Status unavailable: DomeLink is using UART";

static int formatAudioStatusJsonDefault(char* buf, size_t bufSize, const char* driverName,
                                        uint8_t capabilities, bool linkOk, bool active,
                                        uint8_t playState, uint8_t device,
                                        uint16_t totalTracks, uint16_t currentTrack) {
    return formatAudioStatusJson(buf, bufSize, driverName, true, capabilities, linkOk, active, playState,
                                 device, totalTracks, currentTrack, 0, "available",
                                 "Sound module RX is available");
}

#define formatAudioStatusJson(...) formatAudioStatusJsonDefault(__VA_ARGS__)

void setUp() {
}
void tearDown() {
}

// The cases below pass a driver name explicitly, so they are sized by the
// values they carry rather than by the endpoint's worst case; the three tests
// at the end of the file own that.

void test_buffer_fits_with_capabilities() {
    char buf[AUDIO_STATUS_JSON_BUF_SIZE];
    int needed =
        formatAudioStatusJson(buf, sizeof(buf), "DY-SV5W", 0xFF, true, true, 0xFF, 0xFF, 65535,
                              65535);
    TEST_ASSERT_GREATER_THAN(0, needed);
    TEST_ASSERT_LESS_THAN_UINT(AUDIO_STATUS_JSON_BUF_SIZE, (unsigned)needed);
    TEST_ASSERT_EQUAL_UINT((unsigned)needed, (unsigned)strlen(buf));
}

void test_typical_case_link_ok_sd_playing() {
    char buf[256];
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
    char buf[256];
    // link_ok=false, device=0xFF (no device / unknown until query responds)
    formatAudioStatusJson(buf, sizeof(buf), "DY-SV5W", CAPS_DY_SV5W, false, false, 0xFF, 0xFF, 0, 0);
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"link_ok\":false"));
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"play_state\":\"unknown\""));
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"device\":\"none\""));
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"total_tracks\":0"));
}

void test_play_state_stop() {
    char buf[256];
    formatAudioStatusJson(buf, sizeof(buf), "DY-SV5W", CAPS_DY_SV5W, true, false, 0x00, 0x01, 10, 3);
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"play_state\":\"stop\""));
}

void test_play_state_paused() {
    char buf[256];
    formatAudioStatusJson(buf, sizeof(buf), "DY-SV5W", CAPS_DY_SV5W, true, false, 0x02, 0x01, 10, 3);
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"play_state\":\"paused\""));
}

void test_device_usb() {
    char buf[256];
    formatAudioStatusJson(buf, sizeof(buf), "DY-SV5W", CAPS_DY_SV5W, true, false, 0x00, 0x00, 5, 1);
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"device\":\"USB\""));
}

void test_device_flash() {
    char buf[256];
    formatAudioStatusJson(buf, sizeof(buf), "DY-SV5W", CAPS_DY_SV5W, true, false, 0x00, 0x02, 5, 1);
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"device\":\"FLASH\""));
}

void test_capabilities_field_present() {
    char buf[256];
    TEST_ASSERT_EQUAL_UINT8(CAPS_DY_SV5W,
        (uint8_t)(AUDIO_CAP_STATUS_QUERY | AUDIO_CAP_DEVICE_TYPE | AUDIO_CAP_TRACK_COUNT | AUDIO_CAP_CURRENT_TRACK));
    formatAudioStatusJson(buf, sizeof(buf), "DY-SV5W", CAPS_DY_SV5W, true, true, 0x01, 0x01, 50, 7);
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"capabilities\":15"));
}

void test_chirp_capabilities_field() {
    char buf[AUDIO_STATUS_JSON_BUF_SIZE];
    // 0x3F = all six bits. CATALOG is the sixth, and the word the page reads to
    // decide which status rows exist at all.
    TEST_ASSERT_EQUAL_UINT8(
        (uint8_t)(AUDIO_CAP_STATUS_QUERY | AUDIO_CAP_DEVICE_TYPE | AUDIO_CAP_TRACK_COUNT |
                  AUDIO_CAP_CURRENT_TRACK | AUDIO_CAP_QUERY_SAFE_PLAYING | AUDIO_CAP_CATALOG),
        CAPS_CHIRP);
    formatAudioStatusJson(buf, sizeof(buf), kFullDriverName, CAPS_CHIRP, true, false, 0x00, 0x03,
                          61, 5);
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"capabilities\":63"));
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"driver\":\"CHIRP Audio Trigger\""));
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"device\":\"Flash+SD\""));
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"total_tracks\":61"));
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"current_track\":5"));
}

void test_device_flash_sd() {
    char buf[AUDIO_STATUS_JSON_BUF_SIZE];
    formatAudioStatusJson(buf, sizeof(buf), kFullDriverName, CAPS_CHIRP, true, false, 0x00, 0x03, 61, 0);
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"device\":\"Flash+SD\""));
}

void test_capabilities_zero_driver() {
    char buf[256];
    formatAudioStatusJson(buf, sizeof(buf), "UNKNOWN", 0, false, false, 0xFF, 0xFF, 0, 0);
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"capabilities\":0"));
}

#undef formatAudioStatusJson

void test_missing_track_field() {
    char buf[256];
    formatAudioStatusJson(buf, sizeof(buf), "MP3Trigger", true, 0x0D, true, true, 0x00, 0xFF,
                          10, 99, 99, "available", "Sound module RX is available");
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"missing_track\":99"));
}

void test_rx_diagnostics_fields_present() {
    char buf[AUDIO_STATUS_JSON_BUF_SIZE];
    formatAudioStatusJson(buf, sizeof(buf), kFullDriverName, true, CAPS_CHIRP, false, false, 0xFF, 0x03,
                          0, 0, 0, kBlockedRxToken, kBlockedRxDetail);
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"rx_status\":\"blocked_by_dome_uart\""));
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"rx_detail\":\"Status unavailable: DomeLink is using UART\""));
}


// -----------------------------------------------------------------------------
// The response the endpoint actually sends is a whole document
// -----------------------------------------------------------------------------

void test_blocked_rx_answer_parses_and_fits() {
    char buf[AUDIO_STATUS_JSON_BUF_SIZE];
    // The case reproduced on #397: RX blocked by the dome link, so link_ok and
    // active are both false and the detail is the long sentence.
    int needed = formatAudioStatusJson(buf, sizeof(buf), kFullDriverName, true, CAPS_CHIRP, false, false,
                                       0xFF, 0x03, 24, 0, 0, kBlockedRxToken, kBlockedRxDetail);

    TEST_ASSERT_LESS_THAN_UINT(AUDIO_STATUS_JSON_BUF_SIZE, (unsigned)needed);
    TEST_ASSERT_EQUAL_CHAR_MESSAGE('}', buf[strlen(buf) - 1],
                                   "a response missing its closing brace is not JSON");

    JsonDocument doc;
    TEST_ASSERT_FALSE_MESSAGE(deserializeJson(doc, buf), "the sent body must parse");
    TEST_ASSERT_EQUAL_STRING(kFullDriverName, doc["driver"]);
    TEST_ASSERT_EQUAL_UINT8(CAPS_CHIRP, (uint8_t)doc["capabilities"]);
    TEST_ASSERT_EQUAL_STRING("Flash+SD", doc["device"]);
    TEST_ASSERT_EQUAL_STRING(kBlockedRxToken, doc["rx_status"]);
    TEST_ASSERT_EQUAL_STRING_MESSAGE(kBlockedRxDetail, doc["rx_detail"],
                                     "the detail is where the old buffer cut the response");
}

void test_the_old_256_byte_buffer_was_too_small_and_says_so() {
    char buf[256];
    int needed = formatAudioStatusJson(buf, sizeof(buf), kFullDriverName, true, CAPS_CHIRP, false, false,
                                       0xFF, 0x03, 24, 0, 0, kBlockedRxToken, kBlockedRxDetail);

    TEST_ASSERT_GREATER_OR_EQUAL_UINT_MESSAGE(
        sizeof(buf), (unsigned)needed,
        "this answer does not fit 256 bytes; the serializer must report that rather than "
        "leave the caller to send what it wrote");
    JsonDocument doc;
    TEST_ASSERT_TRUE_MESSAGE(deserializeJson(doc, buf),
                             "and what it wrote is genuinely not parsable JSON");
}

void test_worst_case_every_field_still_fits() {
    char buf[AUDIO_STATUS_JSON_BUF_SIZE];
    // Sound off answers "off", the longer of the two output words.
    int needed = formatAudioStatusJson(buf, sizeof(buf), kFullDriverName, false, 0xFF, false, false, 0xFF,
                                       0xFE, 65535, 65535, 65535, kBlockedRxToken,
                                       kBlockedRxDetail);

    TEST_ASSERT_LESS_THAN_UINT(AUDIO_STATUS_JSON_BUF_SIZE, (unsigned)needed);
    TEST_ASSERT_GREATER_THAN_UINT_MESSAGE(
        256u, (unsigned)needed,
        "the worst case is larger than the buffer this endpoint used to carry");
    JsonDocument doc;
    TEST_ASSERT_FALSE(deserializeJson(doc, buf));
    TEST_ASSERT_EQUAL_STRING("unknown", doc["play_state"]);
    TEST_ASSERT_EQUAL_STRING("unknown", doc["device"]);
    TEST_ASSERT_EQUAL_UINT16(65535, (uint16_t)doc["missing_track"]);
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
    RUN_TEST(test_missing_track_field);
    RUN_TEST(test_rx_diagnostics_fields_present);
    RUN_TEST(test_blocked_rx_answer_parses_and_fits);
    RUN_TEST(test_the_old_256_byte_buffer_was_too_small_and_says_so);
    RUN_TEST(test_worst_case_every_field_still_fits);

    return UNITY_END();
}
