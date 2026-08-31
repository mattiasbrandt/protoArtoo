// =============================================================================
// test/test_native/test_api_audio_routes/test_api_audio_routes.cpp
//
// Native unit tests for the twelve audio handlers through the WebRequest seam's
// host-test backend (ADR 0021). Each handler is driven exactly as a device
// backend would drive it and the assertions are on the captured response -- no
// vendor web-server type appears here, which is the property the port exists to
// establish.
//
// The two chunked payloads matter most: the host backend drives the body
// producer in 64-byte chunks, so a producer that mishandles an offset split
// fails here rather than only on hardware.
//
// What is deliberately not covered:
//   - CHIRP binding *contents* in GET /api/audio/tracks. The Preferences stub
//     stores per instance, so a POST's writes are invisible to the GET's own
//     handle. The structural assertion (both binding objects present exactly
//     when the backend is catalog-capable) is what the host can prove; the
//     values are device behaviour.
//   - Audibility. No sound module is attached at the bench, and the apply cores
//     and audio config map this port reuses are covered by their own suites.
// =============================================================================
#include <unity.h>

#include <cstdio>
#include <cstring>

#include "api_audio.h"
#include "audio_driver.h"
#include "audio_test_hooks.h"  // g_test_audio_queue_ok/play_track/volume - shared with
                                // test_console_module.cpp's #221 remainder sound.action.*
                                // executors, the same stub this file already drove
#include "config_cache.h"
#include "robot_state.h"
#include "web_request_test_backend.h"

// Recorded side effects and controls from src/native_test_stubs.cpp.
extern uint8_t g_test_audio_capabilities;
extern const char* g_test_audio_driver_name;
extern unsigned g_test_audio_stop_calls;
extern unsigned g_test_audio_dollar_calls;
extern unsigned g_test_audio_query_calls;
extern unsigned g_test_audio_refresh_catalog_calls;
extern unsigned g_test_audio_refresh_bindings_calls;
extern unsigned g_test_audio_play_banked_calls;
extern uint16_t g_test_audio_last_banked_index;
extern uint8_t g_test_audio_last_banked_bank;
extern char g_test_audio_last_banked_page;
extern bool g_test_audio_catalog_ready;
extern AudioCatalogBank g_test_audio_catalog_banks[8];
extern uint8_t g_test_audio_catalog_bank_count;
extern AudioCatalogEntry g_test_audio_catalog_entries[16];
extern uint16_t g_test_audio_catalog_entry_count;
extern unsigned g_test_applied_mood;
extern unsigned g_test_status_broadcast_count;

namespace {

WebRequestTestBackend backend;

void resetBackend() {
    backend = WebRequestTestBackend{};
}

void callGet(WebRequestHandler handler, const WebRequestTestParam* params, size_t count) {
    resetBackend();
    backend.params = params;
    backend.paramCount = count;
    WebRequest req(&backend);
    handler(req);
}

void callPost(WebRequestHandler handler, const WebRequestTestParam* params, size_t count) {
    callGet(handler, params, count);
}

bool bodyContains(const char* needle) {
    return strstr(backend.sentBody, needle) != nullptr;
}

// The chunked producers are only proven correct if the assembled body is the
// whole body -- sentBody stops at the buffer, sentBodyLength does not.
bool bodyIsComplete() {
    return backend.sentBodyLength == strlen(backend.sentBody);
}

void setCatalogSupported(bool supported) {
    g_test_audio_capabilities = supported ? AudioDriver::AUDIO_CAP_CATALOG : 0;
}

void setSleeping(bool sleeping) {
    robotState.sleepMode = sleeping;
}

}  // namespace

void setUp() {
    resetBackend();
    robotState = RobotState{};
    ConfigSnapshot snap = {};
    configCacheApply(snap);

    g_test_audio_capabilities = 0;
    g_test_audio_driver_name = "TEST";
    g_test_audio_queue_ok = true;
    g_test_audio_play_track_calls = 0;
    g_test_audio_last_track = 0;
    g_test_audio_stop_calls = 0;
    g_test_audio_volume_calls = 0;
    g_test_audio_last_volume = 0;
    g_test_audio_dollar_calls = 0;
    g_test_audio_query_calls = 0;
    g_test_audio_refresh_catalog_calls = 0;
    g_test_audio_refresh_bindings_calls = 0;
    g_test_audio_play_banked_calls = 0;
    g_test_audio_last_banked_index = 0;
    g_test_audio_last_banked_bank = 0;
    g_test_audio_last_banked_page = '\0';
    g_test_audio_catalog_ready = false;
    g_test_audio_catalog_bank_count = 0;
    g_test_audio_catalog_entry_count = 0;
    g_test_applied_mood = 0;
    g_test_status_broadcast_count = 0;
}

void tearDown() {
}

// -----------------------------------------------------------------------------
// GET /api/audio
// -----------------------------------------------------------------------------

void test_audio_get_reports_module_status() {
    g_test_audio_driver_name = "CHIRP";
    g_test_audio_capabilities = AudioDriver::AUDIO_CAP_STATUS_QUERY;
    robotState.audio_module_link_ok = true;
    robotState.audio_module_play_state = 1;
    robotState.audio_module_current_track = 42;
    robotState.audio_module_rx_status = AUDIO_RX_AVAILABLE;

    callGet(handleAudioGet, nullptr, 0);

    TEST_ASSERT_EQUAL_INT(200, backend.sentCode);
    TEST_ASSERT_EQUAL_STRING("application/json", backend.sentContentType);
    TEST_ASSERT_TRUE(bodyContains("\"driver\":\"CHIRP\""));
    TEST_ASSERT_TRUE(bodyContains("\"rx_status\":\"available\""));
}

// -----------------------------------------------------------------------------
// POST /api/audio
// -----------------------------------------------------------------------------

void test_audio_post_without_action_is_rejected() {
    callPost(handleAudioPost, nullptr, 0);
    TEST_ASSERT_EQUAL_INT(400, backend.sentCode);
    TEST_ASSERT_TRUE(bodyContains("missing action parameter"));
}

void test_audio_post_unknown_action_is_rejected() {
    const WebRequestTestParam params[] = {{"action", "warble"}};
    callPost(handleAudioPost, params, 1);
    TEST_ASSERT_EQUAL_INT(400, backend.sentCode);
    TEST_ASSERT_TRUE(bodyContains("unknown action"));
    TEST_ASSERT_EQUAL_UINT(0u, g_test_audio_play_track_calls);
}

void test_audio_post_play_queues_the_track() {
    const WebRequestTestParam params[] = {{"action", "play"}, {"track", "7"}};
    callPost(handleAudioPost, params, 2);
    TEST_ASSERT_EQUAL_INT(200, backend.sentCode);
    TEST_ASSERT_EQUAL_STRING("{\"ok\":true}", backend.sentBody);
    TEST_ASSERT_EQUAL_UINT(1u, g_test_audio_play_track_calls);
    TEST_ASSERT_EQUAL_UINT16(7, g_test_audio_last_track);
}

void test_audio_post_play_while_sleeping_is_locked() {
    setSleeping(true);
    const WebRequestTestParam params[] = {{"action", "play"}, {"track", "7"}};
    callPost(handleAudioPost, params, 2);
    TEST_ASSERT_EQUAL_INT(423, backend.sentCode);
    TEST_ASSERT_TRUE(bodyContains("sleeping"));
    TEST_ASSERT_EQUAL_UINT(0u, g_test_audio_play_track_calls);
}

void test_audio_post_play_without_track_is_rejected() {
    const WebRequestTestParam params[] = {{"action", "play"}};
    callPost(handleAudioPost, params, 1);
    TEST_ASSERT_EQUAL_INT(400, backend.sentCode);
    TEST_ASSERT_TRUE(bodyContains("play requires track parameter"));
}

// Unparseable input lands on the range answer, the same one the vendor's
// toInt() produced by reading garbage as 0.
void test_audio_post_play_rejects_unparseable_and_out_of_range_tracks() {
    const WebRequestTestParam garbage[] = {{"action", "play"}, {"track", "abc"}};
    callPost(handleAudioPost, garbage, 2);
    TEST_ASSERT_EQUAL_INT(400, backend.sentCode);
    TEST_ASSERT_TRUE(bodyContains("track must be"));

    const WebRequestTestParam tooBig[] = {{"action", "play"}, {"track", "70000"}};
    callPost(handleAudioPost, tooBig, 2);
    TEST_ASSERT_EQUAL_INT(400, backend.sentCode);
    TEST_ASSERT_EQUAL_UINT(0u, g_test_audio_play_track_calls);
}

void test_audio_post_stop_queues_a_stop() {
    const WebRequestTestParam params[] = {{"action", "stop"}};
    callPost(handleAudioPost, params, 1);
    TEST_ASSERT_EQUAL_INT(200, backend.sentCode);
    TEST_ASSERT_EQUAL_UINT(1u, g_test_audio_stop_calls);
}

void test_audio_post_stop_reports_a_full_queue() {
    g_test_audio_queue_ok = false;
    const WebRequestTestParam params[] = {{"action", "stop"}};
    callPost(handleAudioPost, params, 1);
    TEST_ASSERT_EQUAL_INT(503, backend.sentCode);
    TEST_ASSERT_TRUE(bodyContains("audio command queue full"));
}

void test_audio_post_volume_applies_and_persists() {
    const WebRequestTestParam params[] = {{"action", "volume"}, {"level", "17"}};
    callPost(handleAudioPost, params, 2);
    TEST_ASSERT_EQUAL_INT(200, backend.sentCode);
    TEST_ASSERT_EQUAL_UINT(1u, g_test_audio_volume_calls);
    TEST_ASSERT_EQUAL_UINT8(17, g_test_audio_last_volume);

    ConfigSnapshot snap = {};
    configCacheRead(&snap);
    TEST_ASSERT_EQUAL_UINT8(17, snap.audio.audioVolume);
}

void test_audio_post_volume_rejects_out_of_range_levels() {
    const WebRequestTestParam tooLoud[] = {{"action", "volume"}, {"level", "31"}};
    callPost(handleAudioPost, tooLoud, 2);
    TEST_ASSERT_EQUAL_INT(400, backend.sentCode);
    TEST_ASSERT_TRUE(bodyContains("level must be"));

    const WebRequestTestParam negative[] = {{"action", "volume"}, {"level", "-1"}};
    callPost(handleAudioPost, negative, 2);
    TEST_ASSERT_EQUAL_INT(400, backend.sentCode);
    TEST_ASSERT_EQUAL_UINT(0u, g_test_audio_volume_calls);
}

void test_audio_post_dollar_requires_a_dollar_prefixed_command() {
    const WebRequestTestParam bare[] = {{"action", "dollar"}, {"cmd", "R"}};
    callPost(handleAudioPost, bare, 2);
    TEST_ASSERT_EQUAL_INT(400, backend.sentCode);
    TEST_ASSERT_TRUE(bodyContains("starting with"));

    const WebRequestTestParam tooLong[] = {{"action", "dollar"}, {"cmd", "$0123456789"}};
    callPost(handleAudioPost, tooLong, 2);
    TEST_ASSERT_EQUAL_INT(400, backend.sentCode);
    TEST_ASSERT_TRUE(bodyContains("cmd too long"));

    const WebRequestTestParam ok[] = {{"action", "dollar"}, {"cmd", "$R"}};
    callPost(handleAudioPost, ok, 2);
    TEST_ASSERT_EQUAL_INT(200, backend.sentCode);
}

// -----------------------------------------------------------------------------
// GET /api/audio/tracks
// -----------------------------------------------------------------------------

void test_tracks_get_serializes_every_field_from_the_config_snapshot() {
    ConfigSnapshot snap = {};
    configCacheRead(&snap);
    snap.audio.snd_scream = 11;
    snap.audio.snd_cat_snarky_lo = 220;
    snap.audio.snd_cat_snarky_hi = 229;
    snap.audio.snd_rand_min = 3;
    snap.audio.snd_rand_max = 9;
    snap.audio.audioVolume = 21;
    snap.audio.snd_int_awake = 45;
    configCacheApply(snap);

    callGet(handleAudioTracksGet, nullptr, 0);

    TEST_ASSERT_EQUAL_INT(200, backend.sentCode);
    TEST_ASSERT_TRUE(backend.sentChunked);
    TEST_ASSERT_TRUE(bodyIsComplete());
    TEST_ASSERT_TRUE(bodyContains("\"scream\":11"));
    // The wire name is the short form even though the config member is snarky.
    TEST_ASSERT_TRUE(bodyContains("\"snd_cat_snrk_lo\":220"));
    TEST_ASSERT_TRUE(bodyContains("\"snd_cat_snrk_hi\":229"));
    TEST_ASSERT_TRUE(bodyContains("\"rand_min\":3"));
    TEST_ASSERT_TRUE(bodyContains("\"rand_max\":9"));
    TEST_ASSERT_TRUE(bodyContains("\"volume\":21"));
    TEST_ASSERT_TRUE(bodyContains("\"snd_int_awake\":45"));
    // Object opens and closes exactly once when there is no catalog.
    TEST_ASSERT_EQUAL_CHAR('{', backend.sentBody[0]);
    TEST_ASSERT_EQUAL_CHAR('}', backend.sentBody[backend.sentBodyLength - 1]);
}

// Payload parity, pinned rather than sampled: this is the pre-port snprintf
// format string filled with the zeroed config setUp() installs. Any change to
// a field name, a field's position, or the separator layout fails here.
void test_tracks_get_matches_the_pre_port_payload_byte_for_byte() {
    setCatalogSupported(false);
    callGet(handleAudioTracksGet, nullptr, 0);

    TEST_ASSERT_EQUAL_INT(200, backend.sentCode);
    TEST_ASSERT_TRUE(bodyIsComplete());
    TEST_ASSERT_EQUAL_STRING(
        "{\"scream\":0,\"faint\":0,\"leia\":0,"
        "\"cantina_s\":0,\"sw_theme\":0,\"imp_march\":0,"
        "\"cantina_l\":0,\"startup\":0,"
        "\"doodoo\":0,\"failure\":0,\"disco\":0,\"mahna\":0,"
        "\"inlove\":0,\"macho\":0,\"gangnam\":0,\"uptown\":0,"
        "\"celebr\":0,\"stayin\":0,\"harlem\":0,\"pbjtime\":0,"
        "\"sys_boot\":0,\"sys_mode_n\":0,\"sys_mode_s\":0,"
        "\"sys_mode_t\":0,\"sys_drv_on\":0,\"sys_dome_on\":0,"
        "\"snd_cat_gen_lo\":0,\"snd_cat_gen_hi\":0,"
        "\"snd_cat_chat_lo\":0,\"snd_cat_chat_hi\":0,"
        "\"snd_cat_hap_lo\":0,\"snd_cat_hap_hi\":0,"
        "\"snd_cat_proc_lo\":0,\"snd_cat_proc_hi\":0,"
        "\"snd_cat_sad_lo\":0,\"snd_cat_sad_hi\":0,"
        "\"snd_cat_sent_lo\":0,\"snd_cat_sent_hi\":0,"
        "\"snd_cat_hum_lo\":0,\"snd_cat_hum_hi\":0,"
        "\"snd_cat_scrm_lo\":0,\"snd_cat_scrm_hi\":0,"
        "\"snd_cat_ooh_lo\":0,\"snd_cat_ooh_hi\":0,"
        "\"snd_cat_alrm_lo\":0,\"snd_cat_alrm_hi\":0,"
        "\"snd_cat_snrk_lo\":0,\"snd_cat_snrk_hi\":0,"
        "\"snd_cat_whis_lo\":0,\"snd_cat_whis_hi\":0,"
        "\"rand_min\":0,\"rand_max\":0,\"volume\":0,"
        "\"snd_int_quiet\":0,\"snd_int_mid\":0,"
        "\"snd_int_full\":0,\"snd_int_awake\":0}",
        backend.sentBody);
}

void test_tracks_get_omits_chirp_bindings_without_a_catalog_backend() {
    setCatalogSupported(false);
    callGet(handleAudioTracksGet, nullptr, 0);
    TEST_ASSERT_EQUAL_INT(200, backend.sentCode);
    TEST_ASSERT_FALSE(bodyContains("chirp_bindings"));
    TEST_ASSERT_FALSE(bodyContains("chirp_category_bindings"));
}

void test_tracks_get_carries_both_binding_objects_on_a_catalog_backend() {
    setCatalogSupported(true);
    callGet(handleAudioTracksGet, nullptr, 0);
    TEST_ASSERT_EQUAL_INT(200, backend.sentCode);
    TEST_ASSERT_TRUE(bodyIsComplete());
    TEST_ASSERT_TRUE(bodyContains("\"chirp_bindings\":{"));
    TEST_ASSERT_TRUE(bodyContains("\"chirp_category_bindings\":{"));
    TEST_ASSERT_EQUAL_CHAR('}', backend.sentBody[backend.sentBodyLength - 1]);
}

// -----------------------------------------------------------------------------
// POST /api/audio/tracks  (apply core reused unchanged)
// -----------------------------------------------------------------------------

void test_tracks_post_applies_a_named_track() {
    const WebRequestTestParam params[] = {{"key", "scream"}, {"track", "12"}};
    callPost(handleAudioTracksPost, params, 2);
    TEST_ASSERT_EQUAL_INT(200, backend.sentCode);
    TEST_ASSERT_EQUAL_STRING("{\"ok\":true}", backend.sentBody);

    ConfigSnapshot snap = {};
    configCacheRead(&snap);
    TEST_ASSERT_EQUAL_UINT16(12, snap.audio.snd_scream);
}

void test_tracks_post_relays_the_apply_core_rejection() {
    const WebRequestTestParam missing[] = {{"key", "scream"}};
    callPost(handleAudioTracksPost, missing, 1);
    TEST_ASSERT_EQUAL_INT(400, backend.sentCode);
    TEST_ASSERT_TRUE(bodyContains("requires key and track parameters"));

    // Banked params against a non-catalog backend is the core's one not-found
    // case, and the shell has to answer 404 rather than 400 for it.
    const WebRequestTestParam banked[] = {
        {"key", "scream"}, {"track", "3"}, {"bank", "2"}, {"page", "B"}};
    callPost(handleAudioTracksPost, banked, 4);
    TEST_ASSERT_EQUAL_INT(404, backend.sentCode);
    TEST_ASSERT_TRUE(bodyContains("catalog unsupported by active backend"));
}

// -----------------------------------------------------------------------------
// POST /api/audio/category-range
// -----------------------------------------------------------------------------

void test_category_range_post_applies_the_pair() {
    const WebRequestTestParam params[] = {
        {"lo_key", "snd_cat_gen_lo"}, {"hi_key", "snd_cat_gen_hi"}, {"lo", "10"}, {"hi", "20"}};
    callPost(handleAudioCategoryRangePost, params, 4);
    TEST_ASSERT_EQUAL_INT(200, backend.sentCode);

    ConfigSnapshot snap = {};
    configCacheRead(&snap);
    TEST_ASSERT_EQUAL_UINT16(10, snap.audio.snd_cat_gen_lo);
    TEST_ASSERT_EQUAL_UINT16(20, snap.audio.snd_cat_gen_hi);
}

void test_category_range_post_relays_the_apply_core_rejection() {
    const WebRequestTestParam params[] = {{"lo_key", "snd_cat_gen_lo"}};
    callPost(handleAudioCategoryRangePost, params, 1);
    TEST_ASSERT_EQUAL_INT(400, backend.sentCode);
    TEST_ASSERT_TRUE(bodyContains("\"ok\":false"));
}

// -----------------------------------------------------------------------------
// Mood category mask map
// -----------------------------------------------------------------------------

void test_mood_map_get_returns_the_configured_masks() {
    ConfigSnapshot snap = {};
    configCacheRead(&snap);
    snap.audio.snd_moodcat_quiet = 5;
    snap.audio.snd_moodcat_mid = 6;
    snap.audio.snd_moodcat_full = 7;
    snap.audio.snd_moodcat_awakeplus = 8;
    configCacheApply(snap);

    callGet(handleAudioMoodMapGet, nullptr, 0);

    TEST_ASSERT_EQUAL_INT(200, backend.sentCode);
    TEST_ASSERT_TRUE(bodyContains("\"quiet\":5"));
    TEST_ASSERT_TRUE(bodyContains("\"awakeplus\":8"));
}

void test_mood_map_post_applies_all_four_masks() {
    const WebRequestTestParam params[] = {
        {"quiet", "1"}, {"mid", "2"}, {"full", "3"}, {"awakeplus", "4"}};
    callPost(handleAudioMoodMapPost, params, 4);
    TEST_ASSERT_EQUAL_INT(200, backend.sentCode);
    TEST_ASSERT_EQUAL_STRING("{\"ok\":true}", backend.sentBody);
}

void test_mood_map_post_rejects_a_partial_form() {
    const WebRequestTestParam params[] = {{"quiet", "1"}, {"mid", "2"}};
    callPost(handleAudioMoodMapPost, params, 2);
    TEST_ASSERT_EQUAL_INT(400, backend.sentCode);
    TEST_ASSERT_TRUE(bodyContains("requires quiet, mid, full, awakeplus"));
}

// -----------------------------------------------------------------------------
// POST /api/mood
// -----------------------------------------------------------------------------

void test_mood_post_applies_a_valid_preset() {
    const WebRequestTestParam params[] = {{"mood", "13"}};
    callPost(handleMoodPost, params, 1);
    TEST_ASSERT_EQUAL_INT(200, backend.sentCode);
    TEST_ASSERT_EQUAL_STRING("{\"ok\":true}", backend.sentBody);
    TEST_ASSERT_EQUAL_UINT(13u, g_test_applied_mood);
    TEST_ASSERT_EQUAL_UINT(1u, g_test_status_broadcast_count);
}

void test_mood_post_rejects_missing_invalid_and_unparseable_moods() {
    callPost(handleMoodPost, nullptr, 0);
    TEST_ASSERT_EQUAL_INT(400, backend.sentCode);
    TEST_ASSERT_TRUE(bodyContains("missing mood parameter"));

    const WebRequestTestParam outOfSet[] = {{"mood", "12"}};
    callPost(handleMoodPost, outOfSet, 1);
    TEST_ASSERT_EQUAL_INT(400, backend.sentCode);
    TEST_ASSERT_TRUE(bodyContains("mood must be 10, 11, 13, or 14"));

    const WebRequestTestParam garbage[] = {{"mood", "quiet"}};
    callPost(handleMoodPost, garbage, 1);
    TEST_ASSERT_EQUAL_INT(400, backend.sentCode);
    TEST_ASSERT_TRUE(bodyContains("mood must be 10, 11, 13, or 14"));

    TEST_ASSERT_EQUAL_UINT(0u, g_test_applied_mood);
}

void test_mood_post_while_sleeping_is_locked() {
    setSleeping(true);
    const WebRequestTestParam params[] = {{"mood", "10"}};
    callPost(handleMoodPost, params, 1);
    TEST_ASSERT_EQUAL_INT(423, backend.sentCode);
    TEST_ASSERT_EQUAL_UINT(0u, g_test_applied_mood);
}

// -----------------------------------------------------------------------------
// POST /api/audio/query
// -----------------------------------------------------------------------------

void test_query_post_enqueues_a_status_poll() {
    callPost(handleAudioQueryPost, nullptr, 0);
    TEST_ASSERT_EQUAL_INT(200, backend.sentCode);
    TEST_ASSERT_EQUAL_UINT(1u, g_test_audio_query_calls);
}

void test_query_post_reports_a_full_queue() {
    g_test_audio_queue_ok = false;
    callPost(handleAudioQueryPost, nullptr, 0);
    TEST_ASSERT_EQUAL_INT(503, backend.sentCode);
}

// -----------------------------------------------------------------------------
// GET /api/audio/catalog
// -----------------------------------------------------------------------------

void test_catalog_get_is_not_found_without_a_catalog_backend() {
    setCatalogSupported(false);
    callGet(handleAudioCatalogGet, nullptr, 0);
    TEST_ASSERT_EQUAL_INT(404, backend.sentCode);
    TEST_ASSERT_TRUE(bodyContains("catalog unsupported by active backend"));
}

void test_catalog_get_reports_an_unready_catalog_as_empty_arrays() {
    setCatalogSupported(true);
    g_test_audio_catalog_ready = false;
    callGet(handleAudioCatalogGet, nullptr, 0);
    TEST_ASSERT_EQUAL_INT(200, backend.sentCode);
    TEST_ASSERT_EQUAL_STRING("{\"ready\":false,\"banks\":[],\"entries\":[]}", backend.sentBody);
}

void test_catalog_get_serializes_banks_and_entries() {
    setCatalogSupported(true);
    g_test_audio_catalog_ready = true;
    g_test_audio_catalog_banks[0] = AudioCatalogBank{};
    g_test_audio_catalog_banks[0].bank = 1;
    g_test_audio_catalog_banks[0].page = 'A';
    snprintf(g_test_audio_catalog_banks[0].dirName,
             sizeof(g_test_audio_catalog_banks[0].dirName), "%s", "01 Chatter");
    g_test_audio_catalog_banks[0].count = 2;
    g_test_audio_catalog_bank_count = 1;

    g_test_audio_catalog_entries[0] = AudioCatalogEntry{};
    g_test_audio_catalog_entries[0].bank = 1;
    g_test_audio_catalog_entries[0].page = 'A';
    g_test_audio_catalog_entries[0].index = 1;
    snprintf(g_test_audio_catalog_entries[0].name,
             sizeof(g_test_audio_catalog_entries[0].name), "%s", "beep");
    g_test_audio_catalog_entry_count = 1;

    callGet(handleAudioCatalogGet, nullptr, 0);

    TEST_ASSERT_EQUAL_INT(200, backend.sentCode);
    TEST_ASSERT_TRUE(backend.sentChunked);
    TEST_ASSERT_TRUE(bodyIsComplete());
    TEST_ASSERT_EQUAL_STRING(
        "{\"ready\":true,"
        "\"banks\":[{\"bank\":1,\"page\":\"A\",\"dir\":\"01 Chatter\",\"count\":2}],"
        "\"entries\":[{\"bank\":1,\"page\":\"A\",\"index\":1,\"name\":\"beep\"}]}",
        backend.sentBody);
}

// A body longer than one host chunk is where an offset-split bug would show.
void test_catalog_get_survives_a_body_spanning_many_chunks() {
    setCatalogSupported(true);
    g_test_audio_catalog_ready = true;
    g_test_audio_catalog_bank_count = 0;
    for (uint16_t i = 0; i < 16; ++i) {
        g_test_audio_catalog_entries[i] = AudioCatalogEntry{};
        g_test_audio_catalog_entries[i].bank = 1;
        g_test_audio_catalog_entries[i].page = 'A';
        g_test_audio_catalog_entries[i].index = (uint16_t)(i + 1);
        snprintf(g_test_audio_catalog_entries[i].name,
                 sizeof(g_test_audio_catalog_entries[i].name), "entry-%u-padding-padding",
                 (unsigned)i);
    }
    g_test_audio_catalog_entry_count = 16;

    callGet(handleAudioCatalogGet, nullptr, 0);

    TEST_ASSERT_EQUAL_INT(200, backend.sentCode);
    TEST_ASSERT_TRUE(backend.sentBodyLength > 64);
    TEST_ASSERT_TRUE(bodyIsComplete());
    TEST_ASSERT_TRUE(bodyContains("\"name\":\"entry-0-padding-padding\""));
    TEST_ASSERT_TRUE(bodyContains("\"name\":\"entry-15-padding-padding\""));
    TEST_ASSERT_EQUAL_CHAR('}', backend.sentBody[backend.sentBodyLength - 1]);
}

void test_catalog_get_filters_by_bank() {
    setCatalogSupported(true);
    g_test_audio_catalog_ready = true;
    g_test_audio_catalog_bank_count = 0;
    for (uint16_t i = 0; i < 2; ++i) {
        g_test_audio_catalog_entries[i] = AudioCatalogEntry{};
        g_test_audio_catalog_entries[i].bank = (uint8_t)(i + 1);
        g_test_audio_catalog_entries[i].page = 'A';
        g_test_audio_catalog_entries[i].index = 1;
        snprintf(g_test_audio_catalog_entries[i].name,
                 sizeof(g_test_audio_catalog_entries[i].name), "bank%u", (unsigned)(i + 1));
    }
    g_test_audio_catalog_entry_count = 2;

    const WebRequestTestParam params[] = {{"bank", "2"}};
    callGet(handleAudioCatalogGet, params, 1);

    TEST_ASSERT_EQUAL_INT(200, backend.sentCode);
    TEST_ASSERT_TRUE(bodyContains("\"name\":\"bank2\""));
    TEST_ASSERT_FALSE(bodyContains("\"name\":\"bank1\""));
}

void test_catalog_get_rejects_a_bank_outside_one_to_six() {
    setCatalogSupported(true);
    const WebRequestTestParam params[] = {{"bank", "9"}};
    callGet(handleAudioCatalogGet, params, 1);
    TEST_ASSERT_EQUAL_INT(400, backend.sentCode);
    TEST_ASSERT_TRUE(bodyContains("bank must be 1-6"));
}

// -----------------------------------------------------------------------------
// POST /api/audio/catalog/refresh
// -----------------------------------------------------------------------------

void test_catalog_refresh_post_enqueues_on_a_catalog_backend() {
    setCatalogSupported(true);
    callPost(handleAudioCatalogRefreshPost, nullptr, 0);
    TEST_ASSERT_EQUAL_INT(200, backend.sentCode);
    TEST_ASSERT_EQUAL_UINT(1u, g_test_audio_refresh_catalog_calls);
}

void test_catalog_refresh_post_is_not_found_without_a_catalog_backend() {
    setCatalogSupported(false);
    callPost(handleAudioCatalogRefreshPost, nullptr, 0);
    TEST_ASSERT_EQUAL_INT(404, backend.sentCode);
    TEST_ASSERT_EQUAL_UINT(0u, g_test_audio_refresh_catalog_calls);
}

// -----------------------------------------------------------------------------
// POST /api/audio/play-banked
// -----------------------------------------------------------------------------

void test_play_banked_post_queues_bank_page_index() {
    setCatalogSupported(true);
    const WebRequestTestParam params[] = {{"index", "5"}, {"bank", "3"}, {"page", "c"}};
    callPost(handleAudioPlayBankedPost, params, 3);
    TEST_ASSERT_EQUAL_INT(200, backend.sentCode);
    TEST_ASSERT_EQUAL_UINT(1u, g_test_audio_play_banked_calls);
    TEST_ASSERT_EQUAL_UINT16(5, g_test_audio_last_banked_index);
    TEST_ASSERT_EQUAL_UINT8(3, g_test_audio_last_banked_bank);
    TEST_ASSERT_EQUAL_CHAR('C', g_test_audio_last_banked_page);
}

void test_play_banked_post_validates_each_parameter() {
    setCatalogSupported(true);

    const WebRequestTestParam missing[] = {{"index", "5"}};
    callPost(handleAudioPlayBankedPost, missing, 1);
    TEST_ASSERT_EQUAL_INT(400, backend.sentCode);
    TEST_ASSERT_TRUE(bodyContains("requires index, bank, page"));

    const WebRequestTestParam badIndex[] = {{"index", "0"}, {"bank", "1"}, {"page", "A"}};
    callPost(handleAudioPlayBankedPost, badIndex, 3);
    TEST_ASSERT_EQUAL_INT(400, backend.sentCode);
    TEST_ASSERT_TRUE(bodyContains("index must be 1-65535"));

    const WebRequestTestParam badBank[] = {{"index", "1"}, {"bank", "7"}, {"page", "A"}};
    callPost(handleAudioPlayBankedPost, badBank, 3);
    TEST_ASSERT_EQUAL_INT(400, backend.sentCode);
    TEST_ASSERT_TRUE(bodyContains("bank must be 1-6"));

    const WebRequestTestParam badPage[] = {{"index", "1"}, {"bank", "1"}, {"page", "AB"}};
    callPost(handleAudioPlayBankedPost, badPage, 3);
    TEST_ASSERT_EQUAL_INT(400, backend.sentCode);
    TEST_ASSERT_TRUE(bodyContains("page must be a single letter A-Z"));

    TEST_ASSERT_EQUAL_UINT(0u, g_test_audio_play_banked_calls);
}

void test_play_banked_post_is_gated_by_sleep_then_catalog_support() {
    setSleeping(true);
    setCatalogSupported(false);
    const WebRequestTestParam params[] = {{"index", "1"}, {"bank", "1"}, {"page", "A"}};
    callPost(handleAudioPlayBankedPost, params, 3);
    TEST_ASSERT_EQUAL_INT(423, backend.sentCode);

    setSleeping(false);
    callPost(handleAudioPlayBankedPost, params, 3);
    TEST_ASSERT_EQUAL_INT(404, backend.sentCode);
}

int main() {
    UNITY_BEGIN();

    RUN_TEST(test_audio_get_reports_module_status);

    RUN_TEST(test_audio_post_without_action_is_rejected);
    RUN_TEST(test_audio_post_unknown_action_is_rejected);
    RUN_TEST(test_audio_post_play_queues_the_track);
    RUN_TEST(test_audio_post_play_while_sleeping_is_locked);
    RUN_TEST(test_audio_post_play_without_track_is_rejected);
    RUN_TEST(test_audio_post_play_rejects_unparseable_and_out_of_range_tracks);
    RUN_TEST(test_audio_post_stop_queues_a_stop);
    RUN_TEST(test_audio_post_stop_reports_a_full_queue);
    RUN_TEST(test_audio_post_volume_applies_and_persists);
    RUN_TEST(test_audio_post_volume_rejects_out_of_range_levels);
    RUN_TEST(test_audio_post_dollar_requires_a_dollar_prefixed_command);

    RUN_TEST(test_tracks_get_serializes_every_field_from_the_config_snapshot);
    RUN_TEST(test_tracks_get_matches_the_pre_port_payload_byte_for_byte);
    RUN_TEST(test_tracks_get_omits_chirp_bindings_without_a_catalog_backend);
    RUN_TEST(test_tracks_get_carries_both_binding_objects_on_a_catalog_backend);
    RUN_TEST(test_tracks_post_applies_a_named_track);
    RUN_TEST(test_tracks_post_relays_the_apply_core_rejection);

    RUN_TEST(test_category_range_post_applies_the_pair);
    RUN_TEST(test_category_range_post_relays_the_apply_core_rejection);

    RUN_TEST(test_mood_map_get_returns_the_configured_masks);
    RUN_TEST(test_mood_map_post_applies_all_four_masks);
    RUN_TEST(test_mood_map_post_rejects_a_partial_form);

    RUN_TEST(test_mood_post_applies_a_valid_preset);
    RUN_TEST(test_mood_post_rejects_missing_invalid_and_unparseable_moods);
    RUN_TEST(test_mood_post_while_sleeping_is_locked);

    RUN_TEST(test_query_post_enqueues_a_status_poll);
    RUN_TEST(test_query_post_reports_a_full_queue);

    RUN_TEST(test_catalog_get_is_not_found_without_a_catalog_backend);
    RUN_TEST(test_catalog_get_reports_an_unready_catalog_as_empty_arrays);
    RUN_TEST(test_catalog_get_serializes_banks_and_entries);
    RUN_TEST(test_catalog_get_survives_a_body_spanning_many_chunks);
    RUN_TEST(test_catalog_get_filters_by_bank);
    RUN_TEST(test_catalog_get_rejects_a_bank_outside_one_to_six);

    RUN_TEST(test_catalog_refresh_post_enqueues_on_a_catalog_backend);
    RUN_TEST(test_catalog_refresh_post_is_not_found_without_a_catalog_backend);

    RUN_TEST(test_play_banked_post_queues_bank_page_index);
    RUN_TEST(test_play_banked_post_validates_each_parameter);
    RUN_TEST(test_play_banked_post_is_gated_by_sleep_then_catalog_support);

    return UNITY_END();
}
