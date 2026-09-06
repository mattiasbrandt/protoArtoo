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

void test_quote_value_with_backslash_escapes_properly(void) {
    char buffer[256];
    const char* result = consoleQuoteValue("has\\backslash", buffer, sizeof(buffer));
    TEST_ASSERT_EQUAL_PTR(buffer, result);
    TEST_ASSERT_EQUAL_STRING("\"has\\\\backslash\"", buffer);
}

// =============================================================================
// Test Runner
// =============================================================================


// The reason field is present exactly when there is a reason. Both adapters ask
// this one helper, so the rule cannot drift between them.
//
// Regression guard: the serial adapter used to suppress the reason whenever it
// equalled CONSOLE_REASON_NOT_IN_THIS_BUILD - then the enum's zero value, and
// doubled as filler on success paths. That would have dropped
// `reason=not-in-this-build` from a genuine `unavailable` answer, which is the
// exact answer a profiler operation owes on a non-profiler build.
void test_reason_is_present_for_every_real_reason_and_absent_for_none(void) {
    TEST_ASSERT_FALSE_MESSAGE(consoleReasonIsPresent(CONSOLE_REASON_NONE),
                              "NONE must not render a reason field");

    const ConsoleReason kReal[] = {
        CONSOLE_REASON_NOT_IN_THIS_BUILD,      CONSOLE_REASON_NOT_ON_THIS_BOARD,
        CONSOLE_REASON_COMPONENT_DISABLED,     CONSOLE_REASON_BLOCKED_BY_STATE,
        CONSOLE_REASON_TEMPORARILY_UNAVAILABLE, CONSOLE_REASON_LINE_TOO_LONG,
        CONSOLE_REASON_SECRET_NOT_SETTABLE,    CONSOLE_REASON_UNKNOWN_OPERATION,
        CONSOLE_REASON_UNKNOWN_ARGUMENT,       CONSOLE_REASON_MISSING_ARGUMENT,
        CONSOLE_REASON_OUT_OF_RANGE,           CONSOLE_REASON_NOT_EXECUTABLE,
        CONSOLE_REASON_EXECUTOR_NOT_READY,     CONSOLE_REASON_QUEUE_FULL,
    };
    for (size_t i = 0; i < sizeof(kReal) / sizeof(kReal[0]); ++i) {
        TEST_ASSERT_TRUE_MESSAGE(consoleReasonIsPresent(kReal[i]),
                                 "a real reason must render a reason field");
    }
}

// A synchronously answered query reports completed, not queued.
void test_completed_outcome_has_its_own_token(void) {
    TEST_ASSERT_EQUAL_STRING("completed", consoleOutcomeString(CONSOLE_OUTCOME_COMPLETED));
    TEST_ASSERT_EQUAL_STRING("queued", consoleOutcomeString(CONSOLE_OUTCOME_QUEUED));
}

// -----------------------------------------------------------------------------
// Record lines (#282)
//
// The serial adapter's record buffer is CONSOLE_RECORD_LINE_MAX bytes and the
// longest value it carries is a help `description`, clamped to 255 by the
// Console module. At 256 that record could not be formatted at all, and the
// emitter's `if (len < sizeof(buf))` test then emitted nothing: no line, no
// marker, no count. These pin both halves of the rule -- a full-length
// description fits, and a record that does not fit reports 0 rather than a
// half-line.
// -----------------------------------------------------------------------------
void test_field_record_carries_a_full_length_description(void) {
    char description[256];
    memset(description, 'D', sizeof(description) - 1);
    description[sizeof(description) - 1] = '\0';  // 255 bytes, the module's clamp

    char line[CONSOLE_RECORD_LINE_MAX];
    size_t len = consoleFormatFieldRecord(line, sizeof(line), 28, "description", description);

    TEST_ASSERT_TRUE_MESSAGE(len > 0,
        "a 255-byte description did not fit the record line: the serial adapter drops it whole");
    TEST_ASSERT_EQUAL_UINT32(len, (uint32_t)strlen(line));
    // The value survives to its last byte -- truncation would be a different
    // defect from the one this pins, and just as invisible.
    static const char kPrefix[] = "< id=28 type=field name=description value=";
    TEST_ASSERT_EQUAL_INT(0, strncmp(line, kPrefix, sizeof(kPrefix) - 1));
    TEST_ASSERT_EQUAL_UINT32((uint32_t)(sizeof(kPrefix) - 1 + 255), (uint32_t)len);
    TEST_ASSERT_EQUAL_CHAR('D', line[len - 1]);
}

void test_record_that_does_not_fit_reports_zero(void) {
    char value[64];
    memset(value, 'V', sizeof(value) - 1);
    value[sizeof(value) - 1] = '\0';

    char small[32];
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0,
        (uint32_t)consoleFormatFieldRecord(small, sizeof(small), 7, "description", value),
        "an over-long field record must report 0, not the length snprintf WOULD have written");
    TEST_ASSERT_EQUAL_UINT32(0, (uint32_t)consoleFormatItemRecord(small, sizeof(small), 7, value));
    TEST_ASSERT_EQUAL_UINT32(0, (uint32_t)consoleFormatBeginRecord(small, sizeof(small), 7, value));
}

void test_closing_records_carry_reason_only_when_there_is_one(void) {
    char line[CONSOLE_RECORD_LINE_MAX];

    size_t len = consoleFormatEndRecord(line, sizeof(line), 9, CONSOLE_STATUS_OK,
                                        CONSOLE_OUTCOME_COMPLETED, CONSOLE_REASON_NONE, "");
    TEST_ASSERT_TRUE(len > 0);
    TEST_ASSERT_EQUAL_STRING("< id=9 type=end status=ok outcome=completed", line);

    len = consoleFormatResultRecord(line, sizeof(line), 10, CONSOLE_STATUS_ERR,
                                    CONSOLE_OUTCOME_UNAVAILABLE,
                                    CONSOLE_REASON_NOT_IN_THIS_BUILD, " dropped=2");
    TEST_ASSERT_TRUE(len > 0);
    TEST_ASSERT_EQUAL_STRING(
        "< id=10 type=result status=err outcome=unavailable reason=not-in-this-build dropped=2",
        line);
}

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
    RUN_TEST(test_reason_is_present_for_every_real_reason_and_absent_for_none);
    RUN_TEST(test_completed_outcome_has_its_own_token);

    // Record line tests
    RUN_TEST(test_field_record_carries_a_full_length_description);
    RUN_TEST(test_record_that_does_not_fit_reports_zero);
    RUN_TEST(test_closing_records_carry_reason_only_when_there_is_one);

    return UNITY_END();
}
