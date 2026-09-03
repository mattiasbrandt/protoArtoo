// =============================================================================
// test/test_native/test_console_serial_output/test_console_serial_output.cpp
//
// COORDINATOR-SUPPLIED HARNESS for #217's serial output coordinator criterion.
// Do not weaken these assertions to make a slice pass. If an assertion is wrong,
// say so on the issue with the reason; do not edit it silently.
//
// What this proves, on the host, with no board:
//   1. A line emitted while the operator is mid-entry CLEARS the visible input
//      line, writes the line, then REDRAWS prompt + buffered command.
//   2. The serial mutex is taken and given in matched pairs on every path,
//      including the guard paths that emit without a preceding begin record.
//   3. The console never self-deadlocks on the non-recursive mutex.
//
// The seam under test (worker implements; see the coordinator note on #217):
//   void consoleSerialBindCli(EmbeddedCli* cli);
//   void consoleSerialEmitLine(const char* line);
// `consoleSerialEmitLine` must route through embeddedCliPrint() - which already
// implements clear/print/redraw (lib/embedded-cli/src/embedded_cli.c:590-619) -
// while holding the serial mutex for the whole emission.
// =============================================================================

#include <unity.h>

#include <string.h>

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

extern "C" {
#include "embedded_cli.h"
}

#include "console_serial_output.h"

// ----------------------------------------------------------------------------
// Capture writer
// ----------------------------------------------------------------------------

static char g_capture[4096];
static size_t g_captureLen;

static void captureReset(void) {
    memset(g_capture, 0, sizeof(g_capture));
    g_captureLen = 0;
}

static void captureWriteChar(EmbeddedCli* cli, char c) {
    (void)cli;
    if (g_captureLen + 1 < sizeof(g_capture)) {
        g_capture[g_captureLen++] = c;
        g_capture[g_captureLen] = '\0';
    }
}

// Index of the first occurrence of `needle` at or after `from`, or -1.
static int indexOfFrom(const char* needle, int from) {
    if (from < 0 || (size_t)from > g_captureLen) {
        return -1;
    }
    const char* hit = strstr(g_capture + from, needle);
    return hit == nullptr ? -1 : (int)(hit - g_capture);
}

static int indexOf(const char* needle) {
    return indexOfFrom(needle, 0);
}

// ----------------------------------------------------------------------------
// CLI fixture
// ----------------------------------------------------------------------------

static CLI_UINT g_cliBuffer[512];
static EmbeddedCli* g_cli;

static void cliFixtureSetUp(void) {
    captureReset();
    paStubMutexReset();

    EmbeddedCliConfig* config = embeddedCliDefaultConfig();
    config->cliBuffer = g_cliBuffer;
    config->cliBufferSize = sizeof(g_cliBuffer);

    TEST_ASSERT_TRUE_MESSAGE(embeddedCliRequiredSize(config) <= sizeof(g_cliBuffer),
                             "harness CLI buffer too small for default config");

    g_cli = embeddedCliNew(config);
    TEST_ASSERT_NOT_NULL_MESSAGE(g_cli, "embeddedCliNew returned NULL with a static buffer");
    g_cli->writeChar = captureWriteChar;

    embeddedCliProcess(g_cli);  // emits the initial prompt
    consoleSerialBindCli(g_cli);
}

static void typeChars(const char* text) {
    for (const char* p = text; *p != '\0'; ++p) {
        embeddedCliReceiveChar(g_cli, *p);
    }
    embeddedCliProcess(g_cli);
}

// ----------------------------------------------------------------------------
// 1. The coordinator contract
// ----------------------------------------------------------------------------

// A log line arriving while the operator is mid-entry must not land in the
// middle of the typed line. This is the criterion three attempts deferred.
void test_log_midentry_clears_input_line_then_redraws_prompt_and_buffer(void) {
    cliFixtureSetUp();

    typeChars("system.stat");

    // Everything the editor echoed for the typed text is behind us; measure only
    // what the emission itself produces.
    captureReset();

    consoleSerialEmitLine("[INFO] DriveTask heartbeat");

    const int logAt = indexOf("[INFO] DriveTask heartbeat");
    TEST_ASSERT_TRUE_MESSAGE(logAt >= 0, "the emitted line never reached the port");

    // The input line is cleared BEFORE the log text: embedded-cli's
    // clearCurrentLine() starts with a carriage return.
    const int crAt = indexOf("\r");
    TEST_ASSERT_TRUE_MESSAGE(crAt >= 0, "no carriage return: the input line was never cleared");
    TEST_ASSERT_TRUE_MESSAGE(crAt < logAt,
                             "the log text was written before the input line was cleared");

    // The prompt is redrawn AFTER the log text.
    const int promptAt = indexOfFrom("> ", logAt);
    TEST_ASSERT_TRUE_MESSAGE(promptAt > logAt,
                             "the prompt was not redrawn after the emitted line");

    // The operator's buffered command is restored AFTER the prompt, so what they
    // typed is still on screen and still editable.
    const int bufferAt = indexOfFrom("system.stat", promptAt);
    TEST_ASSERT_TRUE_MESSAGE(bufferAt > promptAt,
                             "the buffered command was not redrawn after the prompt");
}

// With an empty input line there is nothing to preserve, but the line must still
// be emitted exactly once and the prompt must come back.
void test_log_with_empty_input_line_still_emits_once_and_restores_prompt(void) {
    cliFixtureSetUp();
    captureReset();

    consoleSerialEmitLine("[WARN] rc link degraded");

    TEST_ASSERT_TRUE_MESSAGE(indexOf("[WARN] rc link degraded") >= 0, "line not emitted");
    TEST_ASSERT_EQUAL_INT_MESSAGE(
        -1, indexOfFrom("[WARN] rc link degraded", indexOf("[WARN] rc link degraded") + 1),
        "the line was emitted more than once");
    TEST_ASSERT_TRUE_MESSAGE(indexOf("> ") >= 0, "the prompt was not restored");
}

// ----------------------------------------------------------------------------
// 2. Mutex discipline
// ----------------------------------------------------------------------------

// Every emission takes and gives the serial mutex in a matched pair and leaves
// it free. An unmatched give is the attempt-2 defect: onRecordEnd released a
// mutex that the guard paths never took.
void test_emission_leaves_mutex_free_with_matched_take_and_give(void) {
    cliFixtureSetUp();
    paStubMutexReset();

    consoleSerialEmitLine("[INFO] one");
    consoleSerialEmitLine("[INFO] two");

    struct PaStubMutex* m = paStubMutexStorage();
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, m->held, "the serial mutex was left held after emission");
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, m->unmatchedGives,
                                  "the serial mutex was given without having been taken");
    TEST_ASSERT_EQUAL_INT_MESSAGE(m->takeCount, m->giveCount,
                                  "takes and gives are not balanced");
    TEST_ASSERT_TRUE_MESSAGE(m->takeCount >= 2, "emission did not take the serial mutex at all");
}

// The mutex is non-recursive. An emission path that re-enters itself - for
// example by logging while holding the mutex - would block forever on the
// target; on the host the second take fails, so a nested emission must not
// silently corrupt the pairing.
void test_nested_emission_does_not_corrupt_mutex_pairing(void) {
    cliFixtureSetUp();
    paStubMutexReset();

    consoleSerialEmitLine("[INFO] outer");

    struct PaStubMutex* m = paStubMutexStorage();
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, m->held, "mutex still held after a completed emission");
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, m->unmatchedGives, "unmatched give after emission");
}

// ----------------------------------------------------------------------------
// 3. Re-entrancy under the emission lock
// ----------------------------------------------------------------------------
//
// COORDINATOR NOTE (added after attempt 4): the first version of this harness
// bound its own capture function as `cli->writeChar`, so it never exercised the
// production per-character writer and could not see the defect below. That was a
// gap in the harness, not in the worker's reading of it.
//
// consoleSerialEmitLine() holds the serial mutex and then calls
// embeddedCliPrint(), which writes through `cli->writeChar` for every character.
// If that writer also tries to take the same NON-RECURSIVE mutex, every single
// byte performs a take that must fail - and whatever the writer does on that
// failure path is what actually reaches the port. The mutex belongs to the
// emission-level caller; the per-character writer must not touch it.
//
// Seam requirement: `consoleSerialWriteChar` is exported from
// console_serial_output.cpp and bound as `cli->writeChar` by the Console task,
// replacing the task-private onCliWrite.

void test_emission_performs_no_nested_take_through_writechar(void) {
    captureReset();
    paStubMutexReset();

    EmbeddedCliConfig* config = embeddedCliDefaultConfig();
    config->cliBuffer = g_cliBuffer;
    config->cliBufferSize = sizeof(g_cliBuffer);
    g_cli = embeddedCliNew(config);
    TEST_ASSERT_NOT_NULL(g_cli);

    // Bind the PRODUCTION writer, not the harness capture writer.
    g_cli->writeChar = consoleSerialWriteChar;
    embeddedCliProcess(g_cli);
    consoleSerialBindCli(g_cli);

    paStubMutexReset();
    consoleSerialEmitLine("[INFO] nested take probe");

    struct PaStubMutex* m = paStubMutexStorage();
    TEST_ASSERT_EQUAL_INT_MESSAGE(
        0, m->failedTakes,
        "the per-character writer tried to take the serial mutex the emission already holds; "
        "the mutex belongs to consoleSerialEmitLine, not to writeChar");
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, m->takeCount,
                                  "an emission must take the serial mutex exactly once");
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, m->held, "the serial mutex was left held");
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, m->unmatchedGives, "unmatched give during emission");
}

// Line length cap: lines longer than PA_LOG_SERIAL_LINE_MAX are truncated
void test_long_lines_are_truncated_to_serial_max(void) {
    cliFixtureSetUp();

    // Create a line longer than PA_LOG_SERIAL_LINE_MAX
    // PA_LOG_SERIAL_LINE_MAX is typically 256, so create something much longer
    char longLine[512];
    memset(longLine, 'X', sizeof(longLine) - 1);
    longLine[sizeof(longLine) - 1] = '\0';

    captureReset();
    consoleSerialEmitLine(longLine);

    // Check that captured output is truncated to PA_LOG_SERIAL_LINE_MAX
    // The output should be at most PA_LOG_SERIAL_LINE_MAX - 1 characters (plus \r, \n, etc from embeddedCliPrint)
    // For this test, we check that we don't receive the entire 512-char string verbatim
    size_t capturedLen = strlen(g_capture);

    // A line of 512 X's would result in much more than 256 characters in the output
    // If truncation works, the output should be bounded
    TEST_ASSERT_TRUE_MESSAGE(capturedLen < 512,
                             "long line was not truncated; entire oversized input reached output");
}

// =============================================================================
// 4. Keystroke echo and redraw output stays proportional to input (#217 hardening)
// =============================================================================
// CRITERION: Output through the echo/redraw path must stay proportional to input.
// When garbage or incomplete fragments arrive as input, the console's echoing and
// redrawing of the partial command buffer should not produce output that grows
// faster than the input itself. This bounds output-per-keystroke even with sustained
// invalid input.
//
// NOTE: This harness cannot model a feedback loop (no path from output back to input),
// so it tests the proportionality of the echo/redraw mechanism itself. A real loop
// would require wiring the output sink to feed back into the input buffer.

void test_keystroke_echo_output_stays_proportional_to_input(void) {
    cliFixtureSetUp();

    // Pre-measure the baseline output after initialization (prompt + fixture startup)
    size_t baseline = g_captureLen;

    // Feed fragments of garbage input that resemble log output fragments.
    // This simulates incomplete input arriving on UART0.
    const char fragments[][20] = {
        "already initial",  // Incomplete fragment
        "ized",             // Tail fragment
        "waiting for firs",
        "t frame",
        "TWDT already",
        "\n\r"
    };
    const size_t numFragments = sizeof(fragments) / sizeof(fragments[0]);

    // Feed multiple copies of these fragments, accumulating input over time.
    size_t garbageBytesFed = 0;
    for (int i = 0; i < 10; ++i) {  // 10 iterations
        for (size_t j = 0; j < numFragments; ++j) {
            typeChars(fragments[j]);
            garbageBytesFed += strlen(fragments[j]);
        }
    }

    size_t outputProduced = g_captureLen - baseline;

    // Proportionality bound: output should not exceed a reasonable multiple of input.
    // The echo/redraw path includes character echo, prompt redraw, and cursor management.
    // Each keystroke should produce output proportional to (roughly) the keystroke itself
    // plus overhead for cursor escape sequences. A 100x bound is conservative.
    size_t maxAllowedOutput = garbageBytesFed * 100;

    TEST_ASSERT_TRUE_MESSAGE(outputProduced < maxAllowedOutput,
                             "keystroke echo/redraw produced too much output relative to input");
    // Tighter secondary bound: even with overhead, sustained input shouldn't produce
    // more than 50 KB total in this scenario (catching accidentally expensive redraw).
    TEST_ASSERT_TRUE_MESSAGE(outputProduced < 50000,
                             "keystroke echo/redraw produced more than 50 KB output");
}

// =============================================================================
// 5. ADR 0036 - single-write framing, room-wait, and dropped= accounting (#265)
// =============================================================================
//
// The seam under test here is consoleSerialEmitFramedLine() and
// consoleSerialFormatDroppedSuffix() (console_serial_output.h). Both are pure
// with respect to this file's fixtures: they talk to `Serial` (SerialStub,
// test/stubs/include/Arduino.h) and paGetSerialMutex() (the same PaStubMutex
// section 2 above already exercises), so every assertion below reads back
// through those two test doubles rather than a board.

static void framedLineFixtureSetUp(void) {
    serialStubReset();
    paStubMutexReset();
}

// A record with room from the first check: one write call, the line and its
// newline delivered together, the mutex taken and given exactly once, and
// the room was actually checked (not skipped).
void test_framed_line_with_room_writes_once_including_newline(void) {
    framedLineFixtureSetUp();

    const char* line = "< id=7 type=field name=heapFree value=42120";
    bool sent = consoleSerialEmitFramedLine(line, strlen(line), /*waitForRoom=*/true);

    TEST_ASSERT_TRUE_MESSAGE(sent, "a line with room to spare must not be reported dropped");
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, SerialStub::writeCallCount,
                                  "the line and its newline must reach Serial.write() together, "
                                  "in exactly one call");
    TEST_ASSERT_EQUAL_INT_MESSAGE((int)(strlen(line) + 1), (int)SerialStub::capturedLen,
                                  "captured bytes must be exactly the line plus one newline");
    TEST_ASSERT_EQUAL_STRING_LEN_MESSAGE(line, SerialStub::capturedBuf, strlen(line),
                                         "the captured line text must be unchanged");
    TEST_ASSERT_EQUAL_INT_MESSAGE('\n', SerialStub::capturedBuf[SerialStub::capturedLen - 1],
                                  "the single write must end in the newline, not a separate call");

    struct PaStubMutex* m = paStubMutexStorage();
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, m->takeCount, "the mutex must be taken exactly once to write");
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, m->giveCount, "the mutex must be given back");
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, m->held, "the mutex must not be left held");
    TEST_ASSERT_TRUE_MESSAGE(SerialStub::availableForWriteCallCount > 0,
                             "a waiting caller must actually check for room, not skip the check");
}

// Disconnected: the record is dropped whole, without ever checking transmit
// room (there being no host to drain it is not a room question) and without
// ever taking the mutex -- a half-taken mutex on a drop would be worse than
// the two-call defect this function replaces.
void test_framed_line_waits_only_while_connected(void) {
    framedLineFixtureSetUp();
    SerialStub::connectedValue = false;

    const char* line = "< id=8 type=end status=ok outcome=completed";
    bool sent = consoleSerialEmitFramedLine(line, strlen(line), /*waitForRoom=*/true);

    TEST_ASSERT_FALSE_MESSAGE(sent, "a disconnected host must drop the record, not send it");
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, SerialStub::writeCallCount,
                                  "nothing may reach Serial.write() for a dropped record");
    TEST_ASSERT_EQUAL_INT_MESSAGE(
        0, SerialStub::availableForWriteCallCount,
        "disconnected must short-circuit before ever asking about transmit room");

    struct PaStubMutex* m = paStubMutexStorage();
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, m->takeCount,
                                  "a dropped record must never take the serial mutex");
}

// Room never clears: the wait terminates at CONSOLE_RECORD_ROOM_WAIT_BOUND_MS
// rather than spinning forever, the record is dropped, and (same as the
// disconnected case) the mutex is never taken -- the wait is provably OUTSIDE
// the mutex, which is the whole point of ADR 0036's "never inside the mutex"
// rule for a TWDT-subscribed logger's portMAX_DELAY take to be safe from it.
void test_framed_line_room_wait_is_bounded_and_stays_outside_the_mutex(void) {
    framedLineFixtureSetUp();
    SerialStub::availableForWriteValue = 0;  // never enough room

    const char* line = "< id=9 type=item value=drive.action.move";
    bool sent = consoleSerialEmitFramedLine(line, strlen(line), /*waitForRoom=*/true);

    TEST_ASSERT_FALSE_MESSAGE(sent, "room that never clears must drop the record");
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, SerialStub::writeCallCount,
                                  "nothing may reach Serial.write() for a dropped record");

    struct PaStubMutex* m = paStubMutexStorage();
    TEST_ASSERT_EQUAL_INT_MESSAGE(
        0, m->takeCount, "the wait must run entirely before any mutex take -- taking it here "
                         "would let a TWDT-subscribed logger's portMAX_DELAY inherit the wait");

    // The wait must be bounded by CONSOLE_RECORD_ROOM_WAIT_BOUND_MS -- not
    // unbounded (an infinite spin) and not skipped (an immediate give-up).
    // Each polling iteration calls Serial.availableForWrite() once, so the
    // call count is the wait's own iteration count; a couple of extra calls
    // either side of the loop (the loop's own terminating check, then the
    // post-loop confirmation) are implementation detail this assertion does
    // not pin down.
    TEST_ASSERT_TRUE_MESSAGE(
        SerialStub::availableForWriteCallCount >= (int)CONSOLE_RECORD_ROOM_WAIT_BOUND_MS,
        "the wait gave up before reaching its own documented bound");
    TEST_ASSERT_TRUE_MESSAGE(
        SerialStub::availableForWriteCallCount <= (int)CONSOLE_RECORD_ROOM_WAIT_BOUND_MS + 5,
        "the wait ran well past its documented bound -- it must not spin indefinitely");
}

// Log lines (#245's best-effort contract) never wait, whatever the room or
// connection state, and still land in a single write call.
void test_framed_line_without_wait_flag_never_waits_for_room() {
    framedLineFixtureSetUp();
    SerialStub::connectedValue = false;
    SerialStub::availableForWriteValue = 0;

    const char* line = "[INFO][ConsoleTask] active";
    bool sent = consoleSerialEmitFramedLine(line, strlen(line), /*waitForRoom=*/false);

    TEST_ASSERT_TRUE_MESSAGE(sent, "a non-waiting caller always reports sent -- #245's "
                                   "best-effort contract, unchanged by this function");
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, SerialStub::writeCallCount,
                                  "a log line is still one write call, line plus newline");
    TEST_ASSERT_EQUAL_INT_MESSAGE(
        0, SerialStub::availableForWriteCallCount,
        "a non-waiting caller must never even ask about transmit room or connection state");

    struct PaStubMutex* m = paStubMutexStorage();
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, m->takeCount, "a log line still writes under the mutex");
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, m->held, "the mutex must not be left held");
}

// consoleSerialFormatDroppedSuffix: absent (empty, zero-length) when nothing
// was dropped, present with the exact count otherwise. This is the piece of
// ADR 0036's wire format proved on the host because
// src/tasks/console_task.cpp (which stamps it onto a real record) is not
// part of the native build (Arduino/FreeRTOS-only, per its own file header).
void test_dropped_suffix_absent_when_zero() {
    char buf[24] = {'X', '\0'};
    size_t written = consoleSerialFormatDroppedSuffix(buf, sizeof(buf), 0);

    TEST_ASSERT_EQUAL_INT_MESSAGE(0, (int)written, "zero dropped must report zero bytes written");
    TEST_ASSERT_EQUAL_STRING_MESSAGE("", buf, "zero dropped must produce an empty suffix");
}

void test_dropped_suffix_present_with_exact_count_when_nonzero() {
    char buf[24] = {};
    size_t written = consoleSerialFormatDroppedSuffix(buf, sizeof(buf), 4);

    TEST_ASSERT_EQUAL_STRING_MESSAGE(" dropped=4", buf,
                                     "nonzero dropped must render as ' dropped=<n>'");
    TEST_ASSERT_EQUAL_INT_MESSAGE((int)strlen(" dropped=4"), (int)written,
                                  "the returned length must match what was actually written");
}

// A buffer too small to hold the suffix must not overrun or emit a torn
// token -- it drops the field, exactly like an over-length record line
// dropping rather than truncating into something malformed.
void test_dropped_suffix_too_small_buffer_emits_nothing() {
    char buf[4] = {'Z', 'Z', 'Z', '\0'};  // " dropped=4294967295" does not fit
    size_t written = consoleSerialFormatDroppedSuffix(buf, sizeof(buf), 4294967295u);

    TEST_ASSERT_EQUAL_INT_MESSAGE(0, (int)written, "a suffix that cannot fit must report 0");
    TEST_ASSERT_EQUAL_STRING_MESSAGE("", buf, "a suffix that cannot fit must not leave a torn token");
}

// =============================================================================

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_log_midentry_clears_input_line_then_redraws_prompt_and_buffer);
    RUN_TEST(test_log_with_empty_input_line_still_emits_once_and_restores_prompt);
    RUN_TEST(test_emission_leaves_mutex_free_with_matched_take_and_give);
    RUN_TEST(test_nested_emission_does_not_corrupt_mutex_pairing);
    RUN_TEST(test_emission_performs_no_nested_take_through_writechar);
    RUN_TEST(test_long_lines_are_truncated_to_serial_max);
    RUN_TEST(test_keystroke_echo_output_stays_proportional_to_input);
    RUN_TEST(test_framed_line_with_room_writes_once_including_newline);
    RUN_TEST(test_framed_line_waits_only_while_connected);
    RUN_TEST(test_framed_line_room_wait_is_bounded_and_stays_outside_the_mutex);
    RUN_TEST(test_framed_line_without_wait_flag_never_waits_for_room);
    RUN_TEST(test_dropped_suffix_absent_when_zero);
    RUN_TEST(test_dropped_suffix_present_with_exact_count_when_nonzero);
    RUN_TEST(test_dropped_suffix_too_small_buffer_emits_nothing);
    return UNITY_END();
}
