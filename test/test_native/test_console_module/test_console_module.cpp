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

#include "api_audio.h"
#include "api_status.h"
#include "audio_task.h"
#include "config_cache.h"
#include "console_catalog.h"
#include "console_module.h"
#include "rc_diagnostics_snapshot.h"
#include "robot_state.h"

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
// is_query: false status entries: never independently executable
// =============================================================================

// mood/sleep-mode/dashboard-health/drive/servo/aux-led/logs are aggregate-
// field registry rows (#212): they describe a field inside another query's
// response, not a standalone command. Attempting to run one directly must
// answer NOT_EXECUTABLE, not EXECUTOR_NOT_READY - the latter implies a future
// ticket owes a fix; the former says none is coming because none applies.
void test_aggregate_field_status_entries_answer_not_executable() {
    const char* aggregateFieldEntries[] = {
        "drive.status.current",       "servo.status.current",
        "aux.status.led-state",       "system.status.sleep-mode",
        "system.status.mood",         "system.status.dashboard-health",
        "system.status.logs",
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

    RUN_TEST(test_aggregate_field_status_entries_answer_not_executable);
    RUN_TEST(test_event_stream_status_entry_answers_not_executable);

    RUN_TEST(test_no_status_entry_is_executor_not_ready);

    return UNITY_END();
}
