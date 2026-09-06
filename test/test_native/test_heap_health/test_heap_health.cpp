// =============================================================================
// test/test_native/test_heap_health/test_heap_health.cpp
//
// Native unit tests for heapFragRatio (include/heap_health.h).
// Covers: empty heap, unfragmented, partial fragmentation, and the #245
// defect-2 shape where the largest-block sample dwarfs the free sample
// (mixed capability masks, or a benign inter-sample race) and the ratio
// must clamp to 0 instead of going hugely negative.
// =============================================================================
#include <unity.h>

#include "heap_health.h"

void setUp() {
}
void tearDown() {
}

void test_zero_free_heap_gives_zero() {
    TEST_ASSERT_EQUAL_FLOAT(0.0f, heapFragRatio(0, 0));
    TEST_ASSERT_EQUAL_FLOAT(0.0f, heapFragRatio(0, 4096));
}

void test_single_contiguous_block_gives_zero() {
    TEST_ASSERT_EQUAL_FLOAT(0.0f, heapFragRatio(114324, 114324));
}

void test_half_fragmented_gives_half() {
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.5f, heapFragRatio(100000, 50000));
}

void test_quarter_largest_gives_three_quarters() {
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.75f, heapFragRatio(100000, 25000));
}

void test_fully_exhausted_largest_gives_one() {
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 1.0f, heapFragRatio(100000, 0));
}

// The exact figures observed on the ESP32-P4 before the fix: free was sampled
// from the internal heap (114324 B) while largest came from a mask that
// included PSRAM (33030132 B). The raw formula gave -287.92; the ratio must
// come back sane instead.
void test_defect_shape_largest_exceeding_free_clamps_to_zero() {
    TEST_ASSERT_EQUAL_FLOAT(0.0f, heapFragRatio(114324, 33030132));
}

void test_benign_race_slightly_larger_largest_clamps_to_zero() {
    TEST_ASSERT_EQUAL_FLOAT(0.0f, heapFragRatio(100000, 100016));
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_zero_free_heap_gives_zero);
    RUN_TEST(test_single_contiguous_block_gives_zero);
    RUN_TEST(test_half_fragmented_gives_half);
    RUN_TEST(test_quarter_largest_gives_three_quarters);
    RUN_TEST(test_fully_exhausted_largest_gives_one);
    RUN_TEST(test_defect_shape_largest_exceeding_free_clamps_to_zero);
    RUN_TEST(test_benign_race_slightly_larger_largest_clamps_to_zero);
    return UNITY_END();
}
