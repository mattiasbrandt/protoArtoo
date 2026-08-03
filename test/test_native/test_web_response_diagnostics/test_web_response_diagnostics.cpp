// =============================================================================
// test/test_native/test_web_response_diagnostics/test_web_response_diagnostics.cpp
//
// Native coverage for allocation-free static-response TCP diagnostics.
// =============================================================================
#include <unity.h>

#include <cstring>

#include "web_response_diagnostics.h"

void setUp() {
}

void tearDown() {
}

// Issue #60: the vendor response path reports one recovery/exhaustion episode
// through this public snapshot without allocating in the callback path.
void test_static_tcp_episode_is_visible_in_snapshot() {
    const WebResponseTcpDiagnostics before = webResponseTcpDiagnosticsSnapshot();

    webResponseTcpRecordZeroProgress(false);
    webResponseTcpRecordZeroProgress(false);
    webResponseTcpRecordZeroProgress(true);
    webResponseTcpRecordRecovery();
    webResponseTcpRecordExhaustion();

    const WebResponseTcpDiagnostics after = webResponseTcpDiagnosticsSnapshot();
    TEST_ASSERT_EQUAL_UINT32(before.zeroProgressAttempts + 3U, after.zeroProgressAttempts);
    TEST_ASSERT_EQUAL_UINT32(before.noSendSpace + 2U, after.noSendSpace);
    TEST_ASSERT_EQUAL_UINT32(before.zeroWithSendSpace + 1U, after.zeroWithSendSpace);
    TEST_ASSERT_EQUAL_UINT32(before.recoveries + 1U, after.recoveries);
    TEST_ASSERT_EQUAL_UINT32(before.exhaustions + 1U, after.exhaustions);
}

// Issue #60: /api/status consumes this public formatter; its exact typical
// payload is the serialized diagnostics contract.
void test_static_tcp_diagnostics_format_as_json_object() {
    const WebResponseTcpDiagnostics diagnostics = {7U, 5U, 2U, 2U, 1U};
    char out[192];

    const bool ok = formatWebResponseTcpDiagnosticsJson(out, sizeof(out), diagnostics);

    TEST_ASSERT_TRUE(ok);
    TEST_ASSERT_EQUAL_STRING(
        "{\"zeroProgress\":7,\"noSendSpace\":5,\"zeroWithSendSpace\":2,"
        "\"recoveries\":2,\"exhaustions\":1}",
        out);
}

// Issue #60: maximum boot-lifetime counters must fit the fixed local object
// budget used by buildStatusJson().
void test_static_tcp_diagnostics_max_values_fit_192_byte_budget() {
    const WebResponseTcpDiagnostics diagnostics = {
        UINT32_MAX, UINT32_MAX, UINT32_MAX, UINT32_MAX, UINT32_MAX};
    char out[192];

    const bool ok = formatWebResponseTcpDiagnosticsJson(out, sizeof(out), diagnostics);

    TEST_ASSERT_TRUE(ok);
    TEST_ASSERT_LESS_THAN(sizeof(out), strlen(out) + 1U);
    TEST_ASSERT_EQUAL_CHAR('}', out[strlen(out) - 1U]);
}

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;
    UNITY_BEGIN();
    RUN_TEST(test_static_tcp_episode_is_visible_in_snapshot);
    RUN_TEST(test_static_tcp_diagnostics_format_as_json_object);
    RUN_TEST(test_static_tcp_diagnostics_max_values_fit_192_byte_budget);
    return UNITY_END();
}
