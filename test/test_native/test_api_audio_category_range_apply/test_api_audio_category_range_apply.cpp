// =============================================================================
// test/test_native/test_api_audio_category_range_apply/test_api_audio_category_range_apply.cpp
//
// Native unit tests for audioCategoryRangeApply() (ADR 0011 audio wave,
// family 2).
// =============================================================================
#include <unity.h>

#include <map>
#include <string>

#include "api_audio_category_range_apply.h"

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

void test_audioCategoryRangeApply_missing_params_rejected(void) {
    std::map<std::string, std::string> m = {{"lo_key", "snd_cat_gen_lo"}};
    ConfigSnapshot snap = {};
    AudioCategoryRangeApplyResult result;
    audioCategoryRangeApply(makeSource(&m), true, &snap, &result);
    TEST_ASSERT_TRUE(result.error.hasError);
    TEST_ASSERT_EQUAL_STRING("requires lo_key, hi_key, lo, hi parameters", result.error.message);
}

void test_audioCategoryRangeApply_invalid_key_pair_rejected(void) {
    std::map<std::string, std::string> m = {
        {"lo_key", "snd_cat_gen_lo"}, {"hi_key", "snd_cat_chat_hi"}, {"lo", "0"}, {"hi", "0"}};
    ConfigSnapshot snap = {};
    AudioCategoryRangeApplyResult result;
    audioCategoryRangeApply(makeSource(&m), true, &snap, &result);
    TEST_ASSERT_TRUE(result.error.hasError);
    TEST_ASSERT_EQUAL_STRING("invalid category key pair", result.error.message);
}

void test_audioCategoryRangeApply_clear_binding_bad_bool_rejected(void) {
    std::map<std::string, std::string> m = {{"lo_key", "snd_cat_gen_lo"},
                                             {"hi_key", "snd_cat_gen_hi"},
                                             {"lo", "0"},
                                             {"hi", "0"},
                                             {"clear_binding", "nope"}};
    ConfigSnapshot snap = {};
    AudioCategoryRangeApplyResult result;
    audioCategoryRangeApply(makeSource(&m), true, &snap, &result);
    TEST_ASSERT_TRUE(result.error.hasError);
    TEST_ASSERT_EQUAL_STRING("clear_binding must be true/false/1/0", result.error.message);
}

void test_audioCategoryRangeApply_clear_binding_with_bank_rejected(void) {
    std::map<std::string, std::string> m = {{"lo_key", "snd_cat_gen_lo"},
                                             {"hi_key", "snd_cat_gen_hi"},
                                             {"lo", "0"},
                                             {"hi", "0"},
                                             {"bank", "1"},
                                             {"clear_binding", "1"}};
    ConfigSnapshot snap = {};
    AudioCategoryRangeApplyResult result;
    audioCategoryRangeApply(makeSource(&m), true, &snap, &result);
    TEST_ASSERT_TRUE(result.error.hasError);
    TEST_ASSERT_EQUAL_STRING("clear_binding cannot be combined with bank/page", result.error.message);
}

void test_audioCategoryRangeApply_bank_without_page_rejected(void) {
    std::map<std::string, std::string> m = {
        {"lo_key", "snd_cat_gen_lo"}, {"hi_key", "snd_cat_gen_hi"}, {"lo", "0"}, {"hi", "0"}, {"bank", "1"}};
    ConfigSnapshot snap = {};
    AudioCategoryRangeApplyResult result;
    audioCategoryRangeApply(makeSource(&m), true, &snap, &result);
    TEST_ASSERT_TRUE(result.error.hasError);
    TEST_ASSERT_EQUAL_STRING("bank and page must be provided together", result.error.message);
}

void test_audioCategoryRangeApply_banked_without_catalog_rejected(void) {
    std::map<std::string, std::string> m = {{"lo_key", "snd_cat_gen_lo"},
                                             {"hi_key", "snd_cat_gen_hi"},
                                             {"lo", "0"},
                                             {"hi", "0"},
                                             {"bank", "1"},
                                             {"page", "A"}};
    ConfigSnapshot snap = {};
    AudioCategoryRangeApplyResult result;
    audioCategoryRangeApply(makeSource(&m), /*catalogSupported=*/false, &snap, &result);
    TEST_ASSERT_TRUE(result.error.hasError);
    TEST_ASSERT_EQUAL_STRING("catalog unsupported by active backend", result.error.message);
}

void test_audioCategoryRangeApply_bank_out_of_range_rejected(void) {
    std::map<std::string, std::string> m = {{"lo_key", "snd_cat_gen_lo"},
                                             {"hi_key", "snd_cat_gen_hi"},
                                             {"lo", "0"},
                                             {"hi", "0"},
                                             {"bank", "9"},
                                             {"page", "A"}};
    ConfigSnapshot snap = {};
    AudioCategoryRangeApplyResult result;
    audioCategoryRangeApply(makeSource(&m), true, &snap, &result);
    TEST_ASSERT_TRUE(result.error.hasError);
    TEST_ASSERT_EQUAL_STRING("bank must be 1-6", result.error.message);
}

void test_audioCategoryRangeApply_invalid_page_rejected(void) {
    std::map<std::string, std::string> m = {{"lo_key", "snd_cat_gen_lo"},
                                             {"hi_key", "snd_cat_gen_hi"},
                                             {"lo", "0"},
                                             {"hi", "0"},
                                             {"bank", "1"},
                                             {"page", "1"}};
    ConfigSnapshot snap = {};
    AudioCategoryRangeApplyResult result;
    audioCategoryRangeApply(makeSource(&m), true, &snap, &result);
    TEST_ASSERT_TRUE(result.error.hasError);
    TEST_ASSERT_EQUAL_STRING("page must be a single letter A-Z", result.error.message);
}

void test_audioCategoryRangeApply_non_integer_range_rejected(void) {
    std::map<std::string, std::string> m = {
        {"lo_key", "snd_cat_gen_lo"}, {"hi_key", "snd_cat_gen_hi"}, {"lo", "abc"}, {"hi", "0"}};
    ConfigSnapshot snap = {};
    AudioCategoryRangeApplyResult result;
    audioCategoryRangeApply(makeSource(&m), true, &snap, &result);
    TEST_ASSERT_TRUE(result.error.hasError);
    TEST_ASSERT_EQUAL_STRING("range values must be non-negative integers", result.error.message);
}

void test_audioCategoryRangeApply_range_over_999_rejected(void) {
    std::map<std::string, std::string> m = {
        {"lo_key", "snd_cat_gen_lo"}, {"hi_key", "snd_cat_gen_hi"}, {"lo", "1000"}, {"hi", "1000"}};
    ConfigSnapshot snap = {};
    AudioCategoryRangeApplyResult result;
    audioCategoryRangeApply(makeSource(&m), true, &snap, &result);
    TEST_ASSERT_TRUE(result.error.hasError);
    TEST_ASSERT_EQUAL_STRING("range values must be 0-999", result.error.message);
}

void test_audioCategoryRangeApply_lo_greater_than_hi_rejected(void) {
    std::map<std::string, std::string> m = {
        {"lo_key", "snd_cat_gen_lo"}, {"hi_key", "snd_cat_gen_hi"}, {"lo", "5"}, {"hi", "1"}};
    ConfigSnapshot snap = {};
    AudioCategoryRangeApplyResult result;
    audioCategoryRangeApply(makeSource(&m), true, &snap, &result);
    TEST_ASSERT_TRUE(result.error.hasError);
}

void test_audioCategoryRangeApply_success_plain_range(void) {
    std::map<std::string, std::string> m = {
        {"lo_key", "snd_cat_gen_lo"}, {"hi_key", "snd_cat_gen_hi"}, {"lo", "10"}, {"hi", "20"}};
    ConfigSnapshot snap = {};
    AudioCategoryRangeApplyResult result;
    audioCategoryRangeApply(makeSource(&m), true, &snap, &result);
    TEST_ASSERT_FALSE(result.error.hasError);
    TEST_ASSERT_EQUAL_UINT16(10, snap.audio.snd_cat_gen_lo);
    TEST_ASSERT_EQUAL_UINT16(20, snap.audio.snd_cat_gen_hi);
    TEST_ASSERT_FALSE(result.hasBankedParams);
    TEST_ASSERT_FALSE(result.clearBinding);
    TEST_ASSERT_EQUAL_STRING("chr_cat_gen", result.categoryNvsKey);
}

void test_audioCategoryRangeApply_success_banked(void) {
    std::map<std::string, std::string> m = {{"lo_key", "snd_cat_gen_lo"},
                                             {"hi_key", "snd_cat_gen_hi"},
                                             {"lo", "1"},
                                             {"hi", "5"},
                                             {"bank", "3"},
                                             {"page", "b"}};
    ConfigSnapshot snap = {};
    AudioCategoryRangeApplyResult result;
    audioCategoryRangeApply(makeSource(&m), true, &snap, &result);
    TEST_ASSERT_FALSE(result.error.hasError);
    TEST_ASSERT_TRUE(result.hasBankedParams);
    TEST_ASSERT_EQUAL_UINT8(3, result.categoryBank);
    TEST_ASSERT_EQUAL('B', result.categoryPage);
}

void test_audioCategoryRangeApply_success_clear_binding(void) {
    std::map<std::string, std::string> m = {{"lo_key", "snd_cat_gen_lo"},
                                             {"hi_key", "snd_cat_gen_hi"},
                                             {"lo", "0"},
                                             {"hi", "0"},
                                             {"clear_binding", "true"}};
    ConfigSnapshot snap = {};
    AudioCategoryRangeApplyResult result;
    audioCategoryRangeApply(makeSource(&m), true, &snap, &result);
    TEST_ASSERT_FALSE(result.error.hasError);
    TEST_ASSERT_TRUE(result.clearBinding);
}

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;
    UNITY_BEGIN();
    RUN_TEST(test_audioCategoryRangeApply_missing_params_rejected);
    RUN_TEST(test_audioCategoryRangeApply_invalid_key_pair_rejected);
    RUN_TEST(test_audioCategoryRangeApply_clear_binding_bad_bool_rejected);
    RUN_TEST(test_audioCategoryRangeApply_clear_binding_with_bank_rejected);
    RUN_TEST(test_audioCategoryRangeApply_bank_without_page_rejected);
    RUN_TEST(test_audioCategoryRangeApply_banked_without_catalog_rejected);
    RUN_TEST(test_audioCategoryRangeApply_bank_out_of_range_rejected);
    RUN_TEST(test_audioCategoryRangeApply_invalid_page_rejected);
    RUN_TEST(test_audioCategoryRangeApply_non_integer_range_rejected);
    RUN_TEST(test_audioCategoryRangeApply_range_over_999_rejected);
    RUN_TEST(test_audioCategoryRangeApply_lo_greater_than_hi_rejected);
    RUN_TEST(test_audioCategoryRangeApply_success_plain_range);
    RUN_TEST(test_audioCategoryRangeApply_success_banked);
    RUN_TEST(test_audioCategoryRangeApply_success_clear_binding);
    return UNITY_END();
}
