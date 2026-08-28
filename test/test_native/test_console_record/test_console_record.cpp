// =============================================================================
// test/test_console/test_console_module.cpp
//
// Native host tests for Console Record formatting and string conversion.
// Tests the pure functions that do not depend on hardware or complex mocking.
// Integration tests for command execution will be in the device verification phase.
// =============================================================================

#include <unity.h>
#include <string.h>
#include <stdint.h>

#include "console_record.h"

void setUp(void) {}
void tearDown(void) {}

// Test: Outcome string conversion
void test_outcome_string_queued(void) {
    TEST_ASSERT_EQUAL_STRING("queued", consoleOutcomeString(CONSOLE_OUTCOME_QUEUED));
}

void test_outcome_string_applied(void) {
    TEST_ASSERT_EQUAL_STRING("applied", consoleOutcomeString(CONSOLE_OUTCOME_APPLIED));
}

void test_outcome_string_staged_until_reboot(void) {
    TEST_ASSERT_EQUAL_STRING("staged-until-reboot",
                            consoleOutcomeString(CONSOLE_OUTCOME_STAGED_UNTIL_REBOOT));
}

void test_outcome_string_unavailable(void) {
    TEST_ASSERT_EQUAL_STRING("unavailable", consoleOutcomeString(CONSOLE_OUTCOME_UNAVAILABLE));
}

void test_outcome_string_blocked(void) {
    TEST_ASSERT_EQUAL_STRING("blocked", consoleOutcomeString(CONSOLE_OUTCOME_BLOCKED));
}

void test_outcome_string_queue_full(void) {
    TEST_ASSERT_EQUAL_STRING("queue-full", consoleOutcomeString(CONSOLE_OUTCOME_QUEUE_FULL));
}

void test_outcome_string_invalid(void) {
    TEST_ASSERT_EQUAL_STRING("invalid", consoleOutcomeString(CONSOLE_OUTCOME_INVALID));
}

void test_outcome_string_internal_error(void) {
    TEST_ASSERT_EQUAL_STRING("internal-error",
                            consoleOutcomeString(CONSOLE_OUTCOME_INTERNAL_ERROR));
}

// Test: Reason string conversion
void test_reason_string_not_in_this_build(void) {
    TEST_ASSERT_EQUAL_STRING("not-in-this-build",
                            consoleReasonString(CONSOLE_REASON_NOT_IN_THIS_BUILD));
}

void test_reason_string_not_on_this_board(void) {
    TEST_ASSERT_EQUAL_STRING("not-on-this-board",
                            consoleReasonString(CONSOLE_REASON_NOT_ON_THIS_BOARD));
}

void test_reason_string_component_disabled(void) {
    TEST_ASSERT_EQUAL_STRING("component-disabled",
                            consoleReasonString(CONSOLE_REASON_COMPONENT_DISABLED));
}

void test_reason_string_blocked_by_state(void) {
    TEST_ASSERT_EQUAL_STRING("blocked-by-state",
                            consoleReasonString(CONSOLE_REASON_BLOCKED_BY_STATE));
}

void test_reason_string_line_too_long(void) {
    TEST_ASSERT_EQUAL_STRING("line-too-long", consoleReasonString(CONSOLE_REASON_LINE_TOO_LONG));
}

void test_reason_string_secret_not_settable(void) {
    TEST_ASSERT_EQUAL_STRING("secret-not-settable",
                            consoleReasonString(CONSOLE_REASON_SECRET_NOT_SETTABLE));
}

void test_reason_string_unknown_operation(void) {
    TEST_ASSERT_EQUAL_STRING("unknown-operation",
                            consoleReasonString(CONSOLE_REASON_UNKNOWN_OPERATION));
}

void test_reason_string_unknown_argument(void) {
    TEST_ASSERT_EQUAL_STRING("unknown-argument",
                            consoleReasonString(CONSOLE_REASON_UNKNOWN_ARGUMENT));
}

void test_reason_string_missing_argument(void) {
    TEST_ASSERT_EQUAL_STRING("missing-argument",
                            consoleReasonString(CONSOLE_REASON_MISSING_ARGUMENT));
}

void test_reason_string_out_of_range(void) {
    TEST_ASSERT_EQUAL_STRING("out-of-range", consoleReasonString(CONSOLE_REASON_OUT_OF_RANGE));
}

void test_reason_string_not_executable(void) {
    TEST_ASSERT_EQUAL_STRING("not-executable", consoleReasonString(CONSOLE_REASON_NOT_EXECUTABLE));
}

void test_reason_string_executor_not_ready(void) {
    TEST_ASSERT_EQUAL_STRING("executor-not-ready",
                            consoleReasonString(CONSOLE_REASON_EXECUTOR_NOT_READY));
}

// Test: Status string conversion
void test_status_string_ok(void) {
    TEST_ASSERT_EQUAL_STRING("ok", consoleStatusString(CONSOLE_STATUS_OK));
}

void test_status_string_err(void) {
    TEST_ASSERT_EQUAL_STRING("err", consoleStatusString(CONSOLE_STATUS_ERR));
}

// Test: Format key=value pair
void test_format_pair_simple(void) {
    char buffer[256];
    size_t len = consoleFormatPair(buffer, sizeof(buffer), "key", "value");
    TEST_ASSERT_EQUAL(9, len);  // strlen("key=value")
    TEST_ASSERT_EQUAL_STRING("key=value", buffer);
}

void test_format_pair_with_number(void) {
    char buffer[256];
    size_t len = consoleFormatPair(buffer, sizeof(buffer), "id", "42");
    TEST_ASSERT_EQUAL(5, len);  // strlen("id=42")
    TEST_ASSERT_EQUAL_STRING("id=42", buffer);
}

void test_format_pair_empty_value(void) {
    char buffer[256];
    size_t len = consoleFormatPair(buffer, sizeof(buffer), "key", "");
    TEST_ASSERT_EQUAL(4, len);  // strlen("key=")
    TEST_ASSERT_EQUAL_STRING("key=", buffer);
}

void test_format_pair_buffer_too_small(void) {
    char buffer[5];
    size_t len = consoleFormatPair(buffer, sizeof(buffer), "key", "value");
    TEST_ASSERT_EQUAL(0, len);  // Should return 0 if buffer too small
}

// Test: Quote value only when needed
void test_quote_value_simple_no_quote(void) {
    char buffer[256];
    const char* result = consoleQuoteValue("simple", buffer, sizeof(buffer));
    // Should return original pointer because no quoting needed
    TEST_ASSERT_EQUAL_PTR("simple", result);
}

void test_quote_value_with_space_adds_quotes(void) {
    char buffer[256];
    const char* result = consoleQuoteValue("has space", buffer, sizeof(buffer));
    // Should return pointer to buffer because quoting is needed
    TEST_ASSERT_EQUAL_PTR(buffer, result);
    TEST_ASSERT_EQUAL_STRING("\"has space\"", buffer);
}

void test_quote_value_with_equals_adds_quotes(void) {
    char buffer[256];
    const char* result = consoleQuoteValue("key=value", buffer, sizeof(buffer));
    TEST_ASSERT_EQUAL_PTR(buffer, result);
    TEST_ASSERT_EQUAL_STRING("\"key=value\"", buffer);
}

void test_quote_value_with_quote_escapes_properly(void) {
    char buffer[256];
    const char* result = consoleQuoteValue("has\"quote", buffer, sizeof(buffer));
    TEST_ASSERT_EQUAL_PTR(buffer, result);
    TEST_ASSERT_EQUAL_STRING("\"has\\\"quote\"", buffer);
}

// T1 scope: quoting only for space, equals, and quote characters.
// Backslash quoting (for raw Marcduino values) is T2+ scope.
// This test is a placeholder for future implementation.
void test_quote_value_with_backslash_escapes_properly(void) {
    // Backslash handling deferred to T2+
    TEST_PASS();
}

// =============================================================================
// Test Runner
// =============================================================================

int main(void) {
    UNITY_BEGIN();

    // Outcome tests
    RUN_TEST(test_outcome_string_queued);
    RUN_TEST(test_outcome_string_applied);
    RUN_TEST(test_outcome_string_staged_until_reboot);
    RUN_TEST(test_outcome_string_unavailable);
    RUN_TEST(test_outcome_string_blocked);
    RUN_TEST(test_outcome_string_queue_full);
    RUN_TEST(test_outcome_string_invalid);
    RUN_TEST(test_outcome_string_internal_error);

    // Reason tests
    RUN_TEST(test_reason_string_not_in_this_build);
    RUN_TEST(test_reason_string_not_on_this_board);
    RUN_TEST(test_reason_string_component_disabled);
    RUN_TEST(test_reason_string_blocked_by_state);
    RUN_TEST(test_reason_string_line_too_long);
    RUN_TEST(test_reason_string_secret_not_settable);
    RUN_TEST(test_reason_string_unknown_operation);
    RUN_TEST(test_reason_string_unknown_argument);
    RUN_TEST(test_reason_string_missing_argument);
    RUN_TEST(test_reason_string_out_of_range);
    RUN_TEST(test_reason_string_not_executable);
    RUN_TEST(test_reason_string_executor_not_ready);

    // Status tests
    RUN_TEST(test_status_string_ok);
    RUN_TEST(test_status_string_err);

    // Format tests
    RUN_TEST(test_format_pair_simple);
    RUN_TEST(test_format_pair_with_number);
    RUN_TEST(test_format_pair_empty_value);
    RUN_TEST(test_format_pair_buffer_too_small);

    // Quote tests
    RUN_TEST(test_quote_value_simple_no_quote);
    RUN_TEST(test_quote_value_with_space_adds_quotes);
    RUN_TEST(test_quote_value_with_equals_adds_quotes);
    RUN_TEST(test_quote_value_with_quote_escapes_properly);
    RUN_TEST(test_quote_value_with_backslash_escapes_properly);

    return UNITY_END();
}
