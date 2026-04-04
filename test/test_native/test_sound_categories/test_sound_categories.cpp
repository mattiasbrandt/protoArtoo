#include <unity.h>

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

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_select_random_track_rejects_zero_lower_bound);
    RUN_TEST(test_select_random_track_rejects_inverted_range);
    RUN_TEST(test_select_random_track_accepts_valid_range_and_bounds_output);
    RUN_TEST(test_select_random_track_generates_in_range_values);
    return UNITY_END();
}
