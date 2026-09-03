// =============================================================================
// test/test_native/test_api_console/test_api_console.cpp
//
// Native unit tests for POST /api/console -- the browser adapter over the
// Console module (ADR 0034), driven through the WebRequest seam's host-test
// backend (ADR 0021).
//
// These cover the one class of defect an adapter over a callback sink can get
// wrong and the serial adapter cannot: the serial writer consumes each record
// value before the module overwrites its buffer, while the web adapter must
// hold every value until the response is serialized. The module emits all
// status fields from a single reused stack buffer, so an adapter that stores
// the callback's pointer instead of copying the bytes returns the same value
// for every field -- and reads a dead frame doing it.
// =============================================================================
#include <unity.h>

#include <ArduinoJson.h>
#include <cstring>
#include <string>
#include <vector>

#include "api_console.h"
#include "console_catalog.h"
#include "console_module.h"
#include "log_buffer.h"             // LogBuffer, logBufferInit()/logBufferAppend(),
                                    // LOG_RING_MAX_LINES - #239's overflow fixture
#include "log_buffer_test_hooks.h"  // g_test_log_buffer/g_test_log_storage - the same
                                    // log-ring stand-in test_api_logs.cpp and
                                    // test_console_module.cpp fill
#include "web_request_test_backend.h"

void setUp() {
    // Reset to empty before every test (#239) - matches test_api_logs.cpp's
    // and test_console_module.cpp's own setUp(), so a log-ring test never
    // sees another test's lines.
    logBufferInit(&g_test_log_buffer, g_test_log_storage, LOG_RING_MAX_LINES);
}

void tearDown() {
}

// -----------------------------------------------------------------------------
// Helpers
// -----------------------------------------------------------------------------

// Run one console line through the handler and hand back the captured backend.
static void runCommand(WebRequestTestBackend& backend, const char* line) {
    static WebRequestTestParam params[1];
    params[0] = {"command", line};
    backend.params = params;
    backend.paramCount = 1;

    WebRequest req(&backend);
    handleConsolePost(req);
}

// Value of the first type=field record with this name, or nullptr.
// Copies into caller storage so the returned text outlives the JsonDocument.
static bool fieldValue(const char* body, const char* name, char* out, size_t outSize) {
    JsonDocument doc;
    if (deserializeJson(doc, body)) {
        return false;
    }
    for (JsonObjectConst rec : doc["records"].as<JsonArrayConst>()) {
        const char* type = rec["type"];
        const char* recName = rec["name"];
        if (type && recName && strcmp(type, "field") == 0 && strcmp(recName, name) == 0) {
            const char* value = rec["value"];
            if (!value) return false;
            snprintf(out, outSize, "%s", value);
            return true;
        }
    }
    return false;
}

static size_t countRecordsOfType(const char* body, const char* wanted) {
    JsonDocument doc;
    if (deserializeJson(doc, body)) {
        return 0;
    }
    size_t seen = 0;
    for (JsonObjectConst rec : doc["records"].as<JsonArrayConst>()) {
        const char* type = rec["type"];
        if (type && strcmp(type, wanted) == 0) ++seen;
    }
    return seen;
}

// -----------------------------------------------------------------------------
// Field values must survive the module's buffer reuse
// -----------------------------------------------------------------------------

// The load-bearing one. heap_caps_get_largest_free_block() returns 262144 in
// the native stub, and littleFsReady is the LAST field the module emits. An
// adapter that stores the sink's pointer hands back the same bytes for both.
//
// #223: the field was "fsReady" (the C parameter name formatHealthJson() and
// captureHealthSnapshot() happen to use) until this rewrite renamed it to
// "littleFsReady" - the real API JSON key
// (src/web/api_status_serializers.cpp's formatHealthJson(), docs/api.md
// /api/health) - per docs/console-protocol.md s.3.5 ("a field record's name=
// is the API JSON key verbatim").
void test_status_field_values_are_not_aliased() {
    WebRequestTestBackend backend;
    runCommand(backend, "system.status.health");
    TEST_ASSERT_EQUAL_INT(200, backend.sentCode);

    char largest[64] = {};
    char littleFsReady[64] = {};
    TEST_ASSERT_TRUE(fieldValue(backend.sentBody, "heapLargestBlock", largest, sizeof(largest)));
    TEST_ASSERT_TRUE(
        fieldValue(backend.sentBody, "littleFsReady", littleFsReady, sizeof(littleFsReady)));

    // Under the aliasing defect both read the same storage, whatever it holds.
    TEST_ASSERT_TRUE_MESSAGE(strcmp(largest, littleFsReady) != 0,
                             "heapLargestBlock and littleFsReady returned identical bytes: "
                             "the sink stored the callback's pointer instead of copying");
}

// Same defect, stated positively: each field keeps its own value.
void test_status_fields_keep_their_own_values() {
    WebRequestTestBackend backend;
    runCommand(backend, "system.status.health");

    char value[64] = {};
    TEST_ASSERT_TRUE(fieldValue(backend.sentBody, "heapLargestBlock", value, sizeof(value)));
    TEST_ASSERT_EQUAL_STRING("262144", value);

    TEST_ASSERT_TRUE(fieldValue(backend.sentBody, "littleFsReady", value, sizeof(value)));
    TEST_ASSERT_EQUAL_STRING("false", value);

    TEST_ASSERT_TRUE(fieldValue(backend.sentBody, "wifiConnected", value, sizeof(value)));
    TEST_ASSERT_EQUAL_STRING("false", value);
}

// -----------------------------------------------------------------------------
// The whole command line reaches the module
// -----------------------------------------------------------------------------

// "help <operation>" only works if arguments survive the adapter. An adapter
// that forwards the first token turns this into an unknown-operation error.
void test_help_with_argument_returns_a_multi_record_answer() {
    WebRequestTestBackend backend;
    runCommand(backend, "help system.status.health");
    TEST_ASSERT_EQUAL_INT(200, backend.sentCode);

    TEST_ASSERT_EQUAL_UINT_MESSAGE(1, countRecordsOfType(backend.sentBody, "begin"),
                                   "help <operation> did not open a multi-record answer: "
                                   "the adapter dropped the argument");
    TEST_ASSERT_EQUAL_UINT(1, countRecordsOfType(backend.sentBody, "end"));
    TEST_ASSERT_EQUAL_UINT(0, countRecordsOfType(backend.sentBody, "result"));

    char value[128] = {};
    TEST_ASSERT_TRUE(fieldValue(backend.sentBody, "type", value, sizeof(value)));
    TEST_ASSERT_EQUAL_STRING("status", value);
}

// -----------------------------------------------------------------------------
// Guard paths keep the wire protocol
// -----------------------------------------------------------------------------

// docs/console-protocol.md: a guard path is ONE type=result record, never a
// synthetic begin+end pair.
void test_unknown_operation_is_a_single_result_record() {
    WebRequestTestBackend backend;
    runCommand(backend, "no.such.operation");
    TEST_ASSERT_EQUAL_INT(200, backend.sentCode);

    TEST_ASSERT_EQUAL_UINT(1, countRecordsOfType(backend.sentBody, "result"));
    TEST_ASSERT_EQUAL_UINT(0, countRecordsOfType(backend.sentBody, "begin"));
    TEST_ASSERT_EQUAL_UINT(0, countRecordsOfType(backend.sentBody, "end"));

    JsonDocument doc;
    TEST_ASSERT_FALSE(deserializeJson(doc, backend.sentBody));
    JsonObjectConst rec = doc["records"][0];
    TEST_ASSERT_EQUAL_STRING("err", rec["status"].as<const char*>());
    TEST_ASSERT_EQUAL_STRING("unknown-operation", rec["reason"].as<const char*>());
}

// A status query is begin + fields + exactly one end.
void test_status_query_is_begin_fields_end() {
    WebRequestTestBackend backend;
    runCommand(backend, "system.status.health");

    TEST_ASSERT_EQUAL_UINT(1, countRecordsOfType(backend.sentBody, "begin"));
    TEST_ASSERT_EQUAL_UINT(1, countRecordsOfType(backend.sentBody, "end"));
    TEST_ASSERT_EQUAL_UINT(0, countRecordsOfType(backend.sentBody, "result"));
    TEST_ASSERT_TRUE(countRecordsOfType(backend.sentBody, "field") > 1);
}

// docs/console-protocol.md: a query is answered synchronously, so its end record
// reports outcome=completed. "queued" would claim work entered a queue that a
// synchronous read never touches.
void test_completed_query_reports_outcome_completed() {
    WebRequestTestBackend backend;
    runCommand(backend, "system.status.health");

    JsonDocument doc;
    TEST_ASSERT_FALSE(deserializeJson(doc, backend.sentBody));
    for (JsonObjectConst rec : doc["records"].as<JsonArrayConst>()) {
        if (strcmp(rec["type"].as<const char*>(), "end") != 0) continue;
        TEST_ASSERT_EQUAL_STRING("ok", rec["status"].as<const char*>());
        TEST_ASSERT_EQUAL_STRING_MESSAGE("completed", rec["outcome"].as<const char*>(),
                                         "a synchronous query must not report queued");
        return;
    }
    TEST_FAIL_MESSAGE("no end record found");
}

// A successful record carries no reason: there is nothing to explain. The
// previous code passed CONSOLE_REASON_NOT_IN_THIS_BUILD as filler on success.
void test_successful_records_carry_no_reason() {
    WebRequestTestBackend backend;
    runCommand(backend, "help system.status.health");

    JsonDocument doc;
    TEST_ASSERT_FALSE(deserializeJson(doc, backend.sentBody));
    for (JsonObjectConst rec : doc["records"].as<JsonArrayConst>()) {
        if (strcmp(rec["status"].as<const char*>() ? rec["status"].as<const char*>() : "", "ok") != 0)
            continue;
        TEST_ASSERT_FALSE_MESSAGE(rec["reason"].is<const char*>(),
                                  "a successful record must not carry a reason field");
    }
}

// A real availability reason must survive to the wire. The suppression guard
// used to exclude not-in-this-build specifically - it was the enum's zero value
// and doubled as success filler - which would have dropped the reason from the
// very answer #224 requires.
void test_error_record_keeps_its_reason() {
    WebRequestTestBackend backend;
    runCommand(backend, "no.such.operation");

    JsonDocument doc;
    TEST_ASSERT_FALSE(deserializeJson(doc, backend.sentBody));
    JsonObjectConst rec = doc["records"][0];
    TEST_ASSERT_EQUAL_STRING("err", rec["status"].as<const char*>());
    TEST_ASSERT_TRUE_MESSAGE(rec["reason"].is<const char*>(),
                             "an error record lost its reason");
    TEST_ASSERT_EQUAL_STRING("unknown-operation", rec["reason"].as<const char*>());
}

// The secret exclusion holds on the BROWSER adapter too, and this is the
// leg the module-level test cannot cover: the web adapter accumulates every
// record value and then serializes them into one JSON response body, so a
// leak here would be a leak into an HTTP response, not just into a record.
// The check is on the whole body, not on any one field - the password must
// not be anywhere in what the dashboard gets back (#227).
void test_password_argument_is_refused_and_never_reaches_the_response_body() {
    WebRequestTestBackend backend;
    runCommand(backend, "wifi.config.settings mode=client sta-password=hunter2hunter2");

    TEST_ASSERT_EQUAL_INT(200, backend.sentCode);
    TEST_ASSERT_NULL_MESSAGE(strstr(backend.sentBody, "hunter2hunter2"),
                             "the refused password value reached the HTTP response body");

    JsonDocument doc;
    TEST_ASSERT_FALSE(deserializeJson(doc, backend.sentBody));
    JsonArrayConst records = doc["records"].as<JsonArrayConst>();
    bool sawReason = false;
    for (JsonObjectConst rec : records) {
        if (rec["reason"].is<const char*>() &&
            strcmp(rec["reason"].as<const char*>(), "secret-not-settable") == 0) {
            sawReason = true;
            TEST_ASSERT_EQUAL_STRING("err", rec["status"].as<const char*>());
            TEST_ASSERT_EQUAL_STRING("invalid", rec["outcome"].as<const char*>());
        }
    }
    TEST_ASSERT_TRUE_MESSAGE(sawReason,
                             "the browser adapter must answer secret-not-settable");

    // The key is named so the operator knows which argument was refused.
    char argument[64] = {};
    TEST_ASSERT_TRUE(fieldValue(backend.sentBody, "argument", argument, sizeof(argument)));
    TEST_ASSERT_EQUAL_STRING("sta-password", argument);
}

// Every record carries the same request id, and ids advance between requests.
void test_request_ids_are_shared_within_and_advance_between() {
    WebRequestTestBackend first;
    runCommand(first, "system.status.health");
    JsonDocument doc1;
    TEST_ASSERT_FALSE(deserializeJson(doc1, first.sentBody));
    uint32_t id1 = doc1["records"][0]["id"];
    for (JsonObjectConst rec : doc1["records"].as<JsonArrayConst>()) {
        TEST_ASSERT_EQUAL_UINT32(id1, rec["id"].as<uint32_t>());
    }

    WebRequestTestBackend second;
    runCommand(second, "system.status.health");
    JsonDocument doc2;
    TEST_ASSERT_FALSE(deserializeJson(doc2, second.sentBody));
    uint32_t id2 = doc2["records"][0]["id"];
    TEST_ASSERT_TRUE_MESSAGE(id2 > id1, "request id did not advance between requests");
}

// -----------------------------------------------------------------------------
// Malformed input is rejected, not silently answered
// -----------------------------------------------------------------------------

void test_empty_command_is_rejected() {
    WebRequestTestBackend backend;
    runCommand(backend, "   ");
    TEST_ASSERT_EQUAL_INT(400, backend.sentCode);
}

// An over-length line is refused with the protocol's own reason, as ONE
// type=result record. docs/console-protocol.md:124-130 defines `end` as the
// record that CLOSES a group opened by `begin`; a guard path that emits a bare
// `end` puts a close on the wire with nothing open, which a reader
// reassembling by Request ID cannot interpret.
void test_over_length_line_is_one_result_record_with_line_too_long() {
    WebRequestTestBackend backend;
    char longLine[512];
    memset(longLine, 'x', sizeof(longLine) - 1);
    longLine[sizeof(longLine) - 1] = '\0';
    runCommand(backend, longLine);

    TEST_ASSERT_EQUAL_UINT_MESSAGE(1, countRecordsOfType(backend.sentBody, "result"),
                                   "an over-length line must answer with one type=result "
                                   "record, not a bare type=end");
    TEST_ASSERT_EQUAL_UINT(0, countRecordsOfType(backend.sentBody, "end"));
    TEST_ASSERT_EQUAL_UINT(0, countRecordsOfType(backend.sentBody, "begin"));

    JsonDocument doc;
    TEST_ASSERT_FALSE(deserializeJson(doc, backend.sentBody));
    JsonObjectConst rec = doc["records"][0];
    TEST_ASSERT_EQUAL_STRING("err", rec["status"].as<const char*>());
    TEST_ASSERT_EQUAL_STRING("line-too-long", rec["reason"].as<const char*>());
}

// -----------------------------------------------------------------------------
// #219 D1 rework: `operations` puts every catalog entry on the wire as an item
// record, and an unrecognized `type=` filter is rejected rather than silently
// answering an empty success. The web adapter's item sink was already wired
// (src/web/api_console.cpp:webOnRecordItem_impl), so this exercises the shared
// module logic at console_module.cpp:437-463 that both adapters call through -
// the serial adapter's own item stub is unreachable from a native test (it is
// gated on `#ifdef ARDUINO` code never in the native build_src_filter) and is
// proven instead by the #215 device transcript.
// -----------------------------------------------------------------------------

void test_operations_lists_catalog_entries_as_items() {
    WebRequestTestBackend backend;
    runCommand(backend, "operations");
    TEST_ASSERT_EQUAL_INT(200, backend.sentCode);

    // #240 removed the web sink's old CONSOLE_RESPONSE_RECORDS_MAX=32 cap for
    // this command specifically: `operations` now streams through
    // WebRequest::sendChunked() instead of the bounded array, so every catalog
    // entry survives one HTTP response. test_operations_delivers_the_full_
    // catalog_terminated_by_end() below asserts the exact count; this test
    // keeps its narrower original job of proving the shared item-building loop
    // (console_module.cpp:437-463) reaches the sink and names a real entry.
    TEST_ASSERT_EQUAL_UINT(1, countRecordsOfType(backend.sentBody, "begin"));
    TEST_ASSERT_EQUAL_UINT(0, countRecordsOfType(backend.sentBody, "result"));
    size_t itemCount = countRecordsOfType(backend.sentBody, "item");
    TEST_ASSERT_TRUE_MESSAGE(itemCount > 0, "operations produced zero item records");

    JsonDocument doc;
    TEST_ASSERT_FALSE(deserializeJson(doc, backend.sentBody));
    bool foundMove = false;
    for (JsonObjectConst rec : doc["records"].as<JsonArrayConst>()) {
        const char* type = rec["type"];
        if (!type || strcmp(type, "item") != 0) continue;
        const char* value = rec["value"];
        if (value && strstr(value, "drive.action.move") != nullptr) {
            foundMove = true;
        }
    }
    TEST_ASSERT_TRUE_MESSAGE(foundMove, "operations list missing drive.action.move");
}

// -----------------------------------------------------------------------------
// #240: the browser adapter silently truncated `operations` at 32 records
// (144 of 175 catalog entries dropped, including the closing `end`) with no
// error and a response that still looked like well-formed JSON. This drives
// the REAL adapter (handleConsolePost -> consoleExecuteCommand -> the real,
// in-image catalog) through WebRequestTestBackend's 64-byte chunk buffer
// (test/stubs/include/web_request_test_backend.h:520), the same
// offset-replay loop the device backend runs (src/web/web_request_psychic.cpp)
// with its own 1024-byte chunk - so a filler that mishandled an offset split
// at a chunk boundary would fail here too, not just at the full device size.
// -----------------------------------------------------------------------------

void test_operations_delivers_the_full_catalog_terminated_by_end() {
    WebRequestTestBackend backend;
    runCommand(backend, "operations");
    TEST_ASSERT_EQUAL_INT(200, backend.sentCode);

    size_t expectedCount = consoleCatalogGetCount();
    // Sanity check on the fixture itself: the whole point of this test is a
    // catalog too large for the old 32-record cap to hold.
    TEST_ASSERT_GREATER_THAN_UINT_MESSAGE(32, expectedCount,
        "catalog fixture must exceed the old bounded-path cap to prove anything");

    TEST_ASSERT_EQUAL_UINT(1, countRecordsOfType(backend.sentBody, "begin"));
    TEST_ASSERT_EQUAL_UINT_MESSAGE(1, countRecordsOfType(backend.sentBody, "end"),
        "the group must be terminated by exactly one end record, not dropped");
    TEST_ASSERT_EQUAL_UINT(0, countRecordsOfType(backend.sentBody, "result"));
    TEST_ASSERT_EQUAL_UINT_MESSAGE(expectedCount, countRecordsOfType(backend.sentBody, "item"),
        "operations must deliver every catalog entry, not a truncated subset");

    JsonDocument doc;
    TEST_ASSERT_FALSE(deserializeJson(doc, backend.sentBody));

    // The end record must be status=ok outcome=completed, matching a
    // synchronous query - and it must be the LAST record (nothing after it
    // in an already-closed group).
    JsonArrayConst records = doc["records"].as<JsonArrayConst>();
    JsonObjectConst lastRecord = records[records.size() - 1];
    TEST_ASSERT_EQUAL_STRING("end", lastRecord["type"].as<const char*>());
    TEST_ASSERT_EQUAL_STRING("ok", lastRecord["status"].as<const char*>());
    TEST_ASSERT_EQUAL_STRING("completed", lastRecord["outcome"].as<const char*>());

    // The exact defect measured on the live board (coordinator pin, #240):
    // the truncated response only ever reached the `drive`/`dome` domains and
    // never a `system.*` entry, which is what left browser Tab completion
    // unable to complete `sys` -> `system.` while the serial adapter could.
    bool foundSystemEntry = false;
    for (JsonObjectConst rec : records) {
        const char* type = rec["type"];
        const char* value = rec["value"];
        if (type && value && strcmp(type, "item") == 0 && strncmp(value, "system.", 7) == 0) {
            foundSystemEntry = true;
            break;
        }
    }
    TEST_ASSERT_TRUE_MESSAGE(foundSystemEntry,
        "operations must reach the system.* domain, not just the first ~30 catalog entries");
}

// Registry has exactly 18 status-type entries (docs/action-registry.yaml) -
// few enough to fit under the web response cap with room for begin/end, so
// this filtered case can assert the exact, complete count. Was 14 before
// #221's remainder reclassified dome.api.get-sequence-last-run/
// -list-sequences/-list-builtin-sequences from type: action to type: status
// (the only way to route them through g_statusExecutors[], src/console/
// console_module.cpp), and 17 before #224 reclassified
// system.api.get-profiler the same way and for the same reason.
void test_operations_type_filter_lists_only_that_type() {
    WebRequestTestBackend backend;
    runCommand(backend, "operations type=status");
    TEST_ASSERT_EQUAL_INT(200, backend.sentCode);
    TEST_ASSERT_EQUAL_UINT(1, countRecordsOfType(backend.sentBody, "begin"));
    TEST_ASSERT_EQUAL_UINT(1, countRecordsOfType(backend.sentBody, "end"));
    TEST_ASSERT_EQUAL_UINT_MESSAGE(18, countRecordsOfType(backend.sentBody, "item"),
        "operations type=status must list every status entry, no more, no less");

    JsonDocument doc;
    TEST_ASSERT_FALSE(deserializeJson(doc, backend.sentBody));
    for (JsonObjectConst rec : doc["records"].as<JsonArrayConst>()) {
        const char* type = rec["type"];
        if (!type || strcmp(type, "item") != 0) continue;
        const char* value = rec["value"];
        TEST_ASSERT_NOT_NULL(value);
        TEST_ASSERT_TRUE_MESSAGE(strstr(value, "(status") != nullptr,
            "operations type=status listed an entry that is not a status entry");
    }
}

// Sub-point of D1: an unrecognized type= value must not answer an empty
// success (status=ok outcome=completed with zero items is indistinguishable
// from "no operations of this type exist").
void test_operations_unknown_type_filter_is_invalid() {
    WebRequestTestBackend backend;
    runCommand(backend, "operations type=bogus");
    TEST_ASSERT_EQUAL_INT(200, backend.sentCode);

    TEST_ASSERT_EQUAL_UINT_MESSAGE(1, countRecordsOfType(backend.sentBody, "result"),
        "an unrecognized type= filter must answer with one type=result record");
    TEST_ASSERT_EQUAL_UINT(0, countRecordsOfType(backend.sentBody, "begin"));
    TEST_ASSERT_EQUAL_UINT(0, countRecordsOfType(backend.sentBody, "end"));

    JsonDocument doc;
    TEST_ASSERT_FALSE(deserializeJson(doc, backend.sentBody));
    JsonObjectConst rec = doc["records"][0];
    TEST_ASSERT_EQUAL_STRING("err", rec["status"].as<const char*>());
    TEST_ASSERT_EQUAL_STRING("invalid", rec["outcome"].as<const char*>());
    TEST_ASSERT_EQUAL_STRING("out-of-range", rec["reason"].as<const char*>());
}

// -----------------------------------------------------------------------------
// #219 D3 rework: `help <op>` must render schema/availability from the
// IN-IMAGE catalog table (ConsoleCatalogEntry) regardless of the FS-resident
// help file's health - consoleEmitHelpForOperation() used to jump straight
// from `type` to the file and never touch entry->aliases/params/available_*
// at all, even though the catalog carries real data for 38 alias entries and
// 29 parameter entries.
// -----------------------------------------------------------------------------

// drive.action.move: no rc_token (no aliases), two required int16 params.
void test_help_emits_catalog_availability_fields() {
    WebRequestTestBackend backend;
    runCommand(backend, "help drive.action.move");
    TEST_ASSERT_EQUAL_INT(200, backend.sentCode);

    char value[64] = {};
    TEST_ASSERT_TRUE(fieldValue(backend.sentBody, "available_on_board", value, sizeof(value)));
    TEST_ASSERT_EQUAL_STRING("true", value);
    TEST_ASSERT_TRUE(fieldValue(backend.sentBody, "available_in_build", value, sizeof(value)));
    TEST_ASSERT_EQUAL_STRING("true", value);
    TEST_ASSERT_TRUE(fieldValue(backend.sentBody, "requires_web_control", value, sizeof(value)));
    TEST_ASSERT_EQUAL_STRING("true", value);

    // Those three are the whole availability set. Readiness is not among them
    // and never was true information: the catalog claimed it for every entry
    // while dispatch refused ~44 of them, so the field is gone from the
    // browser adapter's help reply too (ADR 0035, #263).
    TEST_ASSERT_FALSE_MESSAGE(fieldValue(backend.sentBody, "executor_ready", value, sizeof(value)),
        "help must not advertise executor readiness at discovery time");
}

void test_help_emits_params_from_catalog_not_file() {
    WebRequestTestBackend backend;
    runCommand(backend, "help drive.action.move");

    char value[128] = {};
    TEST_ASSERT_TRUE_MESSAGE(fieldValue(backend.sentBody, "params", value, sizeof(value)),
        "help must render params from the in-image catalog");
    TEST_ASSERT_EQUAL_STRING("speed:int16:required,steer:int16:required", value);

    // No rc_token on this entry: the aliases field must be entirely absent,
    // not present-and-empty (matches the reason= field's presence convention).
    TEST_ASSERT_FALSE_MESSAGE(fieldValue(backend.sentBody, "aliases", value, sizeof(value)),
        "drive.action.move has no rc_token alias and must not carry an aliases field");
}

// drive.action.speed: has rc_token "drive_speed" (one alias) and one param.
void test_help_emits_aliases_from_catalog() {
    WebRequestTestBackend backend;
    runCommand(backend, "help drive.action.speed");

    char value[64] = {};
    TEST_ASSERT_TRUE_MESSAGE(fieldValue(backend.sentBody, "aliases", value, sizeof(value)),
        "drive.action.speed has an rc_token alias that must reach help");
    TEST_ASSERT_EQUAL_STRING("drive_speed", value);

    TEST_ASSERT_TRUE(fieldValue(backend.sentBody, "params", value, sizeof(value)));
    TEST_ASSERT_EQUAL_STRING("value:float:required", value);
}

// -----------------------------------------------------------------------------
// #219 D4 rework: bare `help`'s detach_key field must be adapter-aware - the
// serial adapter has a real detach convention (Ctrl-C); the browser adapter
// does not and must not claim one. The web half is reachable through the real
// handler (webOnRecordField_impl -> WebRequestTestBackend); the serial half
// needs a direct consoleExecuteCommand() call with CONSOLE_SOURCE_SERIAL,
// since src/tasks/console_task.cpp itself is native-unreachable.
// -----------------------------------------------------------------------------

static const int kDetachMaxFields = 8;
static char g_detachNames[kDetachMaxFields][32];
static char g_detachValues[kDetachMaxFields][64];
static int g_detachFieldCount = 0;

static void detachCapBegin(uint32_t, const char*) {}
static void detachCapField(uint32_t, const char* name, const char* value) {
    if (g_detachFieldCount >= kDetachMaxFields) return;
    snprintf(g_detachNames[g_detachFieldCount], sizeof(g_detachNames[0]), "%s", name);
    snprintf(g_detachValues[g_detachFieldCount], sizeof(g_detachValues[0]), "%s", value);
    g_detachFieldCount++;
}
static void detachCapEnd(uint32_t, ConsoleStatus, ConsoleOutcome, ConsoleReason) {}

static const char* detachFieldNamed(const char* name) {
    for (int i = 0; i < g_detachFieldCount; i++) {
        if (strcmp(g_detachNames[i], name) == 0) return g_detachValues[i];
    }
    return nullptr;
}

static void runBareHelpWithSource(ConsoleCommandSource source) {
    g_detachFieldCount = 0;
    ConsoleRecordSink sink = {};
    sink.onRecordBegin = detachCapBegin;
    sink.onRecordField = detachCapField;
    sink.onRecordEnd = detachCapEnd;

    ConsoleRequest req = {};
    req.requestId = 1;
    req.source = source;
    req.operationName = "help";
    consoleExecuteCommand(&req, &sink);
}

void test_bare_help_serial_source_carries_detach_key() {
    runBareHelpWithSource(CONSOLE_SOURCE_SERIAL);
    const char* value = detachFieldNamed("detach_key");
    TEST_ASSERT_NOT_NULL_MESSAGE(value, "the serial source must carry a detach_key field");
    TEST_ASSERT_EQUAL_STRING("Ctrl-C", value);
}

void test_bare_help_web_source_has_no_detach_key() {
    runBareHelpWithSource(CONSOLE_SOURCE_WEB);
    TEST_ASSERT_NULL_MESSAGE(detachFieldNamed("detach_key"),
        "the web adapter has no detach convention and must not claim one");
}

// Same check through the real HTTP handler (belt-and-braces: proves the web
// adapter's own source assignment in api_console.cpp, not just the module).
void test_help_over_web_adapter_has_no_detach_key() {
    WebRequestTestBackend backend;
    runCommand(backend, "help");
    char value[64] = {};
    TEST_ASSERT_FALSE_MESSAGE(fieldValue(backend.sentBody, "detach_key", value, sizeof(value)),
        "POST /api/console must not claim a detach_key");
}

// -----------------------------------------------------------------------------
// #239: system.status.logs over the browser adapter.
//
// The defect this guards: CONSOLE_RESPONSE_RECORDS_MAX is 32
// (src/web/api_console.cpp), LOG_RING_MAX_LINES is 48 (include/log_buffer.h),
// and the query emits begin + N items + end = N + 2 records - so a ring at
// 31+ lines overflowed the bounded sink and answered HTTP 500 instead of the
// log history. A 48-line ring is at or near full in normal operation (any
// operator running at DEBUG log level), so this was the ordinary browser
// case, not an edge case. These tests exercise the REAL web handler
// (handleConsolePost -> consoleExecuteCommand -> the real ring), the same
// path a live device request takes - not just the module with a test sink.
// -----------------------------------------------------------------------------

// All item values, in wire order (oldest-recorded first).
static std::vector<std::string> collectItemValues(const char* body) {
    std::vector<std::string> out;
    JsonDocument doc;
    if (deserializeJson(doc, body)) return out;
    for (JsonObjectConst rec : doc["records"].as<JsonArrayConst>()) {
        const char* type = rec["type"];
        if (type && strcmp(type, "item") == 0) {
            const char* value = rec["value"];
            out.push_back(value != nullptr ? value : "");
        }
    }
    return out;
}

static bool responseIsTruncated(const char* body) {
    JsonDocument doc;
    if (deserializeJson(doc, body)) return false;
    return doc["truncated"] | false;
}

void test_logs_query_small_ring_is_not_truncated() {
    for (size_t i = 0; i < 5; ++i) {
        char line[32];
        snprintf(line, sizeof(line), "line-%zu", i);
        logBufferAppend(&g_test_log_buffer, line);
    }

    WebRequestTestBackend backend;
    runCommand(backend, "system.status.logs");
    TEST_ASSERT_EQUAL_INT(200, backend.sentCode);

    TEST_ASSERT_EQUAL_UINT(1, countRecordsOfType(backend.sentBody, "begin"));
    TEST_ASSERT_EQUAL_UINT(1, countRecordsOfType(backend.sentBody, "end"));
    TEST_ASSERT_EQUAL_UINT(0, countRecordsOfType(backend.sentBody, "result"));
    TEST_ASSERT_FALSE_MESSAGE(responseIsTruncated(backend.sentBody),
        "a ring well under the cap must not be reported as truncated");

    std::vector<std::string> items = collectItemValues(backend.sentBody);
    TEST_ASSERT_EQUAL_UINT(5, items.size());
    TEST_ASSERT_EQUAL_STRING("line-0", items.front().c_str());
    TEST_ASSERT_EQUAL_STRING("line-4", items.back().c_str());
}

// The defect itself: a ring over the old 32-record cap must answer 200 with
// the recent history, never HTTP 500.
void test_logs_query_full_ring_answers_200_not_500() {
    for (size_t i = 0; i < LOG_RING_MAX_LINES; ++i) {
        char line[32];
        snprintf(line, sizeof(line), "line-%zu", i);
        logBufferAppend(&g_test_log_buffer, line);
    }

    WebRequestTestBackend backend;
    runCommand(backend, "system.status.logs");

    TEST_ASSERT_EQUAL_INT_MESSAGE(200, backend.sentCode,
        "a full 48-line ring must not overflow the browser adapter (#239)");
    TEST_ASSERT_EQUAL_UINT(1, countRecordsOfType(backend.sentBody, "begin"));
    TEST_ASSERT_EQUAL_UINT_MESSAGE(1, countRecordsOfType(backend.sentBody, "end"),
        "the group must be terminated, not dropped");
}

// The truncation must be visible on the wire, and it must keep the NEWEST
// lines - a live diagnostic query is most useful showing what just happened,
// not the oldest surviving entries.
void test_logs_query_full_ring_is_truncated_and_keeps_the_newest_lines() {
    for (size_t i = 0; i < LOG_RING_MAX_LINES; ++i) {
        char line[32];
        snprintf(line, sizeof(line), "line-%zu", i);
        logBufferAppend(&g_test_log_buffer, line);
    }

    WebRequestTestBackend backend;
    runCommand(backend, "system.status.logs");
    TEST_ASSERT_EQUAL_INT(200, backend.sentCode);

    TEST_ASSERT_TRUE_MESSAGE(responseIsTruncated(backend.sentBody),
        "a ring this deep must report truncated:true, not answer silently");

    std::vector<std::string> items = collectItemValues(backend.sentBody);
    TEST_ASSERT_TRUE_MESSAGE(items.size() > 0, "truncation must not mean zero items");
    TEST_ASSERT_TRUE_MESSAGE(items.size() < LOG_RING_MAX_LINES,
        "this test only proves something if fewer than the full ring came back");

    // Newest kept item must be the ring's actual newest line.
    char newestExpected[32];
    snprintf(newestExpected, sizeof(newestExpected), "line-%zu", LOG_RING_MAX_LINES - 1);
    TEST_ASSERT_EQUAL_STRING(newestExpected, items.back().c_str());

    // The oldest line in the ring must NOT be present - recency, not an
    // arbitrary or oldest-first cutoff.
    bool foundOldest = false;
    for (const auto& v : items) {
        if (v == "line-0") foundOldest = true;
    }
    TEST_ASSERT_FALSE_MESSAGE(foundOldest,
        "truncation must drop the OLDEST lines, keeping the most recent ones");
}

// -----------------------------------------------------------------------------
// Static storage must not leak between requests (#266)
// -----------------------------------------------------------------------------

// handleConsolePost() moved `command`/`webSink`/`routeScratch` from stack
// locals to static storage so its own stack frame stays small enough for the
// httpd task's 8 KB stack (see handleConsolePost()'s own comment: 3776 B ->
// 240 B, measured via `-fstack-usage`). Static storage means the SAME memory
// backs every request on this (single, serializing) httpd task, so an
// incomplete reset before use would leak one request's sink state into the
// next - the one new risk this refactor introduces. Provably-able-to-fail:
// deleting the `memset(&webSink, 0, sizeof(webSink))` call in
// handleConsolePost() turns this red (verified 2026-09-03) - the truncated
// log response's leftover item records and its `truncated:true` flag survive
// into the following, otherwise-clean request.
void test_static_websink_does_not_leak_into_the_next_request() {
    // First request: overflow the ring so the response comes back
    // truncated:true with a full house of "item" records - the sink state
    // most worth proving does NOT survive into the next call.
    for (size_t i = 0; i < LOG_RING_MAX_LINES; ++i) {
        char line[32];
        snprintf(line, sizeof(line), "line-%zu", i);
        logBufferAppend(&g_test_log_buffer, line);
    }
    WebRequestTestBackend firstBackend;
    runCommand(firstBackend, "system.status.logs");
    TEST_ASSERT_TRUE_MESSAGE(responseIsTruncated(firstBackend.sentBody),
        "setup expected the first request to be truncated");
    TEST_ASSERT_TRUE(countRecordsOfType(firstBackend.sentBody, "item") > 0);

    // Second request: the smallest possible response, on a fresh backend. If
    // the static ConsoleWebSink were not fully reset, this response would
    // carry forward the first request's item records, its truncated flag, or
    // its stale record count.
    WebRequestTestBackend secondBackend;
    runCommand(secondBackend, "no.such.operation");
    TEST_ASSERT_EQUAL_INT(200, secondBackend.sentCode);
    TEST_ASSERT_EQUAL_UINT_MESSAGE(1, countRecordsOfType(secondBackend.sentBody, "result"),
        "a stale ConsoleWebSink leaked extra records from the previous request");
    TEST_ASSERT_EQUAL_UINT_MESSAGE(0, countRecordsOfType(secondBackend.sentBody, "item"),
        "a stale ConsoleWebSink leaked item records from the previous request");
    TEST_ASSERT_FALSE_MESSAGE(responseIsTruncated(secondBackend.sentBody),
        "a stale ConsoleWebSink leaked the previous request's truncated:true");
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_status_field_values_are_not_aliased);
    RUN_TEST(test_status_fields_keep_their_own_values);
    RUN_TEST(test_help_with_argument_returns_a_multi_record_answer);
    RUN_TEST(test_unknown_operation_is_a_single_result_record);
    RUN_TEST(test_status_query_is_begin_fields_end);
    RUN_TEST(test_completed_query_reports_outcome_completed);
    RUN_TEST(test_successful_records_carry_no_reason);
    RUN_TEST(test_error_record_keeps_its_reason);
    RUN_TEST(test_password_argument_is_refused_and_never_reaches_the_response_body);
    RUN_TEST(test_request_ids_are_shared_within_and_advance_between);
    RUN_TEST(test_empty_command_is_rejected);
    RUN_TEST(test_over_length_line_is_one_result_record_with_line_too_long);
    RUN_TEST(test_operations_lists_catalog_entries_as_items);
    RUN_TEST(test_operations_delivers_the_full_catalog_terminated_by_end);
    RUN_TEST(test_operations_type_filter_lists_only_that_type);
    RUN_TEST(test_operations_unknown_type_filter_is_invalid);
    RUN_TEST(test_help_emits_catalog_availability_fields);
    RUN_TEST(test_help_emits_params_from_catalog_not_file);
    RUN_TEST(test_help_emits_aliases_from_catalog);
    RUN_TEST(test_bare_help_serial_source_carries_detach_key);
    RUN_TEST(test_bare_help_web_source_has_no_detach_key);
    RUN_TEST(test_help_over_web_adapter_has_no_detach_key);

    RUN_TEST(test_logs_query_small_ring_is_not_truncated);
    RUN_TEST(test_logs_query_full_ring_answers_200_not_500);
    RUN_TEST(test_logs_query_full_ring_is_truncated_and_keeps_the_newest_lines);
    RUN_TEST(test_static_websink_does_not_leak_into_the_next_request);
    return UNITY_END();
}
