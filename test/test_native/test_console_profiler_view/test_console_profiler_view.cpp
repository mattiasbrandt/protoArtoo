// =============================================================================
// test/test_native/test_console_profiler_view/test_console_profiler_view.cpp
//
// Native tests for the Console rendering of a profiler reading (#224,
// include/console_profiler_view.h).
//
// The measurement itself is ESP32-only: profilerRead() lives inside
// #if PA_HEAP_PROFILE and is not in this binary. What IS in this binary, and
// what an operator actually reads, is the rendering - which fields appear,
// what they are named, what the item lines look like, and the measurement
// disclosure the ticket's acceptance criterion asks for. That half is fed a
// synthetic ProfilerReading here and asserted directly.
//
// Field names are checked against the /api/profiler JSON keys they mirror
// (docs/console-protocol.md s.3.5). The JSON builder itself cannot be linked
// natively - src/web/api_profiler.cpp is wholly inside #if PA_HEAP_PROFILE -
// so the keys are cited from it by name rather than read out of it, the same
// two-legs-plus-a-citation shape test_console_module.cpp already uses for
// dome.status.current.
// =============================================================================
#include <unity.h>

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "console_profiler_view.h"

// =============================================================================
// Capture sink
// =============================================================================

struct CapturedField {
    std::string name;
    std::string value;
};

static std::vector<CapturedField> g_fields;
static std::vector<std::string> g_items;
static bool g_endCalled;
static ConsoleStatus g_status;
static ConsoleOutcome g_outcome;
static ConsoleReason g_reason;

static void capField(uint32_t, const char* name, const char* value) {
    g_fields.push_back({name, value});
}
static void capItem(uint32_t, const char* value) {
    g_items.push_back(value);
}
static void capEnd(uint32_t, ConsoleStatus status, ConsoleOutcome outcome, ConsoleReason reason) {
    g_endCalled = true;
    g_status = status;
    g_outcome = outcome;
    g_reason = reason;
}

static ConsoleRecordSink makeSink() {
    ConsoleRecordSink sink = {};
    sink.onRecordField = capField;
    sink.onRecordItem = capItem;
    sink.onRecordEnd = capEnd;
    return sink;
}

static void resetCapture() {
    g_fields.clear();
    g_items.clear();
    g_endCalled = false;
    g_status = CONSOLE_STATUS_ERR;
    g_outcome = CONSOLE_OUTCOME_INTERNAL_ERROR;
    g_reason = CONSOLE_REASON_NONE;
}

static const char* fieldValue(const char* name) {
    for (const CapturedField& f : g_fields) {
        if (f.name == name) return f.value.c_str();
    }
    return nullptr;
}

static bool hasField(const char* name) {
    return fieldValue(name) != nullptr;
}

// The first item whose leading token matches `kind`, or nullptr.
static const char* itemOfKind(const char* kind, int nth = 0) {
    const size_t kindLen = strlen(kind);
    int seen = 0;
    for (const std::string& item : g_items) {
        if (item.compare(0, kindLen, kind) == 0 && item[kindLen] == ' ') {
            if (seen == nth) return item.c_str();
            seen++;
        }
    }
    return nullptr;
}

// =============================================================================
// A synthetic reading. Values are distinguishable from each other on purpose:
// a renderer that pairs the right key with the wrong member is only caught
// when no two members share a value.
// =============================================================================

static ProfilerReading makeReading() {
    ProfilerReading r = {};
    r.heapFree = 42120;
    r.heapMin = 31840;
    r.heapLargest = 21060;
    r.fragRatio = 0.5f;
    r.allocBlocks = 610;
    r.freeBlocks = 12;
    r.totalBlocks = 622;
    r.windowMinFree = 30112;

    r.currentWindowOpen = true;
    snprintf(r.currentWindowLabel, sizeof(r.currentWindowLabel), "%s", "audio_play");

    r.windowCount = 2;
    snprintf(r.windows[0].label, sizeof(r.windows[0].label), "%s", "boot");
    r.windows[0].heapMinDuring = 29001;
    r.windows[0].largestBlockAtClose = 19002;
    r.windows[0].windowOpenTs = 1003;
    snprintf(r.windows[1].label, sizeof(r.windows[1].label), "%s", "sse_connect");
    r.windows[1].heapMinDuring = 28004;
    r.windows[1].largestBlockAtClose = 18005;
    r.windows[1].windowOpenTs = 2006;

    r.taskStacks[0].name = "DriveTask";
    r.taskStacks[0].hwmBytes = 3072;  // > 2048 -> ok
    r.taskStacks[0].found = true;
    r.taskStacks[1].name = "AudioTask";
    r.taskStacks[1].hwmBytes = 1536;  // 1024..2048 -> warn
    r.taskStacks[1].found = true;
    r.taskStacks[2].name = "SeqDisp";
    r.taskStacks[2].hwmBytes = 512;  // <= 1024 -> crit
    r.taskStacks[2].found = true;
    r.taskStacks[3].name = "DomeLinkTask";
    r.taskStacks[3].hwmBytes = 0;
    r.taskStacks[3].found = false;  // not running in this image
    return r;
}

// A two-entry request-lifecycle ring, supplied the way the real ring is:
// count plus an oldest-first indexed reader.
static bool fakeTraceAt(size_t index, ProfilerRequestTrace* out) {
    static const char* kPaths[] = {"/api/health", "/api/status"};
    if (index >= 2) return false;
    snprintf(out->path, sizeof(out->path), "%s", kPaths[index]);
    out->startMs = (uint32_t)(1000 + index * 10);
    out->handlerDoneMs = (uint32_t)(1004 + index * 10);
    return true;
}

static void render(const ProfilerReading& r, size_t traceCount,
                   bool (*traceAt)(size_t, ProfilerRequestTrace*)) {
    resetCapture();
    ConsoleRecordSink sink = makeSink();
    consoleEmitProfilerReading(7, r, traceCount, traceAt, &sink);
}

// =============================================================================
// Tests
// =============================================================================

// The scalar half. Names are the /api/profiler JSON keys verbatim
// (src/web/api_profiler.cpp buildProfilerJson: heapFree, heapMin, heapLargest,
// fragRatio, allocBlocks, freeBlocks, totalBlocks, failedAllocs), so a serial
// transcript and the REST response name one measurement one way.
void test_scalar_fields_use_the_api_json_keys_and_carry_the_reading() {
    ProfilerReading r = makeReading();
    render(r, 0, nullptr);

    TEST_ASSERT_EQUAL_STRING("42120", fieldValue("heapFree"));
    TEST_ASSERT_EQUAL_STRING("31840", fieldValue("heapMin"));
    TEST_ASSERT_EQUAL_STRING("21060", fieldValue("heapLargest"));
    TEST_ASSERT_EQUAL_STRING("0.500", fieldValue("fragRatio"));
    TEST_ASSERT_EQUAL_STRING("610", fieldValue("allocBlocks"));
    TEST_ASSERT_EQUAL_STRING("12", fieldValue("freeBlocks"));
    TEST_ASSERT_EQUAL_STRING("622", fieldValue("totalBlocks"));
    TEST_ASSERT_EQUAL_STRING("0", fieldValue("failedAllocs"));
}

// The answer completes synchronously - nothing was queued and nothing changed
// (docs/console-protocol.md s.3.3).
void test_reading_ends_completed_with_no_reason() {
    render(makeReading(), 0, nullptr);

    TEST_ASSERT_TRUE(g_endCalled);
    TEST_ASSERT_EQUAL(CONSOLE_STATUS_OK, g_status);
    TEST_ASSERT_EQUAL(CONSOLE_OUTCOME_COMPLETED, g_outcome);
    TEST_ASSERT_EQUAL(CONSOLE_REASON_NONE, g_reason);
}

// #224 acceptance criterion 2: profiling output discloses that serial/log
// traffic perturbs the measurement. One stable field, always present, quoted
// because it contains spaces (docs/console-protocol.md s.3.5).
void test_output_discloses_that_console_traffic_perturbs_the_measurement() {
    render(makeReading(), 0, nullptr);

    const char* note = fieldValue("measurement_note");
    TEST_ASSERT_NOT_NULL_MESSAGE(note, "the measurement disclosure must always be present");
    TEST_ASSERT_EQUAL_MESSAGE('"', note[0], "a value with spaces is quoted");
    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(note, "serial console"),
                                 "the disclosure must name serial traffic");
    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(note, "log level"),
                                 "the disclosure must name log traffic");
}

// The disclosure is a property of the measurement, not of one value, so it is
// there whether or not anything interesting happened.
void test_the_disclosure_is_present_on_an_empty_reading() {
    ProfilerReading empty = {};
    render(empty, 0, nullptr);

    TEST_ASSERT_NOT_NULL(fieldValue("measurement_note"));
}

// lastFail* mirrors the JSON's "lastFail" object, which is omitted entirely
// when nothing has failed. A zero lastFailSize would read as a zero-byte
// allocation failing.
void test_last_failed_allocation_fields_are_absent_when_nothing_failed() {
    ProfilerReading r = makeReading();
    r.failedAllocs = 0;
    render(r, 0, nullptr);

    TEST_ASSERT_FALSE(hasField("lastFailSize"));
    TEST_ASSERT_FALSE(hasField("lastFailCaps"));
    TEST_ASSERT_FALSE(hasField("lastFailBt"));
}

void test_last_failed_allocation_fields_carry_the_backtrace_when_one_failed() {
    ProfilerReading r = makeReading();
    r.failedAllocs = 3;
    r.lastFailSize = 64;
    r.lastFailCaps = 4098;
    r.lastFailBtDepth = 2;
    r.lastFailBt[0] = 0x400d1234;
    r.lastFailBt[1] = 0x400d5678;
    render(r, 0, nullptr);

    TEST_ASSERT_EQUAL_STRING("3", fieldValue("failedAllocs"));
    TEST_ASSERT_EQUAL_STRING("64", fieldValue("lastFailSize"));
    TEST_ASSERT_EQUAL_STRING("4098", fieldValue("lastFailCaps"));
    TEST_ASSERT_EQUAL_STRING("0x400d1234,0x400d5678", fieldValue("lastFailBt"));
}

// RISC-V records no backtrace (esp_backtrace_* is Xtensa-only,
// src/web/api_profiler.cpp). The field is still emitted, so "no backtrace"
// is visible rather than indistinguishable from a dropped field.
void test_an_empty_backtrace_still_emits_the_field() {
    ProfilerReading r = makeReading();
    r.failedAllocs = 1;
    r.lastFailBtDepth = 0;
    render(r, 0, nullptr);

    TEST_ASSERT_TRUE(hasField("lastFailBt"));
    TEST_ASSERT_EQUAL_STRING("", fieldValue("lastFailBt"));
}

// Item lines: leading token names the JSON array/object the line comes from,
// then key:value pairs - the shape dome.api.list-sequences already uses, with
// colons so an item never looks like a record's own key=value pairs.
void test_task_stacks_render_as_items_with_the_shared_status_thresholds() {
    render(makeReading(), 0, nullptr);

    TEST_ASSERT_EQUAL_STRING("taskStack name:DriveTask hwmBytes:3072 status:ok",
                             itemOfKind("taskStack", 0));
    TEST_ASSERT_EQUAL_STRING("taskStack name:AudioTask hwmBytes:1536 status:warn",
                             itemOfKind("taskStack", 1));
    TEST_ASSERT_EQUAL_STRING("taskStack name:SeqDisp hwmBytes:512 status:crit",
                             itemOfKind("taskStack", 2));
}

// A task that is not running in this image is skipped, exactly as the JSON
// skips it - and this is the case api_profiler.cpp's own comment calls out as
// the expensive misreading, so it is asserted rather than assumed.
void test_a_task_that_is_not_running_is_not_listed() {
    render(makeReading(), 0, nullptr);

    for (const std::string& item : g_items) {
        TEST_ASSERT_NULL_MESSAGE(strstr(item.c_str(), "DomeLinkTask"),
                                 "a task with found=false must not be listed");
    }
}

void test_open_and_closed_mode_windows_render_as_distinct_item_kinds() {
    render(makeReading(), 0, nullptr);

    TEST_ASSERT_EQUAL_STRING("currentWindow label:audio_play heapFree:30112",
                             itemOfKind("currentWindow"));
    TEST_ASSERT_EQUAL_STRING("window label:boot heapFree:29001 largestBlock:19002 ts:1003",
                             itemOfKind("window", 0));
    TEST_ASSERT_EQUAL_STRING("window label:sse_connect heapFree:28004 largestBlock:18005 ts:2006",
                             itemOfKind("window", 1));
}

void test_no_open_window_emits_no_current_window_item() {
    ProfilerReading r = makeReading();
    r.currentWindowOpen = false;
    render(r, 0, nullptr);

    TEST_ASSERT_NULL(itemOfKind("currentWindow"));
}

// The ring is streamed through the indexed reader, oldest first, so the
// Console never holds all 32 entries on its stack.
void test_request_trace_streams_through_the_indexed_reader() {
    render(makeReading(), 2, fakeTraceAt);

    TEST_ASSERT_EQUAL_STRING("requestTrace path:/api/health startMs:1000 handlerDoneMs:1004",
                             itemOfKind("requestTrace", 0));
    TEST_ASSERT_EQUAL_STRING("requestTrace path:/api/status startMs:1010 handlerDoneMs:1014",
                             itemOfKind("requestTrace", 1));
}

void test_a_null_trace_reader_emits_no_request_trace_items() {
    render(makeReading(), 5, nullptr);

    TEST_ASSERT_NULL(itemOfKind("requestTrace"));
}

// Every rendered item must fit its buffer, or snprintf silently truncates the
// line an operator is reading. The widest shape is a full-length request path.
void test_the_widest_item_line_fits_its_buffer() {
    ProfilerReading r = {};
    r.taskStacks[0].name = "SafetyMonitor";
    r.taskStacks[0].hwmBytes = 4294967295U;
    r.taskStacks[0].found = true;
    r.currentWindowOpen = true;
    memset(r.currentWindowLabel, 'w', sizeof(r.currentWindowLabel) - 1);
    r.windowMinFree = 4294967295U;
    r.windowCount = 1;
    memset(r.windows[0].label, 'x', sizeof(r.windows[0].label) - 1);
    r.windows[0].heapMinDuring = 4294967295U;
    r.windows[0].largestBlockAtClose = 4294967295U;
    r.windows[0].windowOpenTs = 4294967295U;

    render(r, 1, [](size_t, ProfilerRequestTrace* out) {
        memset(out->path, 'p', sizeof(out->path) - 1);
        out->path[sizeof(out->path) - 1] = '\0';
        out->startMs = 4294967295U;
        out->handlerDoneMs = 4294967295U;
        return true;
    });

    TEST_ASSERT_GREATER_THAN(0, (int)g_items.size());
    for (const std::string& item : g_items) {
        TEST_ASSERT_LESS_THAN_MESSAGE((size_t)CONSOLE_PROFILER_ITEM_MAX, item.size() + 1,
                                      item.c_str());
    }
}

// A record line is CONSOLE_RECORD_LINE_MAX bytes on serial
// (include/console_record.h, src/tasks/console_task.cpp), and the disclosure
// is the longest value this query emits. If it ever outgrows the line the
// serial adapter drops the record silently - so the budget is asserted, not
// eyeballed.
void test_the_disclosure_fits_a_serial_record_line() {
    render(makeReading(), 0, nullptr);

    const char* note = fieldValue("measurement_note");
    TEST_ASSERT_NOT_NULL(note);
    // "< id=4294967295 type=field name=measurement_note value=" + the value
    const size_t rendered = strlen("< id=4294967295 type=field name=measurement_note value=") +
                            strlen(note);
    TEST_ASSERT_LESS_THAN_MESSAGE((size_t)CONSOLE_RECORD_LINE_MAX, rendered + 1, note);
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_scalar_fields_use_the_api_json_keys_and_carry_the_reading);
    RUN_TEST(test_reading_ends_completed_with_no_reason);
    RUN_TEST(test_output_discloses_that_console_traffic_perturbs_the_measurement);
    RUN_TEST(test_the_disclosure_is_present_on_an_empty_reading);
    RUN_TEST(test_last_failed_allocation_fields_are_absent_when_nothing_failed);
    RUN_TEST(test_last_failed_allocation_fields_carry_the_backtrace_when_one_failed);
    RUN_TEST(test_an_empty_backtrace_still_emits_the_field);
    RUN_TEST(test_task_stacks_render_as_items_with_the_shared_status_thresholds);
    RUN_TEST(test_a_task_that_is_not_running_is_not_listed);
    RUN_TEST(test_open_and_closed_mode_windows_render_as_distinct_item_kinds);
    RUN_TEST(test_no_open_window_emits_no_current_window_item);
    RUN_TEST(test_request_trace_streams_through_the_indexed_reader);
    RUN_TEST(test_a_null_trace_reader_emits_no_request_trace_items);
    RUN_TEST(test_the_widest_item_line_fits_its_buffer);
    RUN_TEST(test_the_disclosure_fits_a_serial_record_line);
    return UNITY_END();
}
