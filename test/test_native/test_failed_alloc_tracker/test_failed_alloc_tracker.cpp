// =============================================================================
// test/test_native/test_failed_alloc_tracker/test_failed_alloc_tracker.cpp
//
// Native tests for the pure counting core of the failed-allocation tracker
// (include/failed_alloc_tracker.h) - the counter /api/status publishes as
// "failedAllocs" and ADR 0017's heap rule reads on a production build.
//
// Covers the two rules the count depends on:
//   1. Every failure is counted, including a re-entrant one.
//   2. Only the outermost call captures the failure detail, so the recursive
//      allocation shape that once overflowed a task stack cannot overwrite the
//      record being built.
// =============================================================================

#include <unity.h>

#include "failed_alloc_tracker.h"

void setUp(void) {
}

void tearDown(void) {
}

void test_first_failure_is_counted_and_captured(void) {
    FailedAllocTrackerState state = {};

    TEST_ASSERT_TRUE(failedAllocTrackerBegin(state, 64U, 0x1404U));
    failedAllocTrackerEnd(state);

    TEST_ASSERT_EQUAL_UINT32(1U, state.count);
    TEST_ASSERT_EQUAL_UINT32(64U, state.lastSize);
    TEST_ASSERT_EQUAL_UINT32(0x1404U, state.lastCaps);
    TEST_ASSERT_FALSE(state.inCallback);
}

void test_sequential_failures_each_count_and_replace_the_detail(void) {
    FailedAllocTrackerState state = {};

    failedAllocTrackerBegin(state, 64U, 0x1404U);
    failedAllocTrackerEnd(state);
    TEST_ASSERT_TRUE(failedAllocTrackerBegin(state, 4096U, 0x0800U));
    failedAllocTrackerEnd(state);

    TEST_ASSERT_EQUAL_UINT32(2U, state.count);
    TEST_ASSERT_EQUAL_UINT32(4096U, state.lastSize);
    TEST_ASSERT_EQUAL_UINT32(0x0800U, state.lastCaps);
}

// The re-entrant shape: an allocation fails, its handler allocates, and that
// allocation fails too before the outer call has released the capture.
void test_reentrant_failure_is_counted_but_does_not_capture(void) {
    FailedAllocTrackerState state = {};

    TEST_ASSERT_TRUE(failedAllocTrackerBegin(state, 64U, 0x1404U));
    TEST_ASSERT_FALSE(failedAllocTrackerBegin(state, 8U, 0x0002U));

    // Counted - a dropped increment would under-report the exact condition
    // ADR 0017's rule is watching for.
    TEST_ASSERT_EQUAL_UINT32(2U, state.count);
    // ...but the outer call's detail survives.
    TEST_ASSERT_EQUAL_UINT32(64U, state.lastSize);
    TEST_ASSERT_EQUAL_UINT32(0x1404U, state.lastCaps);

    failedAllocTrackerEnd(state);
    TEST_ASSERT_FALSE(state.inCallback);
}

// After the outer call releases, the next failure captures again - the guard
// is a re-entry guard, not a one-shot latch.
void test_capture_resumes_after_the_outer_call_ends(void) {
    FailedAllocTrackerState state = {};

    failedAllocTrackerBegin(state, 64U, 0x1404U);
    failedAllocTrackerBegin(state, 8U, 0x0002U);
    failedAllocTrackerEnd(state);

    TEST_ASSERT_TRUE(failedAllocTrackerBegin(state, 128U, 0x0004U));
    failedAllocTrackerEnd(state);

    TEST_ASSERT_EQUAL_UINT32(3U, state.count);
    TEST_ASSERT_EQUAL_UINT32(128U, state.lastSize);
    TEST_ASSERT_EQUAL_UINT32(0x0004U, state.lastCaps);
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_first_failure_is_counted_and_captured);
    RUN_TEST(test_sequential_failures_each_count_and_replace_the_detail);
    RUN_TEST(test_reentrant_failure_is_counted_but_does_not_capture);
    RUN_TEST(test_capture_resumes_after_the_outer_call_ends);
    return UNITY_END();
}
