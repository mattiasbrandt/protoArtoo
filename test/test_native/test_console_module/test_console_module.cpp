// =============================================================================
// test/test_native/test_console_module/test_console_module.cpp
//
// Native tests for the Console module's status-query executors (#223, ADR
// 0034). Every type: status entry that carries a registry fields: list is
// driven through consoleExecuteCommand() directly - the same entry point both
// adapters call - never through a REST handler, and never through JSON
// produced then reparsed.
//
// The load-bearing assertion (#223 acceptance criterion 1): for every query,
// the registry's fields: list (read from the generated catalog), the real
// JSON builder's key set, and the record emitter's field names all agree.
// Three legs, checked pairwise so a single drifted name in any one of them
// fails a test, not a silent mismatch two of the three would hide.
//
// One entry, dome.status.current, cannot get the JSON-builder leg natively:
// its builder is buildStatusJson() in src/web/web_server.cpp, which is not in
// [env:native]'s build_src_filter (ArduinoOTA/ESPmDNS/LittleFS/Update.h are
// real vendor dependencies, not an oversight) and platformio.ini is fenced on
// this ticket. That query gets the two native-provable legs (registry fields
// == record emitter names) plus a source citation for the third, called out
// explicitly below rather than silently skipped.
// =============================================================================
#include <unity.h>

#include <ArduinoJson.h>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "action_registry.h"
#include "api_audio.h"
#include "api_status.h"
#include "audio_task.h"
#include "config_cache.h"
#include "console_catalog.h"
#include "console_module.h"
#include "log_buffer.h"  // LogBuffer, logBufferInit()/logBufferAppend() - fills the ring
                         // g_test_log_buffer below for system.status.logs (#239)
#include "log_buffer_test_hooks.h"  // g_test_log_buffer/g_test_log_storage - the same
                                    // log-ring stand-in test_api_logs.cpp fills
                                    // (native_test_stubs.cpp; main.cpp's real ring is
                                    // not compiled in [env:native])
#include "rc_diagnostics_snapshot.h"
#include "rc_input.h"
#include "rc_input_test_hooks.h"  // g_test_dispatch_* - control/observe the
                                  // native stub of dispatchRcTriggerActionTest() (#220)
#include "robot_state.h"

// src/native_test_stubs.cpp's recorded side effect for requestStatusBroadcastNow(),
// which configCommitApplied() (#226) calls on a successful persist.
extern unsigned g_test_status_broadcast_count;

// =============================================================================
// Capture sink: records every begin/field/item/result/end call.
// =============================================================================

static const int kMaxFields = 24;

struct CapturedRecord {
    char names[kMaxFields][40];
    char values[kMaxFields][160];
    int fieldCount;
    bool beginCalled;
    bool resultCalled;
    bool endCalled;
    ConsoleStatus status;
    ConsoleOutcome outcome;
    ConsoleReason reason;
};

static CapturedRecord g_cap;

static void capBegin(uint32_t, const char*) {
    g_cap.beginCalled = true;
}

static void capField(uint32_t, const char* name, const char* value) {
    if (g_cap.fieldCount >= kMaxFields) return;
    snprintf(g_cap.names[g_cap.fieldCount], sizeof(g_cap.names[0]), "%s", name);
    snprintf(g_cap.values[g_cap.fieldCount], sizeof(g_cap.values[0]), "%s", value);
    g_cap.fieldCount++;
}

static void capItem(uint32_t, const char*) {}

static void capResult(uint32_t, ConsoleStatus status, ConsoleOutcome outcome, ConsoleReason reason) {
    g_cap.resultCalled = true;
    g_cap.status = status;
    g_cap.outcome = outcome;
    g_cap.reason = reason;
}

static void capEnd(uint32_t, ConsoleStatus status, ConsoleOutcome outcome, ConsoleReason reason) {
    g_cap.endCalled = true;
    g_cap.status = status;
    g_cap.outcome = outcome;
    g_cap.reason = reason;
}

static void runQuery(const char* operationName) {
    memset(&g_cap, 0, sizeof(g_cap));
    ConsoleRecordSink sink = {};
    sink.onRecordBegin = capBegin;
    sink.onRecordField = capField;
    sink.onRecordItem = capItem;
    sink.onRecordResult = capResult;
    sink.onRecordEnd = capEnd;

    ConsoleRequest req = {};
    req.requestId = 1;
    req.source = CONSOLE_SOURCE_SERIAL;
    req.operationName = operationName;
    consoleExecuteCommand(&req, &sink);
}

// Like runQuery() but lets the test pick the adapter source - needed to
// prove #220's SRC_SERIAL_CONSOLE vs SRC_WEB_CONSOLE attribution, which
// runQuery()'s hardcoded CONSOLE_SOURCE_SERIAL cannot exercise.
static void runCommandFrom(const char* operationName, ConsoleCommandSource source) {
    memset(&g_cap, 0, sizeof(g_cap));
    ConsoleRecordSink sink = {};
    sink.onRecordBegin = capBegin;
    sink.onRecordField = capField;
    sink.onRecordItem = capItem;
    sink.onRecordResult = capResult;
    sink.onRecordEnd = capEnd;

    ConsoleRequest req = {};
    req.requestId = 1;
    req.source = source;
    req.operationName = operationName;
    consoleExecuteCommand(&req, &sink);
}

static const char* capturedValue(const char* name) {
    for (int i = 0; i < g_cap.fieldCount; i++) {
        if (strcmp(g_cap.names[i], name) == 0) return g_cap.values[i];
    }
    return nullptr;
}

// =============================================================================
// Key-set helpers for the three-way check
// =============================================================================

static std::vector<std::string> sortedCopy(std::vector<std::string> v) {
    std::sort(v.begin(), v.end());
    return v;
}

// The registry's fields: list for one status entry, via the generated catalog
// (never hand-typed here - a change to docs/action-registry.yaml regenerates
// this without touching the test).
static std::vector<std::string> catalogFieldNames(const char* operationName) {
    std::vector<std::string> out;
    const ConsoleCatalogEntry* entry = consoleCatalogFindByName(operationName);
    if (entry == nullptr || entry->fields == nullptr) return out;
    for (const char* const* f = entry->fields; *f != nullptr; ++f) {
        out.push_back(*f);
    }
    return sortedCopy(out);
}

// The field names consoleExecuteCommand() actually emitted for the query
// currently captured in g_cap.
static std::vector<std::string> emittedFieldNames() {
    std::vector<std::string> out;
    for (int i = 0; i < g_cap.fieldCount; i++) {
        out.push_back(g_cap.names[i]);
    }
    return sortedCopy(out);
}

// Top-level key set of a JSON object produced by one of the real JSON
// builders (formatHealthJson/formatWifiJson/formatAudioStatusJson/
// formatSerialJson) - the actual REST response shape, not a second hand-typed
// copy of what it "should" be.
static std::vector<std::string> jsonTopLevelKeys(const char* json) {
    std::vector<std::string> out;
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, json);
    TEST_ASSERT_FALSE_MESSAGE(err, "test-constructed JSON failed to parse");
    for (JsonPair kv : doc.as<JsonObject>()) {
        out.push_back(kv.key().c_str());
    }
    return sortedCopy(out);
}

void setUp() {
    robotState = RobotState{};
    ConfigSnapshot snap = {};
    configCacheApply(snap);
    g_test_dispatch_action_calls = 0;
    g_test_last_dispatch_target = ROBOT_ACTION_NONE;
    g_test_last_dispatch_source = SRC_NONE;
    g_test_dispatch_outcome = RcDispatchOutcome::kQueued;
    // Reset to empty before every test (#239) - matches test_api_logs.cpp's
    // own setUp(), so a log-ring test never sees another test's lines.
    logBufferInit(&g_test_log_buffer, g_test_log_storage, LOG_RING_MAX_LINES);
}
void tearDown() {}

// =============================================================================
// system.status.health
// =============================================================================

void test_health_three_way_field_match() {
    char json[256];
    // Same shape formatHealthJson() actually emits - values are arbitrary,
    // only the key set matters here.
    formatHealthJson(json, sizeof(json), true, false, false, true, false, false, true, 1000, 900,
                     800, -50);
    std::vector<std::string> jsonKeys = jsonTopLevelKeys(json);
    std::vector<std::string> registryFields = catalogFieldNames("system.status.health");

    runQuery("system.status.health");
    std::vector<std::string> emitted = emittedFieldNames();

    TEST_ASSERT_TRUE_MESSAGE(!jsonKeys.empty(), "formatHealthJson produced no keys");
    TEST_ASSERT_TRUE(jsonKeys == registryFields);
    TEST_ASSERT_TRUE(registryFields == emitted);
}

void test_health_executes_synchronously_and_carries_real_state() {
    robotState.estop = true;
    runQuery("system.status.health");

    TEST_ASSERT_TRUE(g_cap.beginCalled);
    TEST_ASSERT_TRUE(g_cap.endCalled);
    TEST_ASSERT_FALSE_MESSAGE(g_cap.resultCalled, "a query answers begin/field/end, not result");
    TEST_ASSERT_EQUAL(CONSOLE_STATUS_OK, g_cap.status);
    TEST_ASSERT_EQUAL(CONSOLE_OUTCOME_COMPLETED, g_cap.outcome);

    // The pipeline carries robotState.estop through captureHealthSnapshot(),
    // not a stub value - this is what proves it reads the real snapshot.
    TEST_ASSERT_EQUAL_STRING("true", capturedValue("estop"));
    // #219 D-era T1 bug this rewrite fixes: the field was named "fsReady"
    // (the C parameter name) instead of "littleFsReady" (the real JSON key).
    TEST_ASSERT_NOT_NULL_MESSAGE(capturedValue("littleFsReady"),
                                "field must be named littleFsReady (the API JSON key), not fsReady");
    TEST_ASSERT_NULL_MESSAGE(capturedValue("fsReady"),
                             "fsReady is not a real JSON key on this response");
}

// =============================================================================
// system.status.wifi
// =============================================================================

void test_wifi_three_way_field_match() {
    char json[256];
    formatWifiJson(json, sizeof(json), "AP", "192.168.4.1", true, false, "", "", 0, false);
    std::vector<std::string> jsonKeys = jsonTopLevelKeys(json);
    std::vector<std::string> registryFields = catalogFieldNames("system.status.wifi");

    runQuery("system.status.wifi");
    std::vector<std::string> emitted = emittedFieldNames();

    TEST_ASSERT_TRUE(jsonKeys == registryFields);
    TEST_ASSERT_TRUE(registryFields == emitted);
}

void test_wifi_carries_active_wifi_config_ssid() {
    WifiConfig activeWifi = {};
    snprintf(activeWifi.ap_ssid, sizeof(activeWifi.ap_ssid), "%s", "protoArtoo-test");
    configCacheSetActiveWifi(activeWifi);

    runQuery("system.status.wifi");

    TEST_ASSERT_EQUAL_STRING("protoArtoo-test", capturedValue("apSsid"));
}

// =============================================================================
// dome.status.current
// =============================================================================

// The JSON-builder leg for this one query cannot run natively - see the file
// header. domeTargetSpeed/domeEnabled are cited verbatim at
// src/web/web_server.cpp (buildStatusJson()'s fixed snprintf format string,
// "domeTargetSpeed":%.3f / "domeEnabled":%s) as of this commit; a future
// rename there would not fail this test, only a device/controller-upload run.
void test_dome_status_current_field_match_registry_to_emitter() {
    std::vector<std::string> registryFields = catalogFieldNames("dome.status.current");
    TEST_ASSERT_TRUE(registryFields == (std::vector<std::string>{"domeEnabled", "domeTargetSpeed"}));

    runQuery("dome.status.current");
    std::vector<std::string> emitted = emittedFieldNames();
    TEST_ASSERT_TRUE(registryFields == emitted);
}

void test_dome_status_current_carries_real_state() {
    robotState.domeTargetSpeed = 0.5f;
    ConfigSnapshot snap = {};
    snap.system.enable_dome_esc = true;
    configCacheApply(snap);

    runQuery("dome.status.current");

    TEST_ASSERT_EQUAL_STRING("0.500", capturedValue("domeTargetSpeed"));
    TEST_ASSERT_EQUAL_STRING("true", capturedValue("domeEnabled"));
    TEST_ASSERT_EQUAL(CONSOLE_OUTCOME_COMPLETED, g_cap.outcome);
}

// =============================================================================
// sound.status.current
// =============================================================================

void test_sound_three_way_field_match() {
    char json[256];
    formatAudioStatusJson(json, sizeof(json), "TEST", 0, true, false, 0, 0, 0, 0, "ok", "ok");
    std::vector<std::string> jsonKeys = jsonTopLevelKeys(json);
    std::vector<std::string> registryFields = catalogFieldNames("sound.status.current");

    runQuery("sound.status.current");
    std::vector<std::string> emitted = emittedFieldNames();

    TEST_ASSERT_TRUE(jsonKeys == registryFields);
    TEST_ASSERT_TRUE(registryFields == emitted);
}

void test_sound_carries_real_state_and_labels() {
    robotState.audio_module_link_ok = true;
    robotState.audio_module_play_state = 1;  // "playing"
    robotState.audio_module_current_track = 42;

    runQuery("sound.status.current");

    TEST_ASSERT_EQUAL_STRING("true", capturedValue("link_ok"));
    TEST_ASSERT_EQUAL_STRING("playing", capturedValue("play_state"));
    TEST_ASSERT_EQUAL_STRING("42", capturedValue("current_track"));
    // #212-era drift this rewrite fixes: the registry claimed fields
    // audioActive/audioLinkOk/activeMood, none of which are real keys on
    // this response (activeMood does not appear in it at all).
    TEST_ASSERT_NULL_MESSAGE(capturedValue("activeMood"),
                             "activeMood is not a real key on GET /api/audio");
}

// =============================================================================
// dome.status.serial-link
// =============================================================================

// This query's registry fields (active/heartbeatRx/heartbeatTx) are a chosen
// subset of formatSerialJson()'s "dome" sub-object (the other keys - label,
// name, hardwareRequired, note - are compile-time literals, not state; see
// include/api_status.h's DomeSerialLinkSnapshot comment). The three-way test
// here is therefore "every registry field is a real key of the dome
// sub-object" (subset), not set equality against the sub-object's full key
// set - documented explicitly rather than silently narrowed.
void test_dome_serial_link_fields_are_real_dome_subobject_keys() {
    char json[768];
    formatSerialJson(json, sizeof(json), true, 5, 7);
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, json);
    TEST_ASSERT_FALSE(err);
    JsonObject domeObj = doc["dome"].as<JsonObject>();
    TEST_ASSERT_FALSE_MESSAGE(domeObj.isNull(), "formatSerialJson must emit a \"dome\" object");

    std::vector<std::string> domeKeys;
    for (JsonPair kv : domeObj) {
        domeKeys.push_back(kv.key().c_str());
    }
    std::sort(domeKeys.begin(), domeKeys.end());

    std::vector<std::string> registryFields = catalogFieldNames("dome.status.serial-link");
    for (const auto& field : registryFields) {
        bool found = std::binary_search(domeKeys.begin(), domeKeys.end(), field);
        TEST_ASSERT_TRUE_MESSAGE(found, field.c_str());
    }

    runQuery("dome.status.serial-link");
    std::vector<std::string> emitted = emittedFieldNames();
    TEST_ASSERT_TRUE(registryFields == emitted);
}

void test_dome_serial_link_carries_real_state() {
    robotState.domeHbRx = 11;
    robotState.bodyHbTx = 13;

    runQuery("dome.status.serial-link");

    // domeConnected() is a fixed native stub returning true
    // (src/native_test_stubs.cpp) - asserted so a future stub change is
    // visible here rather than only on device.
    TEST_ASSERT_EQUAL_STRING("true", capturedValue("active"));
    TEST_ASSERT_EQUAL_STRING("11", capturedValue("heartbeatRx"));
    TEST_ASSERT_EQUAL_STRING("13", capturedValue("heartbeatTx"));
}

// =============================================================================
// rc.status.snapshot
// =============================================================================

// mode is a real top-level key of populateRcDiagnosticsJson()'s output.
// sbus1/sbus2 are real keys, but nested at sources.sbus1/sources.sbus2 (a
// deeply nested response - sources/channels/digital/mappingProfile/raw), so
// the Console collapses each source into one summary token rather than
// expanding every nested key to its own field. This test proves both real
// locations rather than asserting a flat set equality that would misdescribe
// the response's actual shape.
void test_rc_snapshot_mode_and_sources_are_real_keys() {
    RcDiagnosticsSnapshot snap = {};
    captureRcDiagnosticsSnapshot(&snap);
    JsonDocument doc;
    bool built = populateRcDiagnosticsJson(doc, snap);
    TEST_ASSERT_TRUE(built);

    TEST_ASSERT_TRUE_MESSAGE(doc["mode"].is<const char*>(), "mode must be a real top-level key");
    JsonObject sources = doc["sources"].as<JsonObject>();
    TEST_ASSERT_FALSE(sources.isNull());
    TEST_ASSERT_TRUE_MESSAGE(sources["sbus1"].is<JsonObject>(),
                             "sbus1 must be a real key under sources");
    TEST_ASSERT_TRUE_MESSAGE(sources["sbus2"].is<JsonObject>(),
                             "sbus2 must be a real key under sources");

    std::vector<std::string> registryFields = catalogFieldNames("rc.status.snapshot");
    TEST_ASSERT_TRUE(registryFields == (std::vector<std::string>{"mode", "sbus1", "sbus2"}));

    runQuery("rc.status.snapshot");
    std::vector<std::string> emitted = emittedFieldNames();
    TEST_ASSERT_TRUE(registryFields == emitted);
}

void test_rc_snapshot_carries_real_source_state() {
    // rcSourceEnabledForMode requires the active mode + enable flags to line
    // up before a source counts as enabled; default dual_sbus mode enables
    // sbus1 without further config, which is enough to prove the pipeline
    // carries real, non-stub source state through to the record.
    robotState.lastSbus1Ms = 100;
    robotState.sbus1LostFrameCount = 3;

    runQuery("rc.status.snapshot");

    const char* sbus1 = capturedValue("sbus1");
    TEST_ASSERT_NOT_NULL(sbus1);
    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(sbus1, "lostFrames:3"),
                                 "sbus1 summary must carry the real lost-frame count");
}

// =============================================================================
// system.status.logs (#239)
// =============================================================================
// This query answers `item` records (recent log lines), not `field` records,
// so it does not fit runQuery()/g_cap's field-capture shape - g_cap's capItem
// is a no-op (no query before this ticket emitted items). A small dedicated
// capture records item values instead, without touching the shared harness
// the ~40 other tests in this file use.

struct CapturedLogItems {
    char values[LOG_RING_MAX_LINES][LOG_LINE_MAX];
    int count;
    bool beginCalled;
    bool endCalled;
    ConsoleStatus status;
    ConsoleOutcome outcome;
    ConsoleReason reason;
};
static CapturedLogItems g_logCap;

static void logCapBegin(uint32_t, const char*) {
    g_logCap.beginCalled = true;
}
static void logCapItem(uint32_t, const char* value) {
    if (g_logCap.count >= (int)LOG_RING_MAX_LINES) return;
    snprintf(g_logCap.values[g_logCap.count], sizeof(g_logCap.values[0]), "%s", value);
    g_logCap.count++;
}
static void logCapEnd(uint32_t, ConsoleStatus status, ConsoleOutcome outcome, ConsoleReason reason) {
    g_logCap.endCalled = true;
    g_logCap.status = status;
    g_logCap.outcome = outcome;
    g_logCap.reason = reason;
}

static void runLogsQuery() {
    memset(&g_logCap, 0, sizeof(g_logCap));
    ConsoleRecordSink sink = {};
    sink.onRecordBegin = logCapBegin;
    sink.onRecordItem = logCapItem;
    sink.onRecordEnd = logCapEnd;

    ConsoleRequest req = {};
    req.requestId = 1;
    req.source = CONSOLE_SOURCE_SERIAL;
    req.operationName = "system.status.logs";
    consoleExecuteCommand(&req, &sink);
}

// The dispatch-table proof (#239 acceptance criterion 2): system.status.logs
// used to answer through the not-executable guard path (is_query: false,
// #223's original, incorrect classification - see the removed entry in
// test_aggregate_field_status_entries_answer_not_executable above). It must
// now execute like any other query: begin, item*, end - never a single
// result record.
void test_logs_query_is_dispatched_not_guarded_as_not_executable() {
    runQuery("system.status.logs");

    TEST_ASSERT_TRUE(g_cap.beginCalled);
    TEST_ASSERT_FALSE_MESSAGE(g_cap.resultCalled, "a query answers begin/item/end, not result");
    TEST_ASSERT_EQUAL(CONSOLE_STATUS_OK, g_cap.status);
    TEST_ASSERT_EQUAL(CONSOLE_OUTCOME_COMPLETED, g_cap.outcome);
}

// The concurrency-safe streaming path itself (#239 acceptance criterion 3):
// ring lines come back as item records, oldest first, matching /api/logs'
// own ordering (test_api_logs.cpp's
// test_buffered_lines_are_returned_oldest_first_newline_separated) even
// though this path never touches recentLogsBodyBuffer()/copyRecentLogs() -
// it reads the ring directly through getLogBufferCount()/copyLogLineAt().
void test_logs_query_streams_ring_lines_as_items_oldest_first() {
    logBufferAppend(&g_test_log_buffer, "first line");
    logBufferAppend(&g_test_log_buffer, "second line");
    logBufferAppend(&g_test_log_buffer, "third line");

    runLogsQuery();

    TEST_ASSERT_TRUE(g_logCap.beginCalled);
    TEST_ASSERT_TRUE(g_logCap.endCalled);
    TEST_ASSERT_EQUAL(CONSOLE_STATUS_OK, g_logCap.status);
    TEST_ASSERT_EQUAL(CONSOLE_OUTCOME_COMPLETED, g_logCap.outcome);
    TEST_ASSERT_EQUAL(CONSOLE_REASON_NONE, g_logCap.reason);
    TEST_ASSERT_EQUAL_INT(3, g_logCap.count);
    TEST_ASSERT_EQUAL_STRING("first line", g_logCap.values[0]);
    TEST_ASSERT_EQUAL_STRING("second line", g_logCap.values[1]);
    TEST_ASSERT_EQUAL_STRING("third line", g_logCap.values[2]);
}

// An empty ring (fresh boot, or right after a level change resets it) is a
// real, expected state, not an error - the answer is a query that completed
// with zero items, not an unavailable/invalid one.
void test_logs_query_empty_ring_answers_completed_with_no_items() {
    runLogsQuery();

    TEST_ASSERT_TRUE(g_logCap.beginCalled);
    TEST_ASSERT_TRUE(g_logCap.endCalled);
    TEST_ASSERT_EQUAL(CONSOLE_STATUS_OK, g_logCap.status);
    TEST_ASSERT_EQUAL(CONSOLE_OUTCOME_COMPLETED, g_logCap.outcome);
    TEST_ASSERT_EQUAL_INT(0, g_logCap.count);
}

// A full ring (LOG_RING_MAX_LINES entries) must come back whole, matching
// test_api_logs.cpp's own test_full_ring_is_served_without_truncating_the_response -
// the two adapters answer the same question from the same ring, through
// different functions, and must agree on "how much".
void test_logs_query_full_ring_reports_every_line() {
    for (size_t i = 0; i < LOG_RING_MAX_LINES; ++i) {
        char line[32];
        snprintf(line, sizeof(line), "line-%zu", i);
        logBufferAppend(&g_test_log_buffer, line);
    }

    runLogsQuery();

    TEST_ASSERT_EQUAL_INT((int)LOG_RING_MAX_LINES, g_logCap.count);
    char lastLine[32];
    snprintf(lastLine, sizeof(lastLine), "line-%zu", LOG_RING_MAX_LINES - 1);
    TEST_ASSERT_EQUAL_STRING("line-0", g_logCap.values[0]);
    TEST_ASSERT_EQUAL_STRING(lastLine, g_logCap.values[LOG_RING_MAX_LINES - 1]);
}

// =============================================================================
// is_query: false status entries: never independently executable
// =============================================================================

// mood/sleep-mode/dashboard-health/drive/servo/aux-led are aggregate-
// field registry rows (#212): they describe a field inside another query's
// response, not a standalone command. Attempting to run one directly must
// answer NOT_EXECUTABLE, not EXECUTOR_NOT_READY - the latter implies a future
// ticket owes a fix; the former says none is coming because none applies.
//
// system.status.logs used to be listed here too (#223's original, incorrect
// classification) - it never fit this shape (docs/action-registry.yaml's own
// comment on that row explains why) and is a real dispatched query as of
// #239; see the system.status.logs section below instead.
void test_aggregate_field_status_entries_answer_not_executable() {
    const char* aggregateFieldEntries[] = {
        "drive.status.current",       "servo.status.current",
        "aux.status.led-state",       "system.status.sleep-mode",
        "system.status.mood",         "system.status.dashboard-health",
    };
    for (const char* name : aggregateFieldEntries) {
        const ConsoleCatalogEntry* entry = consoleCatalogFindByName(name);
        TEST_ASSERT_NOT_NULL_MESSAGE(entry, name);
        TEST_ASSERT_FALSE_MESSAGE(entry->is_query, name);

        runQuery(name);
        TEST_ASSERT_TRUE_MESSAGE(g_cap.resultCalled, name);
        TEST_ASSERT_FALSE_MESSAGE(g_cap.beginCalled, name);
        TEST_ASSERT_EQUAL_MESSAGE(CONSOLE_STATUS_ERR, g_cap.status, name);
        TEST_ASSERT_EQUAL_MESSAGE(CONSOLE_OUTCOME_UNAVAILABLE, g_cap.outcome, name);
        TEST_ASSERT_EQUAL_MESSAGE(CONSOLE_REASON_NOT_EXECUTABLE, g_cap.reason, name);
    }
}

// system.api.event-stream is executor: none and is_query: false for a
// different reason (a stream, not aggregate metadata) but must answer the
// same way through this same guard path.
void test_event_stream_status_entry_answers_not_executable() {
    runQuery("system.api.event-stream");
    TEST_ASSERT_TRUE(g_cap.resultCalled);
    TEST_ASSERT_EQUAL(CONSOLE_OUTCOME_UNAVAILABLE, g_cap.outcome);
    TEST_ASSERT_EQUAL(CONSOLE_REASON_NOT_EXECUTABLE, g_cap.reason);
}

// =============================================================================
// executor-not-ready count (#223 acceptance criterion)
// =============================================================================

// Sweeps every type: status catalog entry the registry marks is_query: true
// (fields: present) and proves the dispatch table answers each one -
// EXECUTOR_NOT_READY must never fire for a status entry once this ticket
// lands. This is the automated form of the "executor-not-ready count" the
// ticket requires reported: this test fails the moment that count is nonzero.
//
// system.status.logs is NOT swept by this loop even though it now has a real
// dispatch row (#239): `entry.is_query` here comes from the COMPILED catalog
// (include/console_catalog.h / src/console/console_catalog.cpp), generated
// from docs/action-registry.yaml by tools/generate_console_catalog.py, which
// #239 deliberately did not re-run - doing so would rewrite data/console_help.txt
// (fenced on that ticket) and shift every later entry's help-text offset,
// since the corrected `executor:` string is a different length. The compiled
// is_query for that one row therefore still reads false until the next
// unrelated regen; system.status.logs gets its own direct test below instead
// of relying on this sweep.
void test_no_status_entry_is_executor_not_ready() {
    size_t count = 0;
    const ConsoleCatalogEntry* entries = consoleCatalogGetEntries(&count);
    int notReadyCount = 0;

    for (size_t i = 0; i < count; ++i) {
        const ConsoleCatalogEntry& entry = entries[i];
        if (strcmp(entry.type, CONSOLE_CATALOG_TYPE_STATUS) != 0) continue;
        if (!entry.is_query) continue;  // aggregate-field rows are exempt, not "not ready"

        runQuery(entry.name);
        if (g_cap.reason == CONSOLE_REASON_EXECUTOR_NOT_READY) {
            notReadyCount++;
        }
    }

    TEST_ASSERT_EQUAL_MESSAGE(0, notReadyCount,
                              "every is_query: true status entry must have a dispatch table row");
}

// =============================================================================
// Non-motion, non-parameterized action dispatch (#220, ADR 0034)
// =============================================================================

// RC token aliases resolve through the same operation and reach the exact
// same RobotActionId as the canonical name - no second dispatch path
// (docs/console-protocol.md s.1.1).
void test_action_alias_resolves_same_target_as_canonical() {
    robotState.webControlEnabled = true;

    runQuery("sound.action.random-humming");
    TEST_ASSERT_EQUAL_UINT(1u, g_test_dispatch_action_calls);
    TEST_ASSERT_EQUAL(SOUND_ACTION_RANDOM_HUMMING, g_test_last_dispatch_target);
    TEST_ASSERT_EQUAL(CONSOLE_STATUS_OK, g_cap.status);
    TEST_ASSERT_EQUAL(CONSOLE_OUTCOME_QUEUED, g_cap.outcome);

    runQuery("sound_rand_humming");  // RC token alias, rc_mapping.h
    TEST_ASSERT_EQUAL_UINT(2u, g_test_dispatch_action_calls);
    TEST_ASSERT_EQUAL(SOUND_ACTION_RANDOM_HUMMING, g_test_last_dispatch_target);
    TEST_ASSERT_EQUAL(CONSOLE_STATUS_OK, g_cap.status);
    TEST_ASSERT_EQUAL(CONSOLE_OUTCOME_QUEUED, g_cap.outcome);
}

// SYSTEM_ACTION_ESTOP is refused by the guard core before dispatch is ever
// attempted - same behavior as /api/actions/test (ACTION_TEST_SAFETY_CRITICAL_BLOCKED).
void test_action_estop_is_blocked_not_dispatched() {
    robotState.webControlEnabled = true;

    runQuery("system.action.estop");

    TEST_ASSERT_EQUAL_UINT(0u, g_test_dispatch_action_calls);
    TEST_ASSERT_TRUE(g_cap.resultCalled);
    TEST_ASSERT_EQUAL(CONSOLE_STATUS_ERR, g_cap.status);
    TEST_ASSERT_EQUAL(CONSOLE_OUTCOME_BLOCKED, g_cap.outcome);
    TEST_ASSERT_EQUAL(CONSOLE_REASON_BLOCKED_BY_STATE, g_cap.reason);
}

// Non-RC Control gate (ADR 0027/0034): webControlEnabled=false blocks the
// same as the REST route, and the dispatch core is never reached.
void test_action_web_control_disabled_is_blocked_not_dispatched() {
    robotState.webControlEnabled = false;

    runQuery("sound.action.random-humming");

    TEST_ASSERT_EQUAL_UINT(0u, g_test_dispatch_action_calls);
    TEST_ASSERT_EQUAL(CONSOLE_STATUS_ERR, g_cap.status);
    TEST_ASSERT_EQUAL(CONSOLE_OUTCOME_BLOCKED, g_cap.outcome);
    TEST_ASSERT_EQUAL(CONSOLE_REASON_BLOCKED_BY_STATE, g_cap.reason);
}

// Analog motion targets are permanently outside this single-shot mechanism
// (#222 wires them through the drive/dome-speed backbone) - distinct reason
// from "not wired yet" so the operator does not expect a future fix here.
void test_action_analog_target_answers_not_executable() {
    robotState.webControlEnabled = true;

    runQuery("drive.action.speed");

    TEST_ASSERT_EQUAL_UINT(0u, g_test_dispatch_action_calls);
    TEST_ASSERT_EQUAL(CONSOLE_OUTCOME_UNAVAILABLE, g_cap.outcome);
    TEST_ASSERT_EQUAL(CONSOLE_REASON_NOT_EXECUTABLE, g_cap.reason);
}

// dome.action.dome-sequence is the one payload-needing target #221 leaves
// unwired (no existing pure validator to reuse for DM:<NAME> forwarding -
// see consoleExecuteAction()'s own comment, console_module.cpp) - still
// genuinely "not ready yet". dome.action.marcduino-sequence/-command are
// NOW wired (below) - #221 closes that gap for exactly these two.
void test_action_dome_sequence_still_answers_executor_not_ready() {
    robotState.webControlEnabled = true;

    runQuery("dome.action.dome-sequence");

    TEST_ASSERT_EQUAL_UINT(0u, g_test_dispatch_action_calls);
    TEST_ASSERT_EQUAL(CONSOLE_OUTCOME_UNAVAILABLE, g_cap.outcome);
    TEST_ASSERT_EQUAL(CONSOLE_REASON_EXECUTOR_NOT_READY, g_cap.reason);
}

// =============================================================================
// Argument tokenizer + schema validation wired into real dispatch (#221,
// ADR 0034, docs/console-protocol.md s.1.2). These run through
// consoleExecuteCommand() with a real combined "operation args" line, the
// same shape both adapters hand it, and dispatchRcTriggerActionTest()'s
// native stub (rc_input_test_hooks.h) so the actual payload plumbed through
// is observable, not just that dispatch happened.
// =============================================================================

// The fact-2 regression this ticket closes: a wired, zero-parameter action
// given an argument no longer silently executes (the pre-#221 serial
// behavior - arguments discarded, action ran anyway) or silently diverge
// from the web adapter's answer. Both adapters now resolve the operation
// (it exists), then reject the unrecognized key by name.
void test_action_zero_param_action_rejects_unknown_argument() {
    robotState.webControlEnabled = true;

    runQuery("sound.action.random-humming foo=bar");

    TEST_ASSERT_EQUAL_UINT(0u, g_test_dispatch_action_calls);
    TEST_ASSERT_TRUE(g_cap.beginCalled);
    TEST_ASSERT_TRUE(g_cap.endCalled);
    TEST_ASSERT_EQUAL(CONSOLE_STATUS_ERR, g_cap.status);
    TEST_ASSERT_EQUAL(CONSOLE_OUTCOME_INVALID, g_cap.outcome);
    TEST_ASSERT_EQUAL(CONSOLE_REASON_UNKNOWN_ARGUMENT, g_cap.reason);
    TEST_ASSERT_EQUAL_STRING("foo", capturedValue("argument"));
}

// A status query takes no arguments (docs/console-protocol.md s.1.1) - the
// same fact-2 divergence class, closed the same way: recognized operation,
// named unknown key, not a silent drop-and-execute.
void test_status_query_rejects_any_argument() {
    runQuery("system.status.health extra=1");

    // Named-key failures use begin/field(argument=<key>)/end - see
    // consoleEmitArgFailure() (console_module.cpp) - not the single-record
    // result shape guard paths without a key to name use.
    TEST_ASSERT_TRUE(g_cap.beginCalled);
    TEST_ASSERT_TRUE(g_cap.endCalled);
    TEST_ASSERT_FALSE(g_cap.resultCalled);
    TEST_ASSERT_EQUAL(CONSOLE_OUTCOME_INVALID, g_cap.outcome);
    TEST_ASSERT_EQUAL(CONSOLE_REASON_UNKNOWN_ARGUMENT, g_cap.reason);
    TEST_ASSERT_EQUAL_STRING("extra", capturedValue("argument"));
}

// Malformed argument syntax (unterminated quote) answers a distinct reason
// from schema failures - the parser never got far enough to resolve a key.
void test_malformed_quoted_argument_is_rejected() {
    runQuery("sound.action.random-humming foo=\"unterminated");

    TEST_ASSERT_FALSE(g_cap.beginCalled);
    TEST_ASSERT_TRUE(g_cap.resultCalled);
    TEST_ASSERT_EQUAL(CONSOLE_OUTCOME_INVALID, g_cap.outcome);
    TEST_ASSERT_EQUAL(CONSOLE_REASON_MALFORMED_ARGUMENT, g_cap.reason);
}

// dome.action.marcduino-sequence: valid 2-digit body-sequence payload
// dispatches with the SAME validator the live RC trigger path uses
// (rcPayloadValidForBodySequence(), include/rc_action_types.h) and the real
// payload reaches dispatchRcTriggerActionTest() - not the pre-#221
// hardcoded "".
void test_action_marcduino_sequence_valid_value_dispatches_with_payload() {
    robotState.webControlEnabled = true;

    runQuery("dome.action.marcduino-sequence value=30");

    TEST_ASSERT_EQUAL_UINT(1u, g_test_dispatch_action_calls);
    TEST_ASSERT_EQUAL(DOME_ACTION_MARCDUINO_SEQ, g_test_last_dispatch_target);
    TEST_ASSERT_EQUAL_STRING("30", g_test_last_dispatch_payload);
    TEST_ASSERT_EQUAL(CONSOLE_STATUS_OK, g_cap.status);
    TEST_ASSERT_EQUAL(CONSOLE_OUTCOME_QUEUED, g_cap.outcome);
}

// Out-of-range body-sequence number (not 30-36): rejected before dispatch,
// naming the argument - not silently forwarded and not "temporarily
// unavailable" (which the raw dispatch-outcome mapping would have answered
// had this reached rcActionResultHasEffect()'s "no effect" path instead).
void test_action_marcduino_sequence_invalid_value_answers_out_of_range() {
    robotState.webControlEnabled = true;

    runQuery("dome.action.marcduino-sequence value=99");

    TEST_ASSERT_EQUAL_UINT(0u, g_test_dispatch_action_calls);
    TEST_ASSERT_EQUAL(CONSOLE_OUTCOME_INVALID, g_cap.outcome);
    TEST_ASSERT_EQUAL(CONSOLE_REASON_OUT_OF_RANGE, g_cap.reason);
    TEST_ASSERT_EQUAL_STRING("value", capturedValue("argument"));
}

// dome.action.marcduino-command with no value= at all: missing-argument,
// not executor-not-ready - the operation IS wired now, it just was not
// given the argument it requires.
void test_action_marcduino_command_missing_value_answers_missing_argument() {
    robotState.webControlEnabled = true;

    runQuery("dome.action.marcduino-command");

    TEST_ASSERT_EQUAL_UINT(0u, g_test_dispatch_action_calls);
    TEST_ASSERT_EQUAL(CONSOLE_OUTCOME_INVALID, g_cap.outcome);
    TEST_ASSERT_EQUAL(CONSOLE_REASON_MISSING_ARGUMENT, g_cap.reason);
    TEST_ASSERT_EQUAL_STRING("value", capturedValue("argument"));
}

// A value not starting with a body-owned prefix (:, $, #) fails the same
// existing validator (rcPayloadValidForMarcduinoCommand()) the live RC
// trigger path already enforces - "accept exactly what the existing
// handlers accept ... no widening".
void test_action_marcduino_command_bad_prefix_answers_out_of_range() {
    robotState.webControlEnabled = true;

    runQuery("dome.action.marcduino-command value=BADPREFIX");

    TEST_ASSERT_EQUAL_UINT(0u, g_test_dispatch_action_calls);
    TEST_ASSERT_EQUAL(CONSOLE_OUTCOME_INVALID, g_cap.outcome);
    TEST_ASSERT_EQUAL(CONSOLE_REASON_OUT_OF_RANGE, g_cap.reason);
}

// A quoted value (spaces, preserved verbatim after unescaping) with a valid
// prefix reaches dispatch with the value exactly as typed.
void test_action_marcduino_command_quoted_value_dispatches_with_payload() {
    robotState.webControlEnabled = true;

    runQuery("dome.action.marcduino-command value=\":OP 1\"");

    TEST_ASSERT_EQUAL_UINT(1u, g_test_dispatch_action_calls);
    TEST_ASSERT_EQUAL(DOME_ACTION_MARCDUINO_CMD, g_test_last_dispatch_target);
    TEST_ASSERT_EQUAL_STRING(":OP 1", g_test_last_dispatch_payload);
    TEST_ASSERT_EQUAL(CONSOLE_OUTCOME_QUEUED, g_cap.outcome);
}

// A value too long to fit RcTriggerBinding::marcduinoPayload[16] (the live
// RC mapping page's own field size) is rejected rather than silently
// truncated or forwarded past the live path's own limit ("no widening").
void test_action_marcduino_command_value_too_long_answers_out_of_range() {
    robotState.webControlEnabled = true;

    runQuery("dome.action.marcduino-command value=:0123456789ABCDEF");

    TEST_ASSERT_EQUAL_UINT(0u, g_test_dispatch_action_calls);
    TEST_ASSERT_EQUAL(CONSOLE_OUTCOME_INVALID, g_cap.outcome);
    TEST_ASSERT_EQUAL(CONSOLE_REASON_OUT_OF_RANGE, g_cap.reason);
}

// A config/status-only registry entry (no RobotActionId at all) stays
// exactly as unready as before this ticket - #220 does not invent a target
// for operations ACTION_REGISTRY never carried.
void test_action_with_no_rc_bindable_target_answers_executor_not_ready() {
    robotState.webControlEnabled = true;

    runQuery("drive.action.move");  // #222's parameterized move action

    TEST_ASSERT_EQUAL_UINT(0u, g_test_dispatch_action_calls);
    TEST_ASSERT_EQUAL(CONSOLE_OUTCOME_UNAVAILABLE, g_cap.outcome);
    TEST_ASSERT_EQUAL(CONSOLE_REASON_EXECUTOR_NOT_READY, g_cap.reason);
}

// The dispatch core's three outcomes map onto their documented Console
// outcome + reason (docs/console-protocol.md s.3.3) - one switch, checked
// per value so a future RcDispatchOutcome addition without a case here
// fails the *build* (consoleMapDispatchOutcome has no default case), not
// silently mis-maps.
void test_action_dispatch_outcome_queued_maps_to_ok_result() {
    robotState.webControlEnabled = true;
    g_test_dispatch_outcome = RcDispatchOutcome::kQueued;

    runQuery("sound.action.random-humming");

    TEST_ASSERT_EQUAL(CONSOLE_STATUS_OK, g_cap.status);
    TEST_ASSERT_EQUAL(CONSOLE_OUTCOME_QUEUED, g_cap.outcome);
    TEST_ASSERT_EQUAL(CONSOLE_REASON_NONE, g_cap.reason);
}

void test_action_dispatch_outcome_queue_full_maps_to_err_result() {
    robotState.webControlEnabled = true;
    g_test_dispatch_outcome = RcDispatchOutcome::kQueueFull;

    runQuery("sound.action.random-humming");

    TEST_ASSERT_EQUAL(CONSOLE_STATUS_ERR, g_cap.status);
    TEST_ASSERT_EQUAL(CONSOLE_OUTCOME_QUEUE_FULL, g_cap.outcome);
    TEST_ASSERT_EQUAL(CONSOLE_REASON_QUEUE_FULL, g_cap.reason);
}

void test_action_dispatch_outcome_blocked_by_state_maps_to_unavailable() {
    robotState.webControlEnabled = true;
    g_test_dispatch_outcome = RcDispatchOutcome::kBlockedByState;

    runQuery("sound.action.random-humming");

    TEST_ASSERT_EQUAL(CONSOLE_STATUS_ERR, g_cap.status);
    TEST_ASSERT_EQUAL(CONSOLE_OUTCOME_UNAVAILABLE, g_cap.outcome);
    TEST_ASSERT_EQUAL(CONSOLE_REASON_TEMPORARILY_UNAVAILABLE, g_cap.reason);
}

// CommandSource provenance (#220 criterion 2): the serial and web adapters
// must be distinguishable downstream, and neither may be confused with
// SRC_WEB_API (the REST /api/actions/test route's own attribution).
void test_action_dispatch_attributes_serial_source() {
    robotState.webControlEnabled = true;

    runCommandFrom("sound.action.random-humming", CONSOLE_SOURCE_SERIAL);

    TEST_ASSERT_EQUAL_UINT(1u, g_test_dispatch_action_calls);
    TEST_ASSERT_EQUAL(SRC_SERIAL_CONSOLE, g_test_last_dispatch_source);
}

void test_action_dispatch_attributes_web_source() {
    robotState.webControlEnabled = true;

    runCommandFrom("sound.action.random-humming", CONSOLE_SOURCE_WEB);

    TEST_ASSERT_EQUAL_UINT(1u, g_test_dispatch_action_calls);
    TEST_ASSERT_EQUAL(SRC_WEB_CONSOLE, g_test_last_dispatch_source);
}

// Sweeps every ACTION_REGISTRY entry that #220 claims to wire (RC-bindable,
// not analog, not payload-needing, not the guarded estop) and proves none
// of them answer executor-not-ready - the automated form of this ticket's
// "executor-not-ready count for action entries" requirement, scoped to what
// #220 actually owns (motion is #222's, parameterized actions are #221/#226's).
void test_scoped_non_motion_actions_are_not_executor_not_ready() {
    robotState.webControlEnabled = true;
    int notReadyCount = 0;
    int scopedCount = 0;

    for (size_t i = 0; i < ACTION_REGISTRY_SIZE; ++i) {
        RobotActionId id = ACTION_REGISTRY[i].id;
        if (id == SYSTEM_ACTION_ESTOP) continue;
        if (robotActionIsAnalog(id)) continue;
        if (robotActionNeedsPayload(id)) continue;

        scopedCount++;
        runQuery(ACTION_REGISTRY[i].name);
        if (g_cap.reason == CONSOLE_REASON_EXECUTOR_NOT_READY) {
            notReadyCount++;
        }
    }

    TEST_ASSERT_GREATER_THAN_MESSAGE(0, scopedCount, "no in-scope action entries found");
    TEST_ASSERT_EQUAL_MESSAGE(0, notReadyCount,
                              "every non-motion, non-parameterized action must dispatch");
}

// Diagnostic (not an assertion beyond "ran"): reports the whole registry's
// action-type executor-not-ready count so the ticket's closing comment can
// cite a real number instead of an estimate. Everything outside #220's scope
// (motion, parameterized actions, config/status entries with no
// RobotActionId) is *expected* to still answer executor-not-ready here -
// #221-#227 own those.
void test_action_executor_not_ready_count_report() {
    robotState.webControlEnabled = true;
    size_t count = 0;
    const ConsoleCatalogEntry* entries = consoleCatalogGetEntries(&count);
    int actionTypeCount = 0;
    int notReadyCount = 0;

    for (size_t i = 0; i < count; ++i) {
        if (strcmp(entries[i].type, CONSOLE_CATALOG_TYPE_ACTION) != 0) continue;
        actionTypeCount++;
        runQuery(entries[i].name);
        if (g_cap.reason == CONSOLE_REASON_EXECUTOR_NOT_READY) {
            notReadyCount++;
        }
    }

    printf("[#220 report] action-type catalog entries: %d, executor-not-ready: %d\n",
           actionTypeCount, notReadyCount);
    TEST_ASSERT_TRUE(true);
}

// =============================================================================
// Component Toggle config dispatch (#226, ADR 0027/0033)
// =============================================================================

void test_component_toggle_read_reports_saved_and_active() {
    ConfigSnapshot saved = {};
    saved.system.enable_arm1 = true;
    configCacheApply(saved);

    // Active still reflects a boot where arm1 was off - the exact "staged,
    // not yet rebooted into" divergence ADR 0027 describes.
    ConfigSnapshot bootedOff = {};
    configCacheSetActiveComponentToggles(bootedOff.system);

    runQuery("system.config.enable_arm1");

    TEST_ASSERT_TRUE(g_cap.beginCalled);
    TEST_ASSERT_FALSE_MESSAGE(g_cap.resultCalled, "a read answers begin/field/end, not result");
    TEST_ASSERT_EQUAL(CONSOLE_STATUS_OK, g_cap.status);
    TEST_ASSERT_EQUAL(CONSOLE_OUTCOME_COMPLETED, g_cap.outcome);
    TEST_ASSERT_EQUAL_STRING("true", capturedValue("saved"));
    TEST_ASSERT_EQUAL_STRING("false", capturedValue("active"));
}

void test_component_toggle_write_persists_and_reports_staged_until_reboot() {
    g_test_status_broadcast_count = 0;

    runQuery("system.config.enable_arm2 value=true");

    TEST_ASSERT_FALSE_MESSAGE(g_cap.beginCalled, "a write answers a single result record");
    TEST_ASSERT_TRUE(g_cap.resultCalled);
    TEST_ASSERT_EQUAL(CONSOLE_STATUS_OK, g_cap.status);
    TEST_ASSERT_EQUAL_MESSAGE(CONSOLE_OUTCOME_STAGED_UNTIL_REBOOT, g_cap.outcome,
                              "ADR 0027: Component Toggle writes are always staged, never applied");

    ConfigSnapshot snap = {};
    configCacheRead(&snap);
    TEST_ASSERT_TRUE_MESSAGE(snap.system.enable_arm2, "the write must reach the config cache");
    TEST_ASSERT_EQUAL_UINT_MESSAGE(1, g_test_status_broadcast_count,
                                  "a successful commit broadcasts status, matching the REST path");
}

// criterion 3's "value= (or the named keys)": the same write also succeeds
// through api_config_apply.cpp's own param name, with no "value=" at all -
// proving the schema check accepts either spelling verbatim, not just the
// generic one.
void test_component_toggle_write_accepts_the_named_key_not_only_value() {
    runQuery("system.config.enable_aux1 enableAux1=true");

    TEST_ASSERT_TRUE(g_cap.resultCalled);
    TEST_ASSERT_EQUAL(CONSOLE_STATUS_OK, g_cap.status);
    TEST_ASSERT_EQUAL(CONSOLE_OUTCOME_STAGED_UNTIL_REBOOT, g_cap.outcome);

    ConfigSnapshot snap = {};
    configCacheRead(&snap);
    TEST_ASSERT_TRUE(snap.system.enable_aux1);
}

void test_component_toggle_write_rejects_an_unknown_argument() {
    runQuery("system.config.enable_aux2 typo=true");

    TEST_ASSERT_TRUE(g_cap.beginCalled);  // consoleEmitArgFailure() begins+fields+ends
    TEST_ASSERT_EQUAL(CONSOLE_STATUS_ERR, g_cap.status);
    TEST_ASSERT_EQUAL(CONSOLE_OUTCOME_INVALID, g_cap.outcome);
    TEST_ASSERT_EQUAL(CONSOLE_REASON_UNKNOWN_ARGUMENT, g_cap.reason);
    TEST_ASSERT_EQUAL_STRING("typo", capturedValue("argument"));

    ConfigSnapshot snap = {};
    configCacheRead(&snap);
    TEST_ASSERT_FALSE_MESSAGE(snap.system.enable_aux2, "a rejected write must not reach the cache");
}

void test_component_toggle_write_rejects_a_malformed_boolean() {
    runQuery("system.config.enable_aux3 value=maybe");

    TEST_ASSERT_TRUE(g_cap.beginCalled);
    TEST_ASSERT_EQUAL(CONSOLE_STATUS_ERR, g_cap.status);
    TEST_ASSERT_EQUAL(CONSOLE_OUTCOME_INVALID, g_cap.outcome);
    TEST_ASSERT_EQUAL(CONSOLE_REASON_OUT_OF_RANGE, g_cap.reason);
    TEST_ASSERT_EQUAL_STRING("enableAux3", capturedValue("argument"));
}

// Mirrors test_action_executor_not_ready_count_report's shape for type=config
// rows - informational, not a pass/fail assertion on the count itself.
void test_config_executor_not_ready_count_report() {
    size_t count = 0;
    const ConsoleCatalogEntry* entries = consoleCatalogGetEntries(&count);
    int configTypeCount = 0;
    int notReadyCount = 0;

    for (size_t i = 0; i < count; ++i) {
        if (strcmp(entries[i].type, CONSOLE_CATALOG_TYPE_CONFIG) != 0) continue;
        configTypeCount++;
        runQuery(entries[i].name);
        if (g_cap.reason == CONSOLE_REASON_EXECUTOR_NOT_READY) {
            notReadyCount++;
        }
    }

    printf("[#226 report] config-type catalog entries: %d, executor-not-ready: %d\n",
           configTypeCount, notReadyCount);
    TEST_ASSERT_TRUE(true);
}

// =============================================================================
// Test Runner
// =============================================================================

int main(int, char**) {
    UNITY_BEGIN();

    RUN_TEST(test_health_three_way_field_match);
    RUN_TEST(test_health_executes_synchronously_and_carries_real_state);

    RUN_TEST(test_wifi_three_way_field_match);
    RUN_TEST(test_wifi_carries_active_wifi_config_ssid);

    RUN_TEST(test_dome_status_current_field_match_registry_to_emitter);
    RUN_TEST(test_dome_status_current_carries_real_state);

    RUN_TEST(test_sound_three_way_field_match);
    RUN_TEST(test_sound_carries_real_state_and_labels);

    RUN_TEST(test_dome_serial_link_fields_are_real_dome_subobject_keys);
    RUN_TEST(test_dome_serial_link_carries_real_state);

    RUN_TEST(test_rc_snapshot_mode_and_sources_are_real_keys);
    RUN_TEST(test_rc_snapshot_carries_real_source_state);

    RUN_TEST(test_logs_query_is_dispatched_not_guarded_as_not_executable);
    RUN_TEST(test_logs_query_streams_ring_lines_as_items_oldest_first);
    RUN_TEST(test_logs_query_empty_ring_answers_completed_with_no_items);
    RUN_TEST(test_logs_query_full_ring_reports_every_line);

    RUN_TEST(test_aggregate_field_status_entries_answer_not_executable);
    RUN_TEST(test_event_stream_status_entry_answers_not_executable);

    RUN_TEST(test_no_status_entry_is_executor_not_ready);

    RUN_TEST(test_action_alias_resolves_same_target_as_canonical);
    RUN_TEST(test_action_estop_is_blocked_not_dispatched);
    RUN_TEST(test_action_web_control_disabled_is_blocked_not_dispatched);
    RUN_TEST(test_action_analog_target_answers_not_executable);
    RUN_TEST(test_action_dome_sequence_still_answers_executor_not_ready);
    RUN_TEST(test_action_with_no_rc_bindable_target_answers_executor_not_ready);
    RUN_TEST(test_action_dispatch_outcome_queued_maps_to_ok_result);
    RUN_TEST(test_action_dispatch_outcome_queue_full_maps_to_err_result);
    RUN_TEST(test_action_dispatch_outcome_blocked_by_state_maps_to_unavailable);
    RUN_TEST(test_action_dispatch_attributes_serial_source);
    RUN_TEST(test_action_dispatch_attributes_web_source);
    RUN_TEST(test_scoped_non_motion_actions_are_not_executor_not_ready);
    RUN_TEST(test_action_executor_not_ready_count_report);

    RUN_TEST(test_action_zero_param_action_rejects_unknown_argument);
    RUN_TEST(test_status_query_rejects_any_argument);
    RUN_TEST(test_malformed_quoted_argument_is_rejected);
    RUN_TEST(test_action_marcduino_sequence_valid_value_dispatches_with_payload);
    RUN_TEST(test_action_marcduino_sequence_invalid_value_answers_out_of_range);
    RUN_TEST(test_action_marcduino_command_missing_value_answers_missing_argument);
    RUN_TEST(test_action_marcduino_command_bad_prefix_answers_out_of_range);
    RUN_TEST(test_action_marcduino_command_quoted_value_dispatches_with_payload);
    RUN_TEST(test_action_marcduino_command_value_too_long_answers_out_of_range);

    RUN_TEST(test_component_toggle_read_reports_saved_and_active);
    RUN_TEST(test_component_toggle_write_persists_and_reports_staged_until_reboot);
    RUN_TEST(test_component_toggle_write_accepts_the_named_key_not_only_value);
    RUN_TEST(test_component_toggle_write_rejects_an_unknown_argument);
    RUN_TEST(test_component_toggle_write_rejects_a_malformed_boolean);
    RUN_TEST(test_config_executor_not_ready_count_report);

    return UNITY_END();
}
