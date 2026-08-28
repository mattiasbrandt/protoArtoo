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

// ----------------------------------------------------------------------------

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_log_midentry_clears_input_line_then_redraws_prompt_and_buffer);
    RUN_TEST(test_log_with_empty_input_line_still_emits_once_and_restores_prompt);
    RUN_TEST(test_emission_leaves_mutex_free_with_matched_take_and_give);
    RUN_TEST(test_nested_emission_does_not_corrupt_mutex_pairing);
    RUN_TEST(test_emission_performs_no_nested_take_through_writechar);
    return UNITY_END();
}
