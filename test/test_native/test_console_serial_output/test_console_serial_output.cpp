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
//   2. No path takes a serial mutex, on any path, including the guard paths
//      that emit without a preceding begin record (was: "taken and given in
//      matched pairs" - see the #270 WORKER NOTE below for the inversion).
//   3. The console cannot self-deadlock on a lock it does not have.
//
// The seam under test (worker implements; see the coordinator note on #217):
//   void consoleSerialBindCli(EmbeddedCli* cli);
//   void consoleSerialEmitLine(const char* line);
// `consoleSerialEmitLine` must render clear/print/redraw as embeddedCliPrint()
// does (lib/embedded-cli/src/embedded_cli.c) and put the whole thing on the
// wire in one write.
//
// WORKER NOTE (#268, reported on the issue, not edited silently): sections 1
// and 6 below read the emitted bytes back through SerialStub (the wire) rather
// than through the CLI's per-character `writeChar` (`g_capture`). Not one
// assertion is weakened - each one is the same string, in the same order, with
// the same message - but their probe had to move, because the emission no
// longer reaches the wire one character at a time. #268 renders the whole
// redraw with embeddedCliPrintToBuffer() and writes it in a single
// Serial.write(), which is what stops the Console task's own unlocked echo
// from landing a byte inside a log line. `writeChar` is the echo path now, so
// a probe there can no longer see an emission at all. Section 7 is the new
// coverage for that property.
//
// The `g_capture`/`writeChar` probe itself stays - it is what section 4's
// echo-proportionality test measures - but its two lookup helpers went with
// the assertions that used them rather than being left orphaned.
//
// WORKER NOTE (#270, reported on the issue, not edited silently): section 2's
// and section 5's four "the mutex was taken exactly once" assertions now read
// "the mutex was never taken". ADR 0037 makes the Console task the only writer
// of this wire, and #270's acceptance requires `logSerialMutex` and its
// accessor to be DELETED - so an assertion that an emission takes a serial
// mutex is an assertion that the ticket was not done. Nothing is weakened:
// each one is INVERTED, not dropped, and an inverted assertion is the tighter
// one here. `takeCount >= 2` was satisfiable by any number of takes; `== 0`
// admits none, so it is the deletion itself that is now pinned, on every path
// section 2 and section 5 already covered. The PaStubMutex singleton the
// assertions read is unchanged and still exercised for real by
// test_console_module.cpp and test_console_concurrency.cpp, which drive the
// Console module's own config-write mutex through it.
//
// The properties those four cases existed to protect are kept, and are now
// structural rather than disciplinary:
//  - "no unmatched give" and "never left held": nothing takes or gives, so
//    neither can happen. Asserted as zero takes plus zero unmatched gives.
//  - "the per-character writer must not nest a take inside the emission's":
//    section 3 keeps its `failedTakes == 0`, which stays meaningful - a
//    writeChar that reached for ANY mutex would show there.
//  - "the console never self-deadlocks on the non-recursive mutex": there is
//    no mutex to deadlock on. What replaced the hazard is
//    embeddedCliPrintToBuffer()'s own re-entrancy refusal, covered by
//    test/test_native/test_cli_print_to_buffer/.
//
// The fixture also now sets `enableAutoComplete = false`, which is what
// src/tasks/console_task.cpp sets. Nothing here depended on the default; a
// redraw rendered under a configuration production does not use is simply a
// weaker fixture, and section 7 asserts the exact tail of a frame.
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

// Index of the first occurrence of `needle` at or after `from`, or -1, over
// what actually reached the wire (SerialStub) - see the WORKER NOTE at the top
// of this file for why the emission tests read the wire and not `g_capture`.
static int wireIndexOfFrom(const char* needle, int from) {
    if (from < 0 || (size_t)from > SerialStub::capturedLen) {
        return -1;
    }
    // capturedBuf is not NUL-terminated by the stub's write(); terminate a
    // scratch copy so strstr has a string to search.
    static char scratch[sizeof(SerialStub::capturedBuf) + 1];
    memcpy(scratch, SerialStub::capturedBuf, SerialStub::capturedLen);
    scratch[SerialStub::capturedLen] = '\0';
    const char* hit = strstr(scratch + from, needle);
    return hit == nullptr ? -1 : (int)(hit - scratch);
}

static int wireIndexOf(const char* needle) {
    return wireIndexOfFrom(needle, 0);
}

// ----------------------------------------------------------------------------
// CLI fixture
// ----------------------------------------------------------------------------

static CLI_UINT g_cliBuffer[512];
static EmbeddedCli* g_cli;

static void cliFixtureSetUp(void) {
    captureReset();
    serialStubReset();
    paStubMutexReset();

    EmbeddedCliConfig* config = embeddedCliDefaultConfig();
    config->cliBuffer = g_cliBuffer;
    config->cliBufferSize = sizeof(g_cliBuffer);
    // As the Console task configures it (src/tasks/console_task.cpp): live
    // autocompletion off, so a redraw ends at the buffered command rather than
    // at a cursor save/restore pair no production redraw carries.
    config->enableAutoComplete = false;

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
    serialStubReset();

    consoleSerialEmitLine("[INFO] DriveTask heartbeat");

    const int logAt = wireIndexOf("[INFO] DriveTask heartbeat");
    TEST_ASSERT_TRUE_MESSAGE(logAt >= 0, "the emitted line never reached the port");

    // The input line is cleared BEFORE the log text: embedded-cli's
    // clearCurrentLine() starts with a carriage return.
    const int crAt = wireIndexOf("\r");
    TEST_ASSERT_TRUE_MESSAGE(crAt >= 0, "no carriage return: the input line was never cleared");
    TEST_ASSERT_TRUE_MESSAGE(crAt < logAt,
                             "the log text was written before the input line was cleared");

    // The prompt is redrawn AFTER the log text.
    const int promptAt = wireIndexOfFrom("> ", logAt);
    TEST_ASSERT_TRUE_MESSAGE(promptAt > logAt,
                             "the prompt was not redrawn after the emitted line");

    // The operator's buffered command is restored AFTER the prompt, so what they
    // typed is still on screen and still editable.
    const int bufferAt = wireIndexOfFrom("system.stat", promptAt);
    TEST_ASSERT_TRUE_MESSAGE(bufferAt > promptAt,
                             "the buffered command was not redrawn after the prompt");
}

// With an empty input line there is nothing to preserve, but the line must still
// be emitted exactly once and the prompt must come back.
void test_log_with_empty_input_line_still_emits_once_and_restores_prompt(void) {
    cliFixtureSetUp();
    serialStubReset();

    consoleSerialEmitLine("[WARN] rc link degraded");

    TEST_ASSERT_TRUE_MESSAGE(wireIndexOf("[WARN] rc link degraded") >= 0, "line not emitted");
    TEST_ASSERT_EQUAL_INT_MESSAGE(
        -1, wireIndexOfFrom("[WARN] rc link degraded", wireIndexOf("[WARN] rc link degraded") + 1),
        "the line was emitted more than once");
    TEST_ASSERT_TRUE_MESSAGE(wireIndexOf("> ") >= 0, "the prompt was not restored");
}

// ----------------------------------------------------------------------------
// 2. No lock on the wire (#270; was "mutex discipline")
// ----------------------------------------------------------------------------

// No emission touches a serial mutex, because there is not one to touch (ADR
// 0037: the Console task is the only writer, so the lock had nothing left to
// coordinate). The attempt-2 defect this case was written for - onRecordEnd
// releasing a mutex the guard paths never took - is unreachable for the same
// reason, and both halves are still asserted: zero takes, zero unmatched
// gives.
void test_emission_takes_no_serial_mutex_at_all(void) {
    cliFixtureSetUp();
    paStubMutexReset();

    consoleSerialEmitLine("[INFO] one");
    consoleSerialEmitLine("[INFO] two");

    struct PaStubMutex* m = paStubMutexStorage();
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, m->takeCount,
                                  "an emission took a serial mutex; #270 deletes it, and a "
                                  "lock on this path means a writer other than the Console "
                                  "task is still assumed to exist");
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, m->held, "a mutex was left held after emission");
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, m->unmatchedGives,
                                  "a mutex was given without having been taken");
}

// A completed emission leaves no lock state behind on any path. This used to
// be about the non-recursive mutex's self-deadlock (an emission that re-entered
// itself by logging while holding it); that hazard is gone with the mutex, and
// the re-entrancy that replaced it is refused by embeddedCliPrintToBuffer()
// itself (test/test_native/test_cli_print_to_buffer/).
void test_completed_emission_leaves_no_lock_state_behind(void) {
    cliFixtureSetUp();
    paStubMutexReset();

    consoleSerialEmitLine("[INFO] outer");

    struct PaStubMutex* m = paStubMutexStorage();
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, m->held, "a mutex was still held after a completed emission");
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
// consoleSerialEmitLine() used to hold the serial mutex and then call
// embeddedCliPrint(), which writes through `cli->writeChar` for every
// character. If that writer also tried to take the same NON-RECURSIVE mutex,
// every single byte performed a take that must fail - and whatever the writer
// did on that failure path was what actually reached the port. #270 removes
// the mutex outright, so the case now asserts what remains true and is worth
// asserting: neither the emission nor the per-character writer reaches for a
// lock at all.
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
        "the per-character writer reached for a mutex during an emission; it has no lock to "
        "take and no lock to contend with (ADR 0037)");
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, m->takeCount,
                                  "an emission must take no serial mutex at all (#270)");
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, m->held, "a mutex was left held");
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

    serialStubReset();
    consoleSerialEmitLine(longLine);

    // Check that captured output is truncated to PA_LOG_SERIAL_LINE_MAX
    // The output should be at most PA_LOG_SERIAL_LINE_MAX - 1 characters (plus \r, \n, etc from the redraw)
    // For this test, we check that we don't receive the entire 512-char string verbatim
    size_t capturedLen = SerialStub::capturedLen;

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
// test/stubs/include/Arduino.h) and, since #270, to nothing else - so every
// assertion below reads back through that one test double rather than a
// board. The PaStubMutex checks that remain are there to prove the absence of
// a lock, not its discipline (see the WORKER NOTE at the top of this file).

static void framedLineFixtureSetUp(void) {
    serialStubReset();
    paStubMutexReset();
}

// A record with room from the first check: one write call, the line and its
// terminator delivered together, the mutex taken and given exactly once, and
// the room was actually checked (not skipped).
//
// WORKER NOTE (#267, reported on the issue, not edited silently): the three
// assertions below said `strlen(line) + 1` and a final '\n' when the sink
// terminated records with a bare LF. #267 decided records carry the same
// "\r\n" the log path already emits, so they now say `+ 2` and check both
// terminator bytes. That is the coordinator's own decision on the ticket
// arriving in the harness, not an assertion weakened to make a slice pass --
// the single-write property they exist to protect is asserted here exactly
// as before, and more tightly (the exact terminator bytes, not just the last
// one).
void test_framed_line_with_room_writes_once_including_newline(void) {
    framedLineFixtureSetUp();

    const char* line = "< id=7 type=field name=heapFree value=42120";
    bool sent = consoleSerialEmitFramedLine(line, strlen(line), /*waitForRoom=*/true);

    TEST_ASSERT_TRUE_MESSAGE(sent, "a line with room to spare must not be reported dropped");
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, SerialStub::writeCallCount,
                                  "the line and its terminator must reach Serial.write() "
                                  "together, in exactly one call");
    TEST_ASSERT_EQUAL_INT_MESSAGE((int)(strlen(line) + 2), (int)SerialStub::capturedLen,
                                  "captured bytes must be exactly the line plus CR LF");
    TEST_ASSERT_EQUAL_STRING_LEN_MESSAGE(line, SerialStub::capturedBuf, strlen(line),
                                         "the captured line text must be unchanged");
    TEST_ASSERT_EQUAL_INT_MESSAGE('\r', SerialStub::capturedBuf[SerialStub::capturedLen - 2],
                                  "the single write must carry the CR, not a bare LF");
    TEST_ASSERT_EQUAL_INT_MESSAGE('\n', SerialStub::capturedBuf[SerialStub::capturedLen - 1],
                                  "the single write must end in the newline, not a separate call");

    struct PaStubMutex* m = paStubMutexStorage();
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, m->takeCount, "writing a record must take no mutex (#270)");
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, m->giveCount, "nothing was taken, so nothing may be given");
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, m->held, "a mutex was left held");
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
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, m->takeCount,
                                  "the pre-bind log path must take no mutex either (#270)");
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, m->held, "a mutex was left held");
}

// #267 - the terminator itself, on both paths.
//
// A Console session must be attached in raw mode so Tab and cursor bytes
// reach the firmware unedited (docs/console-protocol.md 8), and raw mode
// disables the kernel's ONLCR NL->CR-NL translation. A bare LF therefore
// feeds the line down WITHOUT returning the carriage, and every record
// starts one column further right than the one before it -- the staircase
// the operator saw, while embedded-cli's log lines (already "\r\n",
// lib/embedded-cli/src/embedded_cli.c:235) sat at column 0 on the same wire.
// Both callers of this emitter are checked: the record sink (waitForRoom
// true) and the pre-binding boot log fallback (false), which shares the wire
// and staircased identically.
void test_framed_line_terminates_with_cr_lf_on_both_policies(void) {
    framedLineFixtureSetUp();

    const char* record = "< id=11 type=item value=drive.action.move";
    TEST_ASSERT_TRUE(consoleSerialEmitFramedLine(record, strlen(record), /*waitForRoom=*/true));
    TEST_ASSERT_EQUAL_STRING_LEN_MESSAGE(
        "\r\n", SerialStub::capturedBuf + strlen(record), 2,
        "a record must end CR LF so a raw-mode terminal returns to column 0");

    framedLineFixtureSetUp();

    const char* log = "[INFO][ConsoleTask] active";
    TEST_ASSERT_TRUE(consoleSerialEmitFramedLine(log, strlen(log), /*waitForRoom=*/false));
    TEST_ASSERT_EQUAL_STRING_LEN_MESSAGE(
        "\r\n", SerialStub::capturedBuf + strlen(log), 2,
        "the boot-time log fallback shares the wire and must end CR LF too");
}

// ADR 0036 reserves transmit room for the WHOLE line including its
// terminator, so the reservation had to follow the extra byte. Room for
// exactly `lineLen + 1` is now one byte short: the record is dropped whole
// rather than written short of its CR. `lineLen + 2` is the smallest room
// that sends -- which pins the reservation to both bytes from either side,
// so neither an unchanged `+ 1` nor an over-cautious `+ 3` survives.
void test_framed_line_room_reservation_covers_both_terminator_bytes(void) {
    const char* line = "< id=12 type=end status=ok outcome=completed";
    const int lineLen = (int)strlen(line);

    framedLineFixtureSetUp();
    SerialStub::availableForWriteValue = lineLen + 1;  // room for the line and ONE terminator byte
    bool sentShort = consoleSerialEmitFramedLine(line, strlen(line), /*waitForRoom=*/true);

    TEST_ASSERT_FALSE_MESSAGE(sentShort,
                              "room for only one terminator byte must drop the record whole, "
                              "not write it short of its CR");
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, SerialStub::writeCallCount,
                                  "nothing may reach Serial.write() for a dropped record");

    framedLineFixtureSetUp();
    SerialStub::availableForWriteValue = lineLen + 2;  // room for the line and CR LF
    bool sentWhole = consoleSerialEmitFramedLine(line, strlen(line), /*waitForRoom=*/true);

    TEST_ASSERT_TRUE_MESSAGE(sentWhole,
                             "room for the line plus CR LF is enough: the record must be sent");
    TEST_ASSERT_EQUAL_INT_MESSAGE(lineLen + 2, (int)SerialStub::capturedLen,
                                  "the whole line and both terminator bytes must reach the wire");
}

// A frame longer than the transport's whole buffer must still be sent.
//
// WORKER TEST (#270): `Serial.availableForWrite()` has a ceiling -- the CDC's
// 256-byte TX ring, UART0's 128-byte FIFO (CONSOLE_SERIAL_TX_ROOM_MAX,
// console_serial_output.h, with the vendor citations). Reserving `lineLen + 2`
// unconditionally therefore asked for room that can never exist once a line
// passed that ceiling, waited out the whole 100 ms bound, and dropped a frame
// both transports would have accepted: UART0 cannot short-write at all, and
// the CDC chunks anything larger than its ring regardless. The reservation is
// capped at the ceiling, which this pins from both sides -- room equal to the
// ceiling sends, one byte less still drops, so neither the cap nor the wait
// itself can quietly disappear.
void test_room_reservation_is_capped_at_what_the_transport_can_offer(void) {
    char line[201];
    memset(line, 'R', sizeof(line) - 1);
    line[sizeof(line) - 1] = '\0';
    const size_t lineLen = strlen(line);
    TEST_ASSERT_TRUE_MESSAGE(lineLen + 2 > CONSOLE_SERIAL_TX_ROOM_MAX,
                             "fixture line must be longer than the transport ceiling");

    framedLineFixtureSetUp();
    SerialStub::availableForWriteValue = (int)CONSOLE_SERIAL_TX_ROOM_MAX;
    bool sentAtCeiling = consoleSerialEmitFramedLine(line, lineLen, /*waitForRoom=*/true);

    TEST_ASSERT_TRUE_MESSAGE(sentAtCeiling,
                             "a frame past the transport's ceiling must be sent once the "
                             "transport is as empty as it can report, not dropped waiting for "
                             "room that can never exist");
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, SerialStub::writeCallCount,
                                  "the frame must still reach the wire in exactly one call");
    TEST_ASSERT_EQUAL_INT_MESSAGE((int)(lineLen + 2), (int)SerialStub::capturedLen,
                                  "the whole line and both terminator bytes must be written");

    framedLineFixtureSetUp();
    SerialStub::availableForWriteValue = (int)CONSOLE_SERIAL_TX_ROOM_MAX - 1;
    bool sentBelowCeiling = consoleSerialEmitFramedLine(line, lineLen, /*waitForRoom=*/true);

    TEST_ASSERT_FALSE_MESSAGE(sentBelowCeiling,
                              "one byte below the ceiling the transport is not as empty as it "
                              "gets: the wait must still run and the frame must still drop");
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, SerialStub::writeCallCount,
                                  "nothing may reach Serial.write() for a dropped frame");
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
// 7. #268 - the redraw is ONE write, so nothing can land inside it
// =============================================================================
//
// On artoo-esp32 a log line arriving while a partial command was buffered sent
// the UART0 sink into a self-sustaining redraw loop. The sink's own defect,
// cited: the interactive log path rendered its redraw through
// embeddedCliPrint(), one Serial.write() per character - ~70 of them for a
// 59-byte line - while the Console task's echo (embeddedCliProcess, which
// holds no lock, src/tasks/console_task.cpp) writes to the same wire. ADR 0036
// had already ruled that out for Console Records ("every line, record or log,
// is written with one call"); the redraw is where that decision had not
// reached.
//
// These assert the property, not the absence of a loop: one write per
// emission, no opening inside it for another writer, and output per log line
// bounded by the line plus its redraw.

static const char* const MIDENTRY_LOG_LINE =
    "[65485][I][WebServer] slowest response phase now 144 ms (0)";

// The whole redraw - clear, line, break, prompt, buffered command - reaches
// the wire in a single Serial.write() call. Character-at-a-time is what gave
// the other writer on this wire ~70 openings.
void test_midentry_redraw_reaches_the_wire_in_one_write(void) {
    cliFixtureSetUp();
    typeChars("sys");

    serialStubReset();
    consoleSerialEmitLine(MIDENTRY_LOG_LINE);

    TEST_ASSERT_EQUAL_INT_MESSAGE(1, SerialStub::writeCallCount,
                                  "the mid-entry redraw must reach the wire in ONE write; "
                                  "every extra call is an opening for another writer to "
                                  "land a byte inside the log line");
    TEST_ASSERT_TRUE_MESSAGE(wireIndexOf(MIDENTRY_LOG_LINE) >= 0,
                             "the log line did not reach the wire whole");
}

// A deterministically placed preemption, in the shape test_console_concurrency
// uses: the Console task's poll runs from inside the emission, at a character
// that is part of the log line's own text. Whether it CAN run there is the
// property - with the redraw delivered in one write there is no such point,
// so the echo lands before or after the frame and never inside it.
static EmbeddedCli* g_interleaveCli;
static bool g_interleaveArmed;
static bool g_interleaveFired;
static int g_interleaveCharCount;

static void interleavingWriteChar(EmbeddedCli* cli, char c) {
    consoleSerialWriteChar(cli, c);  // the production echo path, onto the wire
    ++g_interleaveCharCount;
    // Char 10 of a redraw is inside the log line's text: clearCurrentLine
    // writes 7 (CR + "sys" + "> " worth of spaces + CR) before the line starts.
    if (g_interleaveArmed && g_interleaveCharCount == 10) {
        g_interleaveArmed = false;
        g_interleaveFired = true;
        embeddedCliReceiveChar(g_interleaveCli, 'X');
        embeddedCliProcess(g_interleaveCli);  // echoes 'X' through this writer
    }
}

void test_console_echo_cannot_land_inside_a_midentry_redraw(void) {
    captureReset();
    serialStubReset();
    paStubMutexReset();

    EmbeddedCliConfig* config = embeddedCliDefaultConfig();
    config->cliBuffer = g_cliBuffer;
    config->cliBufferSize = sizeof(g_cliBuffer);
    config->enableAutoComplete = false;
    g_interleaveCli = embeddedCliNew(config);
    TEST_ASSERT_NOT_NULL(g_interleaveCli);
    g_interleaveCli->writeChar = interleavingWriteChar;
    embeddedCliProcess(g_interleaveCli);
    consoleSerialBindCli(g_interleaveCli);

    for (const char* p = "sys"; *p != '\0'; ++p) {
        embeddedCliReceiveChar(g_interleaveCli, *p);
    }
    embeddedCliProcess(g_interleaveCli);

    serialStubReset();
    g_interleaveCharCount = 0;
    g_interleaveFired = false;
    g_interleaveArmed = true;

    consoleSerialEmitLine(MIDENTRY_LOG_LINE);

    TEST_ASSERT_FALSE_MESSAGE(g_interleaveFired,
                              "the Console task's echo path ran from inside a log emission: "
                              "the redraw is still being handed to the transport character "
                              "by character, so another writer can land a byte inside it");
    TEST_ASSERT_TRUE_MESSAGE(wireIndexOf(MIDENTRY_LOG_LINE) >= 0,
                             "the log line is not contiguous on the wire - something was "
                             "written inside it (docs/console-protocol.md section 6)");

    consoleSerialBindCli(nullptr);  // leave no dangling instance for later tests
}

// What comes back after the line is the prompt and the OPERATOR'S buffered
// command - not a fragment of the line just printed, which is what the board
// was repeating for megabytes.
void test_redraw_ends_with_prompt_and_buffered_command(void) {
    cliFixtureSetUp();
    typeChars("sys");

    serialStubReset();
    consoleSerialEmitLine(MIDENTRY_LOG_LINE);

    TEST_ASSERT_TRUE(SerialStub::capturedLen >= 5);
    TEST_ASSERT_EQUAL_MEMORY_MESSAGE(
        "> sys", SerialStub::capturedBuf + SerialStub::capturedLen - 5, 5,
        "the redraw must end with the prompt and the buffered command, not with a "
        "fragment of the log line");
}

// Sustained log traffic with a command buffered: output stays bounded by the
// input that caused it - one write and one redraw's worth of bytes per line,
// however many lines arrive.
void test_sustained_log_traffic_stays_bounded_per_line(void) {
    cliFixtureSetUp();
    typeChars("sys");

    serialStubReset();

    const int lines = 20;
    for (int i = 0; i < lines; ++i) {
        consoleSerialEmitLine(MIDENTRY_LOG_LINE);
    }

    TEST_ASSERT_EQUAL_INT_MESSAGE(lines, SerialStub::writeCallCount,
                                  "each log line must cost exactly one write");

    // Per line: the clear (CR + prompt+command worth of spaces + CR), the line,
    // CR LF, the prompt, the command back. 16 bytes covers the fixed parts and
    // any cursor move at these sizes.
    const size_t perLineBound = strlen(MIDENTRY_LOG_LINE) + 2 * strlen("sys") + 16;
    TEST_ASSERT_TRUE_MESSAGE(SerialStub::capturedLen <= perLineBound * (size_t)lines,
                             "output grew faster than the log traffic that caused it");
}

// =============================================================================

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_log_midentry_clears_input_line_then_redraws_prompt_and_buffer);
    RUN_TEST(test_log_with_empty_input_line_still_emits_once_and_restores_prompt);
    RUN_TEST(test_emission_takes_no_serial_mutex_at_all);
    RUN_TEST(test_completed_emission_leaves_no_lock_state_behind);
    RUN_TEST(test_emission_performs_no_nested_take_through_writechar);
    RUN_TEST(test_long_lines_are_truncated_to_serial_max);
    RUN_TEST(test_keystroke_echo_output_stays_proportional_to_input);
    RUN_TEST(test_framed_line_with_room_writes_once_including_newline);
    RUN_TEST(test_framed_line_waits_only_while_connected);
    RUN_TEST(test_framed_line_room_wait_is_bounded_and_stays_outside_the_mutex);
    RUN_TEST(test_framed_line_without_wait_flag_never_waits_for_room);
    RUN_TEST(test_framed_line_terminates_with_cr_lf_on_both_policies);
    RUN_TEST(test_framed_line_room_reservation_covers_both_terminator_bytes);
    RUN_TEST(test_room_reservation_is_capped_at_what_the_transport_can_offer);
    RUN_TEST(test_dropped_suffix_absent_when_zero);
    RUN_TEST(test_dropped_suffix_present_with_exact_count_when_nonzero);
    RUN_TEST(test_dropped_suffix_too_small_buffer_emits_nothing);
    RUN_TEST(test_midentry_redraw_reaches_the_wire_in_one_write);
    RUN_TEST(test_console_echo_cannot_land_inside_a_midentry_redraw);
    RUN_TEST(test_redraw_ends_with_prompt_and_buffered_command);
    RUN_TEST(test_sustained_log_traffic_stays_bounded_per_line);
    return UNITY_END();
}
