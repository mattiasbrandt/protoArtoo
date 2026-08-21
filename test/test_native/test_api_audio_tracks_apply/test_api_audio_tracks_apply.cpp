// =============================================================================
// test/test_native/test_api_audio_tracks_apply/test_api_audio_tracks_apply.cpp
//
// Native unit tests for audioTracksApply() (ADR 0011 audio wave, family 3).
// =============================================================================
#include <unity.h>

#include <map>
#include <string>

#include "api_audio_tracks_apply.h"

namespace {

const char* mapGet(void* ctx, const char* name) {
    auto* m = static_cast<std::map<std::string, std::string>*>(ctx);
    auto it = m->find(name);
    if (it == m->end()) {
        return nullptr;
    }
    return it->second.c_str();
}

ConfigParamSource makeSource(std::map<std::string, std::string>* m) {
    ConfigParamSource src;
    src.ctx = m;
    src.get = mapGet;
    return src;
}

}  // namespace

void setUp(void) {
}
void tearDown(void) {
}

void test_audioTracksApply_missing_params_rejected(void) {
    std::map<std::string, std::string> m = {{"key", "scream"}};
    ConfigSnapshot snap = {};
    AudioTracksApplyResult result;
    audioTracksApply(makeSource(&m), true, &snap, &result);
    TEST_ASSERT_TRUE(result.error.hasError);
    TEST_ASSERT_EQUAL_STRING("requires key and track parameters", result.error.message);
}

void test_audioTracksApply_unknown_key_rejected(void) {
    std::map<std::string, std::string> m = {{"key", "not_a_real_key"}, {"track", "5"}};
    ConfigSnapshot snap = {};
    AudioTracksApplyResult result;
    audioTracksApply(makeSource(&m), true, &snap, &result);
    TEST_ASSERT_TRUE(result.error.hasError);
    TEST_ASSERT_EQUAL_STRING("unknown key", result.error.message);
}

void test_audioTracksApply_non_integer_track_rejected(void) {
    std::map<std::string, std::string> m = {{"key", "scream"}, {"track", "abc"}};
    ConfigSnapshot snap = {};
    AudioTracksApplyResult result;
    audioTracksApply(makeSource(&m), true, &snap, &result);
    TEST_ASSERT_TRUE(result.error.hasError);
    TEST_ASSERT_EQUAL_STRING("track must be a non-negative integer", result.error.message);
}

// --- plain named track: 0 rejected unless zero-allowed ---
void test_audioTracksApply_named_track_zero_rejected(void) {
    std::map<std::string, std::string> m = {{"key", "scream"}, {"track", "0"}};
    ConfigSnapshot snap = {};
    AudioTracksApplyResult result;
    audioTracksApply(makeSource(&m), true, &snap, &result);
    TEST_ASSERT_TRUE(result.error.hasError);
    TEST_ASSERT_EQUAL_STRING("track must be 1-999", result.error.message);
}

void test_audioTracksApply_zero_allowed_key_accepts_zero(void) {
    std::map<std::string, std::string> m = {{"key", "doodoo"}, {"track", "0"}};
    ConfigSnapshot snap = {};
    AudioTracksApplyResult result;
    audioTracksApply(makeSource(&m), true, &snap, &result);
    TEST_ASSERT_FALSE(result.error.hasError);
    TEST_ASSERT_EQUAL_UINT16(0, snap.audio.snd_doodoo);
}

void test_audioTracksApply_named_track_over_999_rejected(void) {
    std::map<std::string, std::string> m = {{"key", "scream"}, {"track", "1000"}};
    ConfigSnapshot snap = {};
    AudioTracksApplyResult result;
    audioTracksApply(makeSource(&m), true, &snap, &result);
    TEST_ASSERT_TRUE(result.error.hasError);
    TEST_ASSERT_EQUAL_STRING("track must be 0-999", result.error.message);
}

void test_audioTracksApply_named_track_success(void) {
    std::map<std::string, std::string> m = {{"key", "scream"}, {"track", "42"}};
    ConfigSnapshot snap = {};
    AudioTracksApplyResult result;
    audioTracksApply(makeSource(&m), true, &snap, &result);
    TEST_ASSERT_FALSE(result.error.hasError);
    TEST_ASSERT_EQUAL_UINT16(42, snap.audio.snd_scream);
    TEST_ASSERT_EQUAL_UINT16(42, result.track);
    TEST_ASSERT_FALSE(result.useBanked);
    TEST_ASSERT_EQUAL_STRING("chr_scream", result.chirpBindingKey);
}

// --- interval key ---
void test_audioTracksApply_interval_over_3600_rejected(void) {
    std::map<std::string, std::string> m = {{"key", "snd_int_quiet"}, {"track", "3601"}};
    ConfigSnapshot snap = {};
    AudioTracksApplyResult result;
    audioTracksApply(makeSource(&m), true, &snap, &result);
    TEST_ASSERT_TRUE(result.error.hasError);
    TEST_ASSERT_EQUAL_STRING("interval must be 0-3600 s", result.error.message);
}

void test_audioTracksApply_interval_zero_allowed(void) {
    std::map<std::string, std::string> m = {{"key", "snd_int_quiet"}, {"track", "0"}};
    ConfigSnapshot snap = {};
    AudioTracksApplyResult result;
    audioTracksApply(makeSource(&m), true, &snap, &result);
    TEST_ASSERT_FALSE(result.error.hasError);
    TEST_ASSERT_EQUAL_UINT16(0, snap.audio.snd_int_quiet);
}

void test_audioTracksApply_interval_key_has_no_chirp_binding(void) {
    std::map<std::string, std::string> m = {{"key", "snd_int_quiet"}, {"track", "10"}};
    ConfigSnapshot snap = {};
    AudioTracksApplyResult result;
    audioTracksApply(makeSource(&m), true, &snap, &result);
    TEST_ASSERT_FALSE(result.error.hasError);
    TEST_ASSERT_EQUAL_STRING("", result.chirpBindingKey);
}

// --- banked mode ---
void test_audioTracksApply_bank_without_page_rejected(void) {
    std::map<std::string, std::string> m = {{"key", "scream"}, {"track", "5"}, {"bank", "1"}};
    ConfigSnapshot snap = {};
    AudioTracksApplyResult result;
    audioTracksApply(makeSource(&m), true, &snap, &result);
    TEST_ASSERT_TRUE(result.error.hasError);
    TEST_ASSERT_EQUAL_STRING("bank and page must be provided together", result.error.message);
}

void test_audioTracksApply_banked_without_catalog_rejected(void) {
    std::map<std::string, std::string> m = {
        {"key", "scream"}, {"track", "5"}, {"bank", "1"}, {"page", "A"}};
    ConfigSnapshot snap = {};
    AudioTracksApplyResult result;
    audioTracksApply(makeSource(&m), /*catalogSupported=*/false, &snap, &result);
    TEST_ASSERT_TRUE(result.error.hasError);
    TEST_ASSERT_TRUE(result.error.notFound);
    TEST_ASSERT_EQUAL_STRING("catalog unsupported by active backend", result.error.message);
}

void test_audioTracksApply_banked_interval_key_rejected(void) {
    std::map<std::string, std::string> m = {
        {"key", "snd_int_quiet"}, {"track", "5"}, {"bank", "1"}, {"page", "A"}};
    ConfigSnapshot snap = {};
    AudioTracksApplyResult result;
    audioTracksApply(makeSource(&m), true, &snap, &result);
    TEST_ASSERT_TRUE(result.error.hasError);
    TEST_ASSERT_EQUAL_STRING("key does not support CHIRP binding", result.error.message);
}

void test_audioTracksApply_bank_out_of_range_rejected(void) {
    std::map<std::string, std::string> m = {
        {"key", "scream"}, {"track", "5"}, {"bank", "9"}, {"page", "A"}};
    ConfigSnapshot snap = {};
    AudioTracksApplyResult result;
    audioTracksApply(makeSource(&m), true, &snap, &result);
    TEST_ASSERT_TRUE(result.error.hasError);
    TEST_ASSERT_EQUAL_STRING("bank must be 1-6", result.error.message);
}

void test_audioTracksApply_banked_index_zero_rejected(void) {
    std::map<std::string, std::string> m = {
        {"key", "scream"}, {"track", "0"}, {"bank", "1"}, {"page", "A"}};
    ConfigSnapshot snap = {};
    AudioTracksApplyResult result;
    audioTracksApply(makeSource(&m), true, &snap, &result);
    TEST_ASSERT_TRUE(result.error.hasError);
    TEST_ASSERT_EQUAL_STRING("banked index must be 1-65535", result.error.message);
}

void test_audioTracksApply_banked_success(void) {
    std::map<std::string, std::string> m = {
        {"key", "scream"}, {"track", "100"}, {"bank", "2"}, {"page", "c"}};
    ConfigSnapshot snap = {};
    AudioTracksApplyResult result;
    audioTracksApply(makeSource(&m), true, &snap, &result);
    TEST_ASSERT_FALSE(result.error.hasError);
    TEST_ASSERT_TRUE(result.useBanked);
    TEST_ASSERT_EQUAL_UINT8(2, result.bank);
    TEST_ASSERT_EQUAL('C', result.page);
    TEST_ASSERT_EQUAL_UINT16(100, snap.audio.snd_scream);
}

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;
    UNITY_BEGIN();
    RUN_TEST(test_audioTracksApply_missing_params_rejected);
    RUN_TEST(test_audioTracksApply_unknown_key_rejected);
    RUN_TEST(test_audioTracksApply_non_integer_track_rejected);
    RUN_TEST(test_audioTracksApply_named_track_zero_rejected);
    RUN_TEST(test_audioTracksApply_zero_allowed_key_accepts_zero);
    RUN_TEST(test_audioTracksApply_named_track_over_999_rejected);
    RUN_TEST(test_audioTracksApply_named_track_success);
    RUN_TEST(test_audioTracksApply_interval_over_3600_rejected);
    RUN_TEST(test_audioTracksApply_interval_zero_allowed);
    RUN_TEST(test_audioTracksApply_interval_key_has_no_chirp_binding);
    RUN_TEST(test_audioTracksApply_bank_without_page_rejected);
    RUN_TEST(test_audioTracksApply_banked_without_catalog_rejected);
    RUN_TEST(test_audioTracksApply_banked_interval_key_rejected);
    RUN_TEST(test_audioTracksApply_bank_out_of_range_rejected);
    RUN_TEST(test_audioTracksApply_banked_index_zero_rejected);
    RUN_TEST(test_audioTracksApply_banked_success);
    return UNITY_END();
}
