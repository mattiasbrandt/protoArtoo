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
//
// #222 adds drive.action.move and the three drive.action.speed-preset-*
// executors (the drive motion section below). Both drive_arbiter.cpp and
// src/web/api_drive.cpp are in [env:native]'s build_src_filter, so the
// consent matrix is proven against the REAL driveArbiterSubmit()/Resolve()
// state machine handleDrivePost() also drives (test_api_motion_routes.cpp),
// not a stand-in - "queue/state evidence" per the ticket's own acceptance
// criterion 4.
// =============================================================================
#include <unity.h>

#include <ArduinoJson.h>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include <freertos/semphr.h>  // paStubMutexReset()/paStubMutexStorage()/PaStubMutex -
                              // simulates the OTHER Console adapter holding
                              // s_configWriteMutex (#226 defect 1 rework)

#include "action_registry.h"
#include "api_config_apply.h"  // configApply()/ConfigApplyResult/ConfigParamSource -
                                 // drives the real Apply Core directly for defect 2's
                                 // table-drift test, bypassing the Console dispatch layer
#include "api_audio.h"
#include "api_status.h"
#include "audio_task.h"
#include "config_cache.h"
#include "console_config_fields.h"  // kComponentToggleFields[] - defect 2 rework:
                                    // proves the table matches configApply() by
                                    // driving the real Apply Core, not a comment's promise
#include "console_catalog.h"
#include "console_module.h"
#include "drive_arbiter.h"  // driveArbiterInit/Reset/Submit/Resolve() - #222's motion
                            // executors submit through the REAL arbiter, so its own
                            // resolve() is the queue/state evidence these tests read
#include "drive_speed_preset.h"  // SpeedPresetId - #222's speed-preset executors
#include "failsafe_gate.h"  // failsafeInit() - driveArbiterSubmit()'s WEB_API path
                            // clears FailsafeLayer::WEB_TIMEOUT through this module
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
#include "commanded_modes_test_hooks.h"  // g_test_commanded_*/g_test_applied_mood/
                                         // g_test_status_broadcast_count - control/observe
                                         // the commanded_modes.h setter stubs (#226)
#include "drive_motion_test_hooks.h"  // g_test_millis, g_test_speed_preset_persist_ok,
                                       // g_test_persisted_speed_preset - control/observe
                                       // the drive arbiter clock and speed-preset
                                       // persistence stub (#222), the same stub
                                       // test_api_motion_routes.cpp drives for the REST
                                       // side of these handlers

#include "audio_test_hooks.h"    // g_test_audio_queue_ok/play_track/volume - shared with
                                  // test_api_audio_routes.cpp for sound.action.play-track/
                                  // set-volume's own queue stub (#221 remainder)
#include "aux_led_test_hooks.h"  // g_test_aux_led_queue_ok - aux.action.led-color/-effect's
                                  // own queue stub (#221 remainder)

#include "sequence_dispatcher.h"  // sequenceDispatcherInit() - dome.action.dome-sequence/
                                   // test-sequence's sequenceStart() queue (#259)
#include "seq_store_index.h"      // SeqIndexEntry, seqStoreIndexAdd()/Clear() -
                                   // dome.action.delete-sequence's own lookup (#259)
#include "seq_store_test_hooks.h"  // g_test_seq_delete_ok/calls - seqStoreDelete()'s own
                                    // stub (#259)

// A drive command reaches the arbiter only through driveArbiterSubmit(), so
// resolving it with the same config DriveTask would use is the queue/state
// evidence #222's acceptance criterion 4 asks for - identical helper to
// test_api_motion_routes.cpp's own (that file drives the REST handler
// directly; this one is a separate native test binary, so it is redefined
// here rather than shared across translation units).
static DriveOutput resolvedDriveOutput() {
    ConfigSnapshot snap = {};
    configCacheRead(&snap);
    DriveArbiterConfig cfg = {};
    cfg.speedLimitMax = snap.drive.speedLimitMax;
    cfg.webDriveTimeoutMs = 60000;
    return driveArbiterResolve(cfg, millis());
}

// dome.action.delete-sequence's own seqStoreDelete()/seqStoreIndexFind() lookup
// (#259) needs a real index entry to find - src/native_test_stubs.cpp stubs the
// LittleFS half of seq_store.cpp, but seq_store_index.{h,cpp} is pure and
// native-real, so seeding it here proves the SAME lookup handleSeqDelete()
// (src/web/api_seq.cpp) drives. Identical shape to test_api_seq_routes.cpp's
// own seedIndex() (a separate native test binary, so redefined here rather
// than shared across translation units - matching resolvedDriveOutput()'s own
// precedent just above).
static void seedTestSeqIndex(const char* name) {
    SeqIndexEntry e = {};
    snprintf(e.name, sizeof(e.name), "%s", name);
    snprintf(e.file, sizeof(e.file), "%s", "seq1.json");
    e.valid = true;
    seqStoreIndexAdd(e);
}

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
    g_test_status_broadcast_count = 0;
    g_test_commanded_stationary = false;
    g_test_commanded_sleep = false;
    g_test_commanded_sleep_calls = 0;
    g_test_commanded_web_control = false;
    g_test_web_control_calls = 0;
    g_test_commanded_rc_debug = false;
    g_test_commanded_rc_debug_calls = 0;
    g_test_applied_mood = 0;

    // #221 remainder: sound.action.play-track/set-volume and
    // aux.action.led-color/-effect's own queue stubs - reset per test rather
    // than relying on each test's own explicit set/restore, matching
    // test_api_audio_routes.cpp's own setUp() for the same globals.
    g_test_audio_queue_ok = true;
    g_test_audio_play_track_calls = 0;
    g_test_audio_last_track = 0;
    g_test_audio_volume_calls = 0;
    g_test_audio_last_volume = 0;
    g_test_aux_led_queue_ok = true;

    // #222: drive.action.move submits through the REAL arbiter, so the
    // arbiter must be initialized and reset per test the same way
    // test_api_motion_routes.cpp's own setUp() does for the REST handler.
    // Non-zero millis: the arbiter reads timestamp 0 as "never submitted",
    // so a frozen zero clock would make every submission invisible to
    // resolvedDriveOutput().
    g_test_millis = 1000;
    driveArbiterInit(&robotStateMux);
    driveArbiterReset();
    failsafeInit(&robotStateMux);
    g_test_speed_preset_persist_ok = true;
    g_test_persisted_speed_preset = SpeedPresetId::Normal;
    // A non-zero speed cap, matching test_api_motion_routes.cpp's own
    // setDriveConfig(300) default: the arbiter clamps output to
    // speedLimitMax, and the blanket zero-config reset above would
    // otherwise clamp every allowed drive.action.move test's output to 0
    // regardless of what was submitted, making the clamp indistinguishable
    // from a rejection.
    ConfigSnapshot driveDefaults = {};
    configCacheRead(&driveDefaults);
    driveDefaults.drive.speedLimitMax = 300;
    configCacheApply(driveDefaults);

    // #259: dome.action.dome-sequence/test-sequence submit through the REAL
    // sequenceStart()/sequenceQueue, matching test_api_seq_routes.cpp's own
    // setUp() justification - without a real queue every accepted DM: name
    // comes back queue-full and the executor's own decisions become
    // unobservable. dome.action.delete-sequence's own lookup needs a clean
    // index and a reset store-delete stub per test.
    sequenceDispatcherInit();
    seqStoreIndexClear();
    g_test_seq_delete_ok = true;
    g_test_seq_delete_calls = 0;
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

// dome.action.dome-sequence (#259): was #221's one deliberately unwired
// payload-needing target (no existing pure validator to reuse for DM:<NAME>
// forwarding - see consoleExecuteAction()'s own comment, console_module.cpp).
// #259 closes it with a direct executor (include/console_direct_action_dome.h)
// that forwards straight to sequenceStart() - the SAME choke point
// handleDomeCmdPost()'s DM: branch (POST /api/dome/cmd, src/web/api_drive.cpp)
// and handleSeqTestPost() (POST /api/seq/test) both call - never through
// ACTION_REGISTRY[]'s guard, which still refuses this payload-needing target
// exactly as before (test_action_analog_target_answers_not_executable's
// sibling assertion for the OTHER two payload targets is unaffected: this
// executor bypasses that guard, it does not loosen it).
void test_action_dome_sequence_unknown_argument_is_rejected() {
    runQuery("dome.action.dome-sequence value=DM:VADER extra=1");

    TEST_ASSERT_EQUAL(CONSOLE_OUTCOME_INVALID, g_cap.outcome);
    TEST_ASSERT_EQUAL(CONSOLE_REASON_UNKNOWN_ARGUMENT, g_cap.reason);
    TEST_ASSERT_EQUAL_STRING("extra", capturedValue("argument"));
}

void test_action_dome_sequence_missing_value_answers_missing_argument() {
    runQuery("dome.action.dome-sequence");

    TEST_ASSERT_EQUAL(CONSOLE_OUTCOME_INVALID, g_cap.outcome);
    TEST_ASSERT_EQUAL(CONSOLE_REASON_MISSING_ARGUMENT, g_cap.reason);
    TEST_ASSERT_EQUAL_STRING("value", capturedValue("argument"));
}

// The row is typed to DM:* only (docs/action-registry.yaml's own
// marcduino_cmd: "DM:<NAME>") - a non-DM: value is out-of-range, not silently
// forwarded the way dome.action.send-command's general '*'/'@' prefixes are.
void test_action_dome_sequence_rejects_a_non_dm_value() {
    runQuery("dome.action.dome-sequence value=:SE01");

    TEST_ASSERT_EQUAL(CONSOLE_OUTCOME_INVALID, g_cap.outcome);
    TEST_ASSERT_EQUAL(CONSOLE_REASON_OUT_OF_RANGE, g_cap.reason);
    TEST_ASSERT_EQUAL_STRING("value", capturedValue("argument"));
}

// A value that would be silently truncated by domeQueueTx()'s DomeTxCmd::buf
// (src/tasks/dome_link.cpp, include/dome_link.h) is refused explicitly
// instead - "no widening" past the real dome TX buffer's own 64-byte size.
void test_action_dome_sequence_rejects_a_value_too_long_for_dome_tx() {
    char longVal[80];
    memset(longVal, 'X', sizeof(longVal) - 1);
    longVal[sizeof(longVal) - 1] = '\0';
    memcpy(longVal, "DM:", 3);
    char line[128];
    snprintf(line, sizeof(line), "dome.action.dome-sequence value=%s", longVal);

    runQuery(line);

    TEST_ASSERT_EQUAL(CONSOLE_OUTCOME_INVALID, g_cap.outcome);
    TEST_ASSERT_EQUAL(CONSOLE_REASON_OUT_OF_RANGE, g_cap.reason);
}

// A DM: name unknown to the Factory catalog and not in the (empty, per
// setUp()) Learned Sequence index takes sequenceStart()'s SEQ_FALLBACK path -
// domeQueueTx() straight through, exactly like an unrecognized DM:* name
// reaching /api/dome/cmd or /api/seq/test does.
void test_action_dome_sequence_unknown_dm_name_forwards_to_dome_fallback() {
    runQuery("dome.action.dome-sequence value=DM:NOT_A_CATALOG_ENTRY");

    TEST_ASSERT_EQUAL(CONSOLE_STATUS_OK, g_cap.status);
    TEST_ASSERT_EQUAL(CONSOLE_OUTCOME_QUEUED, g_cap.outcome);
}

// A real Factory Sequence name (sequence_catalog.cpp) takes sequenceStart()'s
// SEQ_CATALOG path instead - the real sequenceQueue send, not the dome TX
// fallback, proving this executor reaches the actual dispatcher choke point
// rather than only ever hitting the fallback branch.
void test_action_dome_sequence_catalog_name_queues_through_the_dispatcher() {
    runQuery("dome.action.dome-sequence value=DM:VADER");

    TEST_ASSERT_EQUAL(CONSOLE_STATUS_OK, g_cap.status);
    TEST_ASSERT_EQUAL(CONSOLE_OUTCOME_QUEUED, g_cap.outcome);
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

// dome.action.send-command (#259): had cpp_enum: null (docs/action-registry.
// yaml) and no RobotActionId at all, so ACTION_REGISTRY[]'s fallback path
// could never reach it - genuinely unwired before this ticket, unlike
// drive.action.move (#222) or servo's rows (#221 remainder), which had a
// direct executor from the start. #259's executor
// (consoleExecuteDomeSendCommand(), include/console_direct_action_dome.h)
// forwards straight to executeManualCommand() (src/web/api_drive.cpp) - the
// SAME dispatch core POST /api/manual-command uses (handleManualCommandPost(),
// src/web/api_system.cpp).
void test_action_send_command_unknown_argument_is_rejected() {
    runQuery("dome.action.send-command command=estop extra=1");

    TEST_ASSERT_EQUAL(CONSOLE_OUTCOME_INVALID, g_cap.outcome);
    TEST_ASSERT_EQUAL(CONSOLE_REASON_UNKNOWN_ARGUMENT, g_cap.reason);
    TEST_ASSERT_EQUAL_STRING("extra", capturedValue("argument"));
}

void test_action_send_command_missing_command_answers_missing_argument() {
    runQuery("dome.action.send-command");

    TEST_ASSERT_EQUAL(CONSOLE_OUTCOME_INVALID, g_cap.outcome);
    TEST_ASSERT_EQUAL(CONSOLE_REASON_MISSING_ARGUMENT, g_cap.reason);
    TEST_ASSERT_EQUAL_STRING("command", capturedValue("argument"));
}

// executeManualCommand() returns false for an unrecognized keyword - the SAME
// single failure shape handleManualCommandPost() answers as its own 400
// "unsupported command" (a pre-existing conflation with an audio-queue-full
// $ command reused verbatim, not introduced here - see the executor's own
// header comment).
void test_action_send_command_unsupported_keyword_answers_out_of_range() {
    runQuery("dome.action.send-command command=not_a_real_command");

    TEST_ASSERT_EQUAL(CONSOLE_OUTCOME_INVALID, g_cap.outcome);
    TEST_ASSERT_EQUAL(CONSOLE_REASON_OUT_OF_RANGE, g_cap.reason);
    TEST_ASSERT_EQUAL_STRING("command", capturedValue("argument"));
}

// The sleep-mode prefix block, reproduced verbatim from
// handleManualCommandPost(): a dome-forwarding prefix ('*'/'@'/'%'/'&'/'!')
// is held while sleeping - blocked-by-state, not dispatched.
void test_action_send_command_dome_forward_prefix_is_blocked_while_sleeping() {
    robotState.sleepMode = true;

    runQuery("dome.action.send-command command=*ST00");

    TEST_ASSERT_EQUAL(CONSOLE_OUTCOME_BLOCKED, g_cap.outcome);
    TEST_ASSERT_EQUAL(CONSOLE_REASON_BLOCKED_BY_STATE, g_cap.reason);
}

// The keyword commands (estop, reboot, enable/disable_web_control, ...) are
// NOT in handleManualCommandPost()'s blockedBySleep prefix set - sleeping
// does not hold them, matching the REST source exactly.
void test_action_send_command_keyword_is_not_blocked_by_sleep() {
    robotState.sleepMode = true;

    runQuery("dome.action.send-command command=enable_web_control");

    TEST_ASSERT_EQUAL(CONSOLE_STATUS_OK, g_cap.status);
    TEST_ASSERT_EQUAL(CONSOLE_OUTCOME_APPLIED, g_cap.outcome);
    TEST_ASSERT_TRUE(g_test_commanded_web_control);
}

// The "estop" keyword reaches the SAME failsafeTrigger() the REST route's
// executeManualCommand() call does - real state, not a stand-in dispatch
// counter.
void test_action_send_command_estop_keyword_dispatches_through_the_real_core() {
    runQuery("dome.action.send-command command=estop");

    TEST_ASSERT_EQUAL(CONSOLE_STATUS_OK, g_cap.status);
    TEST_ASSERT_EQUAL(CONSOLE_OUTCOME_APPLIED, g_cap.outcome);
    TEST_ASSERT_TRUE(failsafeIsActive());
}

// dome.action.sequence-stop (#259): no arguments, matching
// handleSeqStopPost() (POST /api/seq/stop, src/web/api_seq.cpp) - an
// unconditional, non-latching transient flag set with no estop/sleep/
// component gate in the REST source, so none is added here either.
void test_action_sequence_stop_rejects_any_argument() {
    runQuery("dome.action.sequence-stop extra=1");

    TEST_ASSERT_EQUAL(CONSOLE_OUTCOME_INVALID, g_cap.outcome);
    TEST_ASSERT_EQUAL(CONSOLE_REASON_UNKNOWN_ARGUMENT, g_cap.reason);
}

void test_action_sequence_stop_sets_the_transient_flag() {
    robotState.seqStopRequested = false;

    runQuery("dome.action.sequence-stop");

    TEST_ASSERT_TRUE(robotState.seqStopRequested);
    TEST_ASSERT_EQUAL(CONSOLE_STATUS_OK, g_cap.status);
    TEST_ASSERT_EQUAL(CONSOLE_OUTCOME_APPLIED, g_cap.outcome);
}

// dome.action.move (#259): speed=<-1.0..1.0>, the same single argument
// POST /api/dome reads (handleDomeSpeedPost(), src/web/api_drive.cpp).
// Consent is reproduced verbatim from that handler: sleeping, then the
// enable_dome_esc Component Toggle, then the queue send - see the executor's
// own header comment (include/console_direct_action_dome.h) for why schema
// validation runs before the sleep gate here rather than between the two REST
// checks.
void test_action_dome_move_unknown_argument_is_rejected() {
    runQuery("dome.action.move speed=0.5 extra=1");

    TEST_ASSERT_EQUAL(CONSOLE_OUTCOME_INVALID, g_cap.outcome);
    TEST_ASSERT_EQUAL(CONSOLE_REASON_UNKNOWN_ARGUMENT, g_cap.reason);
    TEST_ASSERT_EQUAL_STRING("extra", capturedValue("argument"));
}

void test_action_dome_move_missing_speed_answers_missing_argument() {
    runQuery("dome.action.move");

    TEST_ASSERT_EQUAL(CONSOLE_OUTCOME_INVALID, g_cap.outcome);
    TEST_ASSERT_EQUAL(CONSOLE_REASON_MISSING_ARGUMENT, g_cap.reason);
    TEST_ASSERT_EQUAL_STRING("speed", capturedValue("argument"));
}

void test_action_dome_move_out_of_range_speed_is_rejected() {
    runQuery("dome.action.move speed=2.5");

    TEST_ASSERT_EQUAL(CONSOLE_OUTCOME_INVALID, g_cap.outcome);
    TEST_ASSERT_EQUAL(CONSOLE_REASON_OUT_OF_RANGE, g_cap.reason);
    TEST_ASSERT_EQUAL_STRING("speed", capturedValue("argument"));
}

void test_action_dome_move_is_blocked_while_sleeping() {
    ConfigSnapshot snap = {};
    configCacheRead(&snap);
    snap.system.enable_dome_esc = true;
    configCacheApply(snap);
    robotState.sleepMode = true;

    runQuery("dome.action.move speed=0.5");

    TEST_ASSERT_EQUAL(CONSOLE_OUTCOME_BLOCKED, g_cap.outcome);
    TEST_ASSERT_EQUAL(CONSOLE_REASON_BLOCKED_BY_STATE, g_cap.reason);
}

void test_action_dome_move_is_refused_when_dome_output_is_disabled() {
    ConfigSnapshot snap = {};
    configCacheRead(&snap);
    snap.system.enable_dome_esc = false;
    configCacheApply(snap);

    runQuery("dome.action.move speed=0.5");

    TEST_ASSERT_EQUAL(CONSOLE_OUTCOME_UNAVAILABLE, g_cap.outcome);
    TEST_ASSERT_EQUAL(CONSOLE_REASON_COMPONENT_DISABLED, g_cap.reason);
}

void test_action_dome_move_queues_when_enabled() {
    ConfigSnapshot snap = {};
    configCacheRead(&snap);
    snap.system.enable_dome_esc = true;
    configCacheApply(snap);

    runQuery("dome.action.move speed=0.5");

    TEST_ASSERT_EQUAL(CONSOLE_STATUS_OK, g_cap.status);
    TEST_ASSERT_EQUAL(CONSOLE_OUTCOME_QUEUED, g_cap.outcome);
}

// dome.action.delete-sequence (#259): name=<string>, the same lookup-then-
// delete handleSeqDelete() (DELETE /api/seq?name=, src/web/api_seq.cpp)
// performs, reusing seqStoreIndexFind()/seqStoreDelete() verbatim. The
// dangling-RC-binding report that REST route also builds is not reproduced -
// see include/console_direct_action_dome.h's header comment for why
// (docs/console-protocol.md s.3.1 reserves multi-record answers for
// queries).
void test_action_delete_sequence_unknown_argument_is_rejected() {
    seedTestSeqIndex("DM:MYSEQ");

    runQuery("dome.action.delete-sequence name=DM:MYSEQ extra=1");

    TEST_ASSERT_EQUAL(CONSOLE_OUTCOME_INVALID, g_cap.outcome);
    TEST_ASSERT_EQUAL(CONSOLE_REASON_UNKNOWN_ARGUMENT, g_cap.reason);
    TEST_ASSERT_EQUAL_STRING("extra", capturedValue("argument"));
    TEST_ASSERT_EQUAL_UINT(0u, g_test_seq_delete_calls);
}

void test_action_delete_sequence_missing_name_answers_missing_argument() {
    runQuery("dome.action.delete-sequence");

    TEST_ASSERT_EQUAL(CONSOLE_OUTCOME_INVALID, g_cap.outcome);
    TEST_ASSERT_EQUAL(CONSOLE_REASON_MISSING_ARGUMENT, g_cap.reason);
    TEST_ASSERT_EQUAL_STRING("name", capturedValue("argument"));
}

void test_action_delete_sequence_unknown_name_answers_out_of_range() {
    runQuery("dome.action.delete-sequence name=DM:NOPE");

    TEST_ASSERT_EQUAL(CONSOLE_OUTCOME_INVALID, g_cap.outcome);
    TEST_ASSERT_EQUAL(CONSOLE_REASON_OUT_OF_RANGE, g_cap.reason);
    TEST_ASSERT_EQUAL_STRING("name", capturedValue("argument"));
    TEST_ASSERT_EQUAL_UINT(0u, g_test_seq_delete_calls);
}

void test_action_delete_sequence_store_failure_answers_internal_error() {
    seedTestSeqIndex("DM:MYSEQ");
    g_test_seq_delete_ok = false;

    runQuery("dome.action.delete-sequence name=DM:MYSEQ");

    TEST_ASSERT_EQUAL_UINT(1u, g_test_seq_delete_calls);
    TEST_ASSERT_EQUAL(CONSOLE_STATUS_ERR, g_cap.status);
    TEST_ASSERT_EQUAL(CONSOLE_OUTCOME_INTERNAL_ERROR, g_cap.outcome);
}

void test_action_delete_sequence_success_calls_the_real_store_delete() {
    seedTestSeqIndex("DM:MYSEQ");

    runQuery("dome.action.delete-sequence name=DM:MYSEQ");

    TEST_ASSERT_EQUAL_UINT(1u, g_test_seq_delete_calls);
    TEST_ASSERT_EQUAL(CONSOLE_STATUS_OK, g_cap.status);
    TEST_ASSERT_EQUAL(CONSOLE_OUTCOME_APPLIED, g_cap.outcome);
}

// dome.action.test-sequence (#259): name=DM:<NAME>, the same DM:-only
// validation and sequenceStart() call handleSeqTestPost() (POST
// /api/seq/test, src/web/api_seq.cpp) makes for its form-field path, reused
// verbatim.
void test_action_test_sequence_unknown_argument_is_rejected() {
    runQuery("dome.action.test-sequence name=DM:VADER extra=1");

    TEST_ASSERT_EQUAL(CONSOLE_OUTCOME_INVALID, g_cap.outcome);
    TEST_ASSERT_EQUAL(CONSOLE_REASON_UNKNOWN_ARGUMENT, g_cap.reason);
    TEST_ASSERT_EQUAL_STRING("extra", capturedValue("argument"));
}

void test_action_test_sequence_missing_name_answers_missing_argument() {
    runQuery("dome.action.test-sequence");

    TEST_ASSERT_EQUAL(CONSOLE_OUTCOME_INVALID, g_cap.outcome);
    TEST_ASSERT_EQUAL(CONSOLE_REASON_MISSING_ARGUMENT, g_cap.reason);
    TEST_ASSERT_EQUAL_STRING("name", capturedValue("argument"));
}

void test_action_test_sequence_rejects_a_non_dm_name() {
    runQuery("dome.action.test-sequence name=:SE01");

    TEST_ASSERT_EQUAL(CONSOLE_OUTCOME_INVALID, g_cap.outcome);
    TEST_ASSERT_EQUAL(CONSOLE_REASON_OUT_OF_RANGE, g_cap.reason);
    TEST_ASSERT_EQUAL_STRING("name", capturedValue("argument"));
}

void test_action_test_sequence_valid_name_queues_through_the_dispatcher() {
    runQuery("dome.action.test-sequence name=DM:NOT_A_CATALOG_ENTRY");

    TEST_ASSERT_EQUAL(CONSOLE_STATUS_OK, g_cap.status);
    TEST_ASSERT_EQUAL(CONSOLE_OUTCOME_QUEUED, g_cap.outcome);
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

// =============================================================================
// Component Toggle table drift check (#226 rework, defect 2)
//
// include/console_config_fields.h's kComponentToggleFields[] says, in prose,
// that its paramKey values are "copied verbatim from api_config_apply.cpp's
// boolFields[] array" and that a rename in one needs a matching edit in the
// other. Nothing enforced that. This drives configApply() - the real Apply
// Core, bypassing the Console dispatch layer entirely - directly with each
// of the 15 entries' paramKey and asserts the named SystemConfig field
// actually flips. A rename in either table without the other breaks this
// immediately: configApply() answers "no supported config fields supplied"
// for the renamed key, or the pointer-to-member reads/writes the wrong
// field, and either way the assertion below fails.
// =============================================================================

namespace {
// The same single-name ConfigParamSource shape test_api_config_apply.cpp's
// mapGet()/makeSource() establish (ADR 0002 MapReader precedent,
// include/api_param_source.h) - a single key/value pair, since each
// Component Toggle write only ever supplies one.
struct SingleParamCtx {
    const char* key;
    const char* value;
};

const char* singleParamGet(void* ctx, const char* name) {
    auto* c = static_cast<SingleParamCtx*>(ctx);
    return strcmp(name, c->key) == 0 ? c->value : nullptr;
}
}  // namespace

void test_component_toggle_table_paramkeys_match_config_apply() {
    for (size_t i = 0; i < kComponentToggleFieldCount; ++i) {
        const ComponentToggleField& field = kComponentToggleFields[i];

        ConfigSnapshot working = {};
        configCacheRead(&working);
        working.system.*(field.field) = false;  // known starting value

        SingleParamCtx ctx{field.paramKey, "true"};
        ConfigParamSource params;
        params.ctx = &ctx;
        params.get = singleParamGet;

        ConfigApplyResult result = {};
        configApply(params, &working, working.system.enable_dome_esc, &result);

        char message[96];
        snprintf(message, sizeof(message), "operation=%s paramKey=%s", field.operationName,
                 field.paramKey);
        TEST_ASSERT_FALSE_MESSAGE(result.error.hasError, message);
        TEST_ASSERT_TRUE_MESSAGE(working.system.*(field.field), message);
    }
}

// =============================================================================
// Non-toggle scalar config rows (#226): applied live, not staged
// =============================================================================

void test_drive_speed_limit_read_and_write() {
    ConfigSnapshot snap = {};
    snap.drive.speedLimitMax = 250;
    configCacheApply(snap);

    runQuery("drive.config.speed-limit");
    TEST_ASSERT_EQUAL(CONSOLE_OUTCOME_COMPLETED, g_cap.outcome);
    TEST_ASSERT_EQUAL_STRING("250", capturedValue("value"));

    runQuery("drive.config.speed-limit value=300");
    TEST_ASSERT_EQUAL(CONSOLE_OUTCOME_APPLIED, g_cap.outcome);

    ConfigSnapshot after = {};
    configCacheRead(&after);
    TEST_ASSERT_EQUAL_INT16(300, after.drive.speedLimitMax);
}

void test_drive_speed_limit_rejects_out_of_range() {
    runQuery("drive.config.speed-limit value=9999");

    TEST_ASSERT_EQUAL(CONSOLE_OUTCOME_INVALID, g_cap.outcome);
    TEST_ASSERT_EQUAL(CONSOLE_REASON_OUT_OF_RANGE, g_cap.reason);
}

void test_aux_led_pin_read_and_write() {
    runQuery("aux.config.led-pin value=2");
    TEST_ASSERT_EQUAL(CONSOLE_OUTCOME_APPLIED, g_cap.outcome);

    runQuery("aux.config.led-pin");
    TEST_ASSERT_EQUAL(CONSOLE_OUTCOME_COMPLETED, g_cap.outcome);
    TEST_ASSERT_EQUAL_STRING("2", capturedValue("value"));
}

void test_aux_led_count_read_and_write() {
    runQuery("aux.config.led-count value=30");
    TEST_ASSERT_EQUAL(CONSOLE_OUTCOME_APPLIED, g_cap.outcome);

    runQuery("aux.config.led-count");
    TEST_ASSERT_EQUAL(CONSOLE_OUTCOME_COMPLETED, g_cap.outcome);
    TEST_ASSERT_EQUAL_STRING("30", capturedValue("value"));
}

void test_rc_mode_read_and_write() {
    runQuery("rc.config.mode value=single_sbus");
    TEST_ASSERT_EQUAL(CONSOLE_OUTCOME_APPLIED, g_cap.outcome);

    runQuery("rc.config.mode");
    TEST_ASSERT_EQUAL(CONSOLE_OUTCOME_COMPLETED, g_cap.outcome);
    TEST_ASSERT_EQUAL_STRING("single_sbus", capturedValue("value"));
}

void test_rc_mode_rejects_an_unknown_mode_string() {
    runQuery("rc.config.mode value=quantum_sbus");

    TEST_ASSERT_EQUAL(CONSOLE_OUTCOME_INVALID, g_cap.outcome);
    TEST_ASSERT_EQUAL(CONSOLE_REASON_OUT_OF_RANGE, g_cap.reason);
}

void test_scalar_config_write_rejects_an_unknown_argument() {
    runQuery("drive.config.speed-limit bogus=1");

    TEST_ASSERT_EQUAL(CONSOLE_OUTCOME_INVALID, g_cap.outcome);
    TEST_ASSERT_EQUAL(CONSOLE_REASON_UNKNOWN_ARGUMENT, g_cap.reason);
    TEST_ASSERT_EQUAL_STRING("bogus", capturedValue("argument"));
}

// =============================================================================
// Cross-adapter serialization (#226 rework, defect 1): consoleWriteScalarConfigField()
// is the sole reader/writer of s_consoleConfigApplyResult, and both Console
// adapters (serial task, browser's psychic server task - both pinned to
// Core 0) can call it concurrently. s_configWriteMutex serializes the whole
// configApply() -> error check -> configCommitApplied() window; these tests
// simulate the OTHER adapter holding it via the native mutex stub's exposed
// singleton (paStubMutexStorage()) - consoleModuleInit() creates
// s_configWriteMutex via xSemaphoreCreateMutexStatic(), which the stub always
// backs with that same singleton, matching the precedent
// test_console_serial_output.cpp already set for inspecting/driving
// paGetSerialMutex()'s stub state the same way.
// =============================================================================

void test_config_write_reports_busy_when_the_mutex_is_already_held() {
    consoleModuleInit();  // idempotent: creates s_configWriteMutex on first call only
    paStubMutexReset();
    struct PaStubMutex* m = paStubMutexStorage();
    m->held = 1;  // simulate the OTHER Console adapter mid-write

    runQuery("system.config.enable_arm1 value=true");

    TEST_ASSERT_EQUAL(CONSOLE_STATUS_ERR, g_cap.status);
    TEST_ASSERT_EQUAL(CONSOLE_OUTCOME_UNAVAILABLE, g_cap.outcome);
    TEST_ASSERT_EQUAL(CONSOLE_REASON_TEMPORARILY_UNAVAILABLE, g_cap.reason);

    ConfigSnapshot after = {};
    configCacheRead(&after);
    TEST_ASSERT_FALSE_MESSAGE(after.system.enable_arm1,
                              "a write blocked by contention must never reach the config cache");

    paStubMutexReset();  // release the simulated hold for later tests
}

// The other half of the same guarantee: a write that DOES acquire the mutex
// must give it back exactly once, or every later write on both adapters
// deadlocks forever - a worse defect than the race being fixed.
void test_config_write_releases_the_mutex_after_a_successful_write() {
    consoleModuleInit();
    paStubMutexReset();

    runQuery("system.config.enable_arm2 value=true");

    TEST_ASSERT_EQUAL(CONSOLE_STATUS_OK, g_cap.status);
    TEST_ASSERT_EQUAL(CONSOLE_OUTCOME_STAGED_UNTIL_REBOOT, g_cap.outcome);

    struct PaStubMutex* m = paStubMutexStorage();
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, m->held, "the config-write mutex was left held after a write");
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, m->unmatchedGives, "unmatched give during the write");
    TEST_ASSERT_EQUAL_INT_MESSAGE(m->takeCount, m->giveCount, "takes and gives are not balanced");
    TEST_ASSERT_TRUE_MESSAGE(m->takeCount >= 1, "the write did not take the mutex at all");

    paStubMutexReset();
}

// A rejected write (fails schema validation before ever reaching configApply())
// must not touch the mutex at all - contention only matters once a write is
// actually about to reach the shared static.
void test_config_write_rejected_before_apply_never_touches_the_mutex() {
    consoleModuleInit();
    paStubMutexReset();

    runQuery("system.config.enable_aux1 bogus=true");

    TEST_ASSERT_EQUAL(CONSOLE_OUTCOME_INVALID, g_cap.outcome);

    struct PaStubMutex* m = paStubMutexStorage();
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, m->takeCount, "an argument-validation rejection reached the mutex");

    paStubMutexReset();
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
// Commanded Mode direct executors (#226 criterion 4)
// =============================================================================

void test_commanded_mode_set_mode_stationary_calls_setter_and_broadcasts() {
    runQuery("system.action.set-mode mode=stationary");

    TEST_ASSERT_FALSE_MESSAGE(g_cap.beginCalled, "a Commanded Mode write answers a single result");
    TEST_ASSERT_TRUE(g_cap.resultCalled);
    TEST_ASSERT_EQUAL(CONSOLE_STATUS_OK, g_cap.status);
    TEST_ASSERT_EQUAL(CONSOLE_OUTCOME_APPLIED, g_cap.outcome);
    TEST_ASSERT_TRUE(g_test_commanded_stationary);
    TEST_ASSERT_EQUAL_UINT(1, g_test_status_broadcast_count);
}

void test_commanded_mode_set_mode_driving_calls_setter() {
    g_test_commanded_stationary = true;
    runQuery("system.action.set-mode mode=driving");

    TEST_ASSERT_EQUAL(CONSOLE_OUTCOME_APPLIED, g_cap.outcome);
    TEST_ASSERT_FALSE(g_test_commanded_stationary);
}

void test_commanded_mode_set_mode_rejects_an_invalid_value() {
    runQuery("system.action.set-mode mode=sideways");

    TEST_ASSERT_EQUAL(CONSOLE_STATUS_ERR, g_cap.status);
    TEST_ASSERT_EQUAL(CONSOLE_OUTCOME_INVALID, g_cap.outcome);
    TEST_ASSERT_EQUAL(CONSOLE_REASON_OUT_OF_RANGE, g_cap.reason);
    TEST_ASSERT_FALSE_MESSAGE(g_test_commanded_stationary,
                             "no state change on a rejected write");
}

void test_commanded_mode_set_mode_missing_argument() {
    runQuery("system.action.set-mode");

    TEST_ASSERT_EQUAL(CONSOLE_OUTCOME_INVALID, g_cap.outcome);
    TEST_ASSERT_EQUAL(CONSOLE_REASON_MISSING_ARGUMENT, g_cap.reason);
    TEST_ASSERT_EQUAL_STRING("mode", capturedValue("argument"));
}

void test_commanded_mode_sleep_broadcasts_only_on_a_real_transition() {
    runQuery("system.action.sleep");
    TEST_ASSERT_EQUAL(CONSOLE_OUTCOME_APPLIED, g_cap.outcome);
    TEST_ASSERT_TRUE(g_test_commanded_sleep);
    TEST_ASSERT_EQUAL_UINT(1, g_test_status_broadcast_count);

    // Same state again - the setter still runs, but nothing changed, so no
    // second broadcast (matches commandedSetSleep()'s own changed-detection).
    runQuery("system.action.sleep");
    TEST_ASSERT_EQUAL(CONSOLE_OUTCOME_APPLIED, g_cap.outcome);
    TEST_ASSERT_EQUAL_UINT(2, g_test_commanded_sleep_calls);
    TEST_ASSERT_EQUAL_UINT(1, g_test_status_broadcast_count);
}

void test_commanded_mode_wake_calls_setter() {
    g_test_commanded_sleep = true;
    runQuery("system.action.wake");

    TEST_ASSERT_EQUAL(CONSOLE_OUTCOME_APPLIED, g_cap.outcome);
    TEST_ASSERT_FALSE(g_test_commanded_sleep);
}

void test_commanded_mode_sleep_rejects_an_argument() {
    runQuery("system.action.sleep extra=1");

    TEST_ASSERT_EQUAL(CONSOLE_OUTCOME_INVALID, g_cap.outcome);
    TEST_ASSERT_EQUAL(CONSOLE_REASON_UNKNOWN_ARGUMENT, g_cap.reason);
    TEST_ASSERT_EQUAL_UINT_MESSAGE(0, g_test_commanded_sleep_calls,
                                  "a rejected write must never reach the setter");
}

void test_commanded_mode_enable_web_control_calls_setter() {
    runQuery("system.action.enable-web-control");

    TEST_ASSERT_EQUAL(CONSOLE_OUTCOME_APPLIED, g_cap.outcome);
    TEST_ASSERT_TRUE(g_test_commanded_web_control);
    TEST_ASSERT_EQUAL_UINT(1, g_test_web_control_calls);
}

void test_commanded_mode_disable_web_control_calls_setter() {
    g_test_commanded_web_control = true;
    runQuery("system.action.disable-web-control");

    TEST_ASSERT_EQUAL(CONSOLE_OUTCOME_APPLIED, g_cap.outcome);
    TEST_ASSERT_FALSE(g_test_commanded_web_control);
}

void test_commanded_mode_rc_debug_enable_and_disable() {
    runQuery("rc.action.toggle-debug enabled=true");
    TEST_ASSERT_EQUAL(CONSOLE_OUTCOME_APPLIED, g_cap.outcome);
    TEST_ASSERT_TRUE(g_test_commanded_rc_debug);

    runQuery("rc.action.toggle-debug enabled=false");
    TEST_ASSERT_EQUAL(CONSOLE_OUTCOME_APPLIED, g_cap.outcome);
    TEST_ASSERT_FALSE(g_test_commanded_rc_debug);
    TEST_ASSERT_EQUAL_UINT(2, g_test_commanded_rc_debug_calls);
}

void test_commanded_mode_rc_debug_missing_argument() {
    runQuery("rc.action.toggle-debug");

    TEST_ASSERT_EQUAL(CONSOLE_OUTCOME_INVALID, g_cap.outcome);
    TEST_ASSERT_EQUAL(CONSOLE_REASON_MISSING_ARGUMENT, g_cap.reason);
    TEST_ASSERT_EQUAL_UINT(0, g_test_commanded_rc_debug_calls);
}

void test_commanded_mode_rc_debug_malformed_value() {
    runQuery("rc.action.toggle-debug enabled=maybe");

    TEST_ASSERT_EQUAL(CONSOLE_OUTCOME_INVALID, g_cap.outcome);
    TEST_ASSERT_EQUAL(CONSOLE_REASON_OUT_OF_RANGE, g_cap.reason);
}

// system.action.set-mode is never routed through ACTION_REGISTRY[]/the
// queued RC dispatch, even though it carries a cpp_enum/rc_token for the
// unrelated momentary-RC-switch binding case - proves the direct-executor
// table is checked first, per this ticket's own dispatch-order comment.
void test_commanded_mode_set_mode_never_reaches_the_queued_dispatch() {
    runQuery("system.action.set-mode mode=stationary");

    TEST_ASSERT_EQUAL_UINT_MESSAGE(0, g_test_dispatch_action_calls,
                                  "set-mode must never reach dispatchRcTriggerActionTest()");
}

// =============================================================================
// Drive motion actions (#222): drive.action.move
//
// Consent matrix proven against the exact rule handleDrivePost() applies
// (src/web/api_drive.cpp): blocked = estop || stationary ||
// (!sbusHealthy && !webControlEnabled). Every case here has a REST-side
// twin in test_api_motion_routes.cpp (test_drive_is_rejected_while_estopped,
// test_drive_is_rejected_when_stationary, test_drive_is_rejected_while_sbus_
// lost_and_web_control_disabled, test_drive_is_allowed_while_sbus_lost_but_
// web_control_enabled, test_drive_clamps_to_the_configured_speed_cap) - same
// RobotState inputs, same driveArbiterResolve() evidence, different entry
// point (consoleExecuteCommand() instead of handleDrivePost()).
// =============================================================================

void test_drive_move_blocked_while_estopped() {
    robotState.webControlEnabled = true;
    robotState.estop = true;

    runQuery("drive.action.move speed=100 steer=0");

    TEST_ASSERT_EQUAL(CONSOLE_STATUS_ERR, g_cap.status);
    TEST_ASSERT_EQUAL(CONSOLE_OUTCOME_BLOCKED, g_cap.outcome);
    TEST_ASSERT_EQUAL(CONSOLE_REASON_BLOCKED_BY_STATE, g_cap.reason);
    TEST_ASSERT_EQUAL_INT16(0, resolvedDriveOutput().speed);
}

void test_drive_move_blocked_while_stationary() {
    robotState.webControlEnabled = true;
    robotState.stationary = true;

    runQuery("drive.action.move speed=100 steer=0");

    TEST_ASSERT_EQUAL(CONSOLE_OUTCOME_BLOCKED, g_cap.outcome);
    TEST_ASSERT_EQUAL(CONSOLE_REASON_BLOCKED_BY_STATE, g_cap.reason);
    TEST_ASSERT_EQUAL_INT16(0, resolvedDriveOutput().speed);
}

// Non-RC control gate (ADR 0027): a non-RC source commanding motion while
// the RC link is unhealthy is exactly what this Commanded Mode consents to -
// and only this. webControlEnabled=false with SBUS lost blocks; the same
// state with webControlEnabled=true (below) does not.
void test_drive_move_blocked_while_sbus_lost_and_web_control_disabled() {
    robotState.webControlEnabled = false;
    robotState.sbusSignalLost = true;

    runQuery("drive.action.move speed=100 steer=0");

    TEST_ASSERT_EQUAL(CONSOLE_OUTCOME_BLOCKED, g_cap.outcome);
    TEST_ASSERT_EQUAL(CONSOLE_REASON_BLOCKED_BY_STATE, g_cap.reason);
    TEST_ASSERT_EQUAL_INT16(0, resolvedDriveOutput().speed);
}

void test_drive_move_allowed_while_sbus_lost_but_web_control_enabled() {
    robotState.webControlEnabled = true;
    robotState.sbusSignalLost = true;

    runQuery("drive.action.move speed=100 steer=0");

    TEST_ASSERT_EQUAL(CONSOLE_STATUS_OK, g_cap.status);
    TEST_ASSERT_EQUAL(CONSOLE_OUTCOME_APPLIED, g_cap.outcome);
    TEST_ASSERT_EQUAL_INT16(100, resolvedDriveOutput().speed);
}

// SBUS healthy and web control not enabled: consent is not needed because
// the gate the pin describes ("a non-RC source commanding motion while the
// RC link is unhealthy") does not apply - the RC link is fine.
void test_drive_move_allowed_when_sbus_is_healthy_without_web_control() {
    robotState.webControlEnabled = false;
    robotState.sbusSignalLost = false;
    robotState.sbusHwFailsafe = false;

    runQuery("drive.action.move speed=50 steer=-25");

    TEST_ASSERT_EQUAL(CONSOLE_OUTCOME_APPLIED, g_cap.outcome);
    TEST_ASSERT_EQUAL_INT16(50, resolvedDriveOutput().speed);
    TEST_ASSERT_EQUAL_INT16(-25, resolvedDriveOutput().steer);
}

// A hardware SBUS failsafe is the same "RC link unhealthy" condition as a
// lost signal (handleDrivePost()'s own sbusHealthy computation ORs both).
void test_drive_move_blocked_while_sbus_hw_failsafe_and_web_control_disabled() {
    robotState.webControlEnabled = false;
    robotState.sbusHwFailsafe = true;

    runQuery("drive.action.move speed=100 steer=0");

    TEST_ASSERT_EQUAL(CONSOLE_OUTCOME_BLOCKED, g_cap.outcome);
    TEST_ASSERT_EQUAL(CONSOLE_REASON_BLOCKED_BY_STATE, g_cap.reason);
}

// driveArbiterResolve() applies its OWN clamp to cfg.speedLimitMax
// (src/drive_arbiter.cpp) as a second, independent safety net - so reading
// the output back through resolvedDriveOutput() at the SAME cap the command
// was submitted under would pass even if consoleExecuteDirectDriveMove()'s
// own clamp (mirroring handleDrivePost()'s) were deleted entirely, since the
// arbiter's clamp alone would still produce the same 200/-200. To prove
// THIS executor clamps its own submission (matching handleDrivePost()
// verbatim, not merely relying on the arbiter's downstream net), the cap is
// raised again before resolving: if consoleExecuteDirectDriveMove() had
// submitted the raw 900/-900, the now-1000 cap would let it straight
// through and this test would see 900/-900, not 200/-200.
void test_drive_move_clamps_to_the_configured_speed_cap() {
    robotState.webControlEnabled = true;
    ConfigSnapshot snap = {};
    snap.drive.speedLimitMax = 200;
    configCacheApply(snap);

    runQuery("drive.action.move speed=900 steer=-900");
    TEST_ASSERT_EQUAL(CONSOLE_OUTCOME_APPLIED, g_cap.outcome);

    ConfigSnapshot raised = {};
    configCacheRead(&raised);
    raised.drive.speedLimitMax = 1000;
    configCacheApply(raised);

    const DriveOutput resolved = resolvedDriveOutput();
    TEST_ASSERT_EQUAL_INT16(200, resolved.speed);
    TEST_ASSERT_EQUAL_INT16(-200, resolved.steer);
}

void test_drive_move_rejects_a_missing_argument() {
    robotState.webControlEnabled = true;

    runQuery("drive.action.move speed=100");

    TEST_ASSERT_EQUAL(CONSOLE_OUTCOME_INVALID, g_cap.outcome);
    TEST_ASSERT_EQUAL(CONSOLE_REASON_MISSING_ARGUMENT, g_cap.reason);
    TEST_ASSERT_EQUAL_STRING("steer", capturedValue("argument"));
    TEST_ASSERT_EQUAL_INT16(0, resolvedDriveOutput().speed);
}

// The catalog's own schema (docs/action-registry.yaml: range [-1000, 1000])
// rejects this before driveArbiterSubmit() is ever reached - distinct from
// the speed-cap clamp above, which only applies to values already inside
// the schema's range.
void test_drive_move_rejects_an_out_of_range_argument() {
    robotState.webControlEnabled = true;

    runQuery("drive.action.move speed=5000 steer=0");

    TEST_ASSERT_EQUAL(CONSOLE_OUTCOME_INVALID, g_cap.outcome);
    TEST_ASSERT_EQUAL(CONSOLE_REASON_OUT_OF_RANGE, g_cap.reason);
    TEST_ASSERT_EQUAL_INT16(0, resolvedDriveOutput().speed);
}

void test_drive_move_rejects_an_unknown_argument() {
    robotState.webControlEnabled = true;

    runQuery("drive.action.move speed=100 steer=0 turbo=true");

    TEST_ASSERT_EQUAL(CONSOLE_OUTCOME_INVALID, g_cap.outcome);
    TEST_ASSERT_EQUAL(CONSOLE_REASON_UNKNOWN_ARGUMENT, g_cap.reason);
    TEST_ASSERT_EQUAL_STRING("turbo", capturedValue("argument"));
}

// Consent depends on RobotState, never on which adapter asked - the serial
// terminal is "a trusted local source" for the SAME reason the web adapter
// is: neither is the RC link (docs/console-implementation-specification.md).
void test_drive_move_consent_is_identical_from_both_adapters() {
    robotState.webControlEnabled = false;
    robotState.sbusSignalLost = true;

    runCommandFrom("drive.action.move speed=100 steer=0", CONSOLE_SOURCE_SERIAL);
    TEST_ASSERT_EQUAL(CONSOLE_OUTCOME_BLOCKED, g_cap.outcome);

    runCommandFrom("drive.action.move speed=100 steer=0", CONSOLE_SOURCE_WEB);
    TEST_ASSERT_EQUAL(CONSOLE_OUTCOME_BLOCKED, g_cap.outcome);

    robotState.webControlEnabled = true;

    runCommandFrom("drive.action.move speed=100 steer=0", CONSOLE_SOURCE_SERIAL);
    TEST_ASSERT_EQUAL(CONSOLE_OUTCOME_APPLIED, g_cap.outcome);

    runCommandFrom("drive.action.move speed=100 steer=0", CONSOLE_SOURCE_WEB);
    TEST_ASSERT_EQUAL(CONSOLE_OUTCOME_APPLIED, g_cap.outcome);
}

// =============================================================================
// Drive motion actions (#222): drive.action.speed-preset-{slow,normal,turbo}
//
// applySpeedPresetPersisted() is the SAME function handleSpeedPresetPost()
// calls (src/web/api_drive.cpp) - reused verbatim, observed here through the
// same g_test_persisted_speed_preset/g_test_speed_preset_persist_ok stub
// test_api_motion_routes.cpp already uses (src/native_test_stubs.cpp).
// handleSpeedPresetPost() applies no estop/stationary/sbus-health gate, so
// these executors add none - proven explicitly below, not just by omission.
// =============================================================================

void test_speed_preset_slow_applies_the_persisted_preset() {
    runQuery("drive.action.speed-preset-slow preset=slow");

    TEST_ASSERT_EQUAL(CONSOLE_STATUS_OK, g_cap.status);
    TEST_ASSERT_EQUAL(CONSOLE_OUTCOME_APPLIED, g_cap.outcome);
    TEST_ASSERT_EQUAL(SpeedPresetId::Slow, g_test_persisted_speed_preset);
}

void test_speed_preset_normal_applies_the_persisted_preset() {
    runQuery("drive.action.speed-preset-normal preset=normal");

    TEST_ASSERT_EQUAL(CONSOLE_OUTCOME_APPLIED, g_cap.outcome);
    TEST_ASSERT_EQUAL(SpeedPresetId::Normal, g_test_persisted_speed_preset);
}

void test_speed_preset_turbo_applies_the_persisted_preset() {
    runQuery("drive.action.speed-preset-turbo preset=turbo");

    TEST_ASSERT_EQUAL(CONSOLE_OUTCOME_APPLIED, g_cap.outcome);
    TEST_ASSERT_EQUAL(SpeedPresetId::Turbo, g_test_persisted_speed_preset);
}

// Each speed-preset action's own catalog schema pins `preset` to that
// action's own name (docs/action-registry.yaml `values:` per entry) - a
// mismatched value is a schema failure, not a silently-ignored argument.
void test_speed_preset_rejects_a_mismatched_preset_value() {
    runQuery("drive.action.speed-preset-slow preset=turbo");

    TEST_ASSERT_EQUAL(CONSOLE_OUTCOME_INVALID, g_cap.outcome);
    TEST_ASSERT_EQUAL(CONSOLE_REASON_OUT_OF_RANGE, g_cap.reason);
}

void test_speed_preset_rejects_a_missing_argument() {
    runQuery("drive.action.speed-preset-normal");

    TEST_ASSERT_EQUAL(CONSOLE_OUTCOME_INVALID, g_cap.outcome);
    TEST_ASSERT_EQUAL(CONSOLE_REASON_MISSING_ARGUMENT, g_cap.reason);
}

void test_speed_preset_reports_a_failed_persist_as_an_explicit_error() {
    g_test_speed_preset_persist_ok = false;

    runQuery("drive.action.speed-preset-turbo preset=turbo");

    TEST_ASSERT_EQUAL(CONSOLE_STATUS_ERR, g_cap.status);
    TEST_ASSERT_EQUAL(CONSOLE_OUTCOME_INTERNAL_ERROR, g_cap.outcome);
}

// Explicit negative-space proof for the pin's scope note: speed-preset
// writes reach applySpeedPresetPersisted() even under estop, stationary and
// a disabled/unhealthy RC link at once - matching handleSpeedPresetPost(),
// which never reads any of those three RobotState fields. A broader gate
// than api_drive.cpp's own would fail this test.
void test_speed_preset_has_no_motion_consent_gate() {
    robotState.estop = true;
    robotState.stationary = true;
    robotState.webControlEnabled = false;
    robotState.sbusSignalLost = true;

    runQuery("drive.action.speed-preset-slow preset=slow");

    TEST_ASSERT_EQUAL(CONSOLE_OUTCOME_APPLIED, g_cap.outcome);
    TEST_ASSERT_EQUAL(SpeedPresetId::Slow, g_test_persisted_speed_preset);
}

// =============================================================================
// system.config.mood (#226 criterion 4: the config-typed view of active mood)
// =============================================================================

void test_mood_config_read_reports_the_live_active_mood() {
    robotState.activeMood = 11;

    runQuery("system.config.mood");

    TEST_ASSERT_TRUE(g_cap.beginCalled);
    TEST_ASSERT_EQUAL(CONSOLE_OUTCOME_COMPLETED, g_cap.outcome);
    TEST_ASSERT_EQUAL_STRING("11", capturedValue("value"));
}

void test_mood_config_write_applies_a_valid_mood() {
    runQuery("system.config.mood value=14");

    TEST_ASSERT_EQUAL(CONSOLE_OUTCOME_APPLIED, g_cap.outcome);
    TEST_ASSERT_EQUAL_UINT(14, g_test_applied_mood);
}

void test_mood_config_write_rejects_an_invalid_mood_id() {
    runQuery("system.config.mood value=99");

    TEST_ASSERT_EQUAL(CONSOLE_OUTCOME_INVALID, g_cap.outcome);
    TEST_ASSERT_EQUAL(CONSOLE_REASON_OUT_OF_RANGE, g_cap.reason);
    TEST_ASSERT_EQUAL_UINT(0, g_test_applied_mood);
}

// =============================================================================
// #221 remainder: the named parameterized action executors reachable through
// g_directActionExecutors[] (#222/#226's table, extended - not a second
// dispatch mechanism). Each drives consoleExecuteCommand() with a real
// "operation args" line, matching the marcduino/drive-motion tests' own
// shape above, not a direct call to the static executor function.
// =============================================================================

// system.action.set-mood: shares applyMood() with system.config.mood above.
void test_direct_set_mood_applies_valid_mood_and_broadcasts() {
    runQuery("system.action.set-mood mood=13");

    TEST_ASSERT_FALSE_MESSAGE(g_cap.beginCalled, "an action write answers a single result");
    TEST_ASSERT_EQUAL(CONSOLE_STATUS_OK, g_cap.status);
    TEST_ASSERT_EQUAL(CONSOLE_OUTCOME_APPLIED, g_cap.outcome);
    TEST_ASSERT_EQUAL_UINT(13, g_test_applied_mood);
    TEST_ASSERT_EQUAL_UINT(1, g_test_status_broadcast_count);
}

void test_direct_set_mood_rejects_an_invalid_mood_id() {
    runQuery("system.action.set-mood mood=99");

    TEST_ASSERT_EQUAL(CONSOLE_OUTCOME_INVALID, g_cap.outcome);
    TEST_ASSERT_EQUAL(CONSOLE_REASON_OUT_OF_RANGE, g_cap.reason);
    TEST_ASSERT_EQUAL_UINT(0, g_test_applied_mood);
}

void test_direct_set_mood_rejects_a_missing_argument() {
    runQuery("system.action.set-mood");

    TEST_ASSERT_EQUAL(CONSOLE_OUTCOME_INVALID, g_cap.outcome);
    TEST_ASSERT_EQUAL(CONSOLE_REASON_MISSING_ARGUMENT, g_cap.reason);
}

// The sleep gate handleMoodPost() applies but system.config.mood's own write
// path (consoleExecuteMoodConfig() above) does not - a pre-existing
// discrepancy between the two rows this new row does not paper over.
void test_direct_set_mood_blocked_while_sleeping() {
    robotState.sleepMode = true;

    runQuery("system.action.set-mood mood=13");

    TEST_ASSERT_EQUAL(CONSOLE_STATUS_ERR, g_cap.status);
    TEST_ASSERT_EQUAL(CONSOLE_OUTCOME_BLOCKED, g_cap.outcome);
    TEST_ASSERT_EQUAL(CONSOLE_REASON_TEMPORARILY_UNAVAILABLE, g_cap.reason);
    TEST_ASSERT_EQUAL_UINT(0, g_test_applied_mood);
}

// system.action.set-identity: droidName=/mdnsUseName=, the same fields
// handleIdentityPost() reads, applied through the shared
// identitySetCommitApplied() Commit Step (include/api_identity.h).
void test_direct_set_identity_applies_and_persists() {
    runQuery("system.action.set-identity droidName=chopper mdnsUseName=true");

    TEST_ASSERT_EQUAL(CONSOLE_STATUS_OK, g_cap.status);
    TEST_ASSERT_EQUAL(CONSOLE_OUTCOME_APPLIED, g_cap.outcome);

    ConfigSnapshot snap = {};
    configCacheRead(&snap);
    TEST_ASSERT_EQUAL_STRING("chopper", snap.system.droid_name);
    TEST_ASSERT_TRUE(snap.system.mdns_use_name);
}

// mdnsUseName is optional (docs/action-registry.yaml required: false) and
// defaults to false when omitted, the same default handleIdentityPost()
// applies.
void test_direct_set_identity_defaults_mdns_to_false_when_omitted() {
    runQuery("system.action.set-identity droidName=r2d2");

    TEST_ASSERT_EQUAL(CONSOLE_OUTCOME_APPLIED, g_cap.outcome);
    ConfigSnapshot snap = {};
    configCacheRead(&snap);
    TEST_ASSERT_FALSE(snap.system.mdns_use_name);
}

void test_direct_set_identity_rejects_an_invalid_name() {
    runQuery("system.action.set-identity droidName=\"R2 D2\"");

    TEST_ASSERT_EQUAL(CONSOLE_OUTCOME_INVALID, g_cap.outcome);
    TEST_ASSERT_EQUAL(CONSOLE_REASON_OUT_OF_RANGE, g_cap.reason);
}

void test_direct_set_identity_rejects_a_missing_name() {
    runQuery("system.action.set-identity");

    TEST_ASSERT_EQUAL(CONSOLE_OUTCOME_INVALID, g_cap.outcome);
    TEST_ASSERT_EQUAL(CONSOLE_REASON_MISSING_ARGUMENT, g_cap.reason);
}

// rc.action.test-bindable: token=<rc-token> - "arm1_toggle" is
// SERVO_ACTION_ARM1_TOGGLE's own rc_token (src/rc_action_types.cpp,
// docs/action-registry.yaml), a non-analog, non-payload target the guard
// allows once web control is enabled - the same dispatch core/stub
// test_action_alias_resolves_same_target_as_canonical exercises above,
// reached here through the by-token path instead of the canonical name.
void test_direct_test_bindable_dispatches_by_rc_token() {
    robotState.webControlEnabled = true;

    runQuery("rc.action.test-bindable token=arm1_toggle");

    TEST_ASSERT_EQUAL_UINT(1u, g_test_dispatch_action_calls);
    TEST_ASSERT_EQUAL(SERVO_ACTION_ARM1_TOGGLE, g_test_last_dispatch_target);
    TEST_ASSERT_EQUAL(CONSOLE_STATUS_OK, g_cap.status);
    TEST_ASSERT_EQUAL(CONSOLE_OUTCOME_QUEUED, g_cap.outcome);
}

void test_direct_test_bindable_rejects_a_missing_token() {
    runQuery("rc.action.test-bindable");

    TEST_ASSERT_EQUAL_UINT(0u, g_test_dispatch_action_calls);
    TEST_ASSERT_EQUAL(CONSOLE_OUTCOME_INVALID, g_cap.outcome);
    TEST_ASSERT_EQUAL(CONSOLE_REASON_MISSING_ARGUMENT, g_cap.reason);
}

void test_direct_test_bindable_rejects_an_unknown_token() {
    runQuery("rc.action.test-bindable token=not_a_real_token");

    TEST_ASSERT_EQUAL_UINT(0u, g_test_dispatch_action_calls);
    TEST_ASSERT_EQUAL(CONSOLE_OUTCOME_INVALID, g_cap.outcome);
    TEST_ASSERT_EQUAL(CONSOLE_REASON_OUT_OF_RANGE, g_cap.reason);
}

// Matches REST's own guard exactly: evaluateActionTestGuard() blocks every
// target (not just payload-needing ones) when web control is disabled -
// requires_web_control: true (docs/action-registry.yaml).
void test_direct_test_bindable_blocked_when_web_control_disabled() {
    robotState.webControlEnabled = false;

    runQuery("rc.action.test-bindable token=arm1_toggle");

    TEST_ASSERT_EQUAL_UINT(0u, g_test_dispatch_action_calls);
    TEST_ASSERT_EQUAL(CONSOLE_OUTCOME_BLOCKED, g_cap.outcome);
    TEST_ASSERT_EQUAL(CONSOLE_REASON_BLOCKED_BY_STATE, g_cap.reason);
}

// Never widens to accept the Marcduino payload carve-out
// consoleExecuteAction() grants dome.action.marcduino-sequence/-command
// above: REST's /api/actions/test never accepted one either, so a
// payload-needing target reached BY TOKEN answers the same as any other
// guard-refused target, never EXECUTOR_NOT_READY's Marcduino exception.
void test_direct_test_bindable_never_widens_to_a_payload_target() {
    robotState.webControlEnabled = true;

    runQuery("rc.action.test-bindable token=seq");  // dome_action_marcduino_sequence's rc_token

    TEST_ASSERT_EQUAL_UINT(0u, g_test_dispatch_action_calls);
    TEST_ASSERT_EQUAL(CONSOLE_OUTCOME_UNAVAILABLE, g_cap.outcome);
    TEST_ASSERT_EQUAL(CONSOLE_REASON_EXECUTOR_NOT_READY, g_cap.reason);
}

// =============================================================================
// sound.action.play-track / sound.action.set-volume (#221 remainder)
// =============================================================================

void test_sound_play_track_queues_and_carries_the_track_number() {
    runQuery("sound.action.play-track track=42");

    TEST_ASSERT_EQUAL(CONSOLE_STATUS_OK, g_cap.status);
    TEST_ASSERT_EQUAL(CONSOLE_OUTCOME_QUEUED, g_cap.outcome);
    TEST_ASSERT_EQUAL_UINT(1u, g_test_audio_play_track_calls);
    TEST_ASSERT_EQUAL_UINT16(42, g_test_audio_last_track);
}

void test_sound_play_track_rejects_an_out_of_range_track() {
    runQuery("sound.action.play-track track=1000");

    TEST_ASSERT_EQUAL(CONSOLE_OUTCOME_INVALID, g_cap.outcome);
    TEST_ASSERT_EQUAL(CONSOLE_REASON_OUT_OF_RANGE, g_cap.reason);
    TEST_ASSERT_EQUAL_UINT(0u, g_test_audio_play_track_calls);
}

// The same sleep gate handleAudioPost()'s action=play branch applies.
void test_sound_play_track_blocked_while_sleeping() {
    robotState.sleepMode = true;

    runQuery("sound.action.play-track track=1");

    TEST_ASSERT_EQUAL(CONSOLE_OUTCOME_BLOCKED, g_cap.outcome);
    TEST_ASSERT_EQUAL(CONSOLE_REASON_TEMPORARILY_UNAVAILABLE, g_cap.reason);
    TEST_ASSERT_EQUAL_UINT(0u, g_test_audio_play_track_calls);
}

void test_sound_play_track_reports_a_full_queue() {
    g_test_audio_queue_ok = false;

    runQuery("sound.action.play-track track=1");

    TEST_ASSERT_EQUAL(CONSOLE_STATUS_ERR, g_cap.status);
    TEST_ASSERT_EQUAL(CONSOLE_OUTCOME_QUEUE_FULL, g_cap.outcome);
    TEST_ASSERT_EQUAL(CONSOLE_REASON_QUEUE_FULL, g_cap.reason);
}

void test_sound_set_volume_applies_and_persists() {
    runQuery("sound.action.set-volume volume=17");

    TEST_ASSERT_EQUAL(CONSOLE_STATUS_OK, g_cap.status);
    TEST_ASSERT_EQUAL(CONSOLE_OUTCOME_APPLIED, g_cap.outcome);
    TEST_ASSERT_EQUAL_UINT(1u, g_test_audio_volume_calls);
    TEST_ASSERT_EQUAL_UINT8(17, g_test_audio_last_volume);

    ConfigSnapshot snap = {};
    configCacheRead(&snap);
    TEST_ASSERT_EQUAL_UINT8(17, snap.audio.audioVolume);
}

void test_sound_set_volume_rejects_an_out_of_range_level() {
    runQuery("sound.action.set-volume volume=31");

    TEST_ASSERT_EQUAL(CONSOLE_OUTCOME_INVALID, g_cap.outcome);
    TEST_ASSERT_EQUAL(CONSOLE_REASON_OUT_OF_RANGE, g_cap.reason);
    TEST_ASSERT_EQUAL_UINT(0u, g_test_audio_volume_calls);
}

void test_sound_set_volume_reports_a_full_queue() {
    g_test_audio_queue_ok = false;

    runQuery("sound.action.set-volume volume=10");

    TEST_ASSERT_EQUAL(CONSOLE_STATUS_ERR, g_cap.status);
    TEST_ASSERT_EQUAL(CONSOLE_OUTCOME_QUEUE_FULL, g_cap.outcome);
    TEST_ASSERT_EQUAL(CONSOLE_REASON_QUEUE_FULL, g_cap.reason);
}

// =============================================================================
// aux.action.led-color / aux.action.led-effect (#221 remainder)
// =============================================================================

void test_aux_led_color_queues_a_valid_rgb_triple() {
    robotState.auxLed.available = true;
    robotState.auxLed.pin = 5;

    runQuery("aux.action.led-color r=10 g=20 b=30");

    TEST_ASSERT_EQUAL(CONSOLE_STATUS_OK, g_cap.status);
    TEST_ASSERT_EQUAL(CONSOLE_OUTCOME_QUEUED, g_cap.outcome);
    TEST_ASSERT_EQUAL_UINT8(10, robotState.auxLed.r);
    TEST_ASSERT_EQUAL_UINT8(20, robotState.auxLed.g);
    TEST_ASSERT_EQUAL_UINT8(30, robotState.auxLed.b);
}

void test_aux_led_color_rejects_an_out_of_range_component() {
    runQuery("aux.action.led-color r=10 g=300 b=30");

    TEST_ASSERT_EQUAL(CONSOLE_OUTCOME_INVALID, g_cap.outcome);
    TEST_ASSERT_EQUAL(CONSOLE_REASON_OUT_OF_RANGE, g_cap.reason);
    TEST_ASSERT_EQUAL_STRING("g", capturedValue("argument"));
}

void test_aux_led_color_reports_component_disabled_when_pin_unset() {
    robotState.auxLed.available = true;
    robotState.auxLed.pin = 0;  // no pin selected: aux.config.led-pin's "disabled" state
    // The native auxLedQueueSetColor() stub (src/native_test_stubs.cpp) only
    // gates on g_test_aux_led_queue_ok, unlike the real implementation
    // (src/tasks/aux_led.cpp, not in [env:native]'s build filter) which also
    // refuses when the strip is unavailable - so this forces the same
    // refusal the real availability gate would produce, to prove
    // consoleAnswerAuxLedRefusal() picks COMPONENT_DISABLED over QUEUE_FULL
    // from robotState.auxLed alone once the call has failed either way.
    g_test_aux_led_queue_ok = false;

    runQuery("aux.action.led-color r=1 g=1 b=1");

    TEST_ASSERT_EQUAL(CONSOLE_OUTCOME_UNAVAILABLE, g_cap.outcome);
    TEST_ASSERT_EQUAL(CONSOLE_REASON_COMPONENT_DISABLED, g_cap.reason);
}

void test_aux_led_color_reports_queue_full_when_available_but_refused() {
    robotState.auxLed.available = true;
    robotState.auxLed.pin = 5;
    g_test_aux_led_queue_ok = false;

    runQuery("aux.action.led-color r=1 g=1 b=1");

    TEST_ASSERT_EQUAL(CONSOLE_OUTCOME_QUEUE_FULL, g_cap.outcome);
    TEST_ASSERT_EQUAL(CONSOLE_REASON_QUEUE_FULL, g_cap.reason);
}

void test_aux_led_effect_queues_a_valid_effect() {
    robotState.auxLed.available = true;
    robotState.auxLed.pin = 5;

    runQuery("aux.action.led-effect effect=pulse");

    TEST_ASSERT_EQUAL(CONSOLE_STATUS_OK, g_cap.status);
    TEST_ASSERT_EQUAL(CONSOLE_OUTCOME_QUEUED, g_cap.outcome);
}

// Regression guard for the registry-generator defect this executor's own
// comment documents: tools/generate_console_catalog.py's YAML loader turns
// the registry's bare `off` value into the Python boolean False, so
// g_enum_aux_action_led_effect_effect[] carries the literal string "False"
// where "off" belongs (src/console/console_catalog.cpp). This executor
// bypasses that enum for its value check (parseAuxLedEffect() directly), so
// "off" - a legitimate value handleAuxLedEffectPost() accepts today - must
// still be accepted here, not rejected as an unlisted enum value.
void test_aux_led_effect_accepts_off_despite_the_buggy_catalog_enum() {
    robotState.auxLed.available = true;
    robotState.auxLed.pin = 5;

    runQuery("aux.action.led-effect effect=off");

    TEST_ASSERT_EQUAL(CONSOLE_STATUS_OK, g_cap.status);
    TEST_ASSERT_EQUAL(CONSOLE_OUTCOME_QUEUED, g_cap.outcome);
}

void test_aux_led_effect_rejects_an_unknown_effect_string() {
    runQuery("aux.action.led-effect effect=strobe");

    TEST_ASSERT_EQUAL(CONSOLE_OUTCOME_INVALID, g_cap.outcome);
    TEST_ASSERT_EQUAL(CONSOLE_REASON_OUT_OF_RANGE, g_cap.reason);
}

void test_aux_led_effect_rejects_an_unknown_argument() {
    runQuery("aux.action.led-effect mode=pulse");

    TEST_ASSERT_EQUAL(CONSOLE_OUTCOME_INVALID, g_cap.outcome);
    TEST_ASSERT_EQUAL(CONSOLE_REASON_UNKNOWN_ARGUMENT, g_cap.reason);
    TEST_ASSERT_EQUAL_STRING("mode", capturedValue("argument"));
}

// =============================================================================
// servo.action.open/close/set-position (#221 remainder). servoCmdQueue's
// enqueue always succeeds on native (the FreeRTOS queue stub's xQueueSend()
// always returns pdTRUE regardless of the queue handle - test/stubs/include/
// freertos/queue.h - and servo carries no purpose-built success/failure
// toggle the way audio/aux-led do), so only the validation and success paths
// are provable here; the queue-full branch is device behaviour, matching
// this project's existing precedent for every other servoCmdQueue caller.
// =============================================================================

void test_servo_open_queues_with_the_resolved_arm_id() {
    runQuery("servo.action.open target=aux2");

    TEST_ASSERT_EQUAL(CONSOLE_STATUS_OK, g_cap.status);
    TEST_ASSERT_EQUAL(CONSOLE_OUTCOME_QUEUED, g_cap.outcome);
}

void test_servo_open_accepts_both_as_the_broadcast_target() {
    runQuery("servo.action.open target=both");

    TEST_ASSERT_EQUAL(CONSOLE_OUTCOME_QUEUED, g_cap.outcome);
}

void test_servo_close_rejects_an_unknown_target() {
    runQuery("servo.action.close target=aux9");

    TEST_ASSERT_EQUAL(CONSOLE_OUTCOME_INVALID, g_cap.outcome);
    TEST_ASSERT_EQUAL(CONSOLE_REASON_OUT_OF_RANGE, g_cap.reason);
}

// set-position's own catalog enum excludes "both" (docs/action-registry.yaml)
// even though handleServoPost()'s parseArmId() would accept it for any
// action - narrower than REST here is not "widening" and is the registry's
// own declared shape, not invented in this dispatch code.
void test_servo_set_position_rejects_both_though_open_close_accept_it() {
    runQuery("servo.action.set-position target=both position_us=1500");

    TEST_ASSERT_EQUAL(CONSOLE_OUTCOME_INVALID, g_cap.outcome);
    TEST_ASSERT_EQUAL(CONSOLE_REASON_OUT_OF_RANGE, g_cap.reason);
}

void test_servo_set_position_queues_with_a_valid_pulse_width() {
    runQuery("servo.action.set-position target=arm1 position_us=1800");

    TEST_ASSERT_EQUAL(CONSOLE_OUTCOME_QUEUED, g_cap.outcome);
}

void test_servo_set_position_rejects_an_out_of_range_pulse_width() {
    runQuery("servo.action.set-position target=arm1 position_us=100");

    TEST_ASSERT_EQUAL(CONSOLE_OUTCOME_INVALID, g_cap.outcome);
    TEST_ASSERT_EQUAL(CONSOLE_REASON_OUT_OF_RANGE, g_cap.reason);
}

void test_servo_set_position_rejects_a_missing_target() {
    runQuery("servo.action.set-position position_us=1500");

    TEST_ASSERT_EQUAL(CONSOLE_OUTCOME_INVALID, g_cap.outcome);
    TEST_ASSERT_EQUAL(CONSOLE_REASON_MISSING_ARGUMENT, g_cap.reason);
}

// Deliberately unwired (see consoleExecuteServoCommand()'s own header
// comment): the registry declares zero params for this row, but the real
// /api/servo endpoint requires an arm for every action including "stop", and
// armId=255 only broadcasts to arm1+arm2, never aux1..3 - a registry/
// implementation decision this ticket does not invent.
void test_servo_stop_still_answers_executor_not_ready() {
    runQuery("servo.action.stop");

    TEST_ASSERT_EQUAL(CONSOLE_OUTCOME_UNAVAILABLE, g_cap.outcome);
    TEST_ASSERT_EQUAL(CONSOLE_REASON_EXECUTOR_NOT_READY, g_cap.reason);
}

// #257: g_directActionExecutors[] split into five per-domain tables
// (include/console_direct_action_{system,drive,sound,aux_rc,servo}.h). Every
// row's own behavior is already asserted above by name (e.g.
// test_commanded_mode_set_mode_stationary_calls_setter_and_broadcasts,
// test_direct_set_mood_applies_valid_mood_and_broadcasts, ...); this is the
// split's own completeness guard, the one the ticket's proof obligation is
// about - all 20 canonical names the single pre-split table carried, called
// with no arguments and asserted to still resolve to a real executor. A row
// silently dropped from a domain header answers EXECUTOR_NOT_READY (no
// dispatch-table row) or UNKNOWN_OPERATION (not even in the catalog) instead
// of running its own argument validation - this fails on either, so a future
// domain-file edit that drops a row fails here even if it also deletes that
// row's own dedicated test above.
void test_257_every_direct_action_row_still_dispatches() {
    static const char* kExpectedDirectActionOperations[] = {
        "system.action.set-mode",
        "system.action.sleep",
        "system.action.wake",
        "system.action.enable-web-control",
        "system.action.disable-web-control",
        "rc.action.toggle-debug",
        "drive.action.move",
        "drive.action.speed-preset-slow",
        "drive.action.speed-preset-normal",
        "drive.action.speed-preset-turbo",
        "system.action.set-mood",
        "system.action.set-identity",
        "rc.action.test-bindable",
        "sound.action.play-track",
        "sound.action.set-volume",
        "aux.action.led-color",
        "aux.action.led-effect",
        "servo.action.open",
        "servo.action.close",
        "servo.action.set-position",
    };
    static const size_t kExpectedCount =
        sizeof(kExpectedDirectActionOperations) / sizeof(kExpectedDirectActionOperations[0]);

    for (size_t i = 0; i < kExpectedCount; ++i) {
        runQuery(kExpectedDirectActionOperations[i]);
        TEST_ASSERT_FALSE_MESSAGE(g_cap.outcome == CONSOLE_OUTCOME_UNAVAILABLE &&
                                       g_cap.reason == CONSOLE_REASON_EXECUTOR_NOT_READY,
                                   kExpectedDirectActionOperations[i]);
        TEST_ASSERT_FALSE_MESSAGE(g_cap.outcome == CONSOLE_OUTCOME_INVALID &&
                                       g_cap.reason == CONSOLE_REASON_UNKNOWN_OPERATION,
                                   kExpectedDirectActionOperations[i]);
        TEST_ASSERT_TRUE_MESSAGE(g_cap.resultCalled || g_cap.endCalled,
                                 kExpectedDirectActionOperations[i]);
    }
}

// dome.action.save-sequence (#259) is the one dome.action.* row #259
// deliberately leaves EXECUTOR_NOT_READY: its REST body (POST /api/seq, a
// full Learned Sequence JSON v1 document with a steps array) is the
// "document/bulk transfer" #206 names out of scope for this epic, and the
// Console's one-line key=value argument grammar has no shape for it - see
// include/console_direct_action_dome.h's own header comment for the full
// reasoning. Asserted here so a future accidental wiring (or an accidental
// unwiring) of this specific row is caught by name, not folded into the
// aggregate #220 report count.
void test_action_save_sequence_stays_executor_not_ready_document_transfer_out_of_scope() {
    runQuery("dome.action.save-sequence");

    TEST_ASSERT_EQUAL(CONSOLE_OUTCOME_UNAVAILABLE, g_cap.outcome);
    TEST_ASSERT_EQUAL(CONSOLE_REASON_EXECUTOR_NOT_READY, g_cap.reason);
}

// The five dome.api.* rows have no consoleExecuteCommand()-reachable behavior
// to assert on at all - see the worker status comment on #221 for why
// (chunked/paginated JSON reads with no Console Record equivalent, not this
// ticket's pattern to invent).

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

    // dome.action.* direct executors (#259, include/console_direct_action_dome.h)
    RUN_TEST(test_action_dome_sequence_unknown_argument_is_rejected);
    RUN_TEST(test_action_dome_sequence_missing_value_answers_missing_argument);
    RUN_TEST(test_action_dome_sequence_rejects_a_non_dm_value);
    RUN_TEST(test_action_dome_sequence_rejects_a_value_too_long_for_dome_tx);
    RUN_TEST(test_action_dome_sequence_unknown_dm_name_forwards_to_dome_fallback);
    RUN_TEST(test_action_dome_sequence_catalog_name_queues_through_the_dispatcher);
    RUN_TEST(test_action_send_command_unknown_argument_is_rejected);
    RUN_TEST(test_action_send_command_missing_command_answers_missing_argument);
    RUN_TEST(test_action_send_command_unsupported_keyword_answers_out_of_range);
    RUN_TEST(test_action_send_command_dome_forward_prefix_is_blocked_while_sleeping);
    RUN_TEST(test_action_send_command_keyword_is_not_blocked_by_sleep);
    RUN_TEST(test_action_send_command_estop_keyword_dispatches_through_the_real_core);
    RUN_TEST(test_action_sequence_stop_rejects_any_argument);
    RUN_TEST(test_action_sequence_stop_sets_the_transient_flag);
    RUN_TEST(test_action_dome_move_unknown_argument_is_rejected);
    RUN_TEST(test_action_dome_move_missing_speed_answers_missing_argument);
    RUN_TEST(test_action_dome_move_out_of_range_speed_is_rejected);
    RUN_TEST(test_action_dome_move_is_blocked_while_sleeping);
    RUN_TEST(test_action_dome_move_is_refused_when_dome_output_is_disabled);
    RUN_TEST(test_action_dome_move_queues_when_enabled);
    RUN_TEST(test_action_delete_sequence_unknown_argument_is_rejected);
    RUN_TEST(test_action_delete_sequence_missing_name_answers_missing_argument);
    RUN_TEST(test_action_delete_sequence_unknown_name_answers_out_of_range);
    RUN_TEST(test_action_delete_sequence_store_failure_answers_internal_error);
    RUN_TEST(test_action_delete_sequence_success_calls_the_real_store_delete);
    RUN_TEST(test_action_test_sequence_unknown_argument_is_rejected);
    RUN_TEST(test_action_test_sequence_missing_name_answers_missing_argument);
    RUN_TEST(test_action_test_sequence_rejects_a_non_dm_name);
    RUN_TEST(test_action_test_sequence_valid_name_queues_through_the_dispatcher);
    RUN_TEST(test_action_save_sequence_stays_executor_not_ready_document_transfer_out_of_scope);

    RUN_TEST(test_component_toggle_read_reports_saved_and_active);
    RUN_TEST(test_component_toggle_write_persists_and_reports_staged_until_reboot);
    RUN_TEST(test_component_toggle_write_accepts_the_named_key_not_only_value);
    RUN_TEST(test_component_toggle_write_rejects_an_unknown_argument);
    RUN_TEST(test_component_toggle_write_rejects_a_malformed_boolean);
    RUN_TEST(test_component_toggle_table_paramkeys_match_config_apply);
    RUN_TEST(test_drive_speed_limit_read_and_write);
    RUN_TEST(test_drive_speed_limit_rejects_out_of_range);
    RUN_TEST(test_aux_led_pin_read_and_write);
    RUN_TEST(test_aux_led_count_read_and_write);
    RUN_TEST(test_rc_mode_read_and_write);
    RUN_TEST(test_rc_mode_rejects_an_unknown_mode_string);
    RUN_TEST(test_scalar_config_write_rejects_an_unknown_argument);
    RUN_TEST(test_config_write_reports_busy_when_the_mutex_is_already_held);
    RUN_TEST(test_config_write_releases_the_mutex_after_a_successful_write);
    RUN_TEST(test_config_write_rejected_before_apply_never_touches_the_mutex);
    RUN_TEST(test_config_executor_not_ready_count_report);

    RUN_TEST(test_commanded_mode_set_mode_stationary_calls_setter_and_broadcasts);
    RUN_TEST(test_commanded_mode_set_mode_driving_calls_setter);
    RUN_TEST(test_commanded_mode_set_mode_rejects_an_invalid_value);
    RUN_TEST(test_commanded_mode_set_mode_missing_argument);
    RUN_TEST(test_commanded_mode_sleep_broadcasts_only_on_a_real_transition);
    RUN_TEST(test_commanded_mode_wake_calls_setter);
    RUN_TEST(test_commanded_mode_sleep_rejects_an_argument);
    RUN_TEST(test_commanded_mode_enable_web_control_calls_setter);
    RUN_TEST(test_commanded_mode_disable_web_control_calls_setter);
    RUN_TEST(test_commanded_mode_rc_debug_enable_and_disable);
    RUN_TEST(test_commanded_mode_rc_debug_missing_argument);
    RUN_TEST(test_commanded_mode_rc_debug_malformed_value);
    RUN_TEST(test_commanded_mode_set_mode_never_reaches_the_queued_dispatch);

    RUN_TEST(test_drive_move_blocked_while_estopped);
    RUN_TEST(test_drive_move_blocked_while_stationary);
    RUN_TEST(test_drive_move_blocked_while_sbus_lost_and_web_control_disabled);
    RUN_TEST(test_drive_move_allowed_while_sbus_lost_but_web_control_enabled);
    RUN_TEST(test_drive_move_allowed_when_sbus_is_healthy_without_web_control);
    RUN_TEST(test_drive_move_blocked_while_sbus_hw_failsafe_and_web_control_disabled);
    RUN_TEST(test_drive_move_clamps_to_the_configured_speed_cap);
    RUN_TEST(test_drive_move_rejects_a_missing_argument);
    RUN_TEST(test_drive_move_rejects_an_out_of_range_argument);
    RUN_TEST(test_drive_move_rejects_an_unknown_argument);
    RUN_TEST(test_drive_move_consent_is_identical_from_both_adapters);

    RUN_TEST(test_speed_preset_slow_applies_the_persisted_preset);
    RUN_TEST(test_speed_preset_normal_applies_the_persisted_preset);
    RUN_TEST(test_speed_preset_turbo_applies_the_persisted_preset);
    RUN_TEST(test_speed_preset_rejects_a_mismatched_preset_value);
    RUN_TEST(test_speed_preset_rejects_a_missing_argument);
    RUN_TEST(test_speed_preset_reports_a_failed_persist_as_an_explicit_error);
    RUN_TEST(test_speed_preset_has_no_motion_consent_gate);

    RUN_TEST(test_mood_config_read_reports_the_live_active_mood);
    RUN_TEST(test_mood_config_write_applies_a_valid_mood);
    RUN_TEST(test_mood_config_write_rejects_an_invalid_mood_id);

    RUN_TEST(test_direct_set_mood_applies_valid_mood_and_broadcasts);
    RUN_TEST(test_direct_set_mood_rejects_an_invalid_mood_id);
    RUN_TEST(test_direct_set_mood_rejects_a_missing_argument);
    RUN_TEST(test_direct_set_mood_blocked_while_sleeping);

    RUN_TEST(test_direct_set_identity_applies_and_persists);
    RUN_TEST(test_direct_set_identity_defaults_mdns_to_false_when_omitted);
    RUN_TEST(test_direct_set_identity_rejects_an_invalid_name);
    RUN_TEST(test_direct_set_identity_rejects_a_missing_name);

    RUN_TEST(test_direct_test_bindable_dispatches_by_rc_token);
    RUN_TEST(test_direct_test_bindable_rejects_a_missing_token);
    RUN_TEST(test_direct_test_bindable_rejects_an_unknown_token);
    RUN_TEST(test_direct_test_bindable_blocked_when_web_control_disabled);
    RUN_TEST(test_direct_test_bindable_never_widens_to_a_payload_target);

    RUN_TEST(test_sound_play_track_queues_and_carries_the_track_number);
    RUN_TEST(test_sound_play_track_rejects_an_out_of_range_track);
    RUN_TEST(test_sound_play_track_blocked_while_sleeping);
    RUN_TEST(test_sound_play_track_reports_a_full_queue);
    RUN_TEST(test_sound_set_volume_applies_and_persists);
    RUN_TEST(test_sound_set_volume_rejects_an_out_of_range_level);
    RUN_TEST(test_sound_set_volume_reports_a_full_queue);

    RUN_TEST(test_aux_led_color_queues_a_valid_rgb_triple);
    RUN_TEST(test_aux_led_color_rejects_an_out_of_range_component);
    RUN_TEST(test_aux_led_color_reports_component_disabled_when_pin_unset);
    RUN_TEST(test_aux_led_color_reports_queue_full_when_available_but_refused);
    RUN_TEST(test_aux_led_effect_queues_a_valid_effect);
    RUN_TEST(test_aux_led_effect_accepts_off_despite_the_buggy_catalog_enum);
    RUN_TEST(test_aux_led_effect_rejects_an_unknown_effect_string);
    RUN_TEST(test_aux_led_effect_rejects_an_unknown_argument);

    RUN_TEST(test_servo_open_queues_with_the_resolved_arm_id);
    RUN_TEST(test_servo_open_accepts_both_as_the_broadcast_target);
    RUN_TEST(test_servo_close_rejects_an_unknown_target);
    RUN_TEST(test_servo_set_position_rejects_both_though_open_close_accept_it);
    RUN_TEST(test_servo_set_position_queues_with_a_valid_pulse_width);
    RUN_TEST(test_servo_set_position_rejects_an_out_of_range_pulse_width);
    RUN_TEST(test_servo_set_position_rejects_a_missing_target);
    RUN_TEST(test_servo_stop_still_answers_executor_not_ready);
    RUN_TEST(test_257_every_direct_action_row_still_dispatches);

    return UNITY_END();
}
