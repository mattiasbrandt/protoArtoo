// =============================================================================
// test/test_native/test_web_admission/test_web_admission.cpp
//
// Native unit tests for the admission decision core.
//
// What is worth pinning here is the decision logic, not the calibration: the
// thresholds are passed in, so these tests survive a retune and still fail if
// the ordering of the checks, the pacing arithmetic or the estop bypass
// changes. The guard's cost and its behaviour under genuine concurrency are
// device evidence and are recorded on the issue instead.
// =============================================================================
#include <unity.h>

#include "web_admission.h"

// Values that make the arithmetic legible, not the calibrated device values.
static constexpr uint32_t kBurst = 6;
static constexpr uint32_t kPerSecond = 8;
static constexpr size_t kFloor = 9000;
static constexpr size_t kFloorDiag = 7500;
static constexpr size_t kAcceptFloor = 8500;
static constexpr int kMaxInflight = 6;

// Stands in for the heap walk. Counting the calls is the point: whether the
// sample is taken at all is the guard's cost story, not an implementation
// detail.
static size_t s_sampleValue = 20000;
static int s_sampleCalls = 0;

static size_t sampleStub(void*) {
    s_sampleCalls++;
    return s_sampleValue;
}

void setUp() {
    s_sampleValue = 20000;
    s_sampleCalls = 0;
}

void tearDown() {
}

// -----------------------------------------------------------------------------
// Token bucket
// -----------------------------------------------------------------------------

void test_bucket_starts_full_and_admits_a_whole_burst() {
    WebAcceptRateLimiter limiter;
    webAcceptRateLimiterInit(&limiter, 1000, kBurst);

    // A browser opening its parallel connections must not be paced out at t=0.
    for (uint32_t i = 0; i < kBurst; i++) {
        TEST_ASSERT_TRUE(webAcceptRateLimiterTake(&limiter, 1000, kBurst, kPerSecond));
    }
}

void test_bucket_paces_out_the_connection_after_the_burst() {
    WebAcceptRateLimiter limiter;
    webAcceptRateLimiterInit(&limiter, 1000, kBurst);
    for (uint32_t i = 0; i < kBurst; i++) {
        webAcceptRateLimiterTake(&limiter, 1000, kBurst, kPerSecond);
    }

    TEST_ASSERT_FALSE(webAcceptRateLimiterTake(&limiter, 1000, kBurst, kPerSecond));
}

void test_bucket_refills_at_the_configured_rate() {
    WebAcceptRateLimiter limiter;
    webAcceptRateLimiterInit(&limiter, 1000, kBurst);
    for (uint32_t i = 0; i < kBurst; i++) {
        webAcceptRateLimiterTake(&limiter, 1000, kBurst, kPerSecond);
    }

    // At 8/s one token is worth 125 ms. Just under is still empty.
    TEST_ASSERT_FALSE(webAcceptRateLimiterTake(&limiter, 1124, kBurst, kPerSecond));
    TEST_ASSERT_TRUE(webAcceptRateLimiterTake(&limiter, 1250, kBurst, kPerSecond));
}

void test_bucket_refill_is_clamped_to_the_burst_size() {
    WebAcceptRateLimiter limiter;
    webAcceptRateLimiterInit(&limiter, 1000, kBurst);
    for (uint32_t i = 0; i < kBurst; i++) {
        webAcceptRateLimiterTake(&limiter, 1000, kBurst, kPerSecond);
    }

    // An hour of silence must not bank an hour of credit; otherwise a quiet
    // controller answers the next burst with no pacing at all.
    for (uint32_t i = 0; i < kBurst; i++) {
        TEST_ASSERT_TRUE(webAcceptRateLimiterTake(&limiter, 1000 + 3600000, kBurst, kPerSecond));
    }
    TEST_ASSERT_FALSE(webAcceptRateLimiterTake(&limiter, 1000 + 3600000, kBurst, kPerSecond));
}

void test_bucket_survives_millis_wraparound() {
    // Elapsed time across the 32-bit rollover must read as a small positive
    // interval, not as a ~49-day one that refills the bucket forever.
    WebAcceptRateLimiter limiter;
    webAcceptRateLimiterInit(&limiter, 0xFFFFFF00u, kBurst);
    for (uint32_t i = 0; i < kBurst; i++) {
        webAcceptRateLimiterTake(&limiter, 0xFFFFFF00u, kBurst, kPerSecond);
    }

    // 0x40 ms past the wrap is 320 ms of elapsed time: two tokens, not more.
    TEST_ASSERT_TRUE(webAcceptRateLimiterTake(&limiter, 0x40u, kBurst, kPerSecond));
    TEST_ASSERT_TRUE(webAcceptRateLimiterTake(&limiter, 0x40u, kBurst, kPerSecond));
    TEST_ASSERT_FALSE(webAcceptRateLimiterTake(&limiter, 0x40u, kBurst, kPerSecond));
}

// -----------------------------------------------------------------------------
// Connection Admission
// -----------------------------------------------------------------------------

static WebAcceptDecision decide(WebAcceptRateLimiter* limiter, uint32_t nowMs) {
    return webAcceptDecide(limiter, nowMs, kBurst, kPerSecond, sampleStub, nullptr, kAcceptFloor);
}

void test_accept_admits_when_rate_and_heap_are_both_healthy() {
    WebAcceptRateLimiter limiter;
    webAcceptRateLimiterInit(&limiter, 1000, kBurst);

    TEST_ASSERT_EQUAL(WebAcceptDecision::kAdmit, decide(&limiter, 1000));
}

void test_accept_rejects_below_the_heap_floor() {
    WebAcceptRateLimiter limiter;
    webAcceptRateLimiterInit(&limiter, 1000, kBurst);
    s_sampleValue = kAcceptFloor - 1;

    TEST_ASSERT_EQUAL(WebAcceptDecision::kRejectHeap, decide(&limiter, 1000));
}

void test_accept_admits_exactly_at_the_heap_floor() {
    // The floor is a minimum, not a strict bound: the calibration places the
    // floors below the warm-under-load level, so the boundary value is still
    // healthy and must not shed a page's own assets.
    WebAcceptRateLimiter limiter;
    webAcceptRateLimiterInit(&limiter, 1000, kBurst);
    s_sampleValue = kAcceptFloor;

    TEST_ASSERT_EQUAL(WebAcceptDecision::kAdmit, decide(&limiter, 1000));
}

void test_accept_checks_rate_before_heap() {
    WebAcceptRateLimiter limiter;
    webAcceptRateLimiterInit(&limiter, 1000, kBurst);
    for (uint32_t i = 0; i < kBurst; i++) {
        decide(&limiter, 1000);
    }
    s_sampleValue = 100;

    TEST_ASSERT_EQUAL(WebAcceptDecision::kRejectRate, decide(&limiter, 1000));
}

void test_paced_out_connection_never_samples_the_heap() {
    // The cost story of the whole guard: sampling the heap may walk it, and
    // that walk happens on the task that services every other connection. A
    // connection being paced out must not trigger one.
    WebAcceptRateLimiter limiter;
    webAcceptRateLimiterInit(&limiter, 1000, kBurst);
    for (uint32_t i = 0; i < kBurst; i++) {
        decide(&limiter, 1000);
    }
    const int callsAfterBurst = s_sampleCalls;

    decide(&limiter, 1000);

    TEST_ASSERT_EQUAL_INT(callsAfterBurst, s_sampleCalls);
}

void test_admission_samples_the_heap_at_most_once() {
    WebAcceptRateLimiter limiter;
    webAcceptRateLimiterInit(&limiter, 1000, kBurst);

    decide(&limiter, 1000);

    TEST_ASSERT_EQUAL_INT(1, s_sampleCalls);
}

void test_accept_does_not_spend_a_token_on_a_heap_rejection() {
    // A connection refused for heap has not been admitted, so it must not
    // consume admission budget -- otherwise a heap-pressure window silently
    // paces out the connections that arrive once heap recovers.
    WebAcceptRateLimiter limiter;
    webAcceptRateLimiterInit(&limiter, 1000, kBurst);
    s_sampleValue = 100;
    for (uint32_t i = 0; i < kBurst; i++) {
        TEST_ASSERT_EQUAL(WebAcceptDecision::kRejectHeap, decide(&limiter, 1000));
    }

    s_sampleValue = 20000;
    for (uint32_t i = 0; i < kBurst; i++) {
        TEST_ASSERT_EQUAL(WebAcceptDecision::kAdmit, decide(&limiter, 1000));
    }
}

// -----------------------------------------------------------------------------
// Cached heap sample
// -----------------------------------------------------------------------------

void test_unprimed_sample_is_due_rather_than_optimistic() {
    WebHeapSampleCache cache = {};

    TEST_ASSERT_TRUE(webHeapSampleDue(&cache, 0, 100));
}

void test_sample_is_not_due_again_within_the_interval() {
    WebHeapSampleCache cache = {};
    webHeapSampleStore(&cache, 1000, 12300);

    TEST_ASSERT_FALSE(webHeapSampleDue(&cache, 1099, 100));
    TEST_ASSERT_TRUE(webHeapSampleDue(&cache, 1100, 100));
    TEST_ASSERT_EQUAL_UINT32(12300, cache.value);
}

void test_sample_interval_survives_millis_wraparound() {
    WebHeapSampleCache cache = {};
    webHeapSampleStore(&cache, 0xFFFFFFF0u, 12300);

    // 0x10 ms past the wrap is 32 ms elapsed, not a rollover-sized eternity.
    TEST_ASSERT_FALSE(webHeapSampleDue(&cache, 0x10u, 100));
    TEST_ASSERT_TRUE(webHeapSampleDue(&cache, 0x80u, 100));
}

// -----------------------------------------------------------------------------
// Request admission
// -----------------------------------------------------------------------------

static WebRequestAdmissionInputs healthyInputs() {
    WebRequestAdmissionInputs in = {};
    in.estop = false;
    in.diagnostic = false;
    in.longLived = false;
    in.inflightRequests = 0;
    in.maxInflightRequests = kMaxInflight;
    in.largestFreeBlock = 20000;
    in.minLargestFreeBlock = kFloor;
    in.minLargestFreeBlockDiagnostic = kFloorDiag;
    return in;
}

void test_ordinary_request_is_admitted_when_healthy() {
    TEST_ASSERT_EQUAL(WebRequestAdmission::kAdmit, webRequestAdmissionDecide(healthyInputs()));
}

void test_estop_is_admitted_at_the_inflight_cap() {
    WebRequestAdmissionInputs in = healthyInputs();
    in.estop = true;
    in.inflightRequests = kMaxInflight;

    TEST_ASSERT_EQUAL(WebRequestAdmission::kAdmit, webRequestAdmissionDecide(in));
}

void test_estop_is_admitted_with_a_dead_heap() {
    // The safety path is not shed by a memory policy. This is the whole reason
    // the request layer exists as well as the connection layer: the socket
    // guard cannot see the URL and so cannot make this distinction.
    WebRequestAdmissionInputs in = healthyInputs();
    in.estop = true;
    in.largestFreeBlock = 0;

    TEST_ASSERT_EQUAL(WebRequestAdmission::kAdmit, webRequestAdmissionDecide(in));
}

void test_ordinary_request_is_refused_at_the_inflight_cap() {
    WebRequestAdmissionInputs in = healthyInputs();
    in.inflightRequests = kMaxInflight;

    TEST_ASSERT_EQUAL(WebRequestAdmission::kRejectInflightCap, webRequestAdmissionDecide(in));
}

void test_long_lived_stream_is_not_counted_against_the_inflight_cap() {
    WebRequestAdmissionInputs in = healthyInputs();
    in.longLived = true;
    in.diagnostic = true;
    in.inflightRequests = kMaxInflight;

    TEST_ASSERT_EQUAL(WebRequestAdmission::kAdmit, webRequestAdmissionDecide(in));
}

void test_ordinary_request_is_refused_below_its_floor() {
    WebRequestAdmissionInputs in = healthyInputs();
    in.largestFreeBlock = kFloor - 1;

    TEST_ASSERT_EQUAL(WebRequestAdmission::kRejectHeapFloor, webRequestAdmissionDecide(in));
}

void test_diagnostics_stay_reachable_below_the_ordinary_floor() {
    // An operator needs the diagnostic endpoint precisely during a rejection
    // window, so it uses the lower floor rather than the ordinary one.
    WebRequestAdmissionInputs in = healthyInputs();
    in.diagnostic = true;
    in.largestFreeBlock = kFloor - 1;

    TEST_ASSERT_EQUAL(WebRequestAdmission::kAdmit, webRequestAdmissionDecide(in));
}

void test_diagnostics_are_not_exempt_from_every_floor() {
    WebRequestAdmissionInputs in = healthyInputs();
    in.diagnostic = true;
    in.largestFreeBlock = kFloorDiag - 1;

    TEST_ASSERT_EQUAL(WebRequestAdmission::kRejectHeapFloor, webRequestAdmissionDecide(in));
}

void test_inflight_cap_is_checked_before_the_heap_floor() {
    // Matches the async stack's ordering, so a run that shows a cap refusal
    // means the same thing on both stacks.
    WebRequestAdmissionInputs in = healthyInputs();
    in.inflightRequests = kMaxInflight;
    in.largestFreeBlock = 0;

    TEST_ASSERT_EQUAL(WebRequestAdmission::kRejectInflightCap, webRequestAdmissionDecide(in));
}

// -----------------------------------------------------------------------------
// Path classification
// -----------------------------------------------------------------------------

void test_estop_paths_are_recognised() {
    TEST_ASSERT_TRUE(webPathIsEstop("/api/estop"));
    // The clear path is part of the same safety surface.
    TEST_ASSERT_TRUE(webPathIsEstop("/api/estop/clear"));
}

void test_lookalike_paths_are_not_treated_as_estop() {
    TEST_ASSERT_FALSE(webPathIsEstop("/api/estopper"));
    TEST_ASSERT_FALSE(webPathIsEstop("/api/status"));
    TEST_ASSERT_FALSE(webPathIsEstop("/estop"));
    TEST_ASSERT_FALSE(webPathIsEstop(""));
}

void test_diagnostic_paths_are_recognised() {
    TEST_ASSERT_TRUE(webPathIsDiagnostic("/api/status"));
    TEST_ASSERT_TRUE(webPathIsDiagnostic("/api/profiler"));
    TEST_ASSERT_TRUE(webPathIsDiagnostic("/api/coredump"));
    TEST_ASSERT_TRUE(webPathIsDiagnostic("/api/events"));
    TEST_ASSERT_FALSE(webPathIsDiagnostic("/api/config"));
    TEST_ASSERT_FALSE(webPathIsDiagnostic("/index.html"));
}

void test_live_update_stream_is_the_only_long_lived_path() {
    TEST_ASSERT_TRUE(webPathIsLongLived("/api/events"));
    TEST_ASSERT_FALSE(webPathIsLongLived("/api/status"));
    TEST_ASSERT_FALSE(webPathIsLongLived("/api/eventsource"));
}

int main() {
    UNITY_BEGIN();

    RUN_TEST(test_bucket_starts_full_and_admits_a_whole_burst);
    RUN_TEST(test_bucket_paces_out_the_connection_after_the_burst);
    RUN_TEST(test_bucket_refills_at_the_configured_rate);
    RUN_TEST(test_bucket_refill_is_clamped_to_the_burst_size);
    RUN_TEST(test_bucket_survives_millis_wraparound);

    RUN_TEST(test_accept_admits_when_rate_and_heap_are_both_healthy);
    RUN_TEST(test_accept_rejects_below_the_heap_floor);
    RUN_TEST(test_accept_admits_exactly_at_the_heap_floor);
    RUN_TEST(test_accept_checks_rate_before_heap);
    RUN_TEST(test_paced_out_connection_never_samples_the_heap);
    RUN_TEST(test_admission_samples_the_heap_at_most_once);
    RUN_TEST(test_accept_does_not_spend_a_token_on_a_heap_rejection);

    RUN_TEST(test_unprimed_sample_is_due_rather_than_optimistic);
    RUN_TEST(test_sample_is_not_due_again_within_the_interval);
    RUN_TEST(test_sample_interval_survives_millis_wraparound);

    RUN_TEST(test_ordinary_request_is_admitted_when_healthy);
    RUN_TEST(test_estop_is_admitted_at_the_inflight_cap);
    RUN_TEST(test_estop_is_admitted_with_a_dead_heap);
    RUN_TEST(test_ordinary_request_is_refused_at_the_inflight_cap);
    RUN_TEST(test_long_lived_stream_is_not_counted_against_the_inflight_cap);
    RUN_TEST(test_ordinary_request_is_refused_below_its_floor);
    RUN_TEST(test_diagnostics_stay_reachable_below_the_ordinary_floor);
    RUN_TEST(test_diagnostics_are_not_exempt_from_every_floor);
    RUN_TEST(test_inflight_cap_is_checked_before_the_heap_floor);

    RUN_TEST(test_estop_paths_are_recognised);
    RUN_TEST(test_lookalike_paths_are_not_treated_as_estop);
    RUN_TEST(test_diagnostic_paths_are_recognised);
    RUN_TEST(test_live_update_stream_is_the_only_long_lived_path);

    return UNITY_END();
}
