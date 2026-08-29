// =============================================================================
// test/test_native/test_console_args/test_console_args.cpp
//
// Native unit tests for include/console_args.h - the shared, transport-
// independent argument contract (#221, ADR 0034, docs/console-protocol.md
// s.1.2/1.3): consoleSplitCommandLine(), consoleParseArgs(),
// consoleUtf8Valid(), consoleArgsFind()/consoleArgsAsParamSource(), and
// consoleValidateArgsAgainstSchema(). Header-only (no .cpp translation unit
// exists or is needed - see the file header in console_args.h for why), so
// this test #includes it directly like any production caller would.
//
// These are the pure-parser tests: no ConsoleRequest, no sink, no
// consoleExecuteCommand() - test_console_module.cpp covers the wired,
// end-to-end behavior (dispatch, schema failures reported through the
// sink). This file proves the tokenizer and validator are correct in
// isolation, including cases no currently-wired operation exercises today
// (numeric range/enum schema checks - #221 closes gap 5 so the DATA
// reaches the catalog, but no ACTION_REGISTRY-dispatchable, non-analog
// entry has range/enum params yet; this file is where that logic is
// proven regardless).
// =============================================================================
#include <unity.h>

#include <cstring>

#include "console_args.h"

void setUp() {}
void tearDown() {}

// =============================================================================
// consoleSplitCommandLine()
// =============================================================================

void test_split_no_args() {
    char line[] = "operations";
    char* name = nullptr;
    char* args = nullptr;
    consoleSplitCommandLine(line, &name, &args);
    TEST_ASSERT_EQUAL_STRING("operations", name);
    TEST_ASSERT_EQUAL_STRING("", args);
}

void test_split_single_space() {
    char line[] = "drive.action.move speed=200 steer=0";
    char* name = nullptr;
    char* args = nullptr;
    consoleSplitCommandLine(line, &name, &args);
    TEST_ASSERT_EQUAL_STRING("drive.action.move", name);
    TEST_ASSERT_EQUAL_STRING("speed=200 steer=0", args);
}

// Multiple spaces between name and args are skipped entirely - whitespace-
// separated per docs/console-protocol.md s.1.2, not a fixed single space.
void test_split_multiple_spaces_collapsed() {
    char line[] = "operations    type=action";
    char* name = nullptr;
    char* args = nullptr;
    consoleSplitCommandLine(line, &name, &args);
    TEST_ASSERT_EQUAL_STRING("operations", name);
    TEST_ASSERT_EQUAL_STRING("type=action", args);
}

void test_split_empty_line() {
    char line[] = "";
    char* name = nullptr;
    char* args = nullptr;
    consoleSplitCommandLine(line, &name, &args);
    TEST_ASSERT_EQUAL_STRING("", name);
    TEST_ASSERT_EQUAL_STRING("", args);
}

void test_split_null_line_does_not_crash() {
    char* name = (char*)"unset";
    char* args = nullptr;
    consoleSplitCommandLine(nullptr, &name, &args);
    TEST_ASSERT_NULL(name);
    TEST_ASSERT_EQUAL_STRING("", args);
}

// =============================================================================
// consoleParseArgs(): the shared key=value tokenizer
// =============================================================================

void test_parse_no_args() {
    char args[] = "";
    ConsoleArgs out = {};
    ConsoleArgParseStatus status = consoleParseArgs(args, &out);
    TEST_ASSERT_EQUAL(CONSOLE_ARGS_PARSE_OK, status);
    TEST_ASSERT_EQUAL_UINT(0u, out.count);
}

void test_parse_null_args_is_ok_with_zero_count() {
    ConsoleArgs out = {};
    ConsoleArgParseStatus status = consoleParseArgs(nullptr, &out);
    TEST_ASSERT_EQUAL(CONSOLE_ARGS_PARSE_OK, status);
    TEST_ASSERT_EQUAL_UINT(0u, out.count);
}

void test_parse_two_simple_pairs() {
    char args[] = "speed=200 steer=0";
    ConsoleArgs out = {};
    TEST_ASSERT_EQUAL(CONSOLE_ARGS_PARSE_OK, consoleParseArgs(args, &out));
    TEST_ASSERT_EQUAL_UINT(2u, out.count);
    TEST_ASSERT_EQUAL_STRING("speed", out.items[0].key);
    TEST_ASSERT_EQUAL_STRING("200", out.items[0].value);
    TEST_ASSERT_EQUAL_STRING("steer", out.items[1].key);
    TEST_ASSERT_EQUAL_STRING("0", out.items[1].value);
}

// Multiple whitespace runs between pairs are skipped (not a fixed single
// space) - same rule as consoleSplitCommandLine().
void test_parse_multiple_spaces_between_pairs() {
    char args[] = "speed=200    steer=0";
    ConsoleArgs out = {};
    TEST_ASSERT_EQUAL(CONSOLE_ARGS_PARSE_OK, consoleParseArgs(args, &out));
    TEST_ASSERT_EQUAL_UINT(2u, out.count);
    TEST_ASSERT_EQUAL_STRING("200", out.items[0].value);
    TEST_ASSERT_EQUAL_STRING("0", out.items[1].value);
}

// Leading/trailing whitespace in the remainder is tolerated.
void test_parse_leading_and_trailing_whitespace() {
    char args[] = "  speed=200  ";
    ConsoleArgs out = {};
    TEST_ASSERT_EQUAL(CONSOLE_ARGS_PARSE_OK, consoleParseArgs(args, &out));
    TEST_ASSERT_EQUAL_UINT(1u, out.count);
    TEST_ASSERT_EQUAL_STRING("speed", out.items[0].key);
    TEST_ASSERT_EQUAL_STRING("200", out.items[0].value);
}

// docs/console-protocol.md s.1.2: double quotes preserve spaces and "="
// inside a value.
void test_parse_quoted_value_preserves_space() {
    char args[] = "sta-ssid=\"Workshop WiFi\"";
    ConsoleArgs out = {};
    TEST_ASSERT_EQUAL(CONSOLE_ARGS_PARSE_OK, consoleParseArgs(args, &out));
    TEST_ASSERT_EQUAL_UINT(1u, out.count);
    TEST_ASSERT_EQUAL_STRING("sta-ssid", out.items[0].key);
    TEST_ASSERT_EQUAL_STRING("Workshop WiFi", out.items[0].value);
}

void test_parse_quoted_value_preserves_equals() {
    char args[] = "key=\"a=b\"";
    ConsoleArgs out = {};
    TEST_ASSERT_EQUAL(CONSOLE_ARGS_PARSE_OK, consoleParseArgs(args, &out));
    TEST_ASSERT_EQUAL_STRING("a=b", out.items[0].value);
}

// An unquoted value's first "=" is the only delimiter; a later "=" stays
// part of the value.
void test_parse_unquoted_value_with_embedded_equals() {
    char args[] = "key=a=b";
    ConsoleArgs out = {};
    TEST_ASSERT_EQUAL(CONSOLE_ARGS_PARSE_OK, consoleParseArgs(args, &out));
    TEST_ASSERT_EQUAL_STRING("a=b", out.items[0].value);
}

// Backslash escapes a quote or a backslash inside quotes (s.1.2).
void test_parse_escaped_quote_and_backslash() {
    char args[] = "key=\"a\\\"b\\\\c\"";  // key="a\"b\\c"
    ConsoleArgs out = {};
    TEST_ASSERT_EQUAL(CONSOLE_ARGS_PARSE_OK, consoleParseArgs(args, &out));
    TEST_ASSERT_EQUAL_STRING("a\"b\\c", out.items[0].value);
}

void test_parse_empty_quoted_value() {
    char args[] = "key=\"\"";
    ConsoleArgs out = {};
    TEST_ASSERT_EQUAL(CONSOLE_ARGS_PARSE_OK, consoleParseArgs(args, &out));
    TEST_ASSERT_EQUAL_UINT(1u, out.count);
    TEST_ASSERT_EQUAL_STRING("", out.items[0].value);
}

void test_parse_bare_empty_value() {
    char args[] = "key=";
    ConsoleArgs out = {};
    TEST_ASSERT_EQUAL(CONSOLE_ARGS_PARSE_OK, consoleParseArgs(args, &out));
    TEST_ASSERT_EQUAL_STRING("", out.items[0].value);
}

// A bare word with no "=" at all is malformed, not a value-less key.
void test_parse_bare_word_is_malformed() {
    char args[] = "notakeyvalue";
    ConsoleArgs out = {};
    TEST_ASSERT_EQUAL(CONSOLE_ARGS_PARSE_MALFORMED, consoleParseArgs(args, &out));
}

void test_parse_empty_key_is_malformed() {
    char args[] = "=value";
    ConsoleArgs out = {};
    TEST_ASSERT_EQUAL(CONSOLE_ARGS_PARSE_MALFORMED, consoleParseArgs(args, &out));
}

void test_parse_unterminated_quote_is_malformed() {
    char args[] = "key=\"unterminated";
    ConsoleArgs out = {};
    TEST_ASSERT_EQUAL(CONSOLE_ARGS_PARSE_MALFORMED, consoleParseArgs(args, &out));
}

// Only \" and \\ are valid escapes inside quotes - anything else after a
// backslash is rejected, not silently kept literal.
void test_parse_bad_escape_is_malformed() {
    char args[] = "key=\"a\\qb\"";
    ConsoleArgs out = {};
    TEST_ASSERT_EQUAL(CONSOLE_ARGS_PARSE_MALFORMED, consoleParseArgs(args, &out));
}

// Junk glued directly onto a closing quote with no separating whitespace.
void test_parse_junk_after_closing_quote_is_malformed() {
    char args[] = "key=\"foo\"bar";
    ConsoleArgs out = {};
    TEST_ASSERT_EQUAL(CONSOLE_ARGS_PARSE_MALFORMED, consoleParseArgs(args, &out));
}

// Invalid UTF-8 inside a quoted value fails explicitly (s.1.3) rather than
// being silently dropped or "fixed". \xC0\x80 is the classic overlong
// 2-byte encoding of NUL - never valid UTF-8.
void test_parse_invalid_utf8_in_quoted_value_is_malformed() {
    char args[] = "key=\"\xC0\x80\"";
    ConsoleArgs out = {};
    TEST_ASSERT_EQUAL(CONSOLE_ARGS_PARSE_MALFORMED, consoleParseArgs(args, &out));
}

// Valid multi-byte UTF-8 (e.g. an accented character) inside a quoted value
// is accepted.
void test_parse_valid_utf8_in_quoted_value_is_accepted() {
    char args[] = "label=\"caf\xC3\xA9\"";  // "café"
    ConsoleArgs out = {};
    TEST_ASSERT_EQUAL(CONSOLE_ARGS_PARSE_OK, consoleParseArgs(args, &out));
    TEST_ASSERT_EQUAL_STRING("caf\xC3\xA9", out.items[0].value);
}

// More pairs than CONSOLE_ARGS_MAX: rejected explicitly, not silently
// truncated to the first N.
void test_parse_too_many_pairs_is_too_many() {
    char args[] = "a=1 b=2 c=3 d=4 e=5 f=6 g=7 h=8 i=9";
    ConsoleArgs out = {};
    TEST_ASSERT_EQUAL(CONSOLE_ARGS_PARSE_TOO_MANY, consoleParseArgs(args, &out));
}

// Exactly CONSOLE_ARGS_MAX pairs is not an error.
void test_parse_exactly_max_pairs_is_ok() {
    char args[] = "a=1 b=2 c=3 d=4 e=5 f=6 g=7 h=8";
    ConsoleArgs out = {};
    TEST_ASSERT_EQUAL(CONSOLE_ARGS_PARSE_OK, consoleParseArgs(args, &out));
    TEST_ASSERT_EQUAL_UINT(8u, out.count);
    TEST_ASSERT_TRUE(8u == CONSOLE_ARGS_MAX);
}

// =============================================================================
// consoleUtf8Valid() in isolation
// =============================================================================

void test_utf8_valid_ascii() {
    TEST_ASSERT_TRUE(consoleUtf8Valid("plain ascii text"));
}

void test_utf8_valid_two_byte_sequence() {
    TEST_ASSERT_TRUE(consoleUtf8Valid("caf\xC3\xA9"));  // café
}

void test_utf8_rejects_overlong_two_byte() {
    // 0xC0/0xC1 lead bytes are always an overlong encoding.
    TEST_ASSERT_FALSE(consoleUtf8Valid("\xC0\x80"));
    TEST_ASSERT_FALSE(consoleUtf8Valid("\xC1\x80"));
}

void test_utf8_rejects_truncated_sequence() {
    TEST_ASSERT_FALSE(consoleUtf8Valid("\xE2\x82"));  // 3-byte lead, only 1 continuation
}

void test_utf8_rejects_surrogate_range() {
    TEST_ASSERT_FALSE(consoleUtf8Valid("\xED\xA0\x80"));  // U+D800, a UTF-16 surrogate
}

void test_utf8_rejects_out_of_range_four_byte() {
    TEST_ASSERT_FALSE(consoleUtf8Valid("\xF4\x90\x80\x80"));  // beyond U+10FFFF
}

void test_utf8_rejects_stray_continuation_byte() {
    TEST_ASSERT_FALSE(consoleUtf8Valid("\x80"));
}

// =============================================================================
// consoleArgsFind() / consoleArgsAsParamSource()
// =============================================================================

void test_args_find_present_and_absent() {
    char args[] = "speed=200 steer=0";
    ConsoleArgs out = {};
    consoleParseArgs(args, &out);
    TEST_ASSERT_EQUAL_STRING("200", consoleArgsFind(out, "speed"));
    TEST_ASSERT_NULL(consoleArgsFind(out, "nope"));
}

// The ConfigParamSource adapter #226 reuses verbatim (docs/console-
// protocol.md "consume the shared parser/request seam") - proves the
// generic seam actually works through that interface, not just through
// consoleArgsFind() directly.
void test_args_as_param_source_reads_through_generic_interface() {
    char args[] = "value=debug";
    ConsoleArgs out = {};
    consoleParseArgs(args, &out);
    ConfigParamSource source = consoleArgsAsParamSource(out);
    TEST_ASSERT_EQUAL_STRING("debug", configParamGet(source, "value"));
    TEST_ASSERT_FALSE(configParamHas(source, "missing"));
    TEST_ASSERT_TRUE(configParamHas(source, "value"));
}

// =============================================================================
// consoleValidateArgsAgainstSchema(): type, range, enum (criterion 2)
// =============================================================================

static const ConsoleParamDescriptor kRangeParams[] = {
    {"speed", CONSOLE_PARAM_TYPE_INT16, true, true, -1000.0, 1000.0, nullptr},
    {"label", CONSOLE_PARAM_TYPE_STRING, false, false, 0.0, 0.0, nullptr},
    {nullptr, nullptr, false, false, 0.0, 0.0, nullptr},
};

static const char* const kPresetValues[] = {"slow", "normal", "turbo", nullptr};
static const ConsoleParamDescriptor kEnumParams[] = {
    {"preset", CONSOLE_PARAM_TYPE_STRING, true, false, 0.0, 0.0, kPresetValues},
    {nullptr, nullptr, false, false, 0.0, 0.0, nullptr},
};

void test_schema_null_params_rejects_any_supplied_key() {
    char args[] = "foo=bar";
    ConsoleArgs out = {};
    consoleParseArgs(args, &out);
    char badKey[40] = {};
    ConsoleArgSchemaStatus status = consoleValidateArgsAgainstSchema(nullptr, out, badKey, sizeof(badKey));
    TEST_ASSERT_EQUAL(CONSOLE_ARG_SCHEMA_UNKNOWN_KEY, status);
    TEST_ASSERT_EQUAL_STRING("foo", badKey);
}

void test_schema_null_params_with_no_args_is_ok() {
    ConsoleArgs out = {};
    TEST_ASSERT_EQUAL(CONSOLE_ARG_SCHEMA_OK, consoleValidateArgsAgainstSchema(nullptr, out, nullptr, 0));
}

void test_schema_unknown_key_is_named() {
    char args[] = "speed=100 unknownKey=x";
    ConsoleArgs out = {};
    consoleParseArgs(args, &out);
    char badKey[40] = {};
    ConsoleArgSchemaStatus status =
        consoleValidateArgsAgainstSchema(kRangeParams, out, badKey, sizeof(badKey));
    TEST_ASSERT_EQUAL(CONSOLE_ARG_SCHEMA_UNKNOWN_KEY, status);
    TEST_ASSERT_EQUAL_STRING("unknownKey", badKey);
}

void test_schema_missing_required_is_named() {
    char args[] = "label=x";  // "speed" required, not supplied
    ConsoleArgs out = {};
    consoleParseArgs(args, &out);
    char badKey[40] = {};
    ConsoleArgSchemaStatus status =
        consoleValidateArgsAgainstSchema(kRangeParams, out, badKey, sizeof(badKey));
    TEST_ASSERT_EQUAL(CONSOLE_ARG_SCHEMA_MISSING_REQUIRED, status);
    TEST_ASSERT_EQUAL_STRING("speed", badKey);
}

void test_schema_optional_param_may_be_absent() {
    char args[] = "speed=100";  // "label" optional, absent - fine
    ConsoleArgs out = {};
    consoleParseArgs(args, &out);
    TEST_ASSERT_EQUAL(CONSOLE_ARG_SCHEMA_OK,
                      consoleValidateArgsAgainstSchema(kRangeParams, out, nullptr, 0));
}

void test_schema_numeric_in_range_is_ok() {
    char args[] = "speed=999";
    ConsoleArgs out = {};
    consoleParseArgs(args, &out);
    TEST_ASSERT_EQUAL(CONSOLE_ARG_SCHEMA_OK,
                      consoleValidateArgsAgainstSchema(kRangeParams, out, nullptr, 0));
}

void test_schema_numeric_out_of_range_is_named() {
    char args[] = "speed=1001";
    ConsoleArgs out = {};
    consoleParseArgs(args, &out);
    char badKey[40] = {};
    ConsoleArgSchemaStatus status =
        consoleValidateArgsAgainstSchema(kRangeParams, out, badKey, sizeof(badKey));
    TEST_ASSERT_EQUAL(CONSOLE_ARG_SCHEMA_OUT_OF_RANGE, status);
    TEST_ASSERT_EQUAL_STRING("speed", badKey);
}

// "200x" must fail, not silently parse as 200 - the whole string must be
// consumed by the numeric parse.
void test_schema_numeric_trailing_garbage_is_out_of_range() {
    char args[] = "speed=200x";
    ConsoleArgs out = {};
    consoleParseArgs(args, &out);
    TEST_ASSERT_EQUAL(CONSOLE_ARG_SCHEMA_OUT_OF_RANGE,
                      consoleValidateArgsAgainstSchema(kRangeParams, out, nullptr, 0));
}

void test_schema_enum_valid_value_is_ok() {
    char args[] = "preset=normal";
    ConsoleArgs out = {};
    consoleParseArgs(args, &out);
    TEST_ASSERT_EQUAL(CONSOLE_ARG_SCHEMA_OK,
                      consoleValidateArgsAgainstSchema(kEnumParams, out, nullptr, 0));
}

void test_schema_enum_invalid_value_is_out_of_range() {
    char args[] = "preset=ludicrous";
    ConsoleArgs out = {};
    consoleParseArgs(args, &out);
    char badKey[40] = {};
    ConsoleArgSchemaStatus status =
        consoleValidateArgsAgainstSchema(kEnumParams, out, badKey, sizeof(badKey));
    TEST_ASSERT_EQUAL(CONSOLE_ARG_SCHEMA_OUT_OF_RANGE, status);
    TEST_ASSERT_EQUAL_STRING("preset", badKey);
}

int main(int, char**) {
    UNITY_BEGIN();

    RUN_TEST(test_split_no_args);
    RUN_TEST(test_split_single_space);
    RUN_TEST(test_split_multiple_spaces_collapsed);
    RUN_TEST(test_split_empty_line);
    RUN_TEST(test_split_null_line_does_not_crash);

    RUN_TEST(test_parse_no_args);
    RUN_TEST(test_parse_null_args_is_ok_with_zero_count);
    RUN_TEST(test_parse_two_simple_pairs);
    RUN_TEST(test_parse_multiple_spaces_between_pairs);
    RUN_TEST(test_parse_leading_and_trailing_whitespace);
    RUN_TEST(test_parse_quoted_value_preserves_space);
    RUN_TEST(test_parse_quoted_value_preserves_equals);
    RUN_TEST(test_parse_unquoted_value_with_embedded_equals);
    RUN_TEST(test_parse_escaped_quote_and_backslash);
    RUN_TEST(test_parse_empty_quoted_value);
    RUN_TEST(test_parse_bare_empty_value);
    RUN_TEST(test_parse_bare_word_is_malformed);
    RUN_TEST(test_parse_empty_key_is_malformed);
    RUN_TEST(test_parse_unterminated_quote_is_malformed);
    RUN_TEST(test_parse_bad_escape_is_malformed);
    RUN_TEST(test_parse_junk_after_closing_quote_is_malformed);
    RUN_TEST(test_parse_invalid_utf8_in_quoted_value_is_malformed);
    RUN_TEST(test_parse_valid_utf8_in_quoted_value_is_accepted);
    RUN_TEST(test_parse_too_many_pairs_is_too_many);
    RUN_TEST(test_parse_exactly_max_pairs_is_ok);

    RUN_TEST(test_utf8_valid_ascii);
    RUN_TEST(test_utf8_valid_two_byte_sequence);
    RUN_TEST(test_utf8_rejects_overlong_two_byte);
    RUN_TEST(test_utf8_rejects_truncated_sequence);
    RUN_TEST(test_utf8_rejects_surrogate_range);
    RUN_TEST(test_utf8_rejects_out_of_range_four_byte);
    RUN_TEST(test_utf8_rejects_stray_continuation_byte);

    RUN_TEST(test_args_find_present_and_absent);
    RUN_TEST(test_args_as_param_source_reads_through_generic_interface);

    RUN_TEST(test_schema_null_params_rejects_any_supplied_key);
    RUN_TEST(test_schema_null_params_with_no_args_is_ok);
    RUN_TEST(test_schema_unknown_key_is_named);
    RUN_TEST(test_schema_missing_required_is_named);
    RUN_TEST(test_schema_optional_param_may_be_absent);
    RUN_TEST(test_schema_numeric_in_range_is_ok);
    RUN_TEST(test_schema_numeric_out_of_range_is_named);
    RUN_TEST(test_schema_numeric_trailing_garbage_is_out_of_range);
    RUN_TEST(test_schema_enum_valid_value_is_ok);
    RUN_TEST(test_schema_enum_invalid_value_is_out_of_range);

    return UNITY_END();
}
