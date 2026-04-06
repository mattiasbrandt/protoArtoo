// =============================================================================
// test/test_native/test_mood_mapping/test_mood_mapping.cpp
//
// Native tests for T11 mood-map helpers.
// Covers:
// - mask validation bounds (0..0x0FFF)
// - API JSON payload format and size budget
// - round-trip JSON decode fidelity for quiet/mid/full/awakeplus
// =============================================================================

#include <ArduinoJson.h>
#include <unity.h>

#include "mood_sound_mapping.h"

void setUp() {
}

void tearDown() {
}

void test_mask_validation_bounds() {
    TEST_ASSERT_TRUE(isValidMoodCategoryMaskValue(0));
    TEST_ASSERT_TRUE(isValidMoodCategoryMaskValue(0x0FFF));
    TEST_ASSERT_FALSE(isValidMoodCategoryMaskValue(0x1000));
}

void test_format_mood_map_json_expected_keys_and_values() {
    MoodCategoryMaskConfig cfg = {72, 79, 2319, 3983};
    char body[128] = {};
    size_t n = formatMoodCategoryMapJson(body, sizeof(body), cfg);

    TEST_ASSERT_GREATER_THAN_UINT(0, n);
    TEST_ASSERT_TRUE(n < sizeof(body));

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, body);
    TEST_ASSERT_TRUE(!err);
    TEST_ASSERT_EQUAL_UINT16(72, doc["quiet"].as<uint16_t>());
    TEST_ASSERT_EQUAL_UINT16(79, doc["mid"].as<uint16_t>());
    TEST_ASSERT_EQUAL_UINT16(2319, doc["full"].as<uint16_t>());
    TEST_ASSERT_EQUAL_UINT16(3983, doc["awakeplus"].as<uint16_t>());
}

void test_format_mood_map_json_budget_under_128_bytes() {
    MoodCategoryMaskConfig cfg = {0x0FFF, 0x0FFF, 0x0FFF, 0x0FFF};
    char body[128] = {};
    size_t n = formatMoodCategoryMapJson(body, sizeof(body), cfg);

    TEST_ASSERT_TRUE(n < sizeof(body));
    TEST_ASSERT_TRUE(n <= 127);
}

void test_mood_map_json_round_trip() {
    MoodCategoryMaskConfig original = {0x0048, 0x004F, 0x090F, 0x0F8F};
    char body[128] = {};
    TEST_ASSERT_TRUE(formatMoodCategoryMapJson(body, sizeof(body), original) < sizeof(body));

    JsonDocument doc;
    TEST_ASSERT_TRUE(!deserializeJson(doc, body));

    MoodCategoryMaskConfig decoded = {
        doc["quiet"].as<uint16_t>(),
        doc["mid"].as<uint16_t>(),
        doc["full"].as<uint16_t>(),
        doc["awakeplus"].as<uint16_t>(),
    };

    TEST_ASSERT_EQUAL_UINT16(original.quiet, decoded.quiet);
    TEST_ASSERT_EQUAL_UINT16(original.mid, decoded.mid);
    TEST_ASSERT_EQUAL_UINT16(original.full, decoded.full);
    TEST_ASSERT_EQUAL_UINT16(original.awakeplus, decoded.awakeplus);
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_mask_validation_bounds);
    RUN_TEST(test_format_mood_map_json_expected_keys_and_values);
    RUN_TEST(test_format_mood_map_json_budget_under_128_bytes);
    RUN_TEST(test_mood_map_json_round_trip);
    return UNITY_END();
}
