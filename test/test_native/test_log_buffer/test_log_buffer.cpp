// =============================================================================
// test/test_native/test_log_buffer/test_log_buffer.cpp
//
// Native unit tests for log ring-buffer helpers and config JSON formatter.
// Tests: logBufferAppend ordering, wrap-around, truncation, logBufferCopy,
//        formatConfigJson output shape.
// =============================================================================
#include <stdio.h>
#include <string.h>
#include <unity.h>

#include "api_helpers.h"
#include "log_buffer.h"

static LogBuffer buf;

void setUp() {
    memset(&buf, 0, sizeof(buf));
}

void tearDown() {
}

// --- logBufferAppend / logBufferCopy basic ---

void test_empty_buffer_copy_returns_empty() {
    char out[64] = "x";
    size_t n = logBufferCopy(&buf, out, sizeof(out));
    TEST_ASSERT_EQUAL_size_t(0, n);
    TEST_ASSERT_EQUAL_CHAR('\0', out[0]);
}

void test_single_append_copy_contains_line() {
    logBufferAppend(&buf, "hello");
    char out[64];
    size_t n = logBufferCopy(&buf, out, sizeof(out));
    TEST_ASSERT_GREATER_THAN(0, (int)n);
    TEST_ASSERT_NOT_NULL(strstr(out, "hello"));
}

void test_append_adds_newline_separator() {
    logBufferAppend(&buf, "line1");
    char out[64];
    logBufferCopy(&buf, out, sizeof(out));
    TEST_ASSERT_NOT_NULL(strchr(out, '\n'));
}

void test_two_lines_ordered_oldest_first() {
    logBufferAppend(&buf, "first");
    logBufferAppend(&buf, "second");
    char out[128];
    logBufferCopy(&buf, out, sizeof(out));
    const char* p1 = strstr(out, "first");
    const char* p2 = strstr(out, "second");
    TEST_ASSERT_NOT_NULL(p1);
    TEST_ASSERT_NOT_NULL(p2);
    TEST_ASSERT_LESS_THAN(p2 - out, p1 - out);
}

void test_count_increments_up_to_capacity() {
    for (size_t i = 0; i < LOG_BUFFER_LINES; ++i) {
        logBufferAppend(&buf, "x");
    }
    TEST_ASSERT_EQUAL_size_t(LOG_BUFFER_LINES, buf.count);
}

void test_count_does_not_exceed_capacity() {
    for (size_t i = 0; i < LOG_BUFFER_LINES + 5; ++i) {
        logBufferAppend(&buf, "x");
    }
    TEST_ASSERT_EQUAL_size_t(LOG_BUFFER_LINES, buf.count);
}

void test_wrap_around_oldest_overwritten() {
    char line[32];
    for (size_t i = 0; i < LOG_BUFFER_LINES; ++i) {
        snprintf(line, sizeof(line), "line%zu", i);
        logBufferAppend(&buf, line);
    }
    logBufferAppend(&buf, "newest");

    char out[LOG_BUFFER_LINES * LOG_LINE_MAX];
    logBufferCopy(&buf, out, sizeof(out));

    TEST_ASSERT_NULL(strstr(out, "line0"));
    TEST_ASSERT_NOT_NULL(strstr(out, "newest"));
}

void test_wrap_around_order_preserved() {
    char line[32];
    const size_t total = LOG_BUFFER_LINES + 3;
    for (size_t i = 0; i < total; ++i) {
        snprintf(line, sizeof(line), "L%zu", i);
        logBufferAppend(&buf, line);
    }
    char out[LOG_BUFFER_LINES * LOG_LINE_MAX];
    logBufferCopy(&buf, out, sizeof(out));

    // After overflow: oldest visible is L3, newest is L(total-1)
    char oldest[16], newest[16];
    snprintf(oldest, sizeof(oldest), "L%zu", total - LOG_BUFFER_LINES);  // first still in ring
    snprintf(newest, sizeof(newest), "L%zu", total - 1);                 // last written
    const char* p_oldest = strstr(out, oldest);
    const char* p_newest = strstr(out, newest);
    TEST_ASSERT_NOT_NULL(p_oldest);
    TEST_ASSERT_NOT_NULL(p_newest);
    TEST_ASSERT_LESS_THAN(p_newest - out, p_oldest - out);
}

void test_long_line_truncated_to_max() {
    char long_line[LOG_LINE_MAX + 32];
    memset(long_line, 'A', sizeof(long_line) - 1);
    long_line[sizeof(long_line) - 1] = '\0';

    logBufferAppend(&buf, long_line);

    size_t stored_len = strlen(buf.lines[0]);
    TEST_ASSERT_LESS_OR_EQUAL(LOG_LINE_MAX - 1, stored_len);
}

void test_copy_output_null_terminated() {
    logBufferAppend(&buf, "abc");
    char out[64];
    memset(out, 0xFF, sizeof(out));
    size_t n = logBufferCopy(&buf, out, sizeof(out));
    TEST_ASSERT_EQUAL_CHAR('\0', out[n]);
}

void test_copy_zero_size_returns_zero() {
    logBufferAppend(&buf, "abc");
    char out[4] = "xxx";
    size_t n = logBufferCopy(&buf, out, 0);
    TEST_ASSERT_EQUAL_size_t(0, n);
}

void test_copy_small_buffer_truncates() {
    logBufferAppend(&buf, "hello world");
    char out[5];
    size_t n = logBufferCopy(&buf, out, sizeof(out));
    TEST_ASSERT_LESS_OR_EQUAL(4, (int)n);
    TEST_ASSERT_EQUAL_CHAR('\0', out[n]);
}

// --- totalWritten ---

void test_totalWritten_starts_at_zero() {
    TEST_ASSERT_EQUAL_UINT32(0, buf.totalWritten);
}

void test_totalWritten_increments_on_each_append() {
    logBufferAppend(&buf, "a");
    TEST_ASSERT_EQUAL_UINT32(1, buf.totalWritten);
    logBufferAppend(&buf, "b");
    TEST_ASSERT_EQUAL_UINT32(2, buf.totalWritten);
    logBufferAppend(&buf, "c");
    TEST_ASSERT_EQUAL_UINT32(3, buf.totalWritten);
}

void test_totalWritten_exceeds_capacity_on_wraparound() {
    for (size_t i = 0; i < LOG_BUFFER_LINES + 5; ++i) {
        logBufferAppend(&buf, "x");
    }
    TEST_ASSERT_EQUAL_UINT32(LOG_BUFFER_LINES + 5, buf.totalWritten);
    // count is still capped at capacity
    TEST_ASSERT_EQUAL_size_t(LOG_BUFFER_LINES, buf.count);
}

// --- formatConfigJson (drive-settings slice only; full config coverage is in test_json_formatters) ---

void test_formatConfigJson_contains_speedLimitMax() {
    char out[512];
    formatConfigJson(out, sizeof(out), 400, 500, false);
    TEST_ASSERT_NOT_NULL(strstr(out, "\"speedLimitMax\":400"));
}

void test_formatConfigJson_contains_webDriveTimeoutMs() {
    char out[512];
    formatConfigJson(out, sizeof(out), 400, 500, false);
    TEST_ASSERT_NOT_NULL(strstr(out, "\"webDriveTimeoutMs\":500"));
}

void test_formatConfigJson_ch8ModeLock_false() {
    char out[512];
    formatConfigJson(out, sizeof(out), 400, 500, false);
    TEST_ASSERT_NOT_NULL(strstr(out, "\"ch8ModeLock\":false"));
}

void test_formatConfigJson_ch8ModeLock_true() {
    char out[512];
    formatConfigJson(out, sizeof(out), 400, 500, true);
    TEST_ASSERT_NOT_NULL(strstr(out, "\"ch8ModeLock\":true"));
}

void test_formatConfigJson_zero_speed_limit() {
    char out[512];
    formatConfigJson(out, sizeof(out), 0, 100, false);
    TEST_ASSERT_NOT_NULL(strstr(out, "\"speedLimitMax\":0"));
}

void test_formatConfigJson_is_valid_json_object() {
    char out[512];
    formatConfigJson(out, sizeof(out), 600, 1000, true);
    TEST_ASSERT_EQUAL_CHAR('{', out[0]);
    TEST_ASSERT_EQUAL_CHAR('}', out[strlen(out) - 1]);
}

int main() {
    UNITY_BEGIN();

    RUN_TEST(test_totalWritten_starts_at_zero);
    RUN_TEST(test_totalWritten_increments_on_each_append);
    RUN_TEST(test_totalWritten_exceeds_capacity_on_wraparound);

    RUN_TEST(test_empty_buffer_copy_returns_empty);
    RUN_TEST(test_single_append_copy_contains_line);
    RUN_TEST(test_append_adds_newline_separator);
    RUN_TEST(test_two_lines_ordered_oldest_first);
    RUN_TEST(test_count_increments_up_to_capacity);
    RUN_TEST(test_count_does_not_exceed_capacity);
    RUN_TEST(test_wrap_around_oldest_overwritten);
    RUN_TEST(test_wrap_around_order_preserved);
    RUN_TEST(test_long_line_truncated_to_max);
    RUN_TEST(test_copy_output_null_terminated);
    RUN_TEST(test_copy_zero_size_returns_zero);
    RUN_TEST(test_copy_small_buffer_truncates);

    RUN_TEST(test_formatConfigJson_contains_speedLimitMax);
    RUN_TEST(test_formatConfigJson_contains_webDriveTimeoutMs);
    RUN_TEST(test_formatConfigJson_ch8ModeLock_false);
    RUN_TEST(test_formatConfigJson_ch8ModeLock_true);
    RUN_TEST(test_formatConfigJson_zero_speed_limit);
    RUN_TEST(test_formatConfigJson_is_valid_json_object);

    return UNITY_END();
}
