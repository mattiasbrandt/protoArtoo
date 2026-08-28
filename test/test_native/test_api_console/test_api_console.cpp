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

#include "api_console.h"
#include "web_request_test_backend.h"

void setUp() {
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
// the native stub, and fsReady is the LAST field the module emits. An adapter
// that stores the sink's pointer hands back the same bytes for both.
void test_status_field_values_are_not_aliased() {
    WebRequestTestBackend backend;
    runCommand(backend, "system.status.health");
    TEST_ASSERT_EQUAL_INT(200, backend.sentCode);

    char largest[64] = {};
    char fsReady[64] = {};
    TEST_ASSERT_TRUE(fieldValue(backend.sentBody, "heapLargestBlock", largest, sizeof(largest)));
    TEST_ASSERT_TRUE(fieldValue(backend.sentBody, "fsReady", fsReady, sizeof(fsReady)));

    // Under the aliasing defect both read the same storage, whatever it holds.
    TEST_ASSERT_TRUE_MESSAGE(strcmp(largest, fsReady) != 0,
                             "heapLargestBlock and fsReady returned identical bytes: "
                             "the sink stored the callback's pointer instead of copying");
}

// Same defect, stated positively: each field keeps its own value.
void test_status_fields_keep_their_own_values() {
    WebRequestTestBackend backend;
    runCommand(backend, "system.status.health");

    char value[64] = {};
    TEST_ASSERT_TRUE(fieldValue(backend.sentBody, "heapLargestBlock", value, sizeof(value)));
    TEST_ASSERT_EQUAL_STRING("262144", value);

    TEST_ASSERT_TRUE(fieldValue(backend.sentBody, "fsReady", value, sizeof(value)));
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

    // The web sink's response buffer (CONSOLE_RESPONSE_RECORDS_MAX = 32,
    // api_console.cpp) caps how many of the catalog's 190 entries survive one
    // HTTP response - a separate, pre-existing capacity limit (#216) that this
    // ticket does not touch. So this only proves the shared item-building loop
    // (console_module.cpp:437-463) reaches the sink at all and names a real
    // entry; the full un-truncated 175/190-entry listing is proven on the
    // serial adapter by the #215 device transcript, per the section 2.1
    // "emitted in full without pagination" contract that only serial can carry.
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

// Registry has exactly 14 status-type entries (docs/action-registry.yaml) -
// few enough to fit under the web response cap with room for begin/end, so
// this filtered case can assert the exact, complete count.
void test_operations_type_filter_lists_only_that_type() {
    WebRequestTestBackend backend;
    runCommand(backend, "operations type=status");
    TEST_ASSERT_EQUAL_INT(200, backend.sentCode);
    TEST_ASSERT_EQUAL_UINT(1, countRecordsOfType(backend.sentBody, "begin"));
    TEST_ASSERT_EQUAL_UINT(1, countRecordsOfType(backend.sentBody, "end"));
    TEST_ASSERT_EQUAL_UINT_MESSAGE(14, countRecordsOfType(backend.sentBody, "item"),
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
// from `type` to the file and never touch entry->aliases/params/available_*/
// executor_ready at all, even though the catalog carries real data for 38
// alias entries and 29 parameter entries.
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
    TEST_ASSERT_TRUE(fieldValue(backend.sentBody, "executor_ready", value, sizeof(value)));
    TEST_ASSERT_EQUAL_STRING("true", value);
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
    RUN_TEST(test_request_ids_are_shared_within_and_advance_between);
    RUN_TEST(test_empty_command_is_rejected);
    RUN_TEST(test_over_length_line_is_one_result_record_with_line_too_long);
    RUN_TEST(test_operations_lists_catalog_entries_as_items);
    RUN_TEST(test_operations_type_filter_lists_only_that_type);
    RUN_TEST(test_operations_unknown_type_filter_is_invalid);
    RUN_TEST(test_help_emits_catalog_availability_fields);
    RUN_TEST(test_help_emits_params_from_catalog_not_file);
    RUN_TEST(test_help_emits_aliases_from_catalog);
    return UNITY_END();
}
