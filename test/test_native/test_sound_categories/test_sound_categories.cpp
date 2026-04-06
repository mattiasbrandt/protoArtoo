#include <unity.h>

#include "mood_sound_mapping.h"
#include "rc_mapping.h"

void setUp() {
}

void tearDown() {
}

void test_select_random_track_rejects_zero_lower_bound() {
    uint16_t track = 123;
    TEST_ASSERT_FALSE(selectRandomTrackInRange(0, 10, 5, &track));
    TEST_ASSERT_EQUAL_UINT16(123, track);
}

void test_select_random_track_rejects_inverted_range() {
    uint16_t track = 456;
    TEST_ASSERT_FALSE(selectRandomTrackInRange(11, 10, 0, &track));
    TEST_ASSERT_EQUAL_UINT16(456, track);
}

void test_select_random_track_accepts_valid_range_and_bounds_output() {
    uint16_t track = 0;
    TEST_ASSERT_TRUE(selectRandomTrackInRange(3, 10, 0, &track));
    TEST_ASSERT_EQUAL_UINT16(3, track);

    TEST_ASSERT_TRUE(selectRandomTrackInRange(3, 10, 7, &track));
    TEST_ASSERT_EQUAL_UINT16(10, track);

    TEST_ASSERT_TRUE(selectRandomTrackInRange(3, 10, 8, &track));
    TEST_ASSERT_EQUAL_UINT16(3, track);
}

void test_select_random_track_generates_in_range_values() {
    for (uint32_t r = 0; r < 128; ++r) {
        uint16_t track = 0;
        TEST_ASSERT_TRUE(selectRandomTrackInRange(3, 10, r, &track));
        TEST_ASSERT_TRUE(track >= 3 && track <= 10);
    }
}

void test_select_random_track_from_category_mask_weighted_pool() {
    SoundCategoryRange ranges[SOUND_CATEGORY_COUNT] = {};
    ranges[1] = {10, 12};  // span 3
    ranges[3] = {20, 21};  // span 2
    const uint16_t mask = (1U << 1) | (1U << 3);

    uint16_t track = 0;
    TEST_ASSERT_TRUE(selectRandomTrackFromCategoryMask(ranges, SOUND_CATEGORY_COUNT, mask, 0, &track));
    TEST_ASSERT_EQUAL_UINT16(10, track);
    TEST_ASSERT_TRUE(selectRandomTrackFromCategoryMask(ranges, SOUND_CATEGORY_COUNT, mask, 2, &track));
    TEST_ASSERT_EQUAL_UINT16(12, track);
    TEST_ASSERT_TRUE(selectRandomTrackFromCategoryMask(ranges, SOUND_CATEGORY_COUNT, mask, 3, &track));
    TEST_ASSERT_EQUAL_UINT16(20, track);
    TEST_ASSERT_TRUE(selectRandomTrackFromCategoryMask(ranges, SOUND_CATEGORY_COUNT, mask, 4, &track));
    TEST_ASSERT_EQUAL_UINT16(21, track);
    TEST_ASSERT_TRUE(selectRandomTrackFromCategoryMask(ranges, SOUND_CATEGORY_COUNT, mask, 5, &track));
    TEST_ASSERT_EQUAL_UINT16(10, track);
}

void test_select_random_track_from_category_mask_returns_false_for_empty_pool() {
    SoundCategoryRange ranges[SOUND_CATEGORY_COUNT] = {};
    ranges[2] = {0, 50};    // inactive (lo==0)
    ranges[5] = {30, 20};   // inactive (lo>hi)
    const uint16_t mask = (1U << 2) | (1U << 5);

    uint16_t track = 999;
    TEST_ASSERT_FALSE(selectRandomTrackFromCategoryMask(ranges, SOUND_CATEGORY_COUNT, mask, 1, &track));
    TEST_ASSERT_EQUAL_UINT16(999, track);
}

void test_select_random_track_for_mood_uses_category_pool_when_configured() {
    SoundCategoryRange ranges[SOUND_CATEGORY_COUNT] = {};
    ranges[0] = {100, 101};
    ranges[2] = {200, 202};

    MoodCategoryMaskConfig masks = {};
    masks.mid = (1U << 0) | (1U << 2);

    uint16_t track = 0;
    bool usedFlatFallback = true;
    TEST_ASSERT_TRUE(selectRandomTrackForMood(13, masks, ranges, SOUND_CATEGORY_COUNT, 1, 9, 3, &track,
                                              &usedFlatFallback));
    TEST_ASSERT_FALSE(usedFlatFallback);
    TEST_ASSERT_EQUAL_UINT16(201, track);
}

void test_select_random_track_for_mood_falls_back_when_pool_empty() {
    SoundCategoryRange ranges[SOUND_CATEGORY_COUNT] = {};
    ranges[1] = {0, 55};  // inactive due to lo==0

    MoodCategoryMaskConfig masks = {};
    masks.quiet = (1U << 1);

    uint16_t track = 0;
    bool usedFlatFallback = false;
    TEST_ASSERT_TRUE(selectRandomTrackForMood(10, masks, ranges, SOUND_CATEGORY_COUNT, 50, 55, 2, &track,
                                              &usedFlatFallback));
    TEST_ASSERT_TRUE(usedFlatFallback);
    TEST_ASSERT_EQUAL_UINT16(52, track);
}

void test_select_random_track_for_mood_zero_forces_flat_fallback() {
    SoundCategoryRange ranges[SOUND_CATEGORY_COUNT] = {};
    ranges[0] = {100, 100};

    MoodCategoryMaskConfig masks = {};
    masks.quiet = (1U << 0);
    masks.mid = (1U << 0);
    masks.full = (1U << 0);
    masks.awakeplus = (1U << 0);

    uint16_t track = 0;
    bool usedFlatFallback = false;
    TEST_ASSERT_TRUE(selectRandomTrackForMood(0, masks, ranges, SOUND_CATEGORY_COUNT, 7, 9, 4, &track,
                                              &usedFlatFallback));
    TEST_ASSERT_TRUE(usedFlatFallback);
    TEST_ASSERT_EQUAL_UINT16(8, track);
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_select_random_track_rejects_zero_lower_bound);
    RUN_TEST(test_select_random_track_rejects_inverted_range);
    RUN_TEST(test_select_random_track_accepts_valid_range_and_bounds_output);
    RUN_TEST(test_select_random_track_generates_in_range_values);
    RUN_TEST(test_select_random_track_from_category_mask_weighted_pool);
    RUN_TEST(test_select_random_track_from_category_mask_returns_false_for_empty_pool);
    RUN_TEST(test_select_random_track_for_mood_uses_category_pool_when_configured);
    RUN_TEST(test_select_random_track_for_mood_falls_back_when_pool_empty);
    RUN_TEST(test_select_random_track_for_mood_zero_forces_flat_fallback);
    return UNITY_END();
}
