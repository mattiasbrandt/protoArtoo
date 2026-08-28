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

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_status_field_values_are_not_aliased);
    RUN_TEST(test_status_fields_keep_their_own_values);
    RUN_TEST(test_help_with_argument_returns_a_multi_record_answer);
    RUN_TEST(test_unknown_operation_is_a_single_result_record);
    RUN_TEST(test_status_query_is_begin_fields_end);
    RUN_TEST(test_request_ids_are_shared_within_and_advance_between);
    RUN_TEST(test_empty_command_is_rejected);
    return UNITY_END();
}
