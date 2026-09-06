/**
 * Test: the serial adapter's answer to an over-length input line (#262).
 *
 * lib/embedded-cli's Patch 7 refuses the line; include/console_line_overflow.h
 * turns that refusal into a Console Record. This covers the second half and
 * the join between them: a real EmbeddedCli, wired exactly the way
 * src/tasks/console_task.cpp wires it, is typed past its command buffer, and
 * the record that comes out is compared against the record
 * src/web/api_console.cpp emits for the same failure.
 *
 * WHY THE COMPARISON IS THE POINT. The two adapters disagreeing about a
 * failure is the defect this ticket closes, so asserting "serial says
 * line-too-long" on its own would not be enough - it has to be the SAME
 * answer, in the same shape. The browser's is one `type=result status=err
 * outcome=invalid reason=line-too-long` record with no begin/end bracket
 * (api_console.cpp's over-length branch, which calls
 * webOnRecordResult_impl() and nothing else), and that is what these tests
 * pin.
 *
 * src/tasks/console_task.cpp itself is not in [env:native]'s
 * build_src_filter and cannot be (that file is fenced), so its callback is
 * mirrored here: two lines, building the sink and calling
 * consoleEmitLineTooLong(). Everything under those two lines - the library's
 * refusal, the record contents, the reason token's spelling on the wire - is
 * the real production code.
 */

#include <unity.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>

#include "console_module.h"
#include "console_record.h"
#include "console_line_overflow.h"

extern "C" {
#include "embedded_cli.h"
}

static const uint16_t kCmdBufferSize = 32;
static const int kUsableChars = kCmdBufferSize - 2;

// Everything the sink saw, in order, rendered the way console_task.cpp
// renders it onto the wire.
static char capturedLines[8][160];
static int capturedCount = 0;
static int commandCount = 0;

static void capture(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
static void capture(const char* fmt, ...) {
    if (capturedCount >= (int)(sizeof(capturedLines) / sizeof(capturedLines[0]))) {
        return;
    }
    va_list args;
    va_start(args, fmt);
    vsnprintf(capturedLines[capturedCount], sizeof(capturedLines[0]), fmt, args);
    va_end(args);
    capturedCount++;
}

static void onBegin(uint32_t requestId, const char* operationType) {
    capture("id=%lu type=begin operation=%s", (unsigned long)requestId, operationType);
}

static void onField(uint32_t requestId, const char* name, const char* value) {
    capture("id=%lu type=field name=%s value=%s", (unsigned long)requestId, name, value);
}

static void onItem(uint32_t requestId, const char* value) {
    capture("id=%lu type=item value=%s", (unsigned long)requestId, value);
}

// The same rendering src/tasks/console_task.cpp's onRecordResult() does,
// including "reason= is present exactly when there is a reason".
static void onResult(uint32_t requestId, ConsoleStatus status, ConsoleOutcome outcome,
                     ConsoleReason reason) {
    char reasonStr[64] = {};
    if (consoleReasonIsPresent(reason)) {
        snprintf(reasonStr, sizeof(reasonStr), " reason=%s", consoleReasonString(reason));
    }
    capture("id=%lu type=result status=%s outcome=%s%s", (unsigned long)requestId,
            consoleStatusString(status), consoleOutcomeString(outcome), reasonStr);
}

static void onEnd(uint32_t requestId, ConsoleStatus status, ConsoleOutcome outcome,
                  ConsoleReason reason) {
    char reasonStr[64] = {};
    if (consoleReasonIsPresent(reason)) {
        snprintf(reasonStr, sizeof(reasonStr), " reason=%s", consoleReasonString(reason));
    }
    capture("id=%lu type=end status=%s outcome=%s%s", (unsigned long)requestId,
            consoleStatusString(status), consoleOutcomeString(outcome), reasonStr);
}

static ConsoleRecordSink testSink() {
    ConsoleRecordSink sink = {
        .onRecordBegin = onBegin,
        .onRecordField = onField,
        .onRecordItem = onItem,
        .onRecordResult = onResult,
        .onRecordEnd = onEnd,
    };
    return sink;
}

// The two lines src/tasks/console_task.cpp's onCliLineTooLong() runs.
static void onCliLineTooLong(EmbeddedCli* cli) {
    (void)cli;
    ConsoleRecordSink sink = testSink();
    consoleEmitLineTooLong(&sink);
}

static void onCliCommand(EmbeddedCli* cli, CliCommand* cmd) {
    (void)cli;
    (void)cmd;
    commandCount++;
}

static void writeChar(EmbeddedCli* cli, char c) {
    (void)cli;
    (void)c;
}

void setUp(void) {
    capturedCount = 0;
    commandCount = 0;
    memset(capturedLines, 0, sizeof(capturedLines));
}

void tearDown(void) {
}

static EmbeddedCli* newWiredCli(CLI_UINT* buffer, size_t bufferBytes) {
    EmbeddedCliConfig config = {
        .invitation = "> ",
        .rxBufferSize = 64,
        .cmdBufferSize = kCmdBufferSize,
        .historyBufferSize = 128,
        .maxBindingCount = 4,
        .cliBuffer = buffer,
        .cliBufferSize = (uint16_t)bufferBytes,
        .enableAutoComplete = false,
    };
    EmbeddedCli* cli = embeddedCliNew(&config);
    TEST_ASSERT_NOT_NULL(cli);
    cli->writeChar = writeChar;
    cli->onCommand = onCliCommand;
    cli->onLineTooLong = onCliLineTooLong;
    return cli;
}

static void type(EmbeddedCli* cli, char c) {
    embeddedCliReceiveChar(cli, c);
    embeddedCliProcess(cli);
}

// Strip the request ID, which is a running global and therefore not a fixed
// string, so the rest of the record can be compared literally.
static const char* afterId(const char* line) {
    const char* space = strchr(line, ' ');
    return space != nullptr ? space + 1 : line;
}

/**
 * The answer, end to end: type past the buffer, press Enter, and exactly one
 * record comes back - the browser adapter's record, verbatim.
 *
 * Removing `embeddedCli->onLineTooLong = onCliLineTooLong` from
 * console_task.cpp is modelled by not wiring the callback here, which is what
 * test_no_callback_still_refuses_the_line covers; reverting Patch 7 itself
 * turns this red with zero captured records and commandCount 1.
 */
void test_overlong_line_answers_exactly_the_browser_adapters_record(void) {
    static CLI_UINT buffer[1024 / sizeof(CLI_UINT)];
    EmbeddedCli* cli = newWiredCli(buffer, sizeof(buffer));

    for (int i = 0; i < kUsableChars + 8; ++i) {
        type(cli, 'x');
    }
    type(cli, '\r');

    TEST_ASSERT_EQUAL_INT_MESSAGE(0, commandCount, "the truncated line executed");
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, capturedCount,
                                  "an over-length line did not produce exactly one record");
    TEST_ASSERT_EQUAL_STRING_MESSAGE("type=result status=err outcome=invalid reason=line-too-long",
                                     afterId(capturedLines[0]),
                                     "the serial refusal is not the browser adapter's record");

    embeddedCliFree(cli);
}

/**
 * The refusal carries a request ID from the same global sequence every other
 * request draws from, so an operator (or a script) reading the stream can
 * attribute it. A refused line never reaches consoleExecuteCommand(), so the
 * ID has to be allocated by the answer itself - if it were not, this record
 * would be the one on the wire with no identity.
 */
void test_refusal_carries_a_request_id_from_the_shared_sequence(void) {
    static CLI_UINT buffer[1024 / sizeof(CLI_UINT)];
    EmbeddedCli* cli = newWiredCli(buffer, sizeof(buffer));

    uint32_t before = consoleGetNextRequestId();

    for (int i = 0; i < kUsableChars + 1; ++i) {
        type(cli, 'y');
    }
    type(cli, '\r');

    uint32_t after = consoleGetNextRequestId();

    TEST_ASSERT_EQUAL_INT(1, capturedCount);
    unsigned long refusalId = 0;
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, sscanf(capturedLines[0], "id=%lu ", &refusalId),
                                  "the refusal record carried no id= field");
    TEST_ASSERT_TRUE_MESSAGE(refusalId > before && refusalId < after,
                             "the refusal's id did not come from the shared request sequence");

    embeddedCliFree(cli);
}

/**
 * A line that fits produces no refusal record at all - the ordinary dispatch
 * path is untouched, and nothing extra appears on the wire in front of it.
 */
void test_line_that_fits_produces_no_refusal_record(void) {
    static CLI_UINT buffer[1024 / sizeof(CLI_UINT)];
    EmbeddedCli* cli = newWiredCli(buffer, sizeof(buffer));

    const char* cmd = "system.status.health";
    for (const char* p = cmd; *p != '\0'; ++p) {
        type(cli, *p);
    }
    type(cli, '\r');

    TEST_ASSERT_EQUAL_INT_MESSAGE(1, commandCount, "a line that fits was not dispatched");
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, capturedCount,
                                  "a line that fits produced a refusal record");

    embeddedCliFree(cli);
}

/**
 * With no callback wired the line is still refused - the discard is the
 * library's safety property, not a consequence of anyone listening - and
 * nothing is emitted, because there is no one to emit through.
 */
void test_no_callback_still_refuses_the_line(void) {
    static CLI_UINT buffer[1024 / sizeof(CLI_UINT)];
    EmbeddedCli* cli = newWiredCli(buffer, sizeof(buffer));
    cli->onLineTooLong = nullptr;

    for (int i = 0; i < kUsableChars + 4; ++i) {
        type(cli, 'z');
    }
    type(cli, '\r');

    TEST_ASSERT_EQUAL_INT_MESSAGE(0, commandCount,
                                  "the truncated line executed with no callback wired");
    TEST_ASSERT_EQUAL_INT(0, capturedCount);

    embeddedCliFree(cli);
}

/**
 * A sink with no result writer must not crash the emitter. The serial adapter
 * always supplies one, but the header is a public seam and a partial sink is
 * a legal shape elsewhere in this module (src/web/api_console.cpp's streaming
 * path leaves onRecordField null).
 */
void test_emitter_tolerates_a_sink_without_a_result_writer(void) {
    ConsoleRecordSink sink = testSink();
    sink.onRecordResult = nullptr;

    consoleEmitLineTooLong(&sink);
    consoleEmitLineTooLong(nullptr);

    TEST_ASSERT_EQUAL_INT(0, capturedCount);
}

/**
 * Patch 7's cost to the caller still fits the caller's fixed buffer.
 *
 * The new onLineTooLong pointer lives in `struct EmbeddedCli`, which the
 * CALLER allocates - src/tasks/console_task.cpp's `CLI_UINT
 * embeddedCliBuffer[512]`, 2048 bytes on both firmware targets, where
 * CLI_UINT is uint32_t. embeddedCliRequiredSize() therefore grows by one
 * pointer, and console_task.cpp checks it at init and DELETES ITS OWN TASK if
 * it does not fit: overrunning this is not a compile error or a crash, it is
 * a board that boots with no serial Console and one log line saying so.
 *
 * The host is a conservative place to check it. Native pointers are 8 bytes
 * against the targets' 4, and every pointer-bearing part of the requirement
 * (the two structs, the bindings array) is correspondingly larger here, while
 * the three byte buffers are identical - so a requirement that fits 2048 on
 * this host fits 2048 on either target with room to spare.
 */
void test_the_console_tasks_configuration_still_fits_its_static_buffer(void) {
    // src/tasks/console_task.cpp: CLI_UINT embeddedCliBuffer[512].
    const uint16_t kConsoleTaskBufferBytes = 2048;

    // Exactly what consoleTask() configures: the library defaults, with live
    // autocompletion off and the caller's own buffer supplied.
    EmbeddedCliConfig* config = embeddedCliDefaultConfig();
    config->enableAutoComplete = false;

    uint16_t required = embeddedCliRequiredSize(config);

    char msg[160];
    snprintf(msg, sizeof(msg),
             "embedded-cli needs %u bytes; console_task.cpp reserves %u and deletes the "
             "Console task when it does not fit",
             (unsigned)required, (unsigned)kConsoleTaskBufferBytes);
    TEST_ASSERT_TRUE_MESSAGE(required <= kConsoleTaskBufferBytes, msg);
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_overlong_line_answers_exactly_the_browser_adapters_record);
    RUN_TEST(test_refusal_carries_a_request_id_from_the_shared_sequence);
    RUN_TEST(test_line_that_fits_produces_no_refusal_record);
    RUN_TEST(test_no_callback_still_refuses_the_line);
    RUN_TEST(test_emitter_tolerates_a_sink_without_a_result_writer);
    return UNITY_END();
}
