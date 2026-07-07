// =============================================================================
// test/test_native/test_api_audio_mood_map_apply/test_api_audio_mood_map_apply.cpp
//
// Native unit tests for audioMoodMapApply() (ADR 0011 audio wave, family 1).
// =============================================================================
#include <unity.h>

#include <map>
#include <string>

#include "api_audio_mood_map_apply.h"

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

void test_audioMoodMapApply_neither_source_returns_error(void) {
    std::map<std::string, std::string> m;
    AudioMoodMapApplyResult result;
    audioMoodMapApply(makeSource(&m), &result);
    TEST_ASSERT_TRUE(result.error.hasError);
    TEST_ASSERT_EQUAL_STRING("requires form fields or json body", result.error.message);
}

void test_audioMoodMapApply_form_all_four_valid(void) {
    std::map<std::string, std::string> m = {
        {"quiet", "1"}, {"mid", "2"}, {"full", "3"}, {"awakeplus", "4"}};
    AudioMoodMapApplyResult result;
    audioMoodMapApply(makeSource(&m), &result);
    TEST_ASSERT_FALSE(result.error.hasError);
    TEST_ASSERT_EQUAL_UINT16(1, result.quiet);
    TEST_ASSERT_EQUAL_UINT16(2, result.mid);
    TEST_ASSERT_EQUAL_UINT16(3, result.full);
    TEST_ASSERT_EQUAL_UINT16(4, result.awakeplus);
}

void test_audioMoodMapApply_form_partial_rejected(void) {
    std::map<std::string, std::string> m = {{"quiet", "1"}, {"mid", "2"}};
    AudioMoodMapApplyResult result;
    audioMoodMapApply(makeSource(&m), &result);
    TEST_ASSERT_TRUE(result.error.hasError);
    TEST_ASSERT_EQUAL_STRING("requires quiet, mid, full, awakeplus", result.error.message);
}

void test_audioMoodMapApply_form_non_integer_rejected(void) {
    std::map<std::string, std::string> m = {
        {"quiet", "nope"}, {"mid", "2"}, {"full", "3"}, {"awakeplus", "4"}};
    AudioMoodMapApplyResult result;
    audioMoodMapApply(makeSource(&m), &result);
    TEST_ASSERT_TRUE(result.error.hasError);
    TEST_ASSERT_EQUAL_STRING("quiet must be a non-negative integer", result.error.message);
}

void test_audioMoodMapApply_form_out_of_range_rejected(void) {
    std::map<std::string, std::string> m = {
        {"quiet", "5000"}, {"mid", "2"}, {"full", "3"}, {"awakeplus", "4"}};
    AudioMoodMapApplyResult result;
    audioMoodMapApply(makeSource(&m), &result);
    TEST_ASSERT_TRUE(result.error.hasError);
    TEST_ASSERT_EQUAL_STRING("quiet must be 0..4095", result.error.message);
}

void test_audioMoodMapApply_json_body_all_four_valid(void) {
    std::map<std::string, std::string> m = {
        {"plain", "{\"quiet\":10,\"mid\":20,\"full\":30,\"awakeplus\":40}"}};
    AudioMoodMapApplyResult result;
    audioMoodMapApply(makeSource(&m), &result);
    TEST_ASSERT_FALSE(result.error.hasError);
    TEST_ASSERT_EQUAL_UINT16(10, result.quiet);
    TEST_ASSERT_EQUAL_UINT16(40, result.awakeplus);
}

void test_audioMoodMapApply_json_body_missing_field_rejected(void) {
    std::map<std::string, std::string> m = {{"plain", "{\"quiet\":10,\"mid\":20,\"full\":30}"}};
    AudioMoodMapApplyResult result;
    audioMoodMapApply(makeSource(&m), &result);
    TEST_ASSERT_TRUE(result.error.hasError);
    TEST_ASSERT_EQUAL_STRING("missing awakeplus", result.error.message);
}

void test_audioMoodMapApply_json_body_string_value_accepted(void) {
    std::map<std::string, std::string> m = {
        {"plain", "{\"quiet\":\"10\",\"mid\":20,\"full\":30,\"awakeplus\":40}"}};
    AudioMoodMapApplyResult result;
    audioMoodMapApply(makeSource(&m), &result);
    TEST_ASSERT_FALSE(result.error.hasError);
    TEST_ASSERT_EQUAL_UINT16(10, result.quiet);
}

void test_audioMoodMapApply_json_body_invalid_json_rejected(void) {
    std::map<std::string, std::string> m = {{"plain", "not json"}};
    AudioMoodMapApplyResult result;
    audioMoodMapApply(makeSource(&m), &result);
    TEST_ASSERT_TRUE(result.error.hasError);
    TEST_ASSERT_EQUAL_STRING("invalid json body", result.error.message);
}

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;
    UNITY_BEGIN();
    RUN_TEST(test_audioMoodMapApply_neither_source_returns_error);
    RUN_TEST(test_audioMoodMapApply_form_all_four_valid);
    RUN_TEST(test_audioMoodMapApply_form_partial_rejected);
    RUN_TEST(test_audioMoodMapApply_form_non_integer_rejected);
    RUN_TEST(test_audioMoodMapApply_form_out_of_range_rejected);
    RUN_TEST(test_audioMoodMapApply_json_body_all_four_valid);
    RUN_TEST(test_audioMoodMapApply_json_body_missing_field_rejected);
    RUN_TEST(test_audioMoodMapApply_json_body_string_value_accepted);
    RUN_TEST(test_audioMoodMapApply_json_body_invalid_json_rejected);
    return UNITY_END();
}
