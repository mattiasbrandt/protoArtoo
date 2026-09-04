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
                              // simulates another config writer holding the
                              // config write lock (#226 defect 1 rework, #269)

#include "action_registry.h"
#include "api_identity.h"        // formatIdentityJson(), IDENTITY_JSON_MAX_BYTES -
                                  // system.api.get-identity's JSON-builder leg (#221)
#include "validation_snapshot.h"  // ValidationSnapshot, captureValidationSnapshot(),
                                   // populateValidationJson() - system.api.get-validation's
                                   // snapshot and JSON-builder legs (#221)
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
#include "console_record.h"  // consoleReasonString() - pins the wire spelling of the
                             // read-only reason, not just its enum value (#226)
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
#include "web_server_test_hooks.h"  // g_test_restart_requests - system.action.reboot's (#225)
                                     // own observation hook, shared with test_api_motion_routes.cpp

#include "sequence_dispatcher.h"  // sequenceDispatcherInit(), sequenceCatalogAt/Count/Find() -
                                   // dome.action.dome-sequence/test-sequence's sequenceStart()
                                   // queue (#259) and dome.api.list-builtin-sequences (#221
                                   // remainder)
#include "seq_store_index.h"      // SeqIndexEntry, seqStoreIndexAdd()/Clear() -
                                   // dome.action.delete-sequence's own lookup (#259) and
                                   // dome.api.list-sequences (#221 remainder)
#include "seq_store_test_hooks.h"  // g_test_seq_delete_ok/calls - seqStoreDelete()'s own
                                    // stub (#259)
#include "seq_json.h"              // seqToggleGroupToString() - dome.api.list-sequences'/
                                    // -list-builtin-sequences' own three-way field-name check
#include "sequence_run_evidence.h"  // SeqRunEvidence, seqEvidenceBegin/RecordTx/End(),
                                     // seqRunOutcomeName() - dome.api.get-sequence-last-run's
                                     // real capture pipeline, driven the same way the
                                     // dispatcher task drives it (#221 remainder)
#include "seq_last_run_json.h"      // populateSeqLastRunJson() - the JSON-builder leg of
                                     // dome.api.get-sequence-last-run's three-way field check

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
static const int kMaxItems = 8;

struct CapturedRecord {
    char names[kMaxFields][40];
    char values[kMaxFields][160];
    int fieldCount;
    // item records, for a query that answers with both scalars and a list -
    // sound.api.get-catalog (#221) is the first. The item-only captures
    // further down (CapturedLogItems/CapturedSeqItems/CapturedOperationItems)
    // stay as they are: they exist for queries that emit no fields at all.
    char items[kMaxItems][96];
    int itemCount;
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

static void capItem(uint32_t, const char* value) {
    if (g_cap.itemCount >= kMaxItems) return;
    snprintf(g_cap.items[g_cap.itemCount], sizeof(g_cap.items[0]), "%s", value);
    g_cap.itemCount++;
}

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
    g_test_restart_requests = 0;
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
    // test_api_audio_routes.cpp's own setUp() for the same globals. #258
    // extends this with the remaining sound.action.* rows' own queue
    // observation globals (track-stop, query-status, every $-letter dollar
    // shortcut and the raw dollar-command passthrough).
    g_test_audio_queue_ok = true;
    g_test_audio_play_track_calls = 0;
    g_test_audio_last_track = 0;
    g_test_audio_volume_calls = 0;
    g_test_audio_last_volume = 0;
    g_test_audio_stop_calls = 0;
    g_test_audio_query_calls = 0;
    g_test_audio_dollar_calls = 0;
    g_test_audio_last_dollar[0] = '\0';
    g_test_aux_led_queue_ok = true;

    // #221: the CHIRP catalog rows (sound.api.get-catalog/-refresh-catalog/
    // -play-banked) gate on the driver's capability word. Default to a
    // catalog-capable backend so each test states only the thing it is
    // about; the not-in-this-build path sets it to 0 explicitly.
    g_test_audio_capabilities = AudioDriver::AUDIO_CAP_CATALOG;
    g_test_audio_catalog_ready = false;
    g_test_audio_catalog_entry_count = 0;
    g_test_audio_refresh_catalog_calls = 0;
    g_test_audio_play_banked_calls = 0;
    g_test_audio_last_banked_index = 0;
    g_test_audio_last_banked_bank = 0;
    g_test_audio_last_banked_page = '\0';

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
    // 384, matching handleHealthGet()'s own buffer (src/web/api_status.cpp) -
    // resetReason (#225) is a variable-length string, not a fixed-width
    // value, so this is no longer bounded by the old fixed-field shape.
    char json[384];
    // Same shape formatHealthJson() actually emits - values are arbitrary,
    // only the key set matters here.
    formatHealthJson(json, sizeof(json), true, false, false, true, false, false, true, 1000, 900,
                     800, -50, 123456, "SOFTWARE");
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
// dome.api.list-sequences / dome.api.list-builtin-sequences (#221 remainder)
// =============================================================================
// Both answer `item` records (one per sequence), not `field` records - the
// same reason system.status.logs needed its own capture above. One small
// capture struct covers both (48 rows/320 bytes: comfortably above both
// SEQ_STORE_MAX (16) and the real, compiled-in Factory catalog's count, and
// above the longest realistic item line - see consoleExecuteDomeApiList
// BuiltinSequences()'s own itemBuf comment, src/console/console_module.cpp).

struct CapturedSeqItems {
    char values[48][320];
    int count;
    bool beginCalled;
    bool endCalled;
    ConsoleStatus status;
    ConsoleOutcome outcome;
    ConsoleReason reason;
};
static CapturedSeqItems g_seqItemCap;

static void seqItemCapBegin(uint32_t, const char*) {
    g_seqItemCap.beginCalled = true;
}
static void seqItemCapItem(uint32_t, const char* value) {
    if (g_seqItemCap.count >= (int)(sizeof(g_seqItemCap.values) / sizeof(g_seqItemCap.values[0]))) {
        return;
    }
    snprintf(g_seqItemCap.values[g_seqItemCap.count], sizeof(g_seqItemCap.values[0]), "%s", value);
    g_seqItemCap.count++;
}
static void seqItemCapEnd(uint32_t, ConsoleStatus status, ConsoleOutcome outcome, ConsoleReason reason) {
    g_seqItemCap.endCalled = true;
    g_seqItemCap.status = status;
    g_seqItemCap.outcome = outcome;
    g_seqItemCap.reason = reason;
}

static void runSeqItemQuery(const char* operationName) {
    memset(&g_seqItemCap, 0, sizeof(g_seqItemCap));
    ConsoleRecordSink sink = {};
    sink.onRecordBegin = seqItemCapBegin;
    sink.onRecordItem = seqItemCapItem;
    sink.onRecordEnd = seqItemCapEnd;

    ConsoleRequest req = {};
    req.requestId = 1;
    req.source = CONSOLE_SOURCE_SERIAL;
    req.operationName = operationName;
    consoleExecuteCommand(&req, &sink);
}

void test_dome_api_list_sequences_streams_the_real_index_as_items() {
    SeqIndexEntry e = {};
    snprintf(e.name, sizeof(e.name), "%s", "DM:MYSEQ");
    e.toggleGroup = TOGGLE_LOW;
    e.suppressMs = 2500;
    snprintf(e.source, sizeof(e.source), "%s", "user");
    e.modified = true;
    e.valid = true;
    seqStoreIndexAdd(e);

    runSeqItemQuery("dome.api.list-sequences");

    TEST_ASSERT_TRUE(g_seqItemCap.beginCalled);
    TEST_ASSERT_TRUE(g_seqItemCap.endCalled);
    TEST_ASSERT_EQUAL(CONSOLE_STATUS_OK, g_seqItemCap.status);
    TEST_ASSERT_EQUAL(CONSOLE_OUTCOME_COMPLETED, g_seqItemCap.outcome);
    TEST_ASSERT_EQUAL_INT(1, g_seqItemCap.count);
    TEST_ASSERT_NOT_NULL(strstr(g_seqItemCap.values[0], "DM:MYSEQ"));
    TEST_ASSERT_NOT_NULL(strstr(g_seqItemCap.values[0], "toggleGroup:low"));
    TEST_ASSERT_NOT_NULL(strstr(g_seqItemCap.values[0], "suppressMs:2500"));
    TEST_ASSERT_NOT_NULL(strstr(g_seqItemCap.values[0], "source:user"));
    TEST_ASSERT_NOT_NULL(strstr(g_seqItemCap.values[0], "modified:true"));
    TEST_ASSERT_NOT_NULL(strstr(g_seqItemCap.values[0], "valid:true"));
    TEST_ASSERT_NOT_NULL(strstr(g_seqItemCap.values[0], "retrained:false"));
}

void test_dome_api_list_sequences_empty_index_answers_completed_with_no_items() {
    runSeqItemQuery("dome.api.list-sequences");

    TEST_ASSERT_TRUE(g_seqItemCap.beginCalled);
    TEST_ASSERT_TRUE(g_seqItemCap.endCalled);
    TEST_ASSERT_EQUAL(CONSOLE_OUTCOME_COMPLETED, g_seqItemCap.outcome);
    TEST_ASSERT_EQUAL_INT(0, g_seqItemCap.count);
}

// "retrained" flips true when a Learned Sequence shadows a real Factory name
// (sequenceCatalogFind(), src/tasks/sequence_catalog.cpp) - the same check
// handleSeqListGet() makes (src/web/api_seq.cpp).
void test_dome_api_list_sequences_reports_retrained_when_shadowing_a_factory_name() {
    SeqIndexEntry e = {};
    snprintf(e.name, sizeof(e.name), "%s", "DM:VADER");  // a real Factory name
    e.valid = true;
    seqStoreIndexAdd(e);

    runSeqItemQuery("dome.api.list-sequences");

    TEST_ASSERT_EQUAL_INT(1, g_seqItemCap.count);
    TEST_ASSERT_NOT_NULL(strstr(g_seqItemCap.values[0], "retrained:true"));
}

// dome.api.list-builtin-sequences reads the real, compiled-in Factory
// catalog (src/tasks/sequence_catalog.cpp) - no seeding possible or needed;
// DM:VADER is catalog index 0 and its shape never changes at runtime.
void test_dome_api_list_builtin_sequences_streams_the_real_catalog_as_items() {
    runSeqItemQuery("dome.api.list-builtin-sequences");

    TEST_ASSERT_TRUE(g_seqItemCap.beginCalled);
    TEST_ASSERT_TRUE(g_seqItemCap.endCalled);
    TEST_ASSERT_EQUAL(CONSOLE_OUTCOME_COMPLETED, g_seqItemCap.outcome);
    TEST_ASSERT_EQUAL_INT((int)sequenceCatalogCount(), g_seqItemCap.count);
    TEST_ASSERT_NOT_NULL(strstr(g_seqItemCap.values[0], "DM:VADER"));
    TEST_ASSERT_NOT_NULL(strstr(g_seqItemCap.values[0], "toggleGroup:none"));
    TEST_ASSERT_NOT_NULL(strstr(g_seqItemCap.values[0], "suppressMs:47000"));
    char stepCountTok[32];
    snprintf(stepCountTok, sizeof(stepCountTok), "stepCount:%u",
             (unsigned)sequenceCatalogAt(0)->stepCount);
    TEST_ASSERT_NOT_NULL(strstr(g_seqItemCap.values[0], stepCountTok));
    TEST_ASSERT_NOT_NULL(strstr(g_seqItemCap.values[0], "purpose:Imperial March"));
}

// =============================================================================
// dome.api.get-sequence-last-run (#221 remainder): field-based
// =============================================================================
// Registered first among this group deliberately: seqEvidenceBegin()
// (src/sequence_run_evidence.cpp) has no reset/undo, matching production (a
// droid never "un-runs" a sequence), so the "no run recorded yet" case can
// only be proven before any other test in this binary calls it.

void test_dome_api_get_sequence_last_run_before_any_run_answers_valid_false_only() {
    runQuery("dome.api.get-sequence-last-run");

    TEST_ASSERT_TRUE(g_cap.beginCalled);
    TEST_ASSERT_TRUE(g_cap.endCalled);
    TEST_ASSERT_EQUAL(CONSOLE_OUTCOME_COMPLETED, g_cap.outcome);
    TEST_ASSERT_EQUAL_STRING("false", capturedValue("valid"));
    TEST_ASSERT_NULL_MESSAGE(capturedValue("name"), "no run recorded yet - name must be absent");
}

// Three-way field match, driven through the REAL capture pipeline
// (seqEvidenceBegin/RecordTx/End(), src/sequence_run_evidence.cpp) exactly
// the way the dispatcher task drives it, so both the JSON-builder leg
// (populateSeqLastRunJson(), src/seq_last_run_json.cpp) and the emitted-
// fields leg see a genuinely completed run with reason and endMs both
// present - the full optional-field set the registry's fields: list claims.
void test_dome_api_get_sequence_last_run_three_way_field_match() {
    seqEvidenceBegin("DM:VADER", 0, 1000, 0);
    SeqAction act = {};
    act.kind = SEQ_ACT_DOME_CMD;
    snprintf(act.payload, sizeof(act.payload), "%s", ":OP01");
    seqEvidenceRecordTx(act, false);
    seqEvidenceEnd(SEQ_RUN_COMPLETED, "test-reason", 2000, 0);

    SeqRunEvidence ev = {};
    bool have = seqEvidenceSnapshot(ev);
    TEST_ASSERT_TRUE(have);

    JsonDocument doc;
    TEST_ASSERT_TRUE(populateSeqLastRunJson(doc, ev, have));
    char json[2048];
    size_t jsonLen = serializeJson(doc, json, sizeof(json));
    TEST_ASSERT_GREATER_THAN(0, (int)jsonLen);
    std::vector<std::string> jsonKeys = jsonTopLevelKeys(json);

    // Subset match, the same shape dome.status.serial-link's own test uses
    // (above): the registry's fields: list is a deliberately chosen subset
    // of populateSeqLastRunJson()'s real top-level keys, not the full set
    // (see that registry entry's own comment for why).
    std::vector<std::string> registryFields = catalogFieldNames("dome.api.get-sequence-last-run");
    TEST_ASSERT_TRUE(registryFields ==
                     (std::vector<std::string>{"endMs", "name", "outcome", "reason", "running",
                                                "source", "startMs", "valid"}));
    for (const auto& field : registryFields) {
        bool found = std::binary_search(jsonKeys.begin(), jsonKeys.end(), field);
        TEST_ASSERT_TRUE_MESSAGE(found, field.c_str());
    }

    runQuery("dome.api.get-sequence-last-run");
    std::vector<std::string> emitted = emittedFieldNames();
    TEST_ASSERT_TRUE(registryFields == emitted);
}

void test_dome_api_get_sequence_last_run_carries_real_state() {
    seqEvidenceBegin("DM:LEIA", 3, 1000, 0);
    seqEvidenceEnd(SEQ_RUN_ABORTED, "estop", 5000, 0);

    runQuery("dome.api.get-sequence-last-run");

    TEST_ASSERT_EQUAL_STRING("true", capturedValue("valid"));
    TEST_ASSERT_EQUAL_STRING("DM:LEIA", capturedValue("name"));
    TEST_ASSERT_EQUAL_STRING("3", capturedValue("source"));
    TEST_ASSERT_EQUAL_STRING("aborted", capturedValue("outcome"));
    TEST_ASSERT_EQUAL_STRING("false", capturedValue("running"));
    TEST_ASSERT_EQUAL_STRING("estop", capturedValue("reason"));
    TEST_ASSERT_EQUAL_STRING("1000", capturedValue("startMs"));
    TEST_ASSERT_EQUAL_STRING("5000", capturedValue("endMs"));
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
// system.status.logs used to be excluded from this sweep: #239 wired it but
// could not re-run tools/generate_console_catalog.py, because data/console_help.txt
// was fenced on that ticket, so the COMPILED catalog still carried
// is_query: false for it. A later unrelated regeneration has since picked the
// registry's own is_query: true up, so the row is swept like any other and
// keeps its own direct tests below as well.
//
// A row that is out of this build never reaches a dispatch table at all -
// consoleExecuteCommand()'s build guard (#224) answers not-in-this-build
// first - so it cannot report EXECUTOR_NOT_READY here either. That is why
// system.api.get-profiler, an is_query: true status row registered only on a
// PA_HEAP_PROFILE build, passes this sweep in a native binary.
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
// The system.* and rc.api.* rows (#221)
// =============================================================================

// system.action.estop-clear calls the one explicit-intent release path
// (failsafeClearEstop(), src/failsafe_gate.cpp), so a latched estop is cleared
// and the drive gate reopens - the same effect POST /api/estop/clear has.
void test_estop_clear_releases_the_latched_estop() {
    failsafeTrigger(FailsafeLayer::ESTOP);
    TEST_ASSERT_TRUE(robotState.estop);

    runQuery("system.action.estop-clear");

    TEST_ASSERT_EQUAL(CONSOLE_STATUS_OK, g_cap.status);
    TEST_ASSERT_EQUAL(CONSOLE_OUTCOME_APPLIED, g_cap.outcome);
    TEST_ASSERT_FALSE(robotState.estop);
}

void test_estop_clear_rejects_any_argument_and_leaves_the_latch_alone() {
    failsafeTrigger(FailsafeLayer::ESTOP);

    runQuery("system.action.estop-clear force=true");

    TEST_ASSERT_EQUAL(CONSOLE_OUTCOME_INVALID, g_cap.outcome);
    TEST_ASSERT_EQUAL(CONSOLE_REASON_UNKNOWN_ARGUMENT, g_cap.reason);
    TEST_ASSERT_EQUAL_STRING("force", capturedValue("argument"));
    TEST_ASSERT_TRUE_MESSAGE(robotState.estop, "a refused command must not clear the latch");
}

// Three-way field match for system.api.get-identity: the registry's fields:,
// formatIdentityJson()'s real top-level keys, and the emitted names. Subset
// match on the JSON side - the manifest keys are deliberately not answered
// (see the registry entry's comment).
void test_system_api_get_identity_three_way_field_match() {
    char json[IDENTITY_JSON_MAX_BYTES] = {};
    TEST_ASSERT_TRUE(formatIdentityJson(json, sizeof(json), "R2D2", true));
    std::vector<std::string> jsonKeys = jsonTopLevelKeys(json);

    std::vector<std::string> registryFields = catalogFieldNames("system.api.get-identity");
    TEST_ASSERT_TRUE(registryFields == (std::vector<std::string>{"droidName", "mdnsUseName"}));
    for (const auto& field : registryFields) {
        TEST_ASSERT_TRUE_MESSAGE(std::binary_search(jsonKeys.begin(), jsonKeys.end(), field),
                                 field.c_str());
    }

    runQuery("system.api.get-identity");
    TEST_ASSERT_TRUE(registryFields == emittedFieldNames());
}

void test_system_api_get_identity_carries_real_config_state() {
    ConfigSnapshot snap = {};
    configCacheRead(&snap);
    snprintf(snap.system.droid_name, sizeof(snap.system.droid_name), "%s", "Chopper");
    snap.system.mdns_use_name = true;
    configCacheApply(snap);

    runQuery("system.api.get-identity");

    TEST_ASSERT_EQUAL(CONSOLE_OUTCOME_COMPLETED, g_cap.outcome);
    TEST_ASSERT_EQUAL_STRING("Chopper", capturedValue("droidName"));
    TEST_ASSERT_EQUAL_STRING("true", capturedValue("mdnsUseName"));
}

// Three-way field match for system.api.get-validation. The four nested keys
// are answered as summary tokens, so the field NAMES still match
// populateValidationJson()'s top-level keys exactly - which is the point of
// collapsing under the real key rather than inventing flattened ones.
void test_system_api_get_validation_three_way_field_match() {
    ValidationSnapshot snap = {};
    captureValidationSnapshot(&snap);
    JsonDocument doc;
    TEST_ASSERT_TRUE(populateValidationJson(doc, snap));
    char json[2048];
    TEST_ASSERT_GREATER_THAN(0, (int)serializeJson(doc, json, sizeof(json)));
    std::vector<std::string> jsonKeys = jsonTopLevelKeys(json);

    std::vector<std::string> registryFields = catalogFieldNames("system.api.get-validation");
    TEST_ASSERT_TRUE(registryFields == jsonKeys);

    runQuery("system.api.get-validation");
    TEST_ASSERT_TRUE(registryFields == emittedFieldNames());
}

// The summary tokens carry live state, not placeholders: a latched estop shows
// up inside the `drive` token.
void test_system_api_get_validation_drive_summary_carries_real_state() {
    failsafeTrigger(FailsafeLayer::ESTOP);

    runQuery("system.api.get-validation");

    TEST_ASSERT_EQUAL(CONSOLE_OUTCOME_COMPLETED, g_cap.outcome);
    const char* drive = capturedValue("drive");
    TEST_ASSERT_NOT_NULL(drive);
    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(drive, "estop:true"), drive);
    // Whitespace-free, so the token cannot be read as a second key=value pair
    // on the wire (the consoleFormatRcSourceSummary() convention).
    TEST_ASSERT_NULL_MESSAGE(strchr(drive, ' '), drive);
}

// rc.api.get-bindable-actions streams ACTION_REGISTRY[] as items - one per
// entry, carrying the RC token POST /api/rc/map accepts.
void test_rc_api_get_bindable_actions_streams_the_registry_as_items() {
    runSeqItemQuery("rc.api.get-bindable-actions");

    TEST_ASSERT_TRUE(g_seqItemCap.beginCalled);
    TEST_ASSERT_TRUE(g_seqItemCap.endCalled);
    TEST_ASSERT_EQUAL(CONSOLE_OUTCOME_COMPLETED, g_seqItemCap.outcome);
    TEST_ASSERT_EQUAL_INT_MESSAGE((int)ACTION_REGISTRY_SIZE, g_seqItemCap.count,
                                  "every ACTION_REGISTRY[] row must be listed");

    // The first row's item line, checked against the registry entry itself
    // rather than a hand-typed string, so a renamed action cannot pass.
    TEST_ASSERT_NOT_NULL(strstr(g_seqItemCap.values[0], ACTION_REGISTRY[0].name));
    char expectedToken[64];
    snprintf(expectedToken, sizeof(expectedToken), "token:%s",
             robotActionIdToString(ACTION_REGISTRY[0].id));
    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_seqItemCap.values[0], expectedToken),
                                 g_seqItemCap.values[0]);
}

// The guarded estop row is safety_critical and not web-testable; the listing
// must report both honestly rather than flattening them to a default.
void test_rc_api_get_bindable_actions_reports_testability_per_row() {
    runSeqItemQuery("rc.api.get-bindable-actions");

    bool sawEstop = false;
    for (int i = 0; i < g_seqItemCap.count; ++i) {
        if (strstr(g_seqItemCap.values[i], "system.action.estop ") == nullptr) continue;
        sawEstop = true;
        TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_seqItemCap.values[i], "safetyCritical:true"),
                                     g_seqItemCap.values[i]);
        TEST_ASSERT_NOT_NULL_MESSAGE(strstr(g_seqItemCap.values[i], "testable:false"),
                                     g_seqItemCap.values[i]);
    }
    TEST_ASSERT_TRUE_MESSAGE(sawEstop, "system.action.estop must appear in the listing");
}

// =============================================================================
// The sound.api.* rows (#221)
// =============================================================================

void test_sound_refresh_catalog_queues_through_the_audio_queue() {
    runQuery("sound.api.refresh-catalog");

    TEST_ASSERT_EQUAL(CONSOLE_STATUS_OK, g_cap.status);
    TEST_ASSERT_EQUAL(CONSOLE_OUTCOME_QUEUED, g_cap.outcome);
    TEST_ASSERT_EQUAL_UINT(1u, g_test_audio_refresh_catalog_calls);
}

void test_sound_refresh_catalog_rejects_any_argument() {
    runQuery("sound.api.refresh-catalog bank=1");

    TEST_ASSERT_EQUAL(CONSOLE_OUTCOME_INVALID, g_cap.outcome);
    TEST_ASSERT_EQUAL(CONSOLE_REASON_UNKNOWN_ARGUMENT, g_cap.reason);
    TEST_ASSERT_EQUAL_STRING("bank", capturedValue("argument"));
    TEST_ASSERT_EQUAL_UINT(0u, g_test_audio_refresh_catalog_calls);
}

// A backend with no AUDIO_CAP_CATALOG is a compile-time driver choice
// (PA_AUDIO_DRIVER), which is what not-in-this-build means - the same fact the
// REST route answers 404 for.
void test_sound_refresh_catalog_without_a_catalog_backend_is_not_in_this_build() {
    g_test_audio_capabilities = 0;

    runQuery("sound.api.refresh-catalog");

    TEST_ASSERT_EQUAL(CONSOLE_OUTCOME_UNAVAILABLE, g_cap.outcome);
    TEST_ASSERT_EQUAL(CONSOLE_REASON_NOT_IN_THIS_BUILD, g_cap.reason);
    TEST_ASSERT_EQUAL_UINT(0u, g_test_audio_refresh_catalog_calls);
}

// The three values reach audioQueuePlayTrackBanked() unchanged - the same
// queue call handleAudioPlayBankedPost() makes.
void test_sound_play_banked_passes_bank_page_index_to_the_queue() {
    runQuery("sound.api.play-banked bank=3 page=C index=42");

    TEST_ASSERT_EQUAL(CONSOLE_STATUS_OK, g_cap.status);
    TEST_ASSERT_EQUAL(CONSOLE_OUTCOME_QUEUED, g_cap.outcome);
    TEST_ASSERT_EQUAL_UINT(1u, g_test_audio_play_banked_calls);
    TEST_ASSERT_EQUAL_UINT(3u, g_test_audio_last_banked_bank);
    TEST_ASSERT_EQUAL_UINT(42u, g_test_audio_last_banked_index);
    TEST_ASSERT_EQUAL_CHAR('C', g_test_audio_last_banked_page);
}

void test_sound_play_banked_missing_key_names_it() {
    runQuery("sound.api.play-banked bank=3 page=C");

    TEST_ASSERT_EQUAL(CONSOLE_OUTCOME_INVALID, g_cap.outcome);
    TEST_ASSERT_EQUAL(CONSOLE_REASON_MISSING_ARGUMENT, g_cap.reason);
    TEST_ASSERT_EQUAL_STRING("index", capturedValue("argument"));
    TEST_ASSERT_EQUAL_UINT(0u, g_test_audio_play_banked_calls);
}

void test_sound_play_banked_unknown_key_names_it() {
    runQuery("sound.api.play-banked bank=3 page=C index=42 volume=9");

    TEST_ASSERT_EQUAL(CONSOLE_OUTCOME_INVALID, g_cap.outcome);
    TEST_ASSERT_EQUAL(CONSOLE_REASON_UNKNOWN_ARGUMENT, g_cap.reason);
    TEST_ASSERT_EQUAL_STRING("volume", capturedValue("argument"));
}

// bank is uint8 range 1-6 in the registry; the schema, not the executor,
// refuses 7 - the "type, range, enum" half of this ticket's criterion.
void test_sound_play_banked_bank_out_of_registry_range_is_refused() {
    runQuery("sound.api.play-banked bank=7 page=C index=42");

    TEST_ASSERT_EQUAL(CONSOLE_OUTCOME_INVALID, g_cap.outcome);
    TEST_ASSERT_EQUAL(CONSOLE_REASON_OUT_OF_RANGE, g_cap.reason);
    TEST_ASSERT_EQUAL_STRING("bank", capturedValue("argument"));
    TEST_ASSERT_EQUAL_UINT(0u, g_test_audio_play_banked_calls);
}

// page is an A-Z enum in the registry. `9` is outside it; so is a lower-case
// letter, which the REST route would have upper-cased - the one recorded
// narrowing, asserted so it stays deliberate.
void test_sound_play_banked_page_outside_the_published_enum_is_refused() {
    runQuery("sound.api.play-banked bank=3 page=9 index=42");
    TEST_ASSERT_EQUAL(CONSOLE_REASON_OUT_OF_RANGE, g_cap.reason);
    TEST_ASSERT_EQUAL_STRING("page", capturedValue("argument"));

    runQuery("sound.api.play-banked bank=3 page=c index=42");
    TEST_ASSERT_EQUAL(CONSOLE_REASON_OUT_OF_RANGE, g_cap.reason);
    TEST_ASSERT_EQUAL_STRING("page", capturedValue("argument"));
    TEST_ASSERT_EQUAL_UINT(0u, g_test_audio_play_banked_calls);
}

void test_sound_play_banked_is_blocked_while_sleeping() {
    robotState.sleepMode = true;

    runQuery("sound.api.play-banked bank=3 page=C index=42");

    TEST_ASSERT_EQUAL(CONSOLE_OUTCOME_BLOCKED, g_cap.outcome);
    TEST_ASSERT_EQUAL(CONSOLE_REASON_TEMPORARILY_UNAVAILABLE, g_cap.reason);
    TEST_ASSERT_EQUAL_UINT(0u, g_test_audio_play_banked_calls);
}

// sound.api.get-catalog answers `ready` as a field and one item per entry.
void test_sound_get_catalog_answers_ready_and_one_item_per_entry() {
    g_test_audio_catalog_ready = true;
    g_test_audio_catalog_entry_count = 2;
    g_test_audio_catalog_entries[0] = AudioCatalogEntry{};
    g_test_audio_catalog_entries[0].bank = 1;
    g_test_audio_catalog_entries[0].page = 'A';
    g_test_audio_catalog_entries[0].index = 7;
    snprintf(g_test_audio_catalog_entries[0].name,
             sizeof(g_test_audio_catalog_entries[0].name), "%s", "Alarm");
    g_test_audio_catalog_entries[1] = AudioCatalogEntry{};
    g_test_audio_catalog_entries[1].bank = 2;
    g_test_audio_catalog_entries[1].page = 'B';
    g_test_audio_catalog_entries[1].index = 9;
    snprintf(g_test_audio_catalog_entries[1].name,
             sizeof(g_test_audio_catalog_entries[1].name), "%s", "Chirp");

    runQuery("sound.api.get-catalog");

    TEST_ASSERT_TRUE(g_cap.beginCalled);
    TEST_ASSERT_EQUAL(CONSOLE_STATUS_OK, g_cap.status);
    TEST_ASSERT_EQUAL(CONSOLE_OUTCOME_COMPLETED, g_cap.outcome);
    TEST_ASSERT_EQUAL_STRING("true", capturedValue("ready"));
    TEST_ASSERT_EQUAL_INT(2, g_cap.itemCount);
    TEST_ASSERT_EQUAL_STRING("bank:1 page:A index:7 name:Alarm", g_cap.items[0]);
    TEST_ASSERT_EQUAL_STRING("bank:2 page:B index:9 name:Chirp", g_cap.items[1]);
}

// An unenumerated catalog is `ready=false` with no items - which is why the
// field is there at all: it separates "nothing enumerated yet" from "the
// backend has no sounds".
void test_sound_get_catalog_reports_an_unenumerated_catalog_as_not_ready() {
    runQuery("sound.api.get-catalog");

    TEST_ASSERT_EQUAL_STRING("false", capturedValue("ready"));
    TEST_ASSERT_EQUAL_INT(0, g_cap.itemCount);
    TEST_ASSERT_EQUAL(CONSOLE_OUTCOME_COMPLETED, g_cap.outcome);
}

void test_sound_get_catalog_without_a_catalog_backend_is_not_in_this_build() {
    g_test_audio_capabilities = 0;

    runQuery("sound.api.get-catalog");

    TEST_ASSERT_EQUAL(CONSOLE_OUTCOME_UNAVAILABLE, g_cap.outcome);
    TEST_ASSERT_EQUAL(CONSOLE_REASON_NOT_IN_THIS_BUILD, g_cap.reason);
    TEST_ASSERT_EQUAL_INT(0, g_cap.itemCount);
}

// A status query takes no arguments; the key is named rather than ignored.
void test_sound_get_catalog_rejects_the_dropped_bank_filter() {
    runQuery("sound.api.get-catalog bank=1");

    TEST_ASSERT_EQUAL(CONSOLE_OUTCOME_INVALID, g_cap.outcome);
    TEST_ASSERT_EQUAL(CONSOLE_REASON_UNKNOWN_ARGUMENT, g_cap.reason);
    TEST_ASSERT_EQUAL_STRING("bank", capturedValue("argument"));
}

// The catalog listing re-reads the driver's pointer and count together on every
// iteration, because AudioTask can grow the entry array (delete[]/new in
// AudioDriverChirp::ensureEntryStorage()) while this walk is in progress. This
// drives that race directly: the sink shrinks the catalog to zero the moment the
// first item lands, which is what a concurrent refresh looks like to this loop.
// A listing that captured the pair once would walk into freed memory here; this
// one stops and still closes its record group.
static int g_catalogShrinkAfter = 0;
static int g_catalogShrinkItems = 0;

static void shrinkingCatalogItem(uint32_t, const char*) {
    g_catalogShrinkItems++;
    if (g_catalogShrinkItems >= g_catalogShrinkAfter) {
        g_test_audio_catalog_entry_count = 0;
    }
}

void test_sound_get_catalog_stops_when_the_catalog_is_refreshed_mid_listing() {
    g_test_audio_catalog_ready = true;
    g_test_audio_catalog_entry_count = 4;
    for (uint16_t i = 0; i < 4; ++i) {
        g_test_audio_catalog_entries[i] = AudioCatalogEntry{};
        g_test_audio_catalog_entries[i].bank = 1;
        g_test_audio_catalog_entries[i].page = 'A';
        g_test_audio_catalog_entries[i].index = (uint16_t)(i + 1);
    }
    g_catalogShrinkAfter = 1;
    g_catalogShrinkItems = 0;

    ConsoleRecordSink sink = {};
    sink.onRecordBegin = capBegin;
    sink.onRecordField = capField;
    sink.onRecordItem = shrinkingCatalogItem;
    sink.onRecordResult = capResult;
    sink.onRecordEnd = capEnd;
    memset(&g_cap, 0, sizeof(g_cap));

    ConsoleRequest req = {};
    req.requestId = 1;
    req.source = CONSOLE_SOURCE_SERIAL;
    req.operationName = "sound.api.get-catalog";
    consoleExecuteCommand(&req, &sink);

    TEST_ASSERT_EQUAL_INT_MESSAGE(1, g_catalogShrinkItems,
                                  "the walk must stop at the refresh, not run on the old count");
    TEST_ASSERT_TRUE_MESSAGE(g_cap.endCalled, "the record group must still be closed");
    TEST_ASSERT_EQUAL(CONSOLE_OUTCOME_COMPLETED, g_cap.outcome);
}

// Three-way field match for sound.api.get-mood-map: the registry's fields:,
// formatMoodCategoryMapJson()'s real JSON keys, and the names the executor
// emitted.
void test_sound_get_mood_map_three_way_field_match() {
    MoodCategoryMaskConfig masks{};
    masks.quiet = 1;
    masks.mid = 2;
    masks.full = 3;
    masks.awakeplus = 4;
    char json[192];
    size_t n = formatMoodCategoryMapJson(json, sizeof(json), masks);
    TEST_ASSERT_LESS_THAN(sizeof(json), n);
    std::vector<std::string> jsonKeys = jsonTopLevelKeys(json);

    std::vector<std::string> registryFields = catalogFieldNames("sound.api.get-mood-map");
    TEST_ASSERT_TRUE(registryFields == jsonKeys);

    runQuery("sound.api.get-mood-map");
    TEST_ASSERT_TRUE(registryFields == emittedFieldNames());
}

// The api-named read and the config-named read are two views of one value and
// share one emitter, so they must answer identically for the same stored word.
void test_sound_get_mood_map_matches_the_config_row_for_the_same_state() {
    ConfigSnapshot snap = {};
    configCacheRead(&snap);
    snap.audio.snd_moodcat_quiet = 11;
    snap.audio.snd_moodcat_mid = 22;
    snap.audio.snd_moodcat_full = 33;
    snap.audio.snd_moodcat_awakeplus = 44;
    configCacheApply(snap);

    runQuery("sound.api.get-mood-map");
    TEST_ASSERT_EQUAL(CONSOLE_OUTCOME_COMPLETED, g_cap.outcome);
    TEST_ASSERT_EQUAL_STRING("11", capturedValue("quiet"));
    TEST_ASSERT_EQUAL_STRING("22", capturedValue("mid"));
    TEST_ASSERT_EQUAL_STRING("33", capturedValue("full"));
    TEST_ASSERT_EQUAL_STRING("44", capturedValue("awakeplus"));

    runQuery("sound.config.mood-category-map");
    TEST_ASSERT_EQUAL_STRING("11", capturedValue("quiet"));
    TEST_ASSERT_EQUAL_STRING("22", capturedValue("mid"));
    TEST_ASSERT_EQUAL_STRING("33", capturedValue("full"));
    TEST_ASSERT_EQUAL_STRING("44", capturedValue("awakeplus"));
}

// =============================================================================
// Named body sequences - the dome.seq.* rows (#221)
// =============================================================================

// The sixteen dome.seq.* rows name one Factory sequence each. They reach the
// same sequenceStart() choke point dome.action.dome-sequence above does, with
// the DM: name read out of the catalog rather than typed as an argument.
void test_dome_seq_named_row_queues_through_the_dispatcher() {
    runQuery("dome.seq.vader");

    TEST_ASSERT_EQUAL(CONSOLE_STATUS_OK, g_cap.status);
    TEST_ASSERT_EQUAL(CONSOLE_OUTCOME_QUEUED, g_cap.outcome);
}

// The operation name is the whole command, so a supplied key is unknown -
// the same answer every other no-argument action gives, with the key named.
void test_dome_seq_named_row_rejects_any_argument() {
    runQuery("dome.seq.hello value=DM:VADER");

    TEST_ASSERT_EQUAL(CONSOLE_OUTCOME_INVALID, g_cap.outcome);
    TEST_ASSERT_EQUAL(CONSOLE_REASON_UNKNOWN_ARGUMENT, g_cap.reason);
    TEST_ASSERT_EQUAL_STRING("value", capturedValue("argument"));
}

// The generator's filter is the thing under test here: consoleCatalogSequenceFor()
// must answer for a row whose registry marcduino_cmd is a literal DM:<NAME> and
// must NOT answer for dome.action.dome-sequence, whose marcduino_cmd is the
// documentation placeholder "DM:<NAME>". A filter that let the placeholder
// through would hand sequenceStart() the literal string "DM:<NAME>".
void test_catalog_sequence_lookup_answers_only_for_literal_dm_rows() {
    TEST_ASSERT_EQUAL_STRING("DM:VADER", consoleCatalogSequenceFor("dome.seq.vader"));
    TEST_ASSERT_EQUAL_STRING("DM:OVERLOAD", consoleCatalogSequenceFor("dome.seq.overload"));
    TEST_ASSERT_NULL_MESSAGE(consoleCatalogSequenceFor("dome.action.dome-sequence"),
                             "the DM:<NAME> placeholder must not read as a sequence name");
    TEST_ASSERT_NULL(consoleCatalogSequenceFor("system.status.health"));
    TEST_ASSERT_NULL(consoleCatalogSequenceFor(nullptr));
}

// Every catalog row the sequence table names dispatches - no name list in
// console_module.cpp to fall out of step with the registry, so this sweeps
// the catalog rather than naming the sixteen.
void test_every_named_sequence_row_dispatches() {
    size_t count = 0;
    const ConsoleCatalogEntry* entries = consoleCatalogGetEntries(&count);
    int named = 0;

    for (size_t i = 0; i < count; ++i) {
        if (consoleCatalogSequenceFor(entries[i].name) == nullptr) continue;
        named++;
        runQuery(entries[i].name);
        TEST_ASSERT_EQUAL_MESSAGE(CONSOLE_OUTCOME_QUEUED, g_cap.outcome, entries[i].name);
        TEST_ASSERT_NOT_EQUAL_MESSAGE(CONSOLE_REASON_EXECUTOR_NOT_READY, g_cap.reason,
                                      entries[i].name);
    }

    TEST_ASSERT_EQUAL_MESSAGE(16, named,
                              "the registry's literal DM: action rows - update this count "
                              "with the registry, never to make the row green");
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

// The closing guard for #221 (epic row #46): the action rows that still answer
// executor-not-ready are exactly these twelve, each carrying a true, specific
// reason on its own docs/action-registry.yaml entry and in the dispatch-site
// comment (consoleExecuteCommand()'s CONSOLE_OP_ACTION case). A thirteenth row
// joining the set fails here, so the next unwired operation cannot arrive
// unexplained; a row leaving it fails here too, so the list cannot rot.
//
// Update this list only together with the reason at both sites - never to make
// the row green.
void test_the_executor_not_ready_set_is_exactly_the_recorded_rows() {
    static const char* const kRecorded[] = {
        // #206 document / bulk transfer
        "dome.api.get-sequence",
        "dome.api.get-layout",
        "dome.action.save-sequence",
        "rc.api.get-map",
        "rc.action.set-map",
        "system.api.get-coredump",
        "system.action.upload-firmware",
        "system.action.upload-filesystem",
        // core unreachable from this module without editing a fenced file
        "system.api.get-coredump-status",
        "system.action.erase-coredump",
        "system.api.get-admission-trace",
        // the browser Console Adapter itself, not an operation
        "system.console",
    };
    const size_t kRecordedCount = sizeof(kRecorded) / sizeof(kRecorded[0]);

    robotState.webControlEnabled = true;
    size_t count = 0;
    const ConsoleCatalogEntry* entries = consoleCatalogGetEntries(&count);

    int notReady = 0;
    for (size_t i = 0; i < count; ++i) {
        if (strcmp(entries[i].type, CONSOLE_CATALOG_TYPE_ACTION) != 0) continue;
        runQuery(entries[i].name);
        if (g_cap.reason != CONSOLE_REASON_EXECUTOR_NOT_READY) continue;
        notReady++;

        bool recorded = false;
        for (size_t r = 0; r < kRecordedCount; ++r) {
            if (strcmp(kRecorded[r], entries[i].name) == 0) {
                recorded = true;
                break;
            }
        }
        TEST_ASSERT_TRUE_MESSAGE(recorded, entries[i].name);
    }

    TEST_ASSERT_EQUAL_INT_MESSAGE((int)kRecordedCount, notReady,
                                  "a recorded row started dispatching, or a new row stopped - "
                                  "update this list together with its reason at the registry "
                                  "and the dispatch site");
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

// system.config.log-level (#225): read renders the live numeric level;
// write accepts the raw 1..4 integer api_config_apply.cpp's paramInt16
// validates.
void test_log_level_read_and_write_the_integer() {
    runQuery("system.config.log-level value=3");
    TEST_ASSERT_EQUAL(CONSOLE_OUTCOME_APPLIED, g_cap.outcome);

    runQuery("system.config.log-level");
    TEST_ASSERT_EQUAL(CONSOLE_OUTCOME_COMPLETED, g_cap.outcome);
    TEST_ASSERT_EQUAL_STRING("3", capturedValue("value"));

    ConfigSnapshot snap = {};
    configCacheRead(&snap);
    TEST_ASSERT_EQUAL_UINT8(3, snap.system.logLevel);
}

// The word form docs/console-protocol.md s.1's own grammar example uses
// ("system.config.log-level value=debug") - each of the four words maps to
// its stored digit, proven against the real config cache, not just the
// executor's own echo.
void test_log_level_accepts_every_word_form() {
    struct {
        const char* word;
        uint8_t digit;
    } cases[] = {
        {"error", 1},
        {"warning", 2},
        {"info", 3},
        {"debug", 4},
    };
    for (const auto& c : cases) {
        char line[64];
        snprintf(line, sizeof(line), "system.config.log-level value=%s", c.word);
        runQuery(line);
        TEST_ASSERT_EQUAL_MESSAGE(CONSOLE_OUTCOME_APPLIED, g_cap.outcome, c.word);

        ConfigSnapshot snap = {};
        configCacheRead(&snap);
        TEST_ASSERT_EQUAL_UINT8_MESSAGE(c.digit, snap.system.logLevel, c.word);
    }
}

// The word form is case-insensitive, matching how the rest of the protocol
// treats operator-typed words (mode= in rc.config.mode is the closest
// existing precedent, and that one IS case-sensitive - log-level's word
// form is deliberately more forgiving since it is this ticket's own
// addition, not an existing wire contract to preserve).
void test_log_level_word_form_is_case_insensitive() {
    runQuery("system.config.log-level value=DEBUG");
    TEST_ASSERT_EQUAL(CONSOLE_OUTCOME_APPLIED, g_cap.outcome);

    ConfigSnapshot snap = {};
    configCacheRead(&snap);
    TEST_ASSERT_EQUAL_UINT8(4, snap.system.logLevel);
}

// The named key (logLevel=) works exactly like value= - the same
// ScalarConfigArg bridge every other row in g_scalarConfigExecutors[] shares
// (src/console/console_module.cpp).
void test_log_level_accepts_the_named_key() {
    runQuery("system.config.log-level logLevel=info");
    TEST_ASSERT_EQUAL(CONSOLE_OUTCOME_APPLIED, g_cap.outcome);

    ConfigSnapshot snap = {};
    configCacheRead(&snap);
    TEST_ASSERT_EQUAL_UINT8(3, snap.system.logLevel);
}

void test_log_level_rejects_an_out_of_range_integer() {
    runQuery("system.config.log-level value=5");

    TEST_ASSERT_EQUAL(CONSOLE_OUTCOME_INVALID, g_cap.outcome);
    TEST_ASSERT_EQUAL(CONSOLE_REASON_OUT_OF_RANGE, g_cap.reason);
}

void test_log_level_rejects_an_unknown_word() {
    runQuery("system.config.log-level value=verbose");

    TEST_ASSERT_EQUAL(CONSOLE_OUTCOME_INVALID, g_cap.outcome);
    TEST_ASSERT_EQUAL(CONSOLE_REASON_OUT_OF_RANGE, g_cap.reason);
}

void test_log_level_rejects_an_unknown_argument() {
    runQuery("system.config.log-level bogus=1");

    TEST_ASSERT_EQUAL(CONSOLE_OUTCOME_INVALID, g_cap.outcome);
    TEST_ASSERT_EQUAL(CONSOLE_REASON_UNKNOWN_ARGUMENT, g_cap.reason);
}

// An extra key alongside a valid value= must still be rejected as unknown -
// the word-form translator only fires for an exactly-one-argument write
// (consoleExecuteSystemLogLevel()'s own comment, src/console/console_module.cpp),
// so this also proves the translator does not silently swallow the second key.
void test_log_level_rejects_an_extra_argument_even_with_a_valid_word() {
    runQuery("system.config.log-level value=debug bogus=1");

    TEST_ASSERT_EQUAL(CONSOLE_OUTCOME_INVALID, g_cap.outcome);
    TEST_ASSERT_EQUAL(CONSOLE_REASON_UNKNOWN_ARGUMENT, g_cap.reason);
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
// Core 0) can call it concurrently. The config write lock (ConfigWriteLock,
// include/api_config.h) serializes the whole configApply() -> error check ->
// configCommitApplied() window, and since #269 the REST config routes take
// the same one; these tests simulate another writer holding it via the native
// mutex stub's exposed singleton (paStubMutexStorage()), which is what every
// xSemaphoreCreateMutexStatic() returns natively, matching the precedent
// test_console_serial_output.cpp already set for inspecting/driving
// paGetSerialMutex()'s stub state the same way.
// =============================================================================

void test_config_write_reports_busy_when_the_mutex_is_already_held() {
    consoleModuleInit();  // idempotent
    paStubMutexReset();
    struct PaStubMutex* m = paStubMutexStorage();
    m->held = 1;  // simulate another config writer mid-write

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
// wifi.config.settings - the grouped WiFi write (#227)
//
// Driven through consoleExecuteCommand() (the entry point both adapters call)
// against the REAL POST /api/wifi Apply Core and its Commit Step:
// src/web/api_wifi_apply.cpp is in [env:native]'s build_src_filter, so
// wifiApply()/wifiCommitApplied() here are the same functions handleWifiPost()
// runs, not stand-ins. That is what makes "validates atomically through the
// WiFi apply core" a checked claim rather than a structural one.
// =============================================================================

// Puts a known WiFi posture in the config cache and declares it the one this
// boot came up on, so pendingApply starts false and any difference a test
// then creates is the test's own.
static void seedWifi(WifiMode mode, const char* staSsid, const char* staPassword,
                     const char* apSsid, const char* apPassword) {
    ConfigSnapshot snap = {};
    configCacheRead(&snap);
    snap.wifi = WifiConfig{};
    snap.wifi.provisioned = true;
    snap.wifi.mode = mode;
    snprintf(snap.wifi.sta_ssid, sizeof(snap.wifi.sta_ssid), "%s", staSsid);
    snprintf(snap.wifi.sta_password, sizeof(snap.wifi.sta_password), "%s", staPassword);
    snprintf(snap.wifi.ap_ssid, sizeof(snap.wifi.ap_ssid), "%s", apSsid);
    snprintf(snap.wifi.ap_password, sizeof(snap.wifi.ap_password), "%s", apPassword);
    configCacheApply(snap);
    configCacheSetActiveWifi(snap.wifi);
    configCacheSetActiveWifiRecovery(false);
}

// True if any emitted field VALUE contains `needle` - the check that matters
// for a secret, since a leak would arrive as a value, not as a field name.
static bool anyCapturedValueContains(const char* needle) {
    for (int i = 0; i < g_cap.fieldCount; i++) {
        if (strstr(g_cap.values[i], needle) != nullptr) return true;
    }
    return false;
}

void test_wifi_settings_read_reports_the_saved_posture_and_no_password() {
    seedWifi(WifiMode::CLIENT, "Workshop WiFi", "hunter2hunter2", "artoo-ap", "apsecret1");

    runQuery("wifi.config.settings");

    TEST_ASSERT_EQUAL(CONSOLE_STATUS_OK, g_cap.status);
    TEST_ASSERT_EQUAL(CONSOLE_OUTCOME_COMPLETED, g_cap.outcome);
    TEST_ASSERT_EQUAL_STRING("true", capturedValue("provisioned"));
    TEST_ASSERT_EQUAL_STRING("client", capturedValue("mode"));
    // An SSID with a space comes back quoted, so the read can be pasted
    // straight back into a write (docs/console-protocol.md s.3.5).
    TEST_ASSERT_EQUAL_STRING("\"Workshop WiFi\"", capturedValue("staSsid"));
    TEST_ASSERT_EQUAL_STRING("artoo-ap", capturedValue("apSsid"));
    // Passwords are reported as set/not-set, never returned.
    TEST_ASSERT_EQUAL_STRING("true", capturedValue("staPasswordSet"));
    TEST_ASSERT_EQUAL_STRING("true", capturedValue("apPasswordSet"));
    TEST_ASSERT_FALSE_MESSAGE(anyCapturedValueContains("hunter2hunter2"),
                              "a read must never echo the stored station password");
    TEST_ASSERT_FALSE_MESSAGE(anyCapturedValueContains("apsecret1"),
                              "a read must never echo the stored AP password");
    // Saved == active at this point, so nothing is owed.
    TEST_ASSERT_EQUAL_STRING("false", capturedValue("pendingApply"));
}

void test_wifi_settings_write_stages_the_group_and_reports_staged_until_reboot() {
    seedWifi(WifiMode::CLIENT, "old-net", "hunter2hunter2", "artoo-ap", "");

    runQuery("wifi.config.settings mode=client sta-ssid=bench-net");

    TEST_ASSERT_EQUAL(CONSOLE_STATUS_OK, g_cap.status);
    // ADR 0015: saved, not hot-applied - and the new settings differ from the
    // posture in force, so a restart is owed.
    TEST_ASSERT_EQUAL(CONSOLE_OUTCOME_STAGED_UNTIL_REBOOT, g_cap.outcome);

    // The Commit Step staged the cache for a subsequent read to see...
    WifiConfig staged = {};
    configCacheReadWifi(&staged);
    TEST_ASSERT_EQUAL_STRING("bench-net", staged.sta_ssid);
    // ...and the omitted password kept its stored value rather than being
    // cleared, which is the Apply Core's own omission-preserving contract
    // reaching the Console unchanged.
    TEST_ASSERT_EQUAL_STRING("hunter2hunter2", staged.sta_password);
    // The AP fields the operator did not name are untouched: a grouped write
    // is not a replace-everything write.
    TEST_ASSERT_EQUAL_STRING("artoo-ap", staged.ap_ssid);
}

void test_wifi_settings_write_reports_applied_when_nothing_is_left_to_restart_for() {
    seedWifi(WifiMode::CLIENT, "bench-net", "hunter2hunter2", "artoo-ap", "");

    // Re-writing the posture already in force leaves nothing pending, so the
    // truthful outcome is applied, not staged-until-reboot.
    runQuery("wifi.config.settings mode=client sta-ssid=bench-net");

    TEST_ASSERT_EQUAL(CONSOLE_STATUS_OK, g_cap.status);
    TEST_ASSERT_EQUAL(CONSOLE_OUTCOME_APPLIED, g_cap.outcome);
}

void test_wifi_settings_client_mode_without_an_ssid_is_rejected_as_a_whole() {
    seedWifi(WifiMode::STANDALONE_AP, "", "", "artoo-ap", "");

    runQuery("wifi.config.settings mode=client");

    TEST_ASSERT_EQUAL(CONSOLE_OUTCOME_INVALID, g_cap.outcome);
    TEST_ASSERT_EQUAL(CONSOLE_REASON_MISSING_ARGUMENT, g_cap.reason);
    TEST_ASSERT_EQUAL_STRING_MESSAGE("sta-ssid", capturedValue("argument"),
                                     "the grouped rule must name the field that failed");

    // Rejected as a whole: the mode the operator did supply must not have
    // been kept on its own.
    WifiConfig after = {};
    configCacheReadWifi(&after);
    TEST_ASSERT_EQUAL_INT_MESSAGE((int)WifiMode::STANDALONE_AP, (int)after.mode,
                                  "a partial group must not reach the config cache");
}

void test_wifi_settings_ap_mode_without_an_ssid_is_rejected_as_a_whole() {
    seedWifi(WifiMode::CLIENT, "bench-net", "", "", "");

    runQuery("wifi.config.settings mode=standalone_ap");

    TEST_ASSERT_EQUAL(CONSOLE_OUTCOME_INVALID, g_cap.outcome);
    TEST_ASSERT_EQUAL(CONSOLE_REASON_MISSING_ARGUMENT, g_cap.reason);
    TEST_ASSERT_EQUAL_STRING("ap-ssid", capturedValue("argument"));
}

// The secret exclusion, in both vocabularies plus a spelling belonging to
// neither: the refusal is a rule about the key, not a list of four strings.
void test_wifi_settings_refuses_every_password_key_spelling() {
    const char* lines[] = {
        "wifi.config.settings sta-password=hunter2hunter2",
        "wifi.config.settings ap-password=hunter2hunter2",
        "wifi.config.settings staPassword=hunter2hunter2",
        "wifi.config.settings apPassword=hunter2hunter2",
        "wifi.config.settings PASSWORD=hunter2hunter2",
        "wifi.config.settings mode=client sta-ssid=bench-net sta-password=hunter2hunter2",
    };
    for (size_t i = 0; i < sizeof(lines) / sizeof(lines[0]); ++i) {
        seedWifi(WifiMode::CLIENT, "bench-net", "storedpassword", "artoo-ap", "");

        runQuery(lines[i]);

        TEST_ASSERT_EQUAL_MESSAGE(CONSOLE_OUTCOME_INVALID, g_cap.outcome, lines[i]);
        TEST_ASSERT_EQUAL_MESSAGE(CONSOLE_REASON_SECRET_NOT_SETTABLE, g_cap.reason, lines[i]);
        // The answer names the key and nothing else - the value never appears
        // in any emitted record.
        TEST_ASSERT_FALSE_MESSAGE(anyCapturedValueContains("hunter2hunter2"),
                                  "the refused password value reached a record");
        // And the refusal happened before the Apply Core ran: the rest of the
        // line was not applied either.
        WifiConfig after = {};
        configCacheReadWifi(&after);
        TEST_ASSERT_EQUAL_STRING_MESSAGE("bench-net", after.sta_ssid,
                                         "a line carrying a secret must not apply its other fields");
        TEST_ASSERT_EQUAL_STRING_MESSAGE("storedpassword", after.sta_password,
                                         "the stored password must be untouched");
    }
}

void test_wifi_settings_accepts_a_utf8_ssid_inside_quotes() {
    seedWifi(WifiMode::CLIENT, "old-net", "", "artoo-ap", "");

    runQuery("wifi.config.settings mode=client sta-ssid=\"Verkstad \xc3\xa5\xc3\xa4\xc3\xb6\"");

    TEST_ASSERT_EQUAL(CONSOLE_STATUS_OK, g_cap.status);
    WifiConfig staged = {};
    configCacheReadWifi(&staged);
    TEST_ASSERT_EQUAL_STRING("Verkstad \xc3\xa5\xc3\xa4\xc3\xb6", staged.sta_ssid);
}

void test_wifi_settings_rejects_malformed_utf8_in_an_unquoted_ssid() {
    seedWifi(WifiMode::CLIENT, "old-net", "", "artoo-ap", "");

    // A lone continuation byte, typed without quotes - the tokenizer's own
    // UTF-8 check only covers quoted values, so this is the case the
    // executor's explicit check exists for.
    runQuery("wifi.config.settings sta-ssid=bad\x80ssid");

    TEST_ASSERT_EQUAL(CONSOLE_OUTCOME_INVALID, g_cap.outcome);
    TEST_ASSERT_EQUAL(CONSOLE_REASON_MALFORMED_ARGUMENT, g_cap.reason);
    TEST_ASSERT_EQUAL_STRING("sta-ssid", capturedValue("argument"));

    WifiConfig after = {};
    configCacheReadWifi(&after);
    TEST_ASSERT_EQUAL_STRING("old-net", after.sta_ssid);
}

void test_wifi_settings_enforces_the_32_byte_ssid_limit() {
    seedWifi(WifiMode::CLIENT, "old-net", "", "artoo-ap", "");

    // 33 bytes: one past WIFI_SSID_MAX_LEN (include/config_store.h), the
    // limit the Apply Core enforces and this adapter does not re-implement.
    runQuery("wifi.config.settings sta-ssid=aaaaaaaaaabbbbbbbbbbccccccccccddd");

    TEST_ASSERT_EQUAL(CONSOLE_OUTCOME_INVALID, g_cap.outcome);
    TEST_ASSERT_EQUAL(CONSOLE_REASON_OUT_OF_RANGE, g_cap.reason);
    TEST_ASSERT_EQUAL_STRING("sta-ssid", capturedValue("argument"));

    WifiConfig after = {};
    configCacheReadWifi(&after);
    TEST_ASSERT_EQUAL_STRING("old-net", after.sta_ssid);
}

// A 32-byte SSID is inside the limit and must be accepted - without this the
// test above would still pass with an off-by-one that rejected 32 too.
void test_wifi_settings_accepts_an_ssid_exactly_at_the_limit() {
    seedWifi(WifiMode::CLIENT, "old-net", "", "artoo-ap", "");

    runQuery("wifi.config.settings sta-ssid=aaaaaaaaaabbbbbbbbbbccccccccccdd");

    TEST_ASSERT_EQUAL(CONSOLE_STATUS_OK, g_cap.status);
    WifiConfig after = {};
    configCacheReadWifi(&after);
    TEST_ASSERT_EQUAL_UINT32(WIFI_SSID_MAX_LEN, (uint32_t)strlen(after.sta_ssid));
}

void test_wifi_settings_rejects_an_unknown_argument_and_a_bad_mode() {
    runQuery("wifi.config.settings bogus=1");
    TEST_ASSERT_EQUAL(CONSOLE_OUTCOME_INVALID, g_cap.outcome);
    TEST_ASSERT_EQUAL(CONSOLE_REASON_UNKNOWN_ARGUMENT, g_cap.reason);
    TEST_ASSERT_EQUAL_STRING("bogus", capturedValue("argument"));

    // The API's own body key is not a second accepted spelling: the Console
    // has one argument vocabulary (docs/console-protocol.md s.1.2).
    runQuery("wifi.config.settings staSsid=bench-net");
    TEST_ASSERT_EQUAL(CONSOLE_REASON_UNKNOWN_ARGUMENT, g_cap.reason);

    runQuery("wifi.config.settings mode=telepathy");
    TEST_ASSERT_EQUAL(CONSOLE_OUTCOME_INVALID, g_cap.outcome);
    TEST_ASSERT_EQUAL(CONSOLE_REASON_OUT_OF_RANGE, g_cap.reason);
    TEST_ASSERT_EQUAL_STRING("mode", capturedValue("argument"));
}

// The read renders the mode with the Console's own token table, which the
// write side validates against the registry's `values:`. If those two ever
// drift, a read's own output stops being a legal write - so feed it back.
void test_wifi_settings_read_mode_round_trips_into_a_write() {
    seedWifi(WifiMode::STANDALONE_AP, "", "", "artoo-ap", "");

    runQuery("wifi.config.settings");
    const char* mode = capturedValue("mode");
    TEST_ASSERT_NOT_NULL(mode);
    TEST_ASSERT_EQUAL_STRING("standalone_ap", mode);

    char line[96] = {};
    snprintf(line, sizeof(line), "wifi.config.settings mode=%s", mode);
    runQuery(line);
    TEST_ASSERT_EQUAL_MESSAGE(CONSOLE_STATUS_OK, g_cap.status,
                              "a mode the read printed must be a mode the write accepts");
}

void test_wifi_settings_write_reports_busy_when_the_mutex_is_already_held() {
    consoleModuleInit();
    seedWifi(WifiMode::CLIENT, "bench-net", "", "artoo-ap", "");
    paStubMutexReset();
    struct PaStubMutex* m = paStubMutexStorage();
    m->held = 1;  // the OTHER Console adapter is mid-write

    runQuery("wifi.config.settings mode=client sta-ssid=other-net");

    TEST_ASSERT_EQUAL(CONSOLE_STATUS_ERR, g_cap.status);
    TEST_ASSERT_EQUAL(CONSOLE_OUTCOME_UNAVAILABLE, g_cap.outcome);
    TEST_ASSERT_EQUAL(CONSOLE_REASON_TEMPORARILY_UNAVAILABLE, g_cap.reason);

    WifiConfig after = {};
    configCacheReadWifi(&after);
    TEST_ASSERT_EQUAL_STRING_MESSAGE("bench-net", after.sta_ssid,
                                     "a write blocked by contention must never reach the cache");

    paStubMutexReset();
}

void test_help_marks_the_wifi_password_params_write_excluded() {
    runQuery("help wifi.config.settings");

    const char* params = capturedValue("params");
    TEST_ASSERT_NOT_NULL(params);
    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(params, "sta-password:string:write-excluded"),
                                 "help must show the station password as write-excluded");
    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(params, "ap-password:string:write-excluded"),
                                 "help must show the AP password as write-excluded");
    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(params, "sta-ssid:string:optional"),
                                 "a settable field must still read as optional");
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

// system.action.reboot (#225): no arguments, a status broadcast, then the
// complete result record, then requestSystemRestart() -
// g_test_restart_requests is the native stub's own observation hook
// (src/native_test_stubs.cpp), the same one test_api_motion_routes.cpp
// already uses for POST /api/reboot. The record-before-restart ordering
// criterion 3 asks for is a source-level property of
// consoleExecuteDirectReboot() (include/console_direct_action_system.h) -
// this proves both calls happen exactly once per accepted command, not the
// ordering between them.
void test_direct_reboot_broadcasts_answers_then_requests_restart() {
    runQuery("system.action.reboot");

    TEST_ASSERT_EQUAL(CONSOLE_STATUS_OK, g_cap.status);
    TEST_ASSERT_EQUAL(CONSOLE_OUTCOME_APPLIED, g_cap.outcome);
    TEST_ASSERT_TRUE_MESSAGE(g_cap.resultCalled, "reboot answers a single result record");
    TEST_ASSERT_EQUAL_UINT(1, g_test_status_broadcast_count);
    TEST_ASSERT_EQUAL_UINT(1, g_test_restart_requests);
}

void test_direct_reboot_rejects_an_argument() {
    runQuery("system.action.reboot delayMs=1000");

    TEST_ASSERT_EQUAL(CONSOLE_OUTCOME_INVALID, g_cap.outcome);
    TEST_ASSERT_EQUAL(CONSOLE_REASON_UNKNOWN_ARGUMENT, g_cap.reason);
    TEST_ASSERT_EQUAL_UINT_MESSAGE(0, g_test_restart_requests,
                                   "a rejected command must never reach requestSystemRestart()");
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
// sound.action.* remainder (#258): every $-letter dollar shortcut, the raw
// dollar-command passthrough, track-stop, query-status, set-mood-map and
// set-category-range. sound.action.random-* (12 rows) are deliberately NOT
// tested here - test_scoped_non_motion_actions_are_not_executor_not_ready
// above already proves the whole ACTION_REGISTRY[] sweep those rows go
// through, and adding a second assertion for them here would test
// consoleExecuteAction() a second time, not this file's own new code.
// =============================================================================

// The nine named-track shortcuts each send one specific two-character
// dollar command - the assertion that actually distinguishes "scream plays"
// from "the wrong track plays". Table-driven over
// consoleExecuteSoundDollarShortcut()'s one shared body so a copy/paste slip
// in any one of the nine thin wrappers (include/console_direct_action_
// sound.h) fails here.
void test_sound_named_track_shortcuts_send_the_right_dollar_command() {
    struct Case {
        const char* operation;
        const char* expectedDollar;
    };
    static const Case kCases[] = {
        {"sound.action.play-track-scream", "$S"},
        {"sound.action.play-track-faint", "$F"},
        {"sound.action.play-track-leia", "$L"},
        {"sound.action.play-track-cantina-short", "$c"},
        {"sound.action.play-track-cantina-long", "$C"},
        {"sound.action.play-track-sw-theme", "$W"},
        {"sound.action.play-track-imperial-march", "$M"},
        {"sound.action.play-track-startup", "$B"},
        {"sound.action.play-track-disco", "$D"},
    };
    for (const Case& c : kCases) {
        g_test_audio_dollar_calls = 0;
        g_test_audio_last_dollar[0] = '\0';

        runQuery(c.operation);

        TEST_ASSERT_EQUAL_MESSAGE(CONSOLE_STATUS_OK, g_cap.status, c.operation);
        TEST_ASSERT_EQUAL_MESSAGE(CONSOLE_OUTCOME_QUEUED, g_cap.outcome, c.operation);
        TEST_ASSERT_EQUAL_UINT_MESSAGE(1u, g_test_audio_dollar_calls, c.operation);
        TEST_ASSERT_EQUAL_STRING_MESSAGE(c.expectedDollar, g_test_audio_last_dollar, c.operation);
    }
}

// The same sleep gate handleAudioPost()'s action=dollar branch applies -
// scream stands in for all nine named-track rows, which share one body.
void test_sound_named_track_shortcut_blocked_while_sleeping() {
    robotState.sleepMode = true;

    runQuery("sound.action.play-track-scream");

    TEST_ASSERT_EQUAL(CONSOLE_OUTCOME_BLOCKED, g_cap.outcome);
    TEST_ASSERT_EQUAL(CONSOLE_REASON_TEMPORARILY_UNAVAILABLE, g_cap.reason);
    TEST_ASSERT_EQUAL_UINT(0u, g_test_audio_dollar_calls);
}

void test_sound_named_track_shortcut_reports_a_full_queue() {
    g_test_audio_queue_ok = false;

    runQuery("sound.action.play-track-scream");

    TEST_ASSERT_EQUAL(CONSOLE_STATUS_ERR, g_cap.status);
    TEST_ASSERT_EQUAL(CONSOLE_OUTCOME_QUEUE_FULL, g_cap.outcome);
    TEST_ASSERT_EQUAL(CONSOLE_REASON_QUEUE_FULL, g_cap.reason);
}

// Every row this file wires through consoleExecuteSoundDollarShortcut()
// declares zero params in the registry, so any supplied argument must be
// unknown - scream stands in for all of them again.
void test_sound_named_track_shortcut_rejects_any_argument() {
    runQuery("sound.action.play-track-scream track=1");

    TEST_ASSERT_EQUAL(CONSOLE_OUTCOME_INVALID, g_cap.outcome);
    TEST_ASSERT_EQUAL(CONSOLE_REASON_UNKNOWN_ARGUMENT, g_cap.reason);
    TEST_ASSERT_EQUAL_UINT(0u, g_test_audio_dollar_calls);
}

void test_sound_quiet_sends_dollar_s() {
    runQuery("sound.action.quiet");

    TEST_ASSERT_EQUAL(CONSOLE_STATUS_OK, g_cap.status);
    TEST_ASSERT_EQUAL(CONSOLE_OUTCOME_QUEUED, g_cap.outcome);
    TEST_ASSERT_EQUAL_UINT(1u, g_test_audio_dollar_calls);
    TEST_ASSERT_EQUAL_STRING("$s", g_test_audio_last_dollar);
}

void test_sound_random_on_off_send_the_right_dollar_command() {
    runQuery("sound.action.random-on");
    TEST_ASSERT_EQUAL(CONSOLE_OUTCOME_QUEUED, g_cap.outcome);
    TEST_ASSERT_EQUAL_STRING("$R", g_test_audio_last_dollar);

    g_test_audio_dollar_calls = 0;
    runQuery("sound.action.random-off");
    TEST_ASSERT_EQUAL(CONSOLE_OUTCOME_QUEUED, g_cap.outcome);
    TEST_ASSERT_EQUAL_UINT(1u, g_test_audio_dollar_calls);
    TEST_ASSERT_EQUAL_STRING("$O", g_test_audio_last_dollar);
}

// The five relative/preset volume shortcuts - distinct dollar letters from
// sound.action.set-volume's own absolute AUDIO_CMD_SET_VOLUME path.
void test_sound_volume_shortcuts_send_the_right_dollar_command() {
    struct Case {
        const char* operation;
        const char* expectedDollar;
    };
    static const Case kCases[] = {
        {"sound.action.volume-up", "$+"},
        {"sound.action.volume-down", "$-"},
        {"sound.action.volume-preset-mid", "$m"},
        {"sound.action.volume-preset-max", "$f"},
        {"sound.action.volume-preset-min", "$p"},
    };
    for (const Case& c : kCases) {
        g_test_audio_dollar_calls = 0;
        g_test_audio_last_dollar[0] = '\0';

        runQuery(c.operation);

        TEST_ASSERT_EQUAL_MESSAGE(CONSOLE_OUTCOME_QUEUED, g_cap.outcome, c.operation);
        TEST_ASSERT_EQUAL_STRING_MESSAGE(c.expectedDollar, g_test_audio_last_dollar, c.operation);
    }
}

// Unlike sound.action.set-volume (no sleep gate), the dollar-routed volume
// shortcuts inherit the dollar path's sleep gate - the behavior difference
// this file's own header comment on consoleExecuteSoundVolumeUp() etc. calls
// out explicitly.
void test_sound_volume_shortcut_blocked_while_sleeping() {
    robotState.sleepMode = true;

    runQuery("sound.action.volume-up");

    TEST_ASSERT_EQUAL(CONSOLE_OUTCOME_BLOCKED, g_cap.outcome);
    TEST_ASSERT_EQUAL(CONSOLE_REASON_TEMPORARILY_UNAVAILABLE, g_cap.reason);
}

void test_sound_dollar_command_sends_the_supplied_cmd() {
    runQuery("sound.action.dollar-command cmd=$007");

    TEST_ASSERT_EQUAL(CONSOLE_STATUS_OK, g_cap.status);
    TEST_ASSERT_EQUAL(CONSOLE_OUTCOME_QUEUED, g_cap.outcome);
    TEST_ASSERT_EQUAL_UINT(1u, g_test_audio_dollar_calls);
    TEST_ASSERT_EQUAL_STRING("$007", g_test_audio_last_dollar);
}

void test_sound_dollar_command_rejects_missing_dollar_prefix() {
    runQuery("sound.action.dollar-command cmd=R");

    TEST_ASSERT_EQUAL(CONSOLE_OUTCOME_INVALID, g_cap.outcome);
    TEST_ASSERT_EQUAL(CONSOLE_REASON_OUT_OF_RANGE, g_cap.reason);
    TEST_ASSERT_EQUAL_UINT(0u, g_test_audio_dollar_calls);
}

void test_sound_dollar_command_rejects_a_too_long_command() {
    runQuery("sound.action.dollar-command cmd=$0123456789");

    TEST_ASSERT_EQUAL(CONSOLE_OUTCOME_INVALID, g_cap.outcome);
    TEST_ASSERT_EQUAL(CONSOLE_REASON_OUT_OF_RANGE, g_cap.reason);
    TEST_ASSERT_EQUAL_UINT(0u, g_test_audio_dollar_calls);
}

void test_sound_dollar_command_rejects_missing_cmd() {
    runQuery("sound.action.dollar-command");

    TEST_ASSERT_EQUAL(CONSOLE_OUTCOME_INVALID, g_cap.outcome);
    TEST_ASSERT_EQUAL(CONSOLE_REASON_MISSING_ARGUMENT, g_cap.reason);
}

void test_sound_dollar_command_rejects_unknown_argument() {
    runQuery("sound.action.dollar-command foo=bar");

    TEST_ASSERT_EQUAL(CONSOLE_OUTCOME_INVALID, g_cap.outcome);
    TEST_ASSERT_EQUAL(CONSOLE_REASON_UNKNOWN_ARGUMENT, g_cap.reason);
}

void test_sound_dollar_command_blocked_while_sleeping() {
    robotState.sleepMode = true;

    runQuery("sound.action.dollar-command cmd=$R");

    TEST_ASSERT_EQUAL(CONSOLE_OUTCOME_BLOCKED, g_cap.outcome);
    TEST_ASSERT_EQUAL(CONSOLE_REASON_TEMPORARILY_UNAVAILABLE, g_cap.reason);
    TEST_ASSERT_EQUAL_UINT(0u, g_test_audio_dollar_calls);
}

void test_sound_dollar_command_reports_a_full_queue() {
    g_test_audio_queue_ok = false;

    runQuery("sound.action.dollar-command cmd=$R");

    TEST_ASSERT_EQUAL(CONSOLE_STATUS_ERR, g_cap.status);
    TEST_ASSERT_EQUAL(CONSOLE_OUTCOME_QUEUE_FULL, g_cap.outcome);
    TEST_ASSERT_EQUAL(CONSOLE_REASON_QUEUE_FULL, g_cap.reason);
}

// track-stop has no sleep gate (handleAudioPost()'s action=stop branch has
// none) - proven here by leaving sleepMode true and still expecting success,
// not merely by omitting a sleep test.
void test_sound_track_stop_queues_even_while_sleeping() {
    robotState.sleepMode = true;

    runQuery("sound.action.track-stop");

    TEST_ASSERT_EQUAL(CONSOLE_STATUS_OK, g_cap.status);
    TEST_ASSERT_EQUAL(CONSOLE_OUTCOME_QUEUED, g_cap.outcome);
    TEST_ASSERT_EQUAL_UINT(1u, g_test_audio_stop_calls);
}

void test_sound_track_stop_reports_a_full_queue() {
    g_test_audio_queue_ok = false;

    runQuery("sound.action.track-stop");

    TEST_ASSERT_EQUAL(CONSOLE_STATUS_ERR, g_cap.status);
    TEST_ASSERT_EQUAL(CONSOLE_OUTCOME_QUEUE_FULL, g_cap.outcome);
    TEST_ASSERT_EQUAL(CONSOLE_REASON_QUEUE_FULL, g_cap.reason);
}

void test_sound_query_status_queues() {
    runQuery("sound.action.query-status");

    TEST_ASSERT_EQUAL(CONSOLE_STATUS_OK, g_cap.status);
    TEST_ASSERT_EQUAL(CONSOLE_OUTCOME_QUEUED, g_cap.outcome);
    TEST_ASSERT_EQUAL_UINT(1u, g_test_audio_query_calls);
}

void test_sound_query_status_reports_a_full_queue() {
    g_test_audio_queue_ok = false;

    runQuery("sound.action.query-status");

    TEST_ASSERT_EQUAL(CONSOLE_STATUS_ERR, g_cap.status);
    TEST_ASSERT_EQUAL(CONSOLE_OUTCOME_QUEUE_FULL, g_cap.outcome);
    TEST_ASSERT_EQUAL(CONSOLE_REASON_QUEUE_FULL, g_cap.reason);
}

void test_sound_set_mood_map_applies_all_four_masks() {
    runQuery("sound.action.set-mood-map quiet=1 mid=2 full=3 awakeplus=4");

    TEST_ASSERT_EQUAL(CONSOLE_STATUS_OK, g_cap.status);
    TEST_ASSERT_EQUAL(CONSOLE_OUTCOME_APPLIED, g_cap.outcome);
}

void test_sound_set_mood_map_rejects_a_partial_form() {
    runQuery("sound.action.set-mood-map quiet=1 mid=2 full=3");

    TEST_ASSERT_EQUAL(CONSOLE_OUTCOME_INVALID, g_cap.outcome);
    TEST_ASSERT_EQUAL(CONSOLE_REASON_MISSING_ARGUMENT, g_cap.reason);
}

void test_sound_set_mood_map_rejects_an_out_of_range_mask() {
    runQuery("sound.action.set-mood-map quiet=5000 mid=2 full=3 awakeplus=4");

    TEST_ASSERT_EQUAL(CONSOLE_OUTCOME_INVALID, g_cap.outcome);
    TEST_ASSERT_EQUAL(CONSOLE_REASON_OUT_OF_RANGE, g_cap.reason);
}

void test_sound_set_category_range_applies_and_persists() {
    runQuery("sound.action.set-category-range lo_key=snd_cat_gen_lo hi_key=snd_cat_gen_hi lo=10 hi=20");

    TEST_ASSERT_EQUAL(CONSOLE_STATUS_OK, g_cap.status);
    TEST_ASSERT_EQUAL(CONSOLE_OUTCOME_APPLIED, g_cap.outcome);

    ConfigSnapshot snap = {};
    configCacheRead(&snap);
    TEST_ASSERT_EQUAL_UINT16(10, snap.audio.snd_cat_gen_lo);
    TEST_ASSERT_EQUAL_UINT16(20, snap.audio.snd_cat_gen_hi);
}

// lo_key/hi_key naming a mismatched pair - audioCategoryRangeApply()'s own
// validation, unreachable by the registry's declared schema (lo_key/hi_key
// are plain strings there, so the pairing rule is not schema-expressible).
void test_sound_set_category_range_rejects_a_mismatched_key_pair() {
    runQuery(
        "sound.action.set-category-range lo_key=snd_cat_gen_lo hi_key=snd_cat_chat_hi lo=1 hi=2");

    TEST_ASSERT_EQUAL(CONSOLE_OUTCOME_INVALID, g_cap.outcome);
    TEST_ASSERT_EQUAL(CONSOLE_REASON_OUT_OF_RANGE, g_cap.reason);
}

void test_sound_set_category_range_rejects_lo_greater_than_hi() {
    runQuery("sound.action.set-category-range lo_key=snd_cat_gen_lo hi_key=snd_cat_gen_hi lo=20 hi=10");

    TEST_ASSERT_EQUAL(CONSOLE_OUTCOME_INVALID, g_cap.outcome);
    TEST_ASSERT_EQUAL(CONSOLE_REASON_OUT_OF_RANGE, g_cap.reason);
}

void test_sound_set_category_range_rejects_bank_as_an_unknown_argument() {
    // bank/page (REST's optional CHIRP-binding extension) are not in this
    // row's registry schema - Console exposes only the plain lo/hi form.
    runQuery(
        "sound.action.set-category-range lo_key=snd_cat_gen_lo hi_key=snd_cat_gen_hi lo=1 hi=2 "
        "bank=1");

    TEST_ASSERT_EQUAL(CONSOLE_OUTCOME_INVALID, g_cap.outcome);
    TEST_ASSERT_EQUAL(CONSOLE_REASON_UNKNOWN_ARGUMENT, g_cap.reason);
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

// servo.action.stop (#221 remainder registry fix + wiring): the row now
// declares the same target enum open/close/set-position do, and
// consoleExecuteServoStop() (include/console_direct_action_servo.h) resolves
// it the same way - see that function's own header comment for why "stop"
// is not simply consoleExecuteServoCommand(SERVO_CMD_POSITION, ...).
void test_servo_stop_queues_with_the_resolved_arm_id() {
    runQuery("servo.action.stop target=arm1");

    TEST_ASSERT_EQUAL(CONSOLE_STATUS_OK, g_cap.status);
    TEST_ASSERT_EQUAL(CONSOLE_OUTCOME_QUEUED, g_cap.outcome);
}

// target=both is still accepted (the catalog enum matches open/close's, not
// set-position's narrower one) even though it only reaches ARM1+ARM2 at the
// servo_task.cpp level - that asymmetry is firmware behaviour this ticket
// does not change, only makes reachable from the Console (see this test
// file's neighbouring servo.action.open test for the same acceptance and
// include/console_direct_action_servo.h's header comment for the citation).
void test_servo_stop_accepts_both_as_the_broadcast_target() {
    runQuery("servo.action.stop target=both");

    TEST_ASSERT_EQUAL(CONSOLE_OUTCOME_QUEUED, g_cap.outcome);
}

void test_servo_stop_rejects_a_missing_target() {
    runQuery("servo.action.stop");

    TEST_ASSERT_EQUAL(CONSOLE_OUTCOME_INVALID, g_cap.outcome);
    TEST_ASSERT_EQUAL(CONSOLE_REASON_MISSING_ARGUMENT, g_cap.reason);
}

void test_servo_stop_rejects_an_unknown_target() {
    runQuery("servo.action.stop target=aux9");

    TEST_ASSERT_EQUAL(CONSOLE_OUTCOME_INVALID, g_cap.outcome);
    TEST_ASSERT_EQUAL(CONSOLE_REASON_OUT_OF_RANGE, g_cap.reason);
}

// No position_us key exists on this row's schema (unlike set-position) - a
// caller supplying one gets the same "unknown argument" answer any other
// unrecognized key does, not a silently ignored value.
void test_servo_stop_rejects_position_us_as_an_unknown_argument() {
    runQuery("servo.action.stop target=arm1 position_us=1500");

    TEST_ASSERT_EQUAL(CONSOLE_OUTCOME_INVALID, g_cap.outcome);
    TEST_ASSERT_EQUAL(CONSOLE_REASON_UNKNOWN_ARGUMENT, g_cap.reason);
    TEST_ASSERT_EQUAL_STRING("position_us", capturedValue("argument"));
}

// #257: g_directActionExecutors[] split into five per-domain tables
// (include/console_direct_action_{system,drive,sound,aux_rc,servo}.h). Every
// row's own behavior is already asserted above by name (e.g.
// test_commanded_mode_set_mode_stationary_calls_setter_and_broadcasts,
// test_direct_set_mood_applies_valid_mood_and_broadcasts, ...); this is the
// split's own completeness guard, the one the ticket's proof obligation is
// about - originally all 20 canonical names the single pre-split table
// carried; #258 extends the list with its own 22 newly-wired sound.action.*
// rows for the same reason, called with no arguments and asserted to still
// resolve to a real executor. A row silently dropped from a domain header
// answers EXECUTOR_NOT_READY (no dispatch-table row) or UNKNOWN_OPERATION
// (not even in the catalog) instead of running its own argument validation -
// this fails on either, so a future domain-file edit that drops a row fails
// here even if it also deletes that row's own dedicated test above.
// sound.action.random-* (12 rows) are NOT here: they dispatch through the
// ACTION_REGISTRY[]/dispatchRcTriggerActionTest() path, never through
// consoleFindDirectActionExecutor()/g_soundDirectActionExecutors[] - this
// list is scoped to that one table's own rows, matching its own name.
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
        "sound.action.play-track-scream",
        "sound.action.play-track-faint",
        "sound.action.play-track-leia",
        "sound.action.play-track-cantina-short",
        "sound.action.play-track-cantina-long",
        "sound.action.play-track-sw-theme",
        "sound.action.play-track-imperial-march",
        "sound.action.play-track-startup",
        "sound.action.play-track-disco",
        "sound.action.quiet",
        "sound.action.random-on",
        "sound.action.random-off",
        "sound.action.volume-up",
        "sound.action.volume-down",
        "sound.action.volume-preset-mid",
        "sound.action.volume-preset-max",
        "sound.action.volume-preset-min",
        "sound.action.dollar-command",
        "sound.action.track-stop",
        "sound.action.query-status",
        "sound.action.set-mood-map",
        "sound.action.set-category-range",
        "aux.action.led-color",
        "aux.action.led-effect",
        "servo.action.open",
        "servo.action.close",
        "servo.action.set-position",
        "servo.action.stop",
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

// dome.api.get-sequence / dome.api.get-layout (#221 remainder): the other
// two of the five dome.api.* rows - the three above them
// (get-sequence-last-run/list-sequences/list-builtin-sequences) are wired
// and covered by their own tests above. These two stay EXECUTOR_NOT_READY
// on purpose, the same document/bulk-transfer #206 exclusion
// dome.action.save-sequence's test above asserts: seqStoreReadFileSlice()/
// domeLayoutCacheReadChunk() are byte-slice readers over one stored
// document (a Learned Sequence JSON v1 file; the dome's cached composed-
// layout JSON), not a gap this ticket owes a Console Record shape for -
// see the registry entries' own comments (docs/action-registry.yaml) and
// consoleExecuteCommand()'s CONSOLE_OP_ACTION case (src/console/
// console_module.cpp) for the full reasoning. Asserted here by name for the
// same reason dome.action.save-sequence's test is: a future accidental
// wiring (or unwiring) is caught, not folded into the aggregate #220
// report count.
void test_action_get_sequence_stays_executor_not_ready_document_transfer_out_of_scope() {
    runQuery("dome.api.get-sequence");

    TEST_ASSERT_EQUAL(CONSOLE_OUTCOME_UNAVAILABLE, g_cap.outcome);
    TEST_ASSERT_EQUAL(CONSOLE_REASON_EXECUTOR_NOT_READY, g_cap.reason);
}

void test_action_get_layout_stays_executor_not_ready_document_transfer_out_of_scope() {
    runQuery("dome.api.get-layout");

    TEST_ASSERT_EQUAL(CONSOLE_OUTCOME_UNAVAILABLE, g_cap.outcome);
    TEST_ASSERT_EQUAL(CONSOLE_REASON_EXECUTOR_NOT_READY, g_cap.reason);
}

// =============================================================================
// Known-but-unavailable operations (#224, ADR 0029/0034)
//
// [env:native] builds with PA_HEAP_PROFILE=0 PA_HEAP_TRACING=0
// PA_ADMISSION_TRACE=1 (platformio.ini), so the four catalog rows carrying a
// registry build_flag: split three-to-one inside ONE binary - which is what
// makes these tests non-vacuous. They prove the execute-time guard refuses
// the three that are compiled out AND lets the one that is compiled in
// through, rather than refusing everything with a build_flag.
// =============================================================================

// `operations` answers item records, so it needs its own capture, the same
// shape (and for the same reason) as runLogsQuery()'s above - the shared
// g_cap harness the other tests use discards items.
struct CapturedOperationItems {
    // Sized from the catalog itself at run time; 256 comfortably exceeds the
    // ~193 entries the registry holds today and the listing is bounded by
    // the catalog, never by input.
    char values[256][160];
    int count;
    bool endCalled;
    ConsoleOutcome outcome;
};
static CapturedOperationItems g_opsCap;

static void opsCapItem(uint32_t, const char* value) {
    if (g_opsCap.count >= (int)(sizeof(g_opsCap.values) / sizeof(g_opsCap.values[0]))) return;
    snprintf(g_opsCap.values[g_opsCap.count], sizeof(g_opsCap.values[0]), "%s", value);
    g_opsCap.count++;
}
static void opsCapEnd(uint32_t, ConsoleStatus, ConsoleOutcome outcome, ConsoleReason) {
    g_opsCap.endCalled = true;
    g_opsCap.outcome = outcome;
}

static void runOperationsListing() {
    memset(&g_opsCap, 0, sizeof(g_opsCap));
    ConsoleRecordSink sink = {};
    sink.onRecordItem = opsCapItem;
    sink.onRecordEnd = opsCapEnd;

    ConsoleRequest req = {};
    req.requestId = 1;
    req.source = CONSOLE_SOURCE_SERIAL;
    req.operationName = "operations";
    consoleExecuteCommand(&req, &sink);
}

// The listing line for one operation, or nullptr when the listing omitted it.
static const char* listedOperationItem(const char* operationName) {
    const size_t nameLen = strlen(operationName);
    for (int i = 0; i < g_opsCap.count; ++i) {
        if (strncmp(g_opsCap.values[i], operationName, nameLen) == 0 &&
            g_opsCap.values[i][nameLen] == ' ') {
            return g_opsCap.values[i];
        }
    }
    return nullptr;
}

// The ticket's central defect (routed here from #219): before this guard the
// three PA_HEAP_PROFILE/PA_HEAP_TRACING rows fell through to whichever
// executor lookup failed and answered executor-not-ready - "not wired yet",
// which reads as "this may start working", when the truth is that the
// feature is not in this image at all.
void test_profiler_snapshot_answers_not_in_this_build() {
    runQuery("system.api.get-profiler");

    TEST_ASSERT_EQUAL(CONSOLE_STATUS_ERR, g_cap.status);
    TEST_ASSERT_EQUAL(CONSOLE_OUTCOME_UNAVAILABLE, g_cap.outcome);
    TEST_ASSERT_EQUAL(CONSOLE_REASON_NOT_IN_THIS_BUILD, g_cap.reason);
}

void test_profiler_trace_start_answers_not_in_this_build() {
    runQuery("system.action.profiler-trace-start");

    TEST_ASSERT_EQUAL(CONSOLE_STATUS_ERR, g_cap.status);
    TEST_ASSERT_EQUAL(CONSOLE_OUTCOME_UNAVAILABLE, g_cap.outcome);
    TEST_ASSERT_EQUAL(CONSOLE_REASON_NOT_IN_THIS_BUILD, g_cap.reason);
}

void test_profiler_trace_stop_answers_not_in_this_build() {
    runQuery("system.action.profiler-trace-stop");

    TEST_ASSERT_EQUAL(CONSOLE_STATUS_ERR, g_cap.status);
    TEST_ASSERT_EQUAL(CONSOLE_OUTCOME_UNAVAILABLE, g_cap.outcome);
    TEST_ASSERT_EQUAL(CONSOLE_REASON_NOT_IN_THIS_BUILD, g_cap.reason);
}

// The other half of the same guard, and the reason a "refuse anything with a
// build_flag" implementation would not pass: PA_ADMISSION_TRACE is 1 in this
// binary, so system.api.get-admission-trace must reach dispatch. It has no
// executor row yet (a later ticket's work), so it answers executor-not-ready
// - the point is only that the build guard did not claim it.
void test_an_enabled_build_flag_is_not_refused_by_the_build_guard() {
    const ConsoleCatalogEntry* entry = consoleCatalogFindByName("system.api.get-admission-trace");
    TEST_ASSERT_NOT_NULL(entry);
    TEST_ASSERT_TRUE_MESSAGE(entry->available_in_build,
                             "PA_ADMISSION_TRACE=1 in [env:native]; this test is vacuous without it");

    runQuery("system.api.get-admission-trace");

    TEST_ASSERT_NOT_EQUAL(CONSOLE_REASON_NOT_IN_THIS_BUILD, g_cap.reason);
}

// Catalog-wide, so a build_flag added to a future registry row is covered
// the day it lands rather than the day someone remembers to add a test.
void test_every_out_of_build_row_answers_not_in_this_build() {
    size_t count = 0;
    const ConsoleCatalogEntry* entries = consoleCatalogGetEntries(&count);
    int checked = 0;

    for (size_t i = 0; i < count; ++i) {
        if (entries[i].available_in_build) continue;
        checked++;
        runQuery(entries[i].name);
        TEST_ASSERT_EQUAL_MESSAGE(CONSOLE_OUTCOME_UNAVAILABLE, g_cap.outcome, entries[i].name);
        TEST_ASSERT_EQUAL_MESSAGE(CONSOLE_REASON_NOT_IN_THIS_BUILD, g_cap.reason, entries[i].name);
    }

    TEST_ASSERT_GREATER_THAN_MESSAGE(0, checked,
                                     "no out-of-build catalog row in this binary - the sweep proved nothing");
}

// The board half of the same guard. It is VACUOUS in every build that ships
// today and says so out loud: tools/generate_console_catalog.py gives the
// drive domain PA_CAP_DRIVE_BACKEND_HOVERBOARD and every other row a literal
// 1, and include/config.h defines that capability as 1 for BOTH PA_BOARD
// values - so no catalog row is off-board on any current board, and
// not-on-this-board is unreachable from a real operation. Reported on #224
// rather than faked with a stand-in row. The sweep is written anyway so the
// first genuinely board-gated row is covered on arrival.
void test_every_off_board_row_answers_not_on_this_board() {
    size_t count = 0;
    const ConsoleCatalogEntry* entries = consoleCatalogGetEntries(&count);
    int checked = 0;

    for (size_t i = 0; i < count; ++i) {
        if (entries[i].available_on_board) continue;
        checked++;
        runQuery(entries[i].name);
        TEST_ASSERT_EQUAL_MESSAGE(CONSOLE_OUTCOME_UNAVAILABLE, g_cap.outcome, entries[i].name);
        TEST_ASSERT_EQUAL_MESSAGE(CONSOLE_REASON_NOT_ON_THIS_BOARD, g_cap.reason, entries[i].name);
    }

    printf("[#224 report] off-board catalog rows in this build: %d\n", checked);
}

// Discovery keeps unavailable operations visible, and names the same reason
// execution does. The two used to disagree for exactly these rows.
void test_operations_lists_out_of_build_rows_with_the_reason_execution_gives() {
    size_t count = 0;
    const ConsoleCatalogEntry* entries = consoleCatalogGetEntries(&count);

    runOperationsListing();
    TEST_ASSERT_TRUE(g_opsCap.endCalled);
    TEST_ASSERT_EQUAL(CONSOLE_OUTCOME_COMPLETED, g_opsCap.outcome);

    int checked = 0;
    for (size_t i = 0; i < count; ++i) {
        if (entries[i].available_in_build) continue;
        checked++;
        const char* item = listedOperationItem(entries[i].name);
        TEST_ASSERT_NOT_NULL_MESSAGE(item, entries[i].name);
        TEST_ASSERT_NOT_NULL_MESSAGE(strstr(item, "not-in-this-build"), item);

        runQuery(entries[i].name);
        TEST_ASSERT_EQUAL_MESSAGE(CONSOLE_REASON_NOT_IN_THIS_BUILD, g_cap.reason, entries[i].name);
    }
    TEST_ASSERT_GREATER_THAN(0, checked);
}

// =============================================================================
// Readiness is reported at execution, not at discovery (ADR 0035, #263)
//
// The catalog used to carry an executor_ready flag the generator hardcoded to
// true for every entry. So `help` claimed every operation was wired, and the
// `operations` listing's executor-not-ready branch could never be reached,
// while dispatch was refusing dozens of rows with exactly that reason. The
// flag is gone; these two tests hold both operator surfaces to that.
// =============================================================================

// Discovery annotates only the two availability facts that are knowable
// without executing. No listing line may carry executor-not-ready: that is an
// execution-time answer, and deriving it from a catalog flag is the shape
// ADR 0035 removed.
void test_operations_never_annotates_executor_not_ready() {
    runOperationsListing();

    TEST_ASSERT_TRUE(g_opsCap.endCalled);
    TEST_ASSERT_EQUAL(CONSOLE_OUTCOME_COMPLETED, g_opsCap.outcome);
    TEST_ASSERT_GREATER_THAN(0, g_opsCap.count);

    for (int i = 0; i < g_opsCap.count; ++i) {
        TEST_ASSERT_NULL_MESSAGE(strstr(g_opsCap.values[i], "executor-not-ready"),
                                 g_opsCap.values[i]);
    }
}

// `help <op>` on a row execution genuinely refuses: it still describes the
// operation and still reports the three real availability facts, and says
// nothing at all about readiness. The row is found by asking dispatch rather
// than being named here, so this keeps testing the same thing as later tickets
// wire more executors - and if the refused set ever empties, the first
// assertion says so out loud instead of passing vacuously.
void test_help_reports_no_readiness_for_a_row_execution_refuses() {
    robotState.webControlEnabled = true;
    size_t count = 0;
    const ConsoleCatalogEntry* entries = consoleCatalogGetEntries(&count);

    const char* refusedRow = nullptr;
    for (size_t i = 0; i < count && refusedRow == nullptr; ++i) {
        if (strcmp(entries[i].type, CONSOLE_CATALOG_TYPE_ACTION) != 0) continue;
        runQuery(entries[i].name);
        if (g_cap.reason == CONSOLE_REASON_EXECUTOR_NOT_READY) {
            refusedRow = entries[i].name;
        }
    }
    TEST_ASSERT_NOT_NULL_MESSAGE(
        refusedRow,
        "no action row answers executor-not-ready any more; point this test at any catalog row");

    char helpCommand[160];
    snprintf(helpCommand, sizeof(helpCommand), "help %s", refusedRow);
    runQuery(helpCommand);

    TEST_ASSERT_TRUE(g_cap.beginCalled);
    TEST_ASSERT_TRUE(g_cap.endCalled);
    TEST_ASSERT_EQUAL(CONSOLE_OUTCOME_COMPLETED, g_cap.outcome);
    TEST_ASSERT_NOT_NULL_MESSAGE(capturedValue("available_on_board"), refusedRow);
    TEST_ASSERT_NOT_NULL_MESSAGE(capturedValue("available_in_build"), refusedRow);
    TEST_ASSERT_NOT_NULL_MESSAGE(capturedValue("requires_web_control"), refusedRow);
    TEST_ASSERT_NULL_MESSAGE(capturedValue("executor_ready"),
                             "help must not advertise executor readiness at discovery time");
}

// #224 reclassified system.api.get-profiler from type: action to type: status
// with is_query: true (docs/action-registry.yaml, the same move #221 made for
// the dome.api.* queries). Asserted against the COMPILED catalog so a future
// registry edit or a lost regeneration is caught here rather than by the row
// quietly answering through the action path, which emits a single result
// record and has no shape for a snapshot.
void test_profiler_snapshot_is_registered_as_an_item_based_query() {
    const ConsoleCatalogEntry* entry = consoleCatalogFindByName("system.api.get-profiler");
    TEST_ASSERT_NOT_NULL(entry);
    TEST_ASSERT_EQUAL_STRING(CONSOLE_CATALOG_TYPE_STATUS, entry->type);
    TEST_ASSERT_TRUE(entry->is_query);
    TEST_ASSERT_NULL_MESSAGE(entry->fields,
                             "item-based query: no fields: list, like system.status.logs");
}

// `help <op>` describes an operation that is not in this build - it does not
// refuse it. available_in_build is one of the catalog fields help already
// renders (consoleEmitHelpForOperation, #219 D3); this asserts it stays that
// way now that execution refuses the same row.
void test_help_describes_an_operation_that_is_not_in_this_build() {
    runQuery("help system.api.get-profiler");

    TEST_ASSERT_TRUE(g_cap.beginCalled);
    TEST_ASSERT_TRUE(g_cap.endCalled);
    TEST_ASSERT_EQUAL(CONSOLE_OUTCOME_COMPLETED, g_cap.outcome);
    TEST_ASSERT_EQUAL_STRING("false", capturedValue("available_in_build"));
    TEST_ASSERT_EQUAL_STRING("true", capturedValue("available_on_board"));
}

// =============================================================================
// Availability reason matrix (#224 acceptance criterion 3)
//
// Each of the five availability reasons (docs/console-protocol.md s.3.3),
// produced by ONE REAL OPERATION driven through consoleExecuteCommand() - the
// entry point both adapters call - rather than by asserting on
// consoleReasonString() or on a hand-built record. Named as a matrix here even
// where a behaviour test above already covers the same path, because "every
// category is produced by something real" is itself the criterion, and reading
// it off five scattered tests is what lets one of them quietly stop covering
// its category.
//
// not-on-this-board is the one exception, and it is a report rather than a
// test: no catalog row is off-board in any image that exists, so no real
// operation can produce it. See
// test_every_off_board_row_answers_not_on_this_board above for the sweep and
// the reason.
// =============================================================================

// 1/5 not-in-this-build: the profiler snapshot on an image built without
// PA_HEAP_PROFILE, which [env:native] is.
void test_reason_matrix_not_in_this_build() {
    runQuery("system.api.get-profiler");

    TEST_ASSERT_EQUAL(CONSOLE_OUTCOME_UNAVAILABLE, g_cap.outcome);
    TEST_ASSERT_EQUAL(CONSOLE_REASON_NOT_IN_THIS_BUILD, g_cap.reason);
}

// 2/5 component-disabled: the Dome ESC Component Toggle off (ADR 0027). The
// dome hardware is not addressed at all here - the executor reads the config
// cache the toggle writes.
void test_reason_matrix_component_disabled_from_a_component_toggle_off() {
    ConfigSnapshot snap = {};
    configCacheRead(&snap);
    snap.system.enable_dome_esc = false;
    configCacheApply(snap);

    runQuery("dome.action.move speed=0.5");

    TEST_ASSERT_EQUAL(CONSOLE_OUTCOME_UNAVAILABLE, g_cap.outcome);
    TEST_ASSERT_EQUAL(CONSOLE_REASON_COMPONENT_DISABLED, g_cap.reason);
}

// 3/5 blocked-by-state: estop latched. The guard core is the same one
// POST /api/drive runs (evaluateActionTestGuard()/driveArbiterSubmit()).
void test_reason_matrix_blocked_by_state_from_estop() {
    robotState.webControlEnabled = true;
    robotState.estop = true;

    runQuery("drive.action.move speed=100 steer=0");

    TEST_ASSERT_EQUAL(CONSOLE_OUTCOME_BLOCKED, g_cap.outcome);
    TEST_ASSERT_EQUAL(CONSOLE_REASON_BLOCKED_BY_STATE, g_cap.reason);
}

// 3/5 again, the other state rule the criterion names: sleep.
void test_reason_matrix_blocked_by_state_from_sleep() {
    ConfigSnapshot snap = {};
    configCacheRead(&snap);
    snap.system.enable_dome_esc = true;
    configCacheApply(snap);
    robotState.sleepMode = true;

    runQuery("dome.action.move speed=0.5");

    TEST_ASSERT_EQUAL(CONSOLE_OUTCOME_BLOCKED, g_cap.outcome);
    TEST_ASSERT_EQUAL(CONSOLE_REASON_BLOCKED_BY_STATE, g_cap.reason);
}

// 4/5 temporarily-unavailable: the shared config write lock already held by
// another writer mid-write - "busy right now; try again", and the reason the
// Console must not simply queue behind it.
void test_reason_matrix_temporarily_unavailable_from_a_busy_config_write() {
    consoleModuleInit();  // idempotent
    paStubMutexReset();
    struct PaStubMutex* m = paStubMutexStorage();
    m->held = 1;  // simulate another config writer mid-write

    runQuery("system.config.enable_arm1 value=true");

    TEST_ASSERT_EQUAL(CONSOLE_OUTCOME_UNAVAILABLE, g_cap.outcome);
    TEST_ASSERT_EQUAL(CONSOLE_REASON_TEMPORARILY_UNAVAILABLE, g_cap.reason);

    paStubMutexReset();  // release the simulated hold for later tests
}

// 4/5 again, the queue half: the dispatch core refusing an action right now.
void test_reason_matrix_temporarily_unavailable_from_a_busy_dispatch_core() {
    robotState.webControlEnabled = true;
    g_test_dispatch_outcome = RcDispatchOutcome::kBlockedByState;

    runQuery("sound.action.random-humming");

    TEST_ASSERT_EQUAL(CONSOLE_OUTCOME_UNAVAILABLE, g_cap.outcome);
    TEST_ASSERT_EQUAL(CONSOLE_REASON_TEMPORARILY_UNAVAILABLE, g_cap.reason);
}

// =============================================================================
// Availability is re-evaluated at execution, not cached from discovery
// (#224 acceptance criterion 4, docs/console-protocol.md s.3.3)
//
// The build and board reasons are compile-time and cannot change while the
// image runs; the three state-driven ones can, and that is where "cached from
// discovery" would be a real defect - an operator who lists the catalog, then
// disarms something, then runs a command, must get the state at the moment
// they ran it.
// =============================================================================

// `operations` lists the row as available, then the Component Toggle goes off,
// then the SAME operation refuses. Discovery is not consulted at execution.
void test_a_component_toggle_flipped_after_discovery_changes_the_execution_answer() {
    ConfigSnapshot snap = {};
    configCacheRead(&snap);
    snap.system.enable_dome_esc = true;
    configCacheApply(snap);

    runOperationsListing();
    const char* listed = listedOperationItem("dome.action.move");
    TEST_ASSERT_NOT_NULL(listed);
    TEST_ASSERT_NULL_MESSAGE(strstr(listed, "component-disabled"),
                             "discovery reports the catalog, not live component state");

    runQuery("dome.action.move speed=0.5");
    TEST_ASSERT_EQUAL(CONSOLE_OUTCOME_QUEUED, g_cap.outcome);

    // The operator turns the Dome ESC off after listing the catalog.
    configCacheRead(&snap);
    snap.system.enable_dome_esc = false;
    configCacheApply(snap);

    runQuery("dome.action.move speed=0.5");
    TEST_ASSERT_EQUAL(CONSOLE_OUTCOME_UNAVAILABLE, g_cap.outcome);
    TEST_ASSERT_EQUAL_MESSAGE(CONSOLE_REASON_COMPONENT_DISABLED, g_cap.reason,
                              "execution must read the toggle now, not when the catalog was listed");
}

// The same guarantee for a safety state: the identical command answers
// differently either side of an estop, with no discovery in between to
// invalidate.
void test_estop_latched_after_a_successful_run_changes_the_execution_answer() {
    robotState.webControlEnabled = true;

    // A drive frame reaches the arbiter directly rather than a queue, so the
    // success outcome is APPLIED (include/console_direct_action_drive.h).
    runQuery("drive.action.move speed=100 steer=0");
    TEST_ASSERT_EQUAL(CONSOLE_OUTCOME_APPLIED, g_cap.outcome);

    robotState.estop = true;

    runQuery("drive.action.move speed=100 steer=0");
    TEST_ASSERT_EQUAL(CONSOLE_OUTCOME_BLOCKED, g_cap.outcome);
    TEST_ASSERT_EQUAL(CONSOLE_REASON_BLOCKED_BY_STATE, g_cap.reason);
}

// =============================================================================
// sound.config.* - the nine rows whose write path is an audio Apply Core (#226)
//
// Driven through consoleExecuteCommand() - the entry point both adapters call
// - against the REAL cores and Commit Steps: src/web/api_audio.cpp and all
// three api_audio_*_apply.cpp are in [env:native]'s build_src_filter, so
// audioTracksApply()/audioTracksCommitApplied() here are the same functions
// handleAudioTracksPost() runs, not stand-ins. Each write is read back out of
// the config cache the Commit Step writes, so "went through the core" is a
// checked claim and not a structural one.
// =============================================================================

static void seedAudioTrack(const char* key, uint16_t value) {
    ConfigSnapshot snap = {};
    configCacheRead(&snap);
    TEST_ASSERT_TRUE_MESSAGE(configAudioSetTrackByKey(&snap.audio, key, value),
                             "test seed used a key AUDIO_TRACK_KEYS does not declare");
    configCacheApply(snap);
}

static uint16_t audioTrackValue(const char* key) {
    ConfigSnapshot snap = {};
    configCacheRead(&snap);
    uint16_t value = 0;
    TEST_ASSERT_TRUE_MESSAGE(configAudioGetTrackByKey(snap.audio, key, &value),
                             "read-back used a key AUDIO_TRACK_KEYS does not declare");
    return value;
}

void test_sound_config_random_min_reads_the_stored_bound() {
    seedAudioTrack("rand_min", 12);

    runQuery("sound.config.random-min");

    TEST_ASSERT_EQUAL(CONSOLE_STATUS_OK, g_cap.status);
    TEST_ASSERT_EQUAL(CONSOLE_OUTCOME_COMPLETED, g_cap.outcome);
    TEST_ASSERT_EQUAL_INT(1, g_cap.fieldCount);
    // The field name is GET /api/audio/tracks' own JSON key, which is also
    // the key the write side addresses (docs/console-protocol.md s.3.5).
    TEST_ASSERT_EQUAL_STRING("12", capturedValue("rand_min"));
}

void test_sound_config_random_min_write_reaches_the_tracks_core() {
    seedAudioTrack("rand_min", 12);

    runQuery("sound.config.random-min track=44");

    TEST_ASSERT_EQUAL(CONSOLE_STATUS_OK, g_cap.status);
    TEST_ASSERT_EQUAL(CONSOLE_OUTCOME_APPLIED, g_cap.outcome);
    TEST_ASSERT_EQUAL_UINT16(44, audioTrackValue("rand_min"));
}

// The core is the only gate on the value, not a copy of its rules in this
// module: these two rows take the identical argument and get opposite
// verdicts, because audioTracksApply()'s zero-allowed key list contains
// sys_boot and not startup (src/web/api_audio_tracks_apply.cpp). No
// adapter-side check could tell them apart without duplicating that list.
void test_sound_config_startup_track_rejects_zero_the_way_rest_does() {
    seedAudioTrack("startup", 5);

    runQuery("sound.config.startup-track track=0");

    TEST_ASSERT_EQUAL(CONSOLE_STATUS_ERR, g_cap.status);
    TEST_ASSERT_EQUAL(CONSOLE_OUTCOME_INVALID, g_cap.outcome);
    TEST_ASSERT_EQUAL(CONSOLE_REASON_OUT_OF_RANGE, g_cap.reason);
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(5, audioTrackValue("startup"),
                                     "a refused write must not have reached the config cache");
}

void test_sound_config_boot_complete_track_accepts_zero_the_way_rest_does() {
    seedAudioTrack("sys_boot", 5);

    runQuery("sound.config.boot-complete-track track=0");

    TEST_ASSERT_EQUAL(CONSOLE_STATUS_OK, g_cap.status);
    TEST_ASSERT_EQUAL(CONSOLE_OUTCOME_APPLIED, g_cap.outcome);
    TEST_ASSERT_EQUAL_UINT16(0, audioTrackValue("sys_boot"));
}

void test_sound_config_network_down_track_write_reaches_the_tracks_core() {
    seedAudioTrack("sys_net_down", 1);

    runQuery("sound.config.network-down-track track=77");

    TEST_ASSERT_EQUAL(CONSOLE_OUTCOME_APPLIED, g_cap.outcome);
    TEST_ASSERT_EQUAL_UINT16(77, audioTrackValue("sys_net_down"));
}

// A row whose operation name fixes the key it writes cannot be pointed at
// another field: `key` is not in its registry schema at all.
void test_sound_config_fixed_key_row_refuses_a_key_argument() {
    runQuery("sound.config.random-min key=scream track=3");

    TEST_ASSERT_EQUAL(CONSOLE_OUTCOME_INVALID, g_cap.outcome);
    TEST_ASSERT_EQUAL(CONSOLE_REASON_UNKNOWN_ARGUMENT, g_cap.reason);
    TEST_ASSERT_EQUAL_STRING("key", capturedValue("argument"));
}

// No widening: bank/page are POST /api/audio/tracks' optional CHIRP-binding
// extension and are in no Console row's schema, so they are refused before a
// core sees them.
void test_sound_config_track_rows_refuse_the_chirp_binding_extension() {
    runQuery("sound.config.random-min track=3 bank=1");

    TEST_ASSERT_EQUAL(CONSOLE_OUTCOME_INVALID, g_cap.outcome);
    TEST_ASSERT_EQUAL(CONSOLE_REASON_UNKNOWN_ARGUMENT, g_cap.reason);
    TEST_ASSERT_EQUAL_STRING("bank", capturedValue("argument"));
}

void test_sound_config_track_assignments_reads_every_named_track() {
    seedAudioTrack("scream", 21);
    seedAudioTrack("pbjtime", 34);

    runQuery("sound.config.track-assignments");

    TEST_ASSERT_EQUAL(CONSOLE_OUTCOME_COMPLETED, g_cap.outcome);
    TEST_ASSERT_EQUAL_INT_MESSAGE(20, g_cap.fieldCount,
                                  "the row's read must list its whole key set");
    TEST_ASSERT_EQUAL_STRING("21", capturedValue("scream"));
    TEST_ASSERT_EQUAL_STRING("34", capturedValue("pbjtime"));
}

void test_sound_config_track_assignments_write_reaches_the_tracks_core() {
    seedAudioTrack("leia", 3);

    runQuery("sound.config.track-assignments key=leia track=88");

    TEST_ASSERT_EQUAL(CONSOLE_OUTCOME_APPLIED, g_cap.outcome);
    TEST_ASSERT_EQUAL_UINT16(88, audioTrackValue("leia"));
}

// The two aggregate rows are scoped by the key enum their registry schema
// declares, so the named-track row cannot write a system track and vice
// versa - the Console offers a subset of what the one REST route accepts,
// never a superset.
void test_sound_config_track_assignments_refuses_a_system_key() {
    seedAudioTrack("sys_boot", 4);

    runQuery("sound.config.track-assignments key=sys_boot track=9");

    TEST_ASSERT_EQUAL(CONSOLE_OUTCOME_INVALID, g_cap.outcome);
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(4, audioTrackValue("sys_boot"),
                                     "an out-of-scope key must not reach the core");
}

void test_sound_config_system_track_assignments_reads_and_writes() {
    seedAudioTrack("sys_drv_on", 6);

    runQuery("sound.config.system-track-assignments");
    TEST_ASSERT_EQUAL(CONSOLE_OUTCOME_COMPLETED, g_cap.outcome);
    TEST_ASSERT_EQUAL_INT(7, g_cap.fieldCount);
    TEST_ASSERT_EQUAL_STRING("6", capturedValue("sys_drv_on"));

    runQuery("sound.config.system-track-assignments key=sys_drv_on track=19");
    TEST_ASSERT_EQUAL(CONSOLE_OUTCOME_APPLIED, g_cap.outcome);
    TEST_ASSERT_EQUAL_UINT16(19, audioTrackValue("sys_drv_on"));
}

void test_sound_config_category_ranges_reads_all_twelve_pairs() {
    seedAudioTrack("snd_cat_gen_lo", 2);
    seedAudioTrack("snd_cat_whis_hi", 300);

    runQuery("sound.config.category-ranges");

    TEST_ASSERT_EQUAL(CONSOLE_OUTCOME_COMPLETED, g_cap.outcome);
    // 24 fields plus begin+end is 26 records - inside the browser adapter's
    // CONSOLE_RESPONSE_RECORDS_MAX of 32 (src/web/api_console.cpp).
    TEST_ASSERT_EQUAL_INT(24, g_cap.fieldCount);
    TEST_ASSERT_EQUAL_STRING("2", capturedValue("snd_cat_gen_lo"));
    TEST_ASSERT_EQUAL_STRING("300", capturedValue("snd_cat_whis_hi"));
}

void test_sound_config_category_ranges_write_reaches_the_category_core() {
    runQuery("sound.config.category-ranges lo_key=snd_cat_gen_lo hi_key=snd_cat_gen_hi lo=5 hi=60");

    TEST_ASSERT_EQUAL(CONSOLE_OUTCOME_APPLIED, g_cap.outcome);
    TEST_ASSERT_EQUAL_UINT16(5, audioTrackValue("snd_cat_gen_lo"));
    TEST_ASSERT_EQUAL_UINT16(60, audioTrackValue("snd_cat_gen_hi"));
}

// lo>hi is a grouped rule with no single attributable argument, and it lives
// in the core - this module never re-tests it.
void test_sound_config_category_ranges_refuses_an_inverted_pair() {
    seedAudioTrack("snd_cat_hap_lo", 10);
    seedAudioTrack("snd_cat_hap_hi", 20);

    runQuery("sound.config.category-ranges lo_key=snd_cat_hap_lo hi_key=snd_cat_hap_hi lo=90 hi=8");

    TEST_ASSERT_EQUAL(CONSOLE_STATUS_ERR, g_cap.status);
    TEST_ASSERT_EQUAL(CONSOLE_OUTCOME_INVALID, g_cap.outcome);
    TEST_ASSERT_EQUAL(CONSOLE_REASON_OUT_OF_RANGE, g_cap.reason);
    TEST_ASSERT_EQUAL_UINT16(10, audioTrackValue("snd_cat_hap_lo"));
    TEST_ASSERT_EQUAL_UINT16(20, audioTrackValue("snd_cat_hap_hi"));
}

void test_sound_config_mood_category_map_reads_the_four_masks() {
    ConfigSnapshot snap = {};
    configCacheRead(&snap);
    snap.audio.snd_moodcat_quiet = 0x001;
    snap.audio.snd_moodcat_mid = 0x012;
    snap.audio.snd_moodcat_full = 0x123;
    snap.audio.snd_moodcat_awakeplus = 0xFFF;
    configCacheApply(snap);

    runQuery("sound.config.mood-category-map");

    TEST_ASSERT_EQUAL(CONSOLE_OUTCOME_COMPLETED, g_cap.outcome);
    TEST_ASSERT_EQUAL_INT(4, g_cap.fieldCount);
    TEST_ASSERT_EQUAL_STRING("1", capturedValue("quiet"));
    TEST_ASSERT_EQUAL_STRING("18", capturedValue("mid"));
    TEST_ASSERT_EQUAL_STRING("291", capturedValue("full"));
    TEST_ASSERT_EQUAL_STRING("4095", capturedValue("awakeplus"));
}

void test_sound_config_mood_category_map_write_reaches_the_mood_map_core() {
    runQuery("sound.config.mood-category-map quiet=1 mid=2 full=3 awakeplus=4");

    TEST_ASSERT_EQUAL(CONSOLE_OUTCOME_APPLIED, g_cap.outcome);

    ConfigSnapshot snap = {};
    configCacheRead(&snap);
    TEST_ASSERT_EQUAL_UINT16(1, snap.audio.snd_moodcat_quiet);
    TEST_ASSERT_EQUAL_UINT16(2, snap.audio.snd_moodcat_mid);
    TEST_ASSERT_EQUAL_UINT16(3, snap.audio.snd_moodcat_full);
    TEST_ASSERT_EQUAL_UINT16(4, snap.audio.snd_moodcat_awakeplus);
}

// All four masks are required as a set - a partial write is not a state the
// mood map has, the same grouped rule wifi.config.settings enforces.
void test_sound_config_mood_category_map_requires_all_four_masks() {
    runQuery("sound.config.mood-category-map quiet=1 mid=2");

    TEST_ASSERT_EQUAL(CONSOLE_OUTCOME_INVALID, g_cap.outcome);
    TEST_ASSERT_EQUAL(CONSOLE_REASON_MISSING_ARGUMENT, g_cap.reason);
}

// Every one of these rows answers a bare read, which is what takes them out
// of the executor-not-ready count the ticket closes on.
void test_sound_config_audio_core_rows_all_answer_a_read() {
    static const char* const kRows[] = {
        "sound.config.random-min",        "sound.config.random-max",
        "sound.config.startup-track",     "sound.config.boot-complete-track",
        "sound.config.network-down-track", "sound.config.track-assignments",
        "sound.config.system-track-assignments", "sound.config.category-ranges",
        "sound.config.mood-category-map",
    };
    for (size_t i = 0; i < sizeof(kRows) / sizeof(kRows[0]); ++i) {
        runQuery(kRows[i]);
        TEST_ASSERT_EQUAL_MESSAGE(CONSOLE_OUTCOME_COMPLETED, g_cap.outcome, kRows[i]);
        TEST_ASSERT_TRUE_MESSAGE(g_cap.fieldCount > 0, kRows[i]);
    }
}


// -----------------------------------------------------------------------------
// sound.config.volume - the config-typed view of sound.action.set-volume's
// value, over the one Commit Step both halves and POST /api/audio share.
// -----------------------------------------------------------------------------

void test_sound_config_volume_reads_the_stored_default() {
    ConfigSnapshot snap = {};
    configCacheRead(&snap);
    snap.audio.audioVolume = 17;
    configCacheApply(snap);

    runQuery("sound.config.volume");

    TEST_ASSERT_EQUAL(CONSOLE_STATUS_OK, g_cap.status);
    TEST_ASSERT_EQUAL(CONSOLE_OUTCOME_COMPLETED, g_cap.outcome);
    TEST_ASSERT_EQUAL_INT(1, g_cap.fieldCount);
    // POST /api/audio's own parameter spelling and GET /api/audio/tracks'
    // JSON key for this value (docs/console-protocol.md s.3.5).
    TEST_ASSERT_EQUAL_STRING("17", capturedValue("volume"));
}

void test_sound_config_volume_write_reaches_the_volume_commit_step() {
    g_test_audio_queue_ok = true;
    g_test_audio_volume_calls = 0;
    g_test_audio_last_volume = 0;

    runQuery("sound.config.volume volume=23");

    TEST_ASSERT_EQUAL(CONSOLE_STATUS_OK, g_cap.status);
    TEST_ASSERT_EQUAL(CONSOLE_OUTCOME_APPLIED, g_cap.outcome);
    // The live apply half of the Commit Step: the same audio queue call
    // handleAudioPost()'s action=volume branch makes.
    TEST_ASSERT_EQUAL_UINT(1, g_test_audio_volume_calls);
    TEST_ASSERT_EQUAL_UINT8(23, g_test_audio_last_volume);
    // ...and the persist half, read back out of the config cache the Commit
    // Step writes.
    ConfigSnapshot snap = {};
    configCacheRead(&snap);
    TEST_ASSERT_EQUAL_UINT8(23, snap.audio.audioVolume);
}

// The bound is the registry's own (uint8, 0-30) read through the shared schema
// validator - not a second copy of it in this module.
void test_sound_config_volume_rejects_a_level_above_the_registry_range() {
    g_test_audio_queue_ok = true;
    g_test_audio_volume_calls = 0;

    runQuery("sound.config.volume volume=31");

    TEST_ASSERT_EQUAL(CONSOLE_STATUS_ERR, g_cap.status);
    TEST_ASSERT_EQUAL(CONSOLE_OUTCOME_INVALID, g_cap.outcome);
    TEST_ASSERT_EQUAL(CONSOLE_REASON_OUT_OF_RANGE, g_cap.reason);
    TEST_ASSERT_EQUAL_STRING("volume", capturedValue("argument"));
    TEST_ASSERT_EQUAL_UINT_MESSAGE(0, g_test_audio_volume_calls,
                                   "a refused level must not reach the audio queue");
}

void test_sound_config_volume_reports_queue_full_when_the_audio_queue_refuses() {
    g_test_audio_queue_ok = false;

    runQuery("sound.config.volume volume=12");

    TEST_ASSERT_EQUAL(CONSOLE_STATUS_ERR, g_cap.status);
    TEST_ASSERT_EQUAL(CONSOLE_OUTCOME_QUEUE_FULL, g_cap.outcome);
    TEST_ASSERT_EQUAL(CONSOLE_REASON_QUEUE_FULL, g_cap.reason);

    g_test_audio_queue_ok = true;
}


// =============================================================================
// The four sound.config.mood-interval-* rows - read, but no write (#226)
//
// The load-bearing claim is not "these four names are refused" but "the
// refusal is driven from the registry", so the tests below check the RULE
// against the catalog flag rather than against the names: every read_only
// entry is refused, every other config row is not, and the flag is what
// separates them. A hardcoded name list in the cascade would pass a
// name-based test and fail these.
// =============================================================================

void test_mood_interval_rows_read_their_stored_value() {
    static const char* const kRows[][2] = {
        {"sound.config.mood-interval-quiet", "snd_int_quiet"},
        {"sound.config.mood-interval-mid", "snd_int_mid"},
        {"sound.config.mood-interval-full", "snd_int_full"},
        {"sound.config.mood-interval-awake-plus", "snd_int_awake"},
    };
    for (size_t i = 0; i < sizeof(kRows) / sizeof(kRows[0]); ++i) {
        seedAudioTrack(kRows[i][1], (uint16_t)(30 + i));
        runQuery(kRows[i][0]);
        TEST_ASSERT_EQUAL_MESSAGE(CONSOLE_STATUS_OK, g_cap.status, kRows[i][0]);
        TEST_ASSERT_EQUAL_MESSAGE(CONSOLE_OUTCOME_COMPLETED, g_cap.outcome, kRows[i][0]);
        TEST_ASSERT_EQUAL_INT_MESSAGE(1, g_cap.fieldCount, kRows[i][0]);
        char expected[8] = {};
        snprintf(expected, sizeof(expected), "%u", (unsigned)(30 + i));
        TEST_ASSERT_EQUAL_STRING_MESSAGE(expected, capturedValue(kRows[i][1]), kRows[i][0]);
    }
}

void test_mood_interval_write_answers_invalid_read_only() {
    seedAudioTrack("snd_int_quiet", 30);

    runQuery("sound.config.mood-interval-quiet value=120");

    TEST_ASSERT_EQUAL(CONSOLE_STATUS_ERR, g_cap.status);
    TEST_ASSERT_EQUAL(CONSOLE_OUTCOME_INVALID, g_cap.outcome);
    TEST_ASSERT_EQUAL(CONSOLE_REASON_READ_ONLY, g_cap.reason);
    TEST_ASSERT_EQUAL_STRING("read-only", consoleReasonString(g_cap.reason));
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(30, audioTrackValue("snd_int_quiet"),
                                     "a read-only refusal must not have changed the value");
}

// The refusal does not depend on the argument spelling: read_only is a
// property of the operation, so ANY argument is refused the same way, before
// a schema or an executor is consulted.
void test_mood_interval_write_is_refused_whatever_the_argument_is_called() {
    static const char* const kLines[] = {
        "sound.config.mood-interval-mid value=1",
        "sound.config.mood-interval-mid track=1",
        "sound.config.mood-interval-mid nonsense=1",
        "sound.config.mood-interval-mid not-even-a-pair",
    };
    for (size_t i = 0; i < sizeof(kLines) / sizeof(kLines[0]); ++i) {
        runQuery(kLines[i]);
        TEST_ASSERT_EQUAL_MESSAGE(CONSOLE_REASON_READ_ONLY, g_cap.reason, kLines[i]);
    }
}

// The rule, stated over the catalog rather than over four names: a write is
// refused with read-only exactly when the catalog says the row is read-only.
// This is the test a hardcoded name list in the cascade could not pass once
// a fifth row was marked - and the one that says a fifth row works for free.
void test_read_only_refusal_follows_the_catalog_flag_not_a_name_list() {
    size_t count = 0;
    const ConsoleCatalogEntry* entries = consoleCatalogGetEntries(&count);
    int readOnlyRows = 0;

    for (size_t i = 0; i < count; ++i) {
        if (strcmp(entries[i].type, CONSOLE_CATALOG_TYPE_CONFIG) != 0) continue;

        char line[160] = {};
        snprintf(line, sizeof(line), "%s value=1", entries[i].name);
        runQuery(line);

        if (entries[i].read_only) {
            readOnlyRows++;
            TEST_ASSERT_EQUAL_MESSAGE(CONSOLE_REASON_READ_ONLY, g_cap.reason, entries[i].name);
        } else {
            TEST_ASSERT_NOT_EQUAL_MESSAGE(CONSOLE_REASON_READ_ONLY, g_cap.reason,
                                          entries[i].name);
        }
    }

    // Every row the registry marks is a row the catalog carries the flag for.
    TEST_ASSERT_EQUAL_INT_MESSAGE(4, readOnlyRows,
                                  "the catalog lost the registry's read_only rows");
}

// A read is never refused by the flag - read_only withholds the write half
// only, which is what keeps these rows out of the executor-not-ready count.
void test_read_only_rows_still_answer_a_bare_read() {
    size_t count = 0;
    const ConsoleCatalogEntry* entries = consoleCatalogGetEntries(&count);
    for (size_t i = 0; i < count; ++i) {
        if (!entries[i].read_only) continue;
        runQuery(entries[i].name);
        TEST_ASSERT_EQUAL_MESSAGE(CONSOLE_STATUS_OK, g_cap.status, entries[i].name);
        TEST_ASSERT_EQUAL_MESSAGE(CONSOLE_OUTCOME_COMPLETED, g_cap.outcome, entries[i].name);
        TEST_ASSERT_TRUE_MESSAGE(g_cap.fieldCount > 0, entries[i].name);
    }
}

// help reports the fact, so an operator learns it before typing a value that
// only a refusal can follow.
void test_help_reports_read_only_for_a_row_that_has_no_write() {
    runQuery("help sound.config.mood-interval-full");

    TEST_ASSERT_EQUAL(CONSOLE_STATUS_OK, g_cap.status);
    TEST_ASSERT_EQUAL_STRING("true", capturedValue("read_only"));

    runQuery("help sound.config.volume");
    TEST_ASSERT_EQUAL_STRING("false", capturedValue("read_only"));
}

// The closing criterion, asserted rather than only reported: no type=config
// row answers executor-not-ready any more.
void test_no_config_row_answers_executor_not_ready() {
    size_t count = 0;
    const ConsoleCatalogEntry* entries = consoleCatalogGetEntries(&count);
    for (size_t i = 0; i < count; ++i) {
        if (strcmp(entries[i].type, CONSOLE_CATALOG_TYPE_CONFIG) != 0) continue;
        runQuery(entries[i].name);
        TEST_ASSERT_NOT_EQUAL_MESSAGE(CONSOLE_REASON_EXECUTOR_NOT_READY, g_cap.reason,
                                      entries[i].name);
    }
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

    RUN_TEST(test_dome_api_list_sequences_streams_the_real_index_as_items);
    RUN_TEST(test_dome_api_list_sequences_empty_index_answers_completed_with_no_items);
    RUN_TEST(test_dome_api_list_sequences_reports_retrained_when_shadowing_a_factory_name);
    RUN_TEST(test_dome_api_list_builtin_sequences_streams_the_real_catalog_as_items);

    RUN_TEST(test_dome_api_get_sequence_last_run_before_any_run_answers_valid_false_only);
    RUN_TEST(test_dome_api_get_sequence_last_run_three_way_field_match);
    RUN_TEST(test_dome_api_get_sequence_last_run_carries_real_state);

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
    RUN_TEST(test_the_executor_not_ready_set_is_exactly_the_recorded_rows);
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
    RUN_TEST(test_dome_seq_named_row_queues_through_the_dispatcher);
    RUN_TEST(test_dome_seq_named_row_rejects_any_argument);
    RUN_TEST(test_catalog_sequence_lookup_answers_only_for_literal_dm_rows);
    RUN_TEST(test_every_named_sequence_row_dispatches);
    RUN_TEST(test_sound_refresh_catalog_queues_through_the_audio_queue);
    RUN_TEST(test_sound_refresh_catalog_rejects_any_argument);
    RUN_TEST(test_sound_refresh_catalog_without_a_catalog_backend_is_not_in_this_build);
    RUN_TEST(test_sound_play_banked_passes_bank_page_index_to_the_queue);
    RUN_TEST(test_sound_play_banked_missing_key_names_it);
    RUN_TEST(test_sound_play_banked_unknown_key_names_it);
    RUN_TEST(test_sound_play_banked_bank_out_of_registry_range_is_refused);
    RUN_TEST(test_sound_play_banked_page_outside_the_published_enum_is_refused);
    RUN_TEST(test_sound_play_banked_is_blocked_while_sleeping);
    RUN_TEST(test_sound_get_catalog_answers_ready_and_one_item_per_entry);
    RUN_TEST(test_sound_get_catalog_reports_an_unenumerated_catalog_as_not_ready);
    RUN_TEST(test_sound_get_catalog_without_a_catalog_backend_is_not_in_this_build);
    RUN_TEST(test_sound_get_catalog_rejects_the_dropped_bank_filter);
    RUN_TEST(test_sound_get_catalog_stops_when_the_catalog_is_refreshed_mid_listing);
    RUN_TEST(test_sound_get_mood_map_three_way_field_match);
    RUN_TEST(test_sound_get_mood_map_matches_the_config_row_for_the_same_state);
    RUN_TEST(test_estop_clear_releases_the_latched_estop);
    RUN_TEST(test_estop_clear_rejects_any_argument_and_leaves_the_latch_alone);
    RUN_TEST(test_system_api_get_identity_three_way_field_match);
    RUN_TEST(test_system_api_get_identity_carries_real_config_state);
    RUN_TEST(test_system_api_get_validation_three_way_field_match);
    RUN_TEST(test_system_api_get_validation_drive_summary_carries_real_state);
    RUN_TEST(test_rc_api_get_bindable_actions_streams_the_registry_as_items);
    RUN_TEST(test_rc_api_get_bindable_actions_reports_testability_per_row);
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
    RUN_TEST(test_action_get_sequence_stays_executor_not_ready_document_transfer_out_of_scope);
    RUN_TEST(test_action_get_layout_stays_executor_not_ready_document_transfer_out_of_scope);

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
    RUN_TEST(test_log_level_read_and_write_the_integer);
    RUN_TEST(test_log_level_accepts_every_word_form);
    RUN_TEST(test_log_level_word_form_is_case_insensitive);
    RUN_TEST(test_log_level_accepts_the_named_key);
    RUN_TEST(test_log_level_rejects_an_out_of_range_integer);
    RUN_TEST(test_log_level_rejects_an_unknown_word);
    RUN_TEST(test_log_level_rejects_an_unknown_argument);
    RUN_TEST(test_log_level_rejects_an_extra_argument_even_with_a_valid_word);
    RUN_TEST(test_scalar_config_write_rejects_an_unknown_argument);
    RUN_TEST(test_config_write_reports_busy_when_the_mutex_is_already_held);
    RUN_TEST(test_config_write_releases_the_mutex_after_a_successful_write);
    RUN_TEST(test_config_write_rejected_before_apply_never_touches_the_mutex);
    RUN_TEST(test_sound_config_random_min_reads_the_stored_bound);
    RUN_TEST(test_sound_config_random_min_write_reaches_the_tracks_core);
    RUN_TEST(test_sound_config_startup_track_rejects_zero_the_way_rest_does);
    RUN_TEST(test_sound_config_boot_complete_track_accepts_zero_the_way_rest_does);
    RUN_TEST(test_sound_config_network_down_track_write_reaches_the_tracks_core);
    RUN_TEST(test_sound_config_fixed_key_row_refuses_a_key_argument);
    RUN_TEST(test_sound_config_track_rows_refuse_the_chirp_binding_extension);
    RUN_TEST(test_sound_config_track_assignments_reads_every_named_track);
    RUN_TEST(test_sound_config_track_assignments_write_reaches_the_tracks_core);
    RUN_TEST(test_sound_config_track_assignments_refuses_a_system_key);
    RUN_TEST(test_sound_config_system_track_assignments_reads_and_writes);
    RUN_TEST(test_sound_config_category_ranges_reads_all_twelve_pairs);
    RUN_TEST(test_sound_config_category_ranges_write_reaches_the_category_core);
    RUN_TEST(test_sound_config_category_ranges_refuses_an_inverted_pair);
    RUN_TEST(test_sound_config_mood_category_map_reads_the_four_masks);
    RUN_TEST(test_sound_config_mood_category_map_write_reaches_the_mood_map_core);
    RUN_TEST(test_sound_config_mood_category_map_requires_all_four_masks);
    RUN_TEST(test_sound_config_audio_core_rows_all_answer_a_read);
    RUN_TEST(test_sound_config_volume_reads_the_stored_default);
    RUN_TEST(test_sound_config_volume_write_reaches_the_volume_commit_step);
    RUN_TEST(test_sound_config_volume_rejects_a_level_above_the_registry_range);
    RUN_TEST(test_sound_config_volume_reports_queue_full_when_the_audio_queue_refuses);
    RUN_TEST(test_mood_interval_rows_read_their_stored_value);
    RUN_TEST(test_mood_interval_write_answers_invalid_read_only);
    RUN_TEST(test_mood_interval_write_is_refused_whatever_the_argument_is_called);
    RUN_TEST(test_read_only_refusal_follows_the_catalog_flag_not_a_name_list);
    RUN_TEST(test_read_only_rows_still_answer_a_bare_read);
    RUN_TEST(test_help_reports_read_only_for_a_row_that_has_no_write);
    RUN_TEST(test_no_config_row_answers_executor_not_ready);
    RUN_TEST(test_config_executor_not_ready_count_report);

    RUN_TEST(test_wifi_settings_read_reports_the_saved_posture_and_no_password);
    RUN_TEST(test_wifi_settings_write_stages_the_group_and_reports_staged_until_reboot);
    RUN_TEST(test_wifi_settings_write_reports_applied_when_nothing_is_left_to_restart_for);
    RUN_TEST(test_wifi_settings_client_mode_without_an_ssid_is_rejected_as_a_whole);
    RUN_TEST(test_wifi_settings_ap_mode_without_an_ssid_is_rejected_as_a_whole);
    RUN_TEST(test_wifi_settings_refuses_every_password_key_spelling);
    RUN_TEST(test_wifi_settings_accepts_a_utf8_ssid_inside_quotes);
    RUN_TEST(test_wifi_settings_rejects_malformed_utf8_in_an_unquoted_ssid);
    RUN_TEST(test_wifi_settings_enforces_the_32_byte_ssid_limit);
    RUN_TEST(test_wifi_settings_accepts_an_ssid_exactly_at_the_limit);
    RUN_TEST(test_wifi_settings_rejects_an_unknown_argument_and_a_bad_mode);
    RUN_TEST(test_wifi_settings_read_mode_round_trips_into_a_write);
    RUN_TEST(test_wifi_settings_write_reports_busy_when_the_mutex_is_already_held);
    RUN_TEST(test_help_marks_the_wifi_password_params_write_excluded);

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

    RUN_TEST(test_direct_reboot_broadcasts_answers_then_requests_restart);
    RUN_TEST(test_direct_reboot_rejects_an_argument);

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

    RUN_TEST(test_sound_named_track_shortcuts_send_the_right_dollar_command);
    RUN_TEST(test_sound_named_track_shortcut_blocked_while_sleeping);
    RUN_TEST(test_sound_named_track_shortcut_reports_a_full_queue);
    RUN_TEST(test_sound_named_track_shortcut_rejects_any_argument);
    RUN_TEST(test_sound_quiet_sends_dollar_s);
    RUN_TEST(test_sound_random_on_off_send_the_right_dollar_command);
    RUN_TEST(test_sound_volume_shortcuts_send_the_right_dollar_command);
    RUN_TEST(test_sound_volume_shortcut_blocked_while_sleeping);
    RUN_TEST(test_sound_dollar_command_sends_the_supplied_cmd);
    RUN_TEST(test_sound_dollar_command_rejects_missing_dollar_prefix);
    RUN_TEST(test_sound_dollar_command_rejects_a_too_long_command);
    RUN_TEST(test_sound_dollar_command_rejects_missing_cmd);
    RUN_TEST(test_sound_dollar_command_rejects_unknown_argument);
    RUN_TEST(test_sound_dollar_command_blocked_while_sleeping);
    RUN_TEST(test_sound_dollar_command_reports_a_full_queue);
    RUN_TEST(test_sound_track_stop_queues_even_while_sleeping);
    RUN_TEST(test_sound_track_stop_reports_a_full_queue);
    RUN_TEST(test_sound_query_status_queues);
    RUN_TEST(test_sound_query_status_reports_a_full_queue);
    RUN_TEST(test_sound_set_mood_map_applies_all_four_masks);
    RUN_TEST(test_sound_set_mood_map_rejects_a_partial_form);
    RUN_TEST(test_sound_set_mood_map_rejects_an_out_of_range_mask);
    RUN_TEST(test_sound_set_category_range_applies_and_persists);
    RUN_TEST(test_sound_set_category_range_rejects_a_mismatched_key_pair);
    RUN_TEST(test_sound_set_category_range_rejects_lo_greater_than_hi);
    RUN_TEST(test_sound_set_category_range_rejects_bank_as_an_unknown_argument);

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
    RUN_TEST(test_servo_stop_queues_with_the_resolved_arm_id);
    RUN_TEST(test_servo_stop_accepts_both_as_the_broadcast_target);
    RUN_TEST(test_servo_stop_rejects_a_missing_target);
    RUN_TEST(test_servo_stop_rejects_an_unknown_target);
    RUN_TEST(test_servo_stop_rejects_position_us_as_an_unknown_argument);
    RUN_TEST(test_257_every_direct_action_row_still_dispatches);

    RUN_TEST(test_profiler_snapshot_answers_not_in_this_build);
    RUN_TEST(test_profiler_trace_start_answers_not_in_this_build);
    RUN_TEST(test_profiler_trace_stop_answers_not_in_this_build);
    RUN_TEST(test_an_enabled_build_flag_is_not_refused_by_the_build_guard);
    RUN_TEST(test_every_out_of_build_row_answers_not_in_this_build);
    RUN_TEST(test_every_off_board_row_answers_not_on_this_board);
    RUN_TEST(test_operations_lists_out_of_build_rows_with_the_reason_execution_gives);
    RUN_TEST(test_operations_never_annotates_executor_not_ready);
    RUN_TEST(test_help_reports_no_readiness_for_a_row_execution_refuses);
    RUN_TEST(test_profiler_snapshot_is_registered_as_an_item_based_query);
    RUN_TEST(test_help_describes_an_operation_that_is_not_in_this_build);

    RUN_TEST(test_reason_matrix_not_in_this_build);
    RUN_TEST(test_reason_matrix_component_disabled_from_a_component_toggle_off);
    RUN_TEST(test_reason_matrix_blocked_by_state_from_estop);
    RUN_TEST(test_reason_matrix_blocked_by_state_from_sleep);
    RUN_TEST(test_reason_matrix_temporarily_unavailable_from_a_busy_config_write);
    RUN_TEST(test_reason_matrix_temporarily_unavailable_from_a_busy_dispatch_core);
    RUN_TEST(test_a_component_toggle_flipped_after_discovery_changes_the_execution_answer);
    RUN_TEST(test_estop_latched_after_a_successful_run_changes_the_execution_answer);

    return UNITY_END();
}
