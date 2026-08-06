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

#include <cstring>

#include "web_admission.h"
#include "web_busy_page.h"

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

void test_upload_paths_are_ordinary_work_and_get_no_exemption() {
    // An OTA upload must face the same cap and the same floor as any other
    // request. Exempting it would make the admission evidence for an upload
    // round-trip meaningless -- the upload would be proving nothing about the
    // policy it is supposed to be running under.
    const char* uploads[] = {"/upload/firmware", "/upload/filesystem"};
    for (unsigned i = 0; i < 2; i++) {
        TEST_ASSERT_FALSE(webPathIsEstop(uploads[i]));
        TEST_ASSERT_FALSE(webPathIsDiagnostic(uploads[i]));
        TEST_ASSERT_FALSE(webPathIsLongLived(uploads[i]));
    }
}

void test_an_upload_is_refused_under_pressure_like_any_other_request() {
    // The corollary of the classification above, stated as behaviour: an
    // upload arriving below the ordinary floor is refused, and it is refused
    // by the ordinary floor rather than the lower diagnostic one.
    WebRequestAdmissionInputs in = healthyInputs();
    in.estop = webPathIsEstop("/upload/firmware");
    in.diagnostic = webPathIsDiagnostic("/upload/firmware");
    in.longLived = webPathIsLongLived("/upload/firmware");
    in.largestFreeBlock = kFloor - 1;

    TEST_ASSERT_EQUAL(WebRequestAdmission::kRejectHeapFloor, webRequestAdmissionDecide(in));

    // ...and admitted once the ordinary floor is cleared.
    in.largestFreeBlock = kFloor;
    TEST_ASSERT_EQUAL(WebRequestAdmission::kAdmit, webRequestAdmissionDecide(in));
}

// -----------------------------------------------------------------------------
// Main-frame navigation detection
// -----------------------------------------------------------------------------

void test_browser_navigation_is_recognised_from_sec_fetch_mode() {
    // What a browser sends when the operator opens or refreshes a page.
    TEST_ASSERT_TRUE(webIsMainFrameNavigation(
        "navigate", "text/html,application/xhtml+xml,application/xml;q=0.9,*/*;q=0.8"));
}

void test_a_page_fetching_its_own_resources_is_not_a_navigation() {
    // Scripts, styles and API calls from an already-loaded page. These are the
    // ones that must keep shedding cheaply.
    TEST_ASSERT_FALSE(webIsMainFrameNavigation("cors", "*/*"));
    TEST_ASSERT_FALSE(webIsMainFrameNavigation("no-cors", "*/*"));
    TEST_ASSERT_FALSE(webIsMainFrameNavigation("same-origin", "application/json"));
}

void test_sec_fetch_mode_wins_over_a_misleading_accept_header() {
    // A same-origin fetch may still advertise text/html. The explicit mode is
    // the authoritative signal, so it must not be second-guessed.
    TEST_ASSERT_FALSE(webIsMainFrameNavigation("same-origin", "text/html"));
}

void test_accept_is_the_fallback_when_sec_fetch_mode_is_absent() {
    TEST_ASSERT_TRUE(webIsMainFrameNavigation(nullptr, "text/html,*/*;q=0.8"));
    TEST_ASSERT_TRUE(webIsMainFrameNavigation("", "text/html"));
    TEST_ASSERT_FALSE(webIsMainFrameNavigation(nullptr, "application/json"));
    TEST_ASSERT_FALSE(webIsMainFrameNavigation("", "*/*"));
}

void test_an_unknown_client_gets_the_cheap_path() {
    // No headers at all: treat as an asset. Guessing "navigation" here would
    // spend the recovery response on a client that will never render it.
    TEST_ASSERT_FALSE(webIsMainFrameNavigation(nullptr, nullptr));
    TEST_ASSERT_FALSE(webIsMainFrameNavigation("", ""));
}

// -----------------------------------------------------------------------------
// Busy Recovery Page wire contract
// -----------------------------------------------------------------------------

void test_busy_response_declares_the_status_the_frontend_branches_on() {
    // data/page_bootstrap.js classifies exactly 503 as "busy"; anything else
    // becomes "no-response" and the operator is told the wrong thing.
    TEST_ASSERT_EQUAL_STRING_LEN("HTTP/1.1 503 Service Unavailable\r\n", kBusyRecoveryResponse, 34);
}

void test_busy_response_carries_the_retry_interval_the_page_counts_down() {
    // data/web_api.js reads Retry-After to drive the retry timing, and the
    // page's own countdown is baked from the same constant.
    TEST_ASSERT_NOT_NULL(strstr(kBusyRecoveryResponse, "Retry-After: 5\r\n"));
}

void test_busy_response_content_length_matches_the_body_it_describes() {
    // The whole reason Content-Length is a literal guarded by a static_assert:
    // a wrong one truncates the page or hangs the browser waiting for bytes
    // that never come.
    const char* separator = strstr(kBusyRecoveryResponse, "\r\n\r\n");
    TEST_ASSERT_NOT_NULL(separator);
    const size_t bodyOffset = (size_t)(separator - kBusyRecoveryResponse) + 4;
    const size_t actualBody = kBusyRecoveryResponseLength - bodyOffset;

    TEST_ASSERT_EQUAL_UINT32(PA_BUSY_BODY_LENGTH, actualBody);
    TEST_ASSERT_NOT_NULL(strstr(kBusyRecoveryResponse, "Content-Length: 2327\r\n"));
}

void test_busy_response_is_a_self_contained_page() {
    // It is served precisely when the controller could not serve the page's
    // own resources, so depending on any of them would guarantee a blank
    // screen. No external stylesheet, script or image may appear.
    TEST_ASSERT_NULL(strstr(kBusyRecoveryResponse, "/style.css"));
    TEST_ASSERT_NULL(strstr(kBusyRecoveryResponse, "src=\""));
    TEST_ASSERT_NULL(strstr(kBusyRecoveryResponse, "<link"));
}

void test_busy_response_tells_the_operator_it_is_busy_and_offers_a_retry() {
    // CONTEXT.md reserves "Controller busy" for an explicit refusal, which is
    // exactly what this response is; and Page Recovery View requires a working
    // Retry now action that does not depend on the failed resources.
    TEST_ASSERT_NOT_NULL(strstr(kBusyRecoveryResponse, "Controller busy"));
    TEST_ASSERT_NOT_NULL(strstr(kBusyRecoveryResponse, "Retry now"));
    TEST_ASSERT_NOT_NULL(strstr(kBusyRecoveryResponse, "location.reload()"));
}

void test_busy_response_closes_the_connection_it_is_shedding() {
    TEST_ASSERT_NOT_NULL(strstr(kBusyRecoveryResponse, "Connection: close\r\n"));
    TEST_ASSERT_NOT_NULL(strstr(kBusyRecoveryResponse, "Cache-Control: no-store\r\n"));
}

// -----------------------------------------------------------------------------
// Socket census
// -----------------------------------------------------------------------------
//
// What is worth pinning is that the census cannot be made to lie by the two
// things the device actually does to it: refuse connections, and reuse them.
// The occupancy number is the input to a decision about max_open_sockets, so a
// count that drifts under refusal would argue for the wrong budget.

void test_census_starts_empty() {
    WebSocketCensus census;
    webSocketCensusInit(&census);

    TEST_ASSERT_EQUAL_INT(0, census.open);
    TEST_ASSERT_EQUAL_INT(0, census.openPeak);
    TEST_ASSERT_EQUAL_UINT32(0u, census.accepted);
    TEST_ASSERT_EQUAL_UINT32(0u, census.requests);
    TEST_ASSERT_EQUAL_UINT32(0u, census.untracked);
}

void test_census_counts_open_sockets_and_remembers_the_peak() {
    WebSocketCensus census;
    webSocketCensusInit(&census);

    TEST_ASSERT_TRUE(webSocketCensusOpen(&census, 4));
    TEST_ASSERT_TRUE(webSocketCensusOpen(&census, 5));
    TEST_ASSERT_TRUE(webSocketCensusOpen(&census, 6));
    TEST_ASSERT_EQUAL_INT(3, census.open);

    TEST_ASSERT_TRUE(webSocketCensusClose(&census, 5));
    TEST_ASSERT_EQUAL_INT(2, census.open);

    // The peak is what the socket budget has to cover, so it must survive the
    // dip that follows it.
    TEST_ASSERT_EQUAL_INT(3, census.openPeak);
    TEST_ASSERT_EQUAL_UINT32(3u, census.accepted);
}

void test_a_refused_connection_does_not_underflow_the_occupancy_count() {
    // The server calls its close callback for a socket the guard refused --
    // httpd_sess_new() routes an open_fn failure through httpd_sess_delete().
    // A bare decrement would go negative by exactly the refusal count, and
    // refusals happen in the pressure windows where occupancy matters most.
    WebSocketCensus census;
    webSocketCensusInit(&census);

    TEST_ASSERT_TRUE(webSocketCensusOpen(&census, 7));
    TEST_ASSERT_FALSE(webSocketCensusClose(&census, 9));

    TEST_ASSERT_EQUAL_INT(1, census.open);
    TEST_ASSERT_EQUAL_UINT32(1u, census.accepted);
}

void test_closing_the_same_socket_twice_only_counts_once() {
    WebSocketCensus census;
    webSocketCensusInit(&census);

    webSocketCensusOpen(&census, 3);
    TEST_ASSERT_TRUE(webSocketCensusClose(&census, 3));
    TEST_ASSERT_FALSE(webSocketCensusClose(&census, 3));

    TEST_ASSERT_EQUAL_INT(0, census.open);
}

void test_a_reused_descriptor_is_a_new_connection() {
    // lwIP hands the same fd back once it is free. Two connections that happen
    // to reuse a number are still two connections, and churn counts them both.
    WebSocketCensus census;
    webSocketCensusInit(&census);

    webSocketCensusOpen(&census, 8);
    webSocketCensusClose(&census, 8);
    webSocketCensusOpen(&census, 8);

    TEST_ASSERT_EQUAL_UINT32(2u, census.accepted);
    TEST_ASSERT_EQUAL_INT(1, census.open);
    TEST_ASSERT_EQUAL_INT(1, census.openPeak);
}

void test_requests_and_connections_are_counted_separately() {
    // Their ratio is the whole keep-alive measurement: one request per socket
    // means the stack closes per response, many means it reuses.
    WebSocketCensus census;
    webSocketCensusInit(&census);

    webSocketCensusOpen(&census, 4);
    for (int i = 0; i < 12; ++i) {
        webSocketCensusRequest(&census);
    }

    TEST_ASSERT_EQUAL_UINT32(1u, census.accepted);
    TEST_ASSERT_EQUAL_UINT32(12u, census.requests);
}

void test_census_reports_rather_than_hides_an_overflow() {
    // The capacity sits above the server's max_open_sockets, so filling it
    // should be impossible in practice. If it ever happens the reading is an
    // undercount, and saying so is what distinguishes "raise the capacity"
    // from "the stack lost sockets".
    WebSocketCensus census;
    webSocketCensusInit(&census);

    for (int fd = 0; fd < WEB_SOCKET_CENSUS_CAPACITY; ++fd) {
        TEST_ASSERT_TRUE(webSocketCensusOpen(&census, fd));
    }
    TEST_ASSERT_FALSE(webSocketCensusOpen(&census, WEB_SOCKET_CENSUS_CAPACITY));

    TEST_ASSERT_EQUAL_INT(WEB_SOCKET_CENSUS_CAPACITY, census.open);
    TEST_ASSERT_EQUAL_UINT32((uint32_t)WEB_SOCKET_CENSUS_CAPACITY + 1u, census.accepted);
    TEST_ASSERT_EQUAL_UINT32(1u, census.untracked);
}

void test_census_ignores_a_descriptor_that_is_not_a_socket() {
    // -1 is the free-slot sentinel. Admitting one would both corrupt the set
    // and make every later close match the wrong slot.
    WebSocketCensus census;
    webSocketCensusInit(&census);

    TEST_ASSERT_FALSE(webSocketCensusOpen(&census, -1));
    TEST_ASSERT_FALSE(webSocketCensusClose(&census, -1));

    TEST_ASSERT_EQUAL_INT(0, census.open);
    TEST_ASSERT_EQUAL_UINT32(0u, census.accepted);
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
    RUN_TEST(test_upload_paths_are_ordinary_work_and_get_no_exemption);
    RUN_TEST(test_an_upload_is_refused_under_pressure_like_any_other_request);

    RUN_TEST(test_browser_navigation_is_recognised_from_sec_fetch_mode);
    RUN_TEST(test_a_page_fetching_its_own_resources_is_not_a_navigation);
    RUN_TEST(test_sec_fetch_mode_wins_over_a_misleading_accept_header);
    RUN_TEST(test_accept_is_the_fallback_when_sec_fetch_mode_is_absent);
    RUN_TEST(test_an_unknown_client_gets_the_cheap_path);

    RUN_TEST(test_busy_response_declares_the_status_the_frontend_branches_on);
    RUN_TEST(test_busy_response_carries_the_retry_interval_the_page_counts_down);
    RUN_TEST(test_busy_response_content_length_matches_the_body_it_describes);
    RUN_TEST(test_busy_response_is_a_self_contained_page);
    RUN_TEST(test_busy_response_tells_the_operator_it_is_busy_and_offers_a_retry);
    RUN_TEST(test_busy_response_closes_the_connection_it_is_shedding);

    RUN_TEST(test_census_starts_empty);
    RUN_TEST(test_census_counts_open_sockets_and_remembers_the_peak);
    RUN_TEST(test_a_refused_connection_does_not_underflow_the_occupancy_count);
    RUN_TEST(test_closing_the_same_socket_twice_only_counts_once);
    RUN_TEST(test_a_reused_descriptor_is_a_new_connection);
    RUN_TEST(test_requests_and_connections_are_counted_separately);
    RUN_TEST(test_census_reports_rather_than_hides_an_overflow);
    RUN_TEST(test_census_ignores_a_descriptor_that_is_not_a_socket);

    return UNITY_END();
}
