// =============================================================================
// test/test_native/test_console_cli_line/test_console_cli_line.cpp
//
// #219 R2 regression coverage: `operations type=<t>` did not filter on the
// serial adapter because onCliCommand() (src/tasks/console_task.cpp) only
// reconstructed embedded-cli's split name/args pair into one command line
// for "help" - every other command, including "operations", reached
// consoleExecuteCommand() with its arguments silently dropped. A native test
// against consoleExecuteCommand() alone could not have caught this: it
// passes the already-combined string by construction, never the split pair
// an adapter actually hands it. That is the "verified through the module,
// never through the adapter" class of defect this ticket keeps producing.
//
// This test closes that gap the way it is actually closable natively:
// src/tasks/console_task.cpp itself is Arduino-only (unconditional
// <Arduino.h>/FreeRTOS includes elsewhere in that file), so it cannot be
// linked into a native test binary. The reconstruction logic was pulled out
// into consoleBuildCommandLine() (src/console/console_cli_line.cpp) - pure
// string manipulation, no Arduino/FreeRTOS dependency - and onCliCommand()
// now just calls it. This test feeds real command lines through a REAL
// embedded-cli instance (the actual parser the serial adapter runs, already
// native-testable - see test_cli_utf8_ingestion et al.), through THAT SAME
// consoleBuildCommandLine() function, into the real consoleExecuteCommand().
// The only thing genuinely untestable off-device is the Arduino-specific
// plumbing around it (Serial output, the request-id counter, the serial
// mutex) - none of which is where R2 lived.
// =============================================================================

#include <unity.h>

#include <cstring>

extern "C" {
#include "embedded_cli.h"
}

#include "console_cli_line.h"
#include "console_module.h"
#include "console_catalog.h"

// -----------------------------------------------------------------------------
// Recording sink - same shape as the other native console tests.
// -----------------------------------------------------------------------------
static int g_itemCount = 0;
static int g_beginCount = 0;
static int g_endCount = 0;
static int g_resultCount = 0;
static ConsoleStatus g_lastResultStatus;
static ConsoleOutcome g_lastResultOutcome;
static ConsoleReason g_lastResultReason;

static void capBegin(uint32_t, const char*) { g_beginCount++; }
static void capField(uint32_t, const char*, const char*) {}
static void capItem(uint32_t, const char*) { g_itemCount++; }
static void capResult(uint32_t, ConsoleStatus status, ConsoleOutcome outcome, ConsoleReason reason) {
    g_resultCount++;
    g_lastResultStatus = status;
    g_lastResultOutcome = outcome;
    g_lastResultReason = reason;
}
static void capEnd(uint32_t, ConsoleStatus, ConsoleOutcome, ConsoleReason) { g_endCount++; }

static void resetCapture() {
    g_itemCount = 0;
    g_beginCount = 0;
    g_endCount = 0;
    g_resultCount = 0;
}

// -----------------------------------------------------------------------------
// The onCommand callback under test: mirrors onCliCommand()'s post-parse
// logic exactly (reconstruct via consoleBuildCommandLine(), then execute) -
// everything console_task.cpp's real onCliCommand() does MINUS the
// Arduino-only parts (Serial emission, the serial mutex, the shared request
// id counter). This is the "adapter path" this test exercises: real
// embedded-cli parsing feeds this, not a hand-built already-combined string.
// -----------------------------------------------------------------------------
static void adapterOnCommand(EmbeddedCli* cli, CliCommand* cmd) {
    (void)cli;
    if (cmd == nullptr || cmd->name == nullptr) {
        return;
    }

    static char operationNameBuf[128];
    const char* operationName =
        consoleBuildCommandLine(cmd->name, cmd->args, operationNameBuf, sizeof(operationNameBuf));

    ConsoleRecordSink sink = {};
    sink.onRecordBegin = capBegin;
    sink.onRecordField = capField;
    sink.onRecordItem = capItem;
    sink.onRecordResult = capResult;
    sink.onRecordEnd = capEnd;

    ConsoleRequest req = {};
    req.requestId = 1;
    req.source = CONSOLE_SOURCE_SERIAL;
    req.operationName = operationName;
    consoleExecuteCommand(&req, &sink);
}

static void writeCharNoop(EmbeddedCli* cli, char c) {
    (void)cli;
    (void)c;
}

// Types a full line through a real EmbeddedCli instance (the same parser
// console_task.cpp drives) and lets it dispatch to adapterOnCommand().
static void typeLine(const char* line) {
    static CLI_UINT cliBuffer[4096 / sizeof(CLI_UINT)];

    EmbeddedCliConfig config = {
        .invitation = "> ",
        .rxBufferSize = 256,
        .cmdBufferSize = 256,
        .historyBufferSize = 256,
        .maxBindingCount = 10,
        .cliBuffer = cliBuffer,
        .cliBufferSize = sizeof(cliBuffer),
        .enableAutoComplete = false,
    };

    EmbeddedCli* cli = embeddedCliNew(&config);
    TEST_ASSERT_NOT_NULL(cli);
    cli->onCommand = adapterOnCommand;
    cli->writeChar = writeCharNoop;

    for (const char* p = line; *p != '\0'; ++p) {
        embeddedCliReceiveChar(cli, *p);
    }
    embeddedCliReceiveChar(cli, '\r');
    embeddedCliProcess(cli);

    embeddedCliFree(cli);
}

void setUp() { resetCapture(); }
void tearDown() {}

// -----------------------------------------------------------------------------
// The regression: "operations type=<t>" typed as one line, through the real
// parser and the real reconstruction function, must actually filter.
// Catalog totals (docs/action-registry.yaml, confirmed against
// test_console_catalog.cpp's exact-190 count): action 128, config 33,
// event 15, status 14.
// -----------------------------------------------------------------------------

void test_operations_type_action_filters_through_the_real_adapter_path() {
    typeLine("operations type=action");

    TEST_ASSERT_EQUAL_INT(1, g_beginCount);
    TEST_ASSERT_EQUAL_INT(1, g_endCount);
    TEST_ASSERT_EQUAL_INT(0, g_resultCount);
    TEST_ASSERT_EQUAL_INT_MESSAGE(128, g_itemCount,
        "operations type=action must list exactly the 128 action entries when "
        "typed as one line through the real embedded-cli parser and "
        "consoleBuildCommandLine() - not when the module is called directly "
        "with a hand-built \"operations type=action\" string");
}

void test_operations_type_event_filters_through_the_real_adapter_path() {
    typeLine("operations type=event");

    TEST_ASSERT_EQUAL_INT(1, g_beginCount);
    TEST_ASSERT_EQUAL_INT(1, g_endCount);
    TEST_ASSERT_EQUAL_INT(0, g_resultCount);
    TEST_ASSERT_EQUAL_INT(15, g_itemCount);
}

void test_operations_type_nonsense_is_invalid_through_the_real_adapter_path() {
    typeLine("operations type=nonsense");

    TEST_ASSERT_EQUAL_INT(0, g_beginCount);
    TEST_ASSERT_EQUAL_INT(0, g_endCount);
    TEST_ASSERT_EQUAL_INT(0, g_itemCount);
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, g_resultCount,
        "an unrecognized type= filter must answer one type=result record, "
        "not list everything");
    TEST_ASSERT_EQUAL_INT(CONSOLE_STATUS_ERR, g_lastResultStatus);
    TEST_ASSERT_EQUAL_INT(CONSOLE_OUTCOME_INVALID, g_lastResultOutcome);
    TEST_ASSERT_EQUAL_INT(CONSOLE_REASON_OUT_OF_RANGE, g_lastResultReason);
}

// Unfiltered `operations` (no args) must still work - proves the fix did not
// regress the no-argument meta-command form.
void test_bare_operations_still_lists_everything_through_the_real_adapter_path() {
    typeLine("operations");

    TEST_ASSERT_EQUAL_INT(1, g_beginCount);
    TEST_ASSERT_EQUAL_INT(1, g_endCount);
    TEST_ASSERT_EQUAL_INT(0, g_resultCount);
    TEST_ASSERT_EQUAL_INT(190, g_itemCount);
}

// help <op> must still work through the same real path (the reconstruction
// function serves both meta-commands; this proves the shared function did
// not regress the "help" case while fixing "operations").
void test_help_with_operation_still_works_through_the_real_adapter_path() {
    typeLine("help drive.action.move");

    TEST_ASSERT_EQUAL_INT(1, g_beginCount);
    TEST_ASSERT_EQUAL_INT(1, g_endCount);
    TEST_ASSERT_EQUAL_INT(0, g_resultCount);
}

// -----------------------------------------------------------------------------
// The reconstruction function in isolation: the scope fence itself.
// -----------------------------------------------------------------------------

void test_registry_entry_command_arguments_are_not_forwarded() {
    // #221/#226 own this contract - consoleBuildCommandLine() must not widen
    // reconstruction to non-meta-commands on its own.
    char buf[128];
    const char* result =
        consoleBuildCommandLine("drive.action.move", "speed=200 steer=0", buf, sizeof(buf));
    TEST_ASSERT_EQUAL_STRING("drive.action.move", result);
}

void test_meta_command_with_no_args_is_unchanged() {
    char buf[128];
    const char* result = consoleBuildCommandLine("operations", nullptr, buf, sizeof(buf));
    TEST_ASSERT_EQUAL_STRING("operations", result);
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_operations_type_action_filters_through_the_real_adapter_path);
    RUN_TEST(test_operations_type_event_filters_through_the_real_adapter_path);
    RUN_TEST(test_operations_type_nonsense_is_invalid_through_the_real_adapter_path);
    RUN_TEST(test_bare_operations_still_lists_everything_through_the_real_adapter_path);
    RUN_TEST(test_help_with_operation_still_works_through_the_real_adapter_path);
    RUN_TEST(test_registry_entry_command_arguments_are_not_forwarded);
    RUN_TEST(test_meta_command_with_no_args_is_unchanged);
    return UNITY_END();
}
