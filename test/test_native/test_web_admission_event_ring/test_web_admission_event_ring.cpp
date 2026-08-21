// =============================================================================
// test/test_native/test_web_admission_trace/test_web_admission_trace.cpp
//
// Native unit tests for the admission decision ring.
//
// This ring exists to be read as evidence, so what is pinned here is the
// properties an argument about the floors would rest on: rows come back oldest
// first, a wrapped ring says how much it dropped rather than quietly losing it,
// a saturating field never wraps into a small plausible number, and the JSON is
// whole whichever chunk boundary it is sliced at. Getting any of those wrong
// produces a profile that reads as clean evidence while being wrong, which is
// worse than no profile.
//
// The eight-byte row packing is tested through the accessors and the document
// rather than by reading bits, so a re-pack that preserves the values does not
// have to be re-argued here.
// =============================================================================
#include <unity.h>

#include <cstdio>
#include <cstring>
#include <string>

#include "web_admission_event_ring.h"

static WebAdmissionTrace s_trace;

void setUp() {
    memset(&s_trace, 0, sizeof(s_trace));
    webAdmissionTraceInit(&s_trace);
}

void tearDown() {
}

// Records a row with the fields most tests do not care about held constant, so
// each test reads as the one thing it is about.
static void record(uint32_t ms, uint32_t block,
                   WebAdmissionTraceOutcome outcome = WebAdmissionTraceOutcome::kAdmit,
                   uint32_t ageMs = 0) {
    webAdmissionTraceRecord(&s_trace, ms, WebAdmissionTraceLayer::kRequest, outcome, block, block,
                            ageMs, 0, WebAdmissionTraceNavigation::kUnknown);
}

// Renders the whole document the way the route does, but in one window, so a
// test can assert on the text. The slice writer is what the device drives; this
// is the same call with an offset of zero and room for everything.
static std::string render() {
    static uint8_t buffer[65536];
    JsonSliceWriter writer(buffer, sizeof(buffer), 0);
    webAdmissionTraceWrite(&s_trace, writer);
    return std::string(reinterpret_cast<const char*>(buffer), writer.written());
}

static WebAdmissionTraceConfig calibration() {
    WebAdmissionTraceConfig config = {};
    config.connectionFloor = 8500;
    config.requestFloor = 9000;
    config.requestFloorDiagnostic = 7500;
    config.sampleIntervalMs = 100;
    config.maxInflightRequests = 6;
    return config;
}

// -----------------------------------------------------------------------------
// Ring arithmetic
// -----------------------------------------------------------------------------

static void test_a_fresh_ring_holds_nothing_and_says_so() {
    const std::string json = render();
    TEST_ASSERT_NOT_NULL(strstr(json.c_str(), "\"total\":0"));
    TEST_ASSERT_NOT_NULL(strstr(json.c_str(), "\"held\":0"));
    TEST_ASSERT_NOT_NULL(strstr(json.c_str(), "\"overwritten\":0"));
    TEST_ASSERT_NOT_NULL(strstr(json.c_str(), "\"rows\":[]"));
}

static void test_rows_come_back_oldest_first() {
    record(100, 14000);
    record(200, 13000);
    record(300, 12000);

    // Offsets are measured from the first row, and each reading has to stay
    // paired with its own offset: a profile read out of order is not a profile,
    // because the dip no longer lines up with the request that caused it.
    const std::string json = render();
    TEST_ASSERT_NOT_NULL(strstr(json.c_str(), "\"baseMs\":100"));
    const char* first = strstr(json.c_str(), "[0,1,14000,");
    const char* second = strstr(json.c_str(), "[100,1,13000,");
    const char* third = strstr(json.c_str(), "[200,1,12000,");
    TEST_ASSERT_NOT_NULL(first);
    TEST_ASSERT_NOT_NULL(second);
    TEST_ASSERT_NOT_NULL(third);
    TEST_ASSERT_TRUE(first < second);
    TEST_ASSERT_TRUE(second < third);
}

static void test_a_wrapped_ring_keeps_the_most_recent_rows() {
    // The dip lands at the end of a page load, so the tail is the half worth
    // keeping when the ring cannot hold everything. Each row carries a distinct
    // reading, so which rows survived is visible in the document.
    for (uint32_t i = 0; i < WEB_ADMISSION_TRACE_CAPACITY + 3; ++i) {
        record(i, 4 * (i + 1));
    }

    const std::string json = render();
    TEST_ASSERT_NULL(strstr(json.c_str(), ",4,4,"));
    TEST_ASSERT_NULL(strstr(json.c_str(), ",12,12,"));
    TEST_ASSERT_NOT_NULL(strstr(json.c_str(), ",16,16,"));

    char lastReading[40];
    snprintf(lastReading, sizeof(lastReading), ",%u,%u,",
             (unsigned)(4 * (WEB_ADMISSION_TRACE_CAPACITY + 3)),
             (unsigned)(4 * (WEB_ADMISSION_TRACE_CAPACITY + 3)));
    TEST_ASSERT_NOT_NULL(strstr(json.c_str(), lastReading));
}

static void test_a_wrapped_ring_publishes_what_it_dropped() {
    for (uint32_t i = 0; i < WEB_ADMISSION_TRACE_CAPACITY + 5; ++i) {
        record(i, 10000);
    }

    char expectedTotal[32];
    char expectedHeld[32];
    snprintf(expectedTotal, sizeof(expectedTotal), "\"total\":%u",
             (unsigned)(WEB_ADMISSION_TRACE_CAPACITY + 5));
    snprintf(expectedHeld, sizeof(expectedHeld), "\"held\":%u",
             (unsigned)WEB_ADMISSION_TRACE_CAPACITY);

    const std::string json = render();
    TEST_ASSERT_NOT_NULL(strstr(json.c_str(), expectedTotal));
    TEST_ASSERT_NOT_NULL(strstr(json.c_str(), expectedHeld));
    // A partial profile has to look partial. Reading a wrapped ring as a whole
    // page load is exactly the mistake this field exists to prevent.
    TEST_ASSERT_NOT_NULL(strstr(json.c_str(), "\"overwritten\":5"));
}

// -----------------------------------------------------------------------------
// Field fidelity
// -----------------------------------------------------------------------------

static void test_a_long_sample_age_saturates_rather_than_wrapping() {
    // The staleness question is the reason this ring exists. An age that
    // wrapped into a small number would read as a freshly taken sample, which
    // is the single reading that must never be wrong here.
    record(1000, 8000, WebAdmissionTraceOutcome::kRejectHeap, 900000);

    char saturated[16];
    snprintf(saturated, sizeof(saturated), ",%u,", (unsigned)kWebAdmissionTraceAgeSaturatedRaw);
    TEST_ASSERT_NOT_NULL(strstr(render().c_str(), saturated));
}

static void test_an_unprimed_cache_reads_as_very_old_not_as_fresh() {
    record(1000, 8000, WebAdmissionTraceOutcome::kRejectHeap, kWebAdmissionTraceAgeUnknown);

    char saturated[16];
    snprintf(saturated, sizeof(saturated), ",%u,", (unsigned)kWebAdmissionTraceAgeSaturatedRaw);
    TEST_ASSERT_NOT_NULL(strstr(render().c_str(), saturated));
}

static void test_an_age_below_the_ceiling_is_kept_exactly() {
    record(1000, 8000, WebAdmissionTraceOutcome::kRejectHeap, 97);
    TEST_ASSERT_NOT_NULL(strstr(render().c_str(), ",97,"));
}

static void test_the_used_and_fresh_readings_are_both_kept() {
    // The gap between them IS the measurement: a decision taken on a reading
    // the heap has already left behind is a different failure from one taken on
    // a reading that is still true.
    webAdmissionTraceRecord(&s_trace, 500, WebAdmissionTraceLayer::kRequest,
                            WebAdmissionTraceOutcome::kRejectHeap, 6132, 13840, 87, 3,
                            WebAdmissionTraceNavigation::kAsset);

    TEST_ASSERT_NOT_NULL(strstr(render().c_str(), "[0,1,6132,13840,87,2,3,0]"));
}

static void test_a_reading_is_kept_to_the_four_bytes_the_allocator_aligns_to() {
    // Readings are stored in 4-byte units. Nothing finer was ever
    // distinguishable in the heap, but the rounding has to go downward: a
    // reading rounded up could clear a floor the heap did not actually clear.
    record(0, 6135, WebAdmissionTraceOutcome::kRejectHeap);
    TEST_ASSERT_NOT_NULL(strstr(render().c_str(), "[0,1,6132,6132,"));
}

static void test_a_reading_beyond_the_field_saturates_rather_than_wrapping() {
    // A wrapped reading would land somewhere plausible and be believed. The
    // ceiling sits far above any block this controller sees, so a value at it
    // is self-evidently a ceiling rather than a measurement.
    record(0, kWebAdmissionTraceBlockMax + 4096);

    char ceiling[32];
    snprintf(ceiling, sizeof(ceiling), "[0,1,%u,", (unsigned)kWebAdmissionTraceBlockMax);
    TEST_ASSERT_NOT_NULL(strstr(render().c_str(), ceiling));
}

static void test_an_offset_beyond_the_field_saturates_rather_than_wrapping() {
    record(1000, 10000);
    record(1000 + kWebAdmissionTraceOffsetMax + 5000, 9000);

    char ceiling[32];
    snprintf(ceiling, sizeof(ceiling), "[%u,1,9000,", (unsigned)kWebAdmissionTraceOffsetMax);
    TEST_ASSERT_NOT_NULL(strstr(render().c_str(), ceiling));
}

static void test_offsets_survive_the_millisecond_counter_rolling_over() {
    // A capture that straddles the rollover must not read as forty-nine days of
    // elapsed time between two adjacent requests.
    record(UINT32_MAX - 100, 12000);
    record(150, 11000);  // 251 ms later, across the wrap

    TEST_ASSERT_NOT_NULL(strstr(render().c_str(), "[251,1,11000,"));
}

static void test_a_connection_layer_row_reads_as_the_connection_layer() {
    webAdmissionTraceRecord(&s_trace, 42, WebAdmissionTraceLayer::kConnection,
                            WebAdmissionTraceOutcome::kRejectHeap, 7412, 7412, 12, 0,
                            WebAdmissionTraceNavigation::kUnknown);

    const std::string json = render();
    TEST_ASSERT_NOT_NULL(strstr(json.c_str(), "[0,0,7412,7412,12,2,0,2]"));
    TEST_ASSERT_NOT_NULL(strstr(json.c_str(), "\"baseMs\":42"));
}

static void test_a_refused_navigation_is_distinguishable_from_a_refused_asset() {
    // The whole ticket turns on this distinction: an asset gets the cheap
    // close, a navigation is the one that must see the Busy Recovery Page.
    webAdmissionTraceRecord(&s_trace, 10, WebAdmissionTraceLayer::kRequest,
                            WebAdmissionTraceOutcome::kRejectHeap, 8000, 8000, 0, 2,
                            WebAdmissionTraceNavigation::kNavigation);
    webAdmissionTraceRecord(&s_trace, 20, WebAdmissionTraceLayer::kRequest,
                            WebAdmissionTraceOutcome::kRejectHeap, 8000, 8000, 0, 2,
                            WebAdmissionTraceNavigation::kAsset);

    const std::string json = render();
    TEST_ASSERT_NOT_NULL(strstr(json.c_str(), "[0,1,8000,8000,0,2,2,1]"));
    TEST_ASSERT_NOT_NULL(strstr(json.c_str(), "[10,1,8000,8000,0,2,2,0]"));
}

static void test_every_packed_field_round_trips_through_its_accessor() {
    // All four share one byte, so a shift or mask that overlaps would corrupt a
    // neighbour rather than itself -- the kind of error that leaves most rows
    // looking right.
    webAdmissionTraceRecord(&s_trace, 0, WebAdmissionTraceLayer::kRequest,
                            WebAdmissionTraceOutcome::kRejectInflight, 4096, 8192, 5, 6,
                            WebAdmissionTraceNavigation::kNavigation);

    const WebAdmissionTraceEntry& e = s_trace.entries[0];
    TEST_ASSERT_EQUAL(WebAdmissionTraceLayer::kRequest, webAdmissionTraceEntryLayer(e));
    TEST_ASSERT_EQUAL(WebAdmissionTraceOutcome::kRejectInflight, webAdmissionTraceEntryOutcome(e));
    TEST_ASSERT_EQUAL(WebAdmissionTraceNavigation::kNavigation,
                      webAdmissionTraceEntryNavigation(e));
    TEST_ASSERT_EQUAL_UINT(6, webAdmissionTraceEntryInflight(e));
    TEST_ASSERT_EQUAL_UINT32(4096, webAdmissionTraceEntryBlock(e));
    TEST_ASSERT_EQUAL_UINT32(8192, webAdmissionTraceEntryFresh(e));
}

// -----------------------------------------------------------------------------
// Slicing
// -----------------------------------------------------------------------------

static void test_the_document_is_identical_however_it_is_sliced() {
    // The device serves this through sendChunked(), which re-walks the body
    // once per chunk and keeps only its window. A producer that is not stable
    // across those calls splices bytes from two different documents together,
    // and the result still parses -- so nothing but this check would catch it.
    webAdmissionTraceConfigure(&s_trace, calibration());
    for (uint32_t i = 0; i < WEB_ADMISSION_TRACE_CAPACITY; ++i) {
        record(i * 7, 14000 - i * 100,
               i % 5 == 0 ? WebAdmissionTraceOutcome::kRejectHeap
                          : WebAdmissionTraceOutcome::kAdmit,
               i);
    }

    const std::string whole = render();

    for (size_t chunkSize : {(size_t)1, (size_t)7, (size_t)64, (size_t)1024}) {
        std::string reassembled;
        uint8_t chunk[1024];
        size_t offset = 0;
        for (;;) {
            JsonSliceWriter writer(chunk, chunkSize, offset);
            webAdmissionTraceWrite(&s_trace, writer);
            const size_t written = writer.written();
            if (written == 0) {
                break;
            }
            reassembled.append(reinterpret_cast<const char*>(chunk), written);
            offset += written;
        }
        TEST_ASSERT_EQUAL_STRING(whole.c_str(), reassembled.c_str());
    }
}

// -----------------------------------------------------------------------------
// Self-description
// -----------------------------------------------------------------------------

static void test_the_profile_carries_the_floors_it_was_taken_against() {
    // Every reading in this document only means something relative to these
    // numbers. A profile that has to be matched to a build by hand is a profile
    // that will eventually be matched to the wrong one.
    webAdmissionTraceConfigure(&s_trace, calibration());
    record(1, 10000);

    TEST_ASSERT_NOT_NULL(strstr(render().c_str(),
                                "\"floors\":{\"connection\":8500,\"request\":9000,"
                                "\"requestDiagnostic\":7500,\"sampleIntervalMs\":100,"
                                "\"maxInflightRequests\":6}"));
}

static void test_arming_the_ring_keeps_the_calibration() {
    // Clearing between runs is the normal way to take a profile, so a clear
    // that dropped the labels would leave every captured run unlabelled.
    webAdmissionTraceConfigure(&s_trace, calibration());
    record(1, 10000);
    webAdmissionTraceInit(&s_trace);

    const std::string json = render();
    TEST_ASSERT_NOT_NULL(strstr(json.c_str(), "\"connection\":8500"));
    TEST_ASSERT_NOT_NULL(strstr(json.c_str(), "\"total\":0"));
}

static void test_the_legend_names_every_column_a_row_carries() {
    record(1, 10000);
    const std::string json = render();
    // Rows are positional arrays, so the legend is the only thing that makes
    // them readable. A column added without its name here produces a profile
    // nobody can interpret six months later.
    TEST_ASSERT_NOT_NULL(
        strstr(json.c_str(), "\"fields\":[\"msOffset\",\"layer\",\"block\",\"fresh\",\"ageMs\","
                             "\"outcome\",\"inflight\",\"nav\"]"));
    TEST_ASSERT_NOT_NULL(strstr(json.c_str(), "\"layers\":[\"conn\",\"req\"]"));
    TEST_ASSERT_NOT_NULL(
        strstr(json.c_str(), "\"outcomes\":[\"admit\",\"rate\",\"heap\",\"inflight\"]"));
    TEST_ASSERT_NOT_NULL(strstr(json.c_str(), "\"nav\":[\"asset\",\"navigation\",\"unknown\"]"));
    // Rows carry no path, and the document has to say so: a reader who assumed
    // otherwise would silently mis-join the harness's request log onto it.
    TEST_ASSERT_NOT_NULL(strstr(json.c_str(), "\"pathsInRows\":false"));
}

int main() {
    UNITY_BEGIN();

    RUN_TEST(test_a_fresh_ring_holds_nothing_and_says_so);
    RUN_TEST(test_rows_come_back_oldest_first);
    RUN_TEST(test_a_wrapped_ring_keeps_the_most_recent_rows);
    RUN_TEST(test_a_wrapped_ring_publishes_what_it_dropped);

    RUN_TEST(test_a_long_sample_age_saturates_rather_than_wrapping);
    RUN_TEST(test_an_unprimed_cache_reads_as_very_old_not_as_fresh);
    RUN_TEST(test_an_age_below_the_ceiling_is_kept_exactly);
    RUN_TEST(test_the_used_and_fresh_readings_are_both_kept);
    RUN_TEST(test_a_reading_is_kept_to_the_four_bytes_the_allocator_aligns_to);
    RUN_TEST(test_a_reading_beyond_the_field_saturates_rather_than_wrapping);
    RUN_TEST(test_an_offset_beyond_the_field_saturates_rather_than_wrapping);
    RUN_TEST(test_offsets_survive_the_millisecond_counter_rolling_over);
    RUN_TEST(test_a_connection_layer_row_reads_as_the_connection_layer);
    RUN_TEST(test_a_refused_navigation_is_distinguishable_from_a_refused_asset);
    RUN_TEST(test_every_packed_field_round_trips_through_its_accessor);

    RUN_TEST(test_the_document_is_identical_however_it_is_sliced);

    RUN_TEST(test_the_profile_carries_the_floors_it_was_taken_against);
    RUN_TEST(test_arming_the_ring_keeps_the_calibration);
    RUN_TEST(test_the_legend_names_every_column_a_row_carries);

    return UNITY_END();
}
