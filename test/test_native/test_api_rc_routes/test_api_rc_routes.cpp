// =============================================================================
// test/test_native/test_api_rc_routes/test_api_rc_routes.cpp
//
// Native unit tests for the RC diagnostics and validation handlers through the
// WebRequest seam's host-test backend (ADR 0021). Each handler is driven
// exactly as a device backend would drive it and the assertions are on the
// captured response -- no vendor web-server type appears here, which is the
// property the port exists to establish.
//
// The payload *shape* is not re-asserted field by field: it comes entirely from
// populateRcDiagnosticsJson() / populateValidationJson(), which this port did
// not touch and which test_rc_diagnostics and test_validation_snapshot already
// cover against tasks/rc_diagnostics_contract.md. What is asserted here is what
// the port newly owns -- status codes, error bodies, the debug toggle's side
// effect, and the payload ceilings the handlers refuse above.
// =============================================================================
#include <unity.h>

#include <ArduinoJson.h>

#include <cstring>

#include "api_rc.h"
#include "api_validation.h"
#include "rc_diagnostics_snapshot.h"
#include "validation_snapshot.h"
#include "web_request_test_backend.h"

// Recorded side effects from src/native_test_stubs.cpp.
extern bool g_test_commanded_rc_debug;
extern unsigned g_test_commanded_rc_debug_calls;

namespace {

// The payload ceilings the handlers refuse above, mirrored here. Kept as
// literals rather than exported from the handlers: a test that read the real
// ceiling could not fail when the ceiling dropped below what the payload needs,
// which is the one regression these bound tests exist to catch.
constexpr size_t kRcPayloadMax = 3072;
constexpr size_t kValidationPayloadMax = 2048;

// A snapshot filled to capacity: all three raw blocks present, the widest
// numeric values, and analogCount/digitalCount set by the caller.
//
// The capture path splits one six-entry name table between analog and digital,
// so a real snapshot has analogCount + digitalCount == 6. Filling both to 6
// therefore over-bounds anything capture can produce -- worth pinning
// separately, because it is the bound that survives a future rebalancing
// between the two buckets.
void fillWorstCaseRc(RcDiagnosticsSnapshot& snap, size_t analogCount, size_t digitalCount) {
    snap = {};
    snap.mode = "standard_pwm";  // longest of the three mode labels
    snap.updatedMs = 0xFFFFFFFFu;

    const char* kSourceKeys[RC_DIAGNOSTICS_SOURCE_CAPACITY] = {"sbus1", "sbus2", "pwm"};
    for (size_t i = 0; i < RC_DIAGNOSTICS_SOURCE_CAPACITY; ++i) {
        snap.sources[i] = {kSourceKeys[i], true, true, 0xFFFFFFFFu, 0xFFFFFFFFu, true};
    }
    snap.sourceCount = RC_DIAGNOSTICS_SOURCE_CAPACITY;

    // The real name table, longest entry first so every slot carries the
    // widest key the payload can contain.
    const char* kNames[RC_DIAGNOSTICS_CHANNEL_CAPACITY] = {"driveSpeed", "driveSteer", "domeSpeed",
                                                           "driveSpeed", "driveSteer", "domeSpeed"};
    const RcBindingConfig kBinding = {RC_BINDING_SBUS1, 15, 65535, 65535, 65535, 65535, true};

    for (size_t i = 0; i < RC_DIAGNOSTICS_CHANNEL_CAPACITY; ++i) {
        // Distinct digital and mapping keys: both land in JSON objects, where
        // repeated keys would collapse and understate the payload.
        static char digitalNames[RC_DIAGNOSTICS_CHANNEL_CAPACITY][16];
        static char mappingNames[RC_DIAGNOSTICS_CHANNEL_CAPACITY][16];
        snprintf(digitalNames[i], sizeof(digitalNames[i]), "driveSpeed%u", (unsigned)i);
        snprintf(mappingNames[i], sizeof(mappingNames[i]), "driveSteer%u", (unsigned)i);

        snap.analogChannels[i] = {(uint8_t)i,  kNames[i], "sbus1", 15,   -32768,
                                  65535,       -1.234f,   -5.678f, true, true};
        snap.digitalChannels[i] = {digitalNames[i], "sbus1", 15, true};
        snap.mappingChannels[i] = {mappingNames[i], kBinding};
    }
    snap.analogCount = analogCount;
    snap.digitalCount = digitalCount;
    snap.mappingCount = RC_DIAGNOSTICS_CHANNEL_CAPACITY;

    for (size_t i = 0; i < RC_DIAGNOSTICS_SBUS_RAW_CAPACITY; ++i) {
        snap.rawSbus1[i] = 65535;
        snap.rawSbus2[i] = 65535;
    }
    for (size_t i = 0; i < RC_DIAGNOSTICS_PWM_RAW_CAPACITY; ++i) {
        snap.rawPwm[i] = 65535;
    }
    snap.hasRawSbus1 = true;
    snap.hasRawSbus2 = true;
    snap.hasRawPwm = true;
}

// Prints the measured size and its headroom. The assertion alone says only
// "it fits"; someone widening either payload needs the actual margin to judge
// whether the buffer still has room, without re-deriving it by hand.
void reportMeasuredSize(const char* what, size_t measured, size_t buffer) {
    char note[128];
    snprintf(note, sizeof(note), "%s: %u bytes of %u (%u spare)", what, (unsigned)measured,
             (unsigned)buffer, (unsigned)(buffer - measured));
    TEST_MESSAGE(note);
}

void fillWorstCaseValidation(ValidationSnapshot& snap) {
    snap = {};
    snap.updatedMs = 0xFFFFFFFFu;
    // FS_WATCHDOG_RESET is the numerically largest FailsafeSource, so both
    // source fields serialize at their widest.
    snap.drive = {true,        true,        true,        true,        FS_WATCHDOG_RESET,
                  0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu,
                  FS_WATCHDOG_RESET};
    snap.domeLink = {"disconnected", 0xFFFFFFFFu, 0xFFFFFFFFu, -2147483647};
    snap.audio = {true, true, 255, 65535, 65535, 65535, 65535, 65535, 65535};

    const char* kSourceKeys[VALIDATION_RC_SOURCE_CAPACITY] = {"sbus1", "sbus2", "pwm"};
    snap.rc.mode = "standard_pwm";
    snap.rc.timeoutMs = 0xFFFFFFFFu;
    for (size_t i = 0; i < VALIDATION_RC_SOURCE_CAPACITY; ++i) {
        snap.rc.sources[i] = {kSourceKeys[i], true, true, true, true, 0xFFFFFFFFu};
    }
    snap.rc.sourceCount = VALIDATION_RC_SOURCE_CAPACITY;
}

}  // namespace

void setUp() {
    g_test_commanded_rc_debug = false;
    g_test_commanded_rc_debug_calls = 0;
}

void tearDown() {
}

// ---- GET /api/rc -----------------------------------------------------------

void test_rc_get_returns_parseable_diagnostics() {
    WebRequestTestBackend backend;
    WebRequest req(&backend);

    handleRcGet(req);

    TEST_ASSERT_EQUAL_INT(200, backend.sentCode);
    TEST_ASSERT_EQUAL_STRING("application/json", backend.sentContentType);
    TEST_ASSERT_EQUAL_UINT(1, backend.sendCalls);

    // Parses, and carries the top-level keys the RC page reads. Anything past
    // that is the snapshot core's contract, covered by test_rc_diagnostics.
    JsonDocument doc;
    TEST_ASSERT_FALSE(deserializeJson(doc, backend.sentBody));
    TEST_ASSERT_TRUE(doc["mode"].is<const char*>());
    TEST_ASSERT_TRUE(doc["sources"].is<JsonObject>());
    TEST_ASSERT_TRUE(doc["channels"].is<JsonArray>());
    TEST_ASSERT_TRUE(doc["mappingProfile"].is<JsonObject>());
}

// The ceiling the port introduced. The async handler streamed and had no size
// limit; a payload above the ceiling now answers 500 rather than truncating, so
// the ceiling has to stay above the worst case by construction.
void test_worst_case_rc_payload_fits_the_response_buffer() {
    // All six channels analog: analog entries are the widest of the two kinds,
    // so this is the largest payload the capture path can actually build.
    RcDiagnosticsSnapshot capturable;
    fillWorstCaseRc(capturable, RC_DIAGNOSTICS_CHANNEL_CAPACITY, 0);
    JsonDocument capturableDoc;
    TEST_ASSERT_TRUE(populateRcDiagnosticsJson(capturableDoc, capturable));
    reportMeasuredSize("capturable worst-case /api/rc", measureJson(capturableDoc),
                       kRcPayloadMax);
    TEST_ASSERT_LESS_THAN_UINT(kRcPayloadMax, measureJson(capturableDoc));

    // Both buckets at capacity -- unreachable today, pinned so the buffer still
    // holds if the analog/digital split ever changes.
    RcDiagnosticsSnapshot overBound;
    fillWorstCaseRc(overBound, RC_DIAGNOSTICS_CHANNEL_CAPACITY, RC_DIAGNOSTICS_CHANNEL_CAPACITY);
    JsonDocument overBoundDoc;
    TEST_ASSERT_TRUE(populateRcDiagnosticsJson(overBoundDoc, overBound));
    reportMeasuredSize("over-bound /api/rc", measureJson(overBoundDoc), kRcPayloadMax);
    TEST_ASSERT_LESS_THAN_UINT(kRcPayloadMax, measureJson(overBoundDoc));
}

void test_rc_get_rejects_an_unbuildable_snapshot() {
    // A null mode is populateRcDiagnosticsJson()'s own failure condition, and
    // the only one reachable without a broken allocator.
    RcDiagnosticsSnapshot snap = {};
    snap.mode = nullptr;
    JsonDocument doc;
    TEST_ASSERT_FALSE(populateRcDiagnosticsJson(doc, snap));
}

// ---- POST /api/rc/debug ----------------------------------------------------

void test_rc_debug_enables_and_reports_ok() {
    const char* kBody = "{\"enabled\":true}";
    WebRequestTestBackend backend;
    backend.body = kBody;
    backend.contentLength = strlen(kBody);
    WebRequest req(&backend);

    handleRcDebugPost(req);

    TEST_ASSERT_EQUAL_INT(200, backend.sentCode);
    TEST_ASSERT_EQUAL_STRING("{\"ok\":true}", backend.sentBody);
    TEST_ASSERT_TRUE(g_test_commanded_rc_debug);
    TEST_ASSERT_EQUAL_UINT(1, g_test_commanded_rc_debug_calls);
}

void test_rc_debug_disables() {
    const char* kBody = "{\"enabled\":false}";
    WebRequestTestBackend backend;
    backend.body = kBody;
    backend.contentLength = strlen(kBody);
    WebRequest req(&backend);

    handleRcDebugPost(req);

    TEST_ASSERT_EQUAL_INT(200, backend.sentCode);
    TEST_ASSERT_FALSE(g_test_commanded_rc_debug);
    TEST_ASSERT_EQUAL_UINT(1, g_test_commanded_rc_debug_calls);
}

// An absent "enabled" field defaults to false rather than failing -- inherited
// from the async handler's `doc["enabled"] | false`, and the reason the beacon
// on page unload cannot accidentally leave verbose logging on.
void test_rc_debug_defaults_missing_field_to_disabled() {
    const char* kBody = "{}";
    WebRequestTestBackend backend;
    backend.body = kBody;
    backend.contentLength = strlen(kBody);
    WebRequest req(&backend);

    handleRcDebugPost(req);

    TEST_ASSERT_EQUAL_INT(200, backend.sentCode);
    TEST_ASSERT_FALSE(g_test_commanded_rc_debug);
    TEST_ASSERT_EQUAL_UINT(1, g_test_commanded_rc_debug_calls);
}

void test_rc_debug_rejects_malformed_json() {
    const char* kBody = "{\"enabled\":";
    WebRequestTestBackend backend;
    backend.body = kBody;
    backend.contentLength = strlen(kBody);
    WebRequest req(&backend);

    handleRcDebugPost(req);

    TEST_ASSERT_EQUAL_INT(400, backend.sentCode);
    TEST_ASSERT_EQUAL_STRING("{\"ok\":false,\"error\":\"invalid json\"}", backend.sentBody);
    TEST_ASSERT_EQUAL_UINT(0, g_test_commanded_rc_debug_calls);
}

// A body the backend consumed as form parameters arrives here as null with a
// non-zero length. The async stack would have handed those bytes to the JSON
// parser and failed the same way.
void test_rc_debug_rejects_a_body_the_backend_parsed_away() {
    WebRequestTestBackend backend;
    backend.body = nullptr;
    backend.contentLength = 16;
    WebRequest req(&backend);

    handleRcDebugPost(req);

    TEST_ASSERT_EQUAL_INT(400, backend.sentCode);
    TEST_ASSERT_EQUAL_STRING("{\"ok\":false,\"error\":\"invalid json\"}", backend.sentBody);
    TEST_ASSERT_EQUAL_UINT(0, g_test_commanded_rc_debug_calls);
}

void test_rc_debug_rejects_an_oversized_body() {
    WebRequestTestBackend backend;
    backend.body = "{\"enabled\":true}";
    backend.contentLength = 129;  // one past RC_DEBUG_BODY_MAX
    WebRequest req(&backend);

    handleRcDebugPost(req);

    TEST_ASSERT_EQUAL_INT(413, backend.sentCode);
    TEST_ASSERT_EQUAL_STRING("{\"ok\":false,\"error\":\"payload too large\"}", backend.sentBody);
    TEST_ASSERT_EQUAL_UINT(0, g_test_commanded_rc_debug_calls);
}

// Parity, not design: the async handler answered 413 for an empty body as well
// as an over-long one, and this port preserves status codes rather than
// improving them.
void test_rc_debug_answers_an_empty_body_as_too_large() {
    WebRequestTestBackend backend;
    backend.contentLength = 0;
    WebRequest req(&backend);

    handleRcDebugPost(req);

    TEST_ASSERT_EQUAL_INT(413, backend.sentCode);
    TEST_ASSERT_EQUAL_STRING("{\"ok\":false,\"error\":\"payload too large\"}", backend.sentBody);
    TEST_ASSERT_EQUAL_UINT(0, g_test_commanded_rc_debug_calls);
}

// ---- GET /api/validation ---------------------------------------------------

void test_validation_get_returns_parseable_snapshot() {
    WebRequestTestBackend backend;
    WebRequest req(&backend);

    handleValidationGet(req);

    TEST_ASSERT_EQUAL_INT(200, backend.sentCode);
    TEST_ASSERT_EQUAL_STRING("application/json", backend.sentContentType);
    TEST_ASSERT_EQUAL_UINT(1, backend.sendCalls);

    JsonDocument doc;
    TEST_ASSERT_FALSE(deserializeJson(doc, backend.sentBody));
    TEST_ASSERT_TRUE(doc["drive"].is<JsonObject>());
    TEST_ASSERT_TRUE(doc["domeLink"].is<JsonObject>());
    TEST_ASSERT_TRUE(doc["audio"].is<JsonObject>());
    TEST_ASSERT_TRUE(doc["rc"].is<JsonObject>());
}

void test_worst_case_validation_payload_fits_the_response_buffer() {
    ValidationSnapshot snap;
    fillWorstCaseValidation(snap);

    JsonDocument doc;
    TEST_ASSERT_TRUE(populateValidationJson(doc, snap));
    reportMeasuredSize("worst-case /api/validation", measureJson(doc), kValidationPayloadMax);
    TEST_ASSERT_LESS_THAN_UINT(kValidationPayloadMax, measureJson(doc));
}

int main(int, char**) {
    UNITY_BEGIN();

    RUN_TEST(test_rc_get_returns_parseable_diagnostics);
    RUN_TEST(test_worst_case_rc_payload_fits_the_response_buffer);
    RUN_TEST(test_rc_get_rejects_an_unbuildable_snapshot);

    RUN_TEST(test_rc_debug_enables_and_reports_ok);
    RUN_TEST(test_rc_debug_disables);
    RUN_TEST(test_rc_debug_defaults_missing_field_to_disabled);
    RUN_TEST(test_rc_debug_rejects_malformed_json);
    RUN_TEST(test_rc_debug_rejects_a_body_the_backend_parsed_away);
    RUN_TEST(test_rc_debug_rejects_an_oversized_body);
    RUN_TEST(test_rc_debug_answers_an_empty_body_as_too_large);

    RUN_TEST(test_validation_get_returns_parseable_snapshot);
    RUN_TEST(test_worst_case_validation_payload_fits_the_response_buffer);

    return UNITY_END();
}
