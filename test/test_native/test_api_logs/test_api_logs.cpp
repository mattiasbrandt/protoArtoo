// =============================================================================
// test/test_native/test_api_logs/test_api_logs.cpp
//
// Native unit tests for GET /api/logs through the WebRequest seam's host-test
// backend (ADR 0021). The ring itself is log_buffer.cpp's, filled through the
// stub buffer in native_test_stubs.cpp, so these assert the handler's real
// copy behavior rather than a canned payload.
// =============================================================================
#include <unity.h>

#include <cstring>

#include "api_logs.h"
#include "log_buffer.h"
#include "web_request_test_backend.h"

extern LogBuffer g_test_log_buffer;

void setUp() {
    g_test_log_buffer = LogBuffer{};
}

void tearDown() {
}

void test_empty_ring_returns_empty_text_body() {
    WebRequestTestBackend backend;
    WebRequest req(&backend);

    handleLogsGet(req);

    TEST_ASSERT_EQUAL_INT(200, backend.sentCode);
    TEST_ASSERT_EQUAL_STRING("text/plain", backend.sentContentType);
    TEST_ASSERT_EQUAL_STRING("", backend.sentBody);
    TEST_ASSERT_EQUAL_UINT(1, backend.sendCalls);
}

void test_buffered_lines_are_returned_oldest_first_newline_separated() {
    logBufferAppend(&g_test_log_buffer, "first line");
    logBufferAppend(&g_test_log_buffer, "second line");

    WebRequestTestBackend backend;
    WebRequest req(&backend);

    handleLogsGet(req);

    TEST_ASSERT_EQUAL_INT(200, backend.sentCode);
    TEST_ASSERT_EQUAL_STRING("first line\nsecond line\n", backend.sentBody);
}

void test_full_ring_is_served_without_truncating_the_response() {
    // Fill the ring to capacity: the handler's buffer is sized from the same
    // constants, so a full ring must come back whole.
    char line[LOG_LINE_MAX];
    for (size_t i = 0; i < LOG_BUFFER_LINES; ++i) {
        memset(line, 'x', sizeof(line) - 1);
        line[sizeof(line) - 1] = '\0';
        logBufferAppend(&g_test_log_buffer, line);
    }

    WebRequestTestBackend backend;
    WebRequest req(&backend);

    handleLogsGet(req);

    // LOG_BUFFER_LINES lines of LOG_LINE_MAX-1 chars, each followed by a
    // newline -- exactly LOG_BUFFER_LINES * LOG_LINE_MAX bytes, which is what
    // the handler's buffer is sized to hold alongside its terminator.
    const size_t expected = LOG_BUFFER_LINES * LOG_LINE_MAX;
    TEST_ASSERT_EQUAL_UINT(expected, backend.sentBodyLength);
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_empty_ring_returns_empty_text_body);
    RUN_TEST(test_buffered_lines_are_returned_oldest_first_newline_separated);
    RUN_TEST(test_full_ring_is_served_without_truncating_the_response);
    return UNITY_END();
}
