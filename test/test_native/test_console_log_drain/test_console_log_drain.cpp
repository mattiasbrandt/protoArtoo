// =============================================================================
// test/test_native/test_console_log_drain/test_console_log_drain.cpp
//
// ADR 0037: the Console task owns the serial wire; log lines reach it through
// the Log Ring (#270).
//
// This is the native half of the invariant's double enforcement. The other
// half is a source scan (test/test_tools/test_one_serial_seam.py), which
// proves no OTHER file writes the wire; this proves the behaviour the scan
// cannot see - that a line logged from a non-Console task reaches the serial
// stub only through the drain, in order, and that the one remaining loss (ring
// eviction) is marked on the wire rather than silent.
//
// What is real here and what is a stand-in: consoleSerialDrainLogs(),
// consoleSerialEmitLine() and the whole write path are the production ones
// (src/console/console_serial_output.cpp), and so is the cursor walk
// underneath paLogDrainNextLine() (logBufferDrainNext, src/log_buffer.cpp).
// src/main.cpp - which owns the ring, the drain cursor and the ownership flag
// on the target - is not in [env:native]'s build_src_filter, so its paLogLine()
// wrapper is stood in for by src/native_test_stubs.cpp in the same shape: write
// the ring once, consult ownership for the wire. The lock main.cpp holds around
// both is the part that cannot be exercised on a single-threaded host, and is
// stated rather than proven (see paLogLine()'s comment for what it buys).
// =============================================================================

#include <unity.h>

#include <stdio.h>
#include <string.h>

#include <Arduino.h>

extern "C" {
#include "embedded_cli.h"
}

#include "console_serial_output.h"
#include "log_buffer.h"
#include "log_buffer_test_hooks.h"
#include "logging.h"

// ----------------------------------------------------------------------------
// Fixture
// ----------------------------------------------------------------------------

static CLI_UINT g_cliBuffer[512];
static EmbeddedCli* g_cli;

// A deliberately shallow ring for the eviction case: the production depth
// ladder (log_buffer.h) is 16..48 lines on this chip target, and filling 48
// lines to show one eviction marker would say nothing 6 lines do not.
static char g_smallStorage[4][LOG_LINE_MAX];

// Everything the wire received, NUL-terminated so strstr can search it.
// SerialStub::capturedBuf is not terminated by the stub's write().
static const char* wire(void) {
    static char scratch[sizeof(SerialStub::capturedBuf) + 1];
    memcpy(scratch, SerialStub::capturedBuf, SerialStub::capturedLen);
    scratch[SerialStub::capturedLen] = '\0';
    return scratch;
}

static int wireIndexOf(const char* needle) {
    const char* hit = strstr(wire(), needle);
    return hit == nullptr ? -1 : (int)(hit - wire());
}

static int wireOccurrences(const char* needle) {
    int count = 0;
    const char* p = wire();
    const size_t len = strlen(needle);
    while ((p = strstr(p, needle)) != nullptr) {
        count++;
        p += len;
    }
    return count;
}

// Put the log ring back to empty and the wire back to unowned, so each case
// starts where a freshly booted controller does. `capacity` lets the eviction
// case run a shallow ring.
static void logRingReset(size_t capacity) {
    if (capacity == 4) {
        logBufferInit(&g_test_log_sink_buffer, g_smallStorage, 4);
    } else {
        logBufferInit(&g_test_log_sink_buffer, g_test_log_sink_storage, LOG_RING_MAX_LINES);
    }
    g_test_log_drain_cursor = 0;
    g_test_log_wire_owned = false;
}

static void fixtureSetUp(size_t ringCapacity) {
    serialStubReset();
    logRingReset(ringCapacity);

    EmbeddedCliConfig* config = embeddedCliDefaultConfig();
    config->cliBuffer = g_cliBuffer;
    config->cliBufferSize = sizeof(g_cliBuffer);
    // As the Console task configures it (src/tasks/console_task.cpp).
    config->enableAutoComplete = false;

    g_cli = embeddedCliNew(config);
    TEST_ASSERT_NOT_NULL_MESSAGE(g_cli, "embeddedCliNew returned NULL with a static buffer");
    g_cli->writeChar = consoleSerialWriteChar;
    embeddedCliProcess(g_cli);  // emits the initial prompt
}

// Bring the fixture to the state the controller is in once the Console task
// has started: the CLI is bound and the wire belongs to it.
static void bindWireToConsole(void) {
    consoleSerialBindCli(g_cli);
    serialStubReset();  // the bind itself is not what these cases measure
}

void setUp(void) {}
void tearDown(void) {}

// ----------------------------------------------------------------------------
// 1. Before the bind: unchanged boot behaviour
// ----------------------------------------------------------------------------

// Until the Console task binds the adapter there is no task to drain the ring,
// so paLogLine() writes the line itself - the boot log, exactly as before ADR
// 0037. This is the "the switch to ring-only IS the bind" half of the rule; it
// is here so the rule cannot be satisfied by silencing the boot log.
void test_a_line_logged_before_the_bind_reaches_the_wire_directly(void) {
    fixtureSetUp(LOG_RING_MAX_LINES);
    serialStubReset();

    paLogLine("[I][main] protoArtoo boot begin");

    TEST_ASSERT_TRUE_MESSAGE(wireIndexOf("protoArtoo boot begin") >= 0,
                             "a pre-bind log line must still reach the wire directly");
    TEST_ASSERT_EQUAL_INT_MESSAGE(
        0, SerialStub::availableForWriteCallCount,
        "the pre-bind path is best-effort and must not wait for transmit room");
}

// ----------------------------------------------------------------------------
// 2. After the bind: the drain is the only way onto the wire
// ----------------------------------------------------------------------------

// THE criterion (ADR 0037): a line logged from a non-Console task never
// reaches the serial stub except through the drain. `paLogLine` here stands
// for a Core 1 task's PA_LOG_* - DriveTask's failsafe line, RCInputTask's link
// warning - which used to write this wire from its own context.
void test_a_line_logged_after_the_bind_reaches_the_wire_only_through_the_drain(void) {
    fixtureSetUp(LOG_RING_MAX_LINES);
    bindWireToConsole();

    paLogLine("[W][drive] failsafe zero-output asserted");

    TEST_ASSERT_EQUAL_INT_MESSAGE(
        0, SerialStub::writeCallCount,
        "a logging task wrote the wire itself; after the bind only the drain may");
    TEST_ASSERT_EQUAL_INT_MESSAGE(-1, wireIndexOf("failsafe zero-output asserted"),
                                  "the line reached the wire without passing the drain");

    const uint32_t drained = consoleSerialDrainLogs();

    TEST_ASSERT_EQUAL_UINT32_MESSAGE(1, drained, "the drain did not write the pending line");
    TEST_ASSERT_TRUE_MESSAGE(wireIndexOf("failsafe zero-output asserted") >= 0,
                             "the drain did not put the line on the wire");
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, wireOccurrences("failsafe zero-output asserted"),
                                  "the line reached the wire more than once");

    // The cursor advanced with it: a second drain has nothing to say.
    const uint32_t again = consoleSerialDrainLogs();
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, again, "the drain re-sent a line it had already written");
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, wireOccurrences("failsafe zero-output asserted"),
                                  "a second drain wrote the same line again");
}

// The mid-entry contract still holds for a drained line: the operator's
// half-typed command is cleared, the line is written, and the prompt and the
// buffered command come back after it (docs/console-protocol.md 6).
void test_a_drained_line_still_clears_and_redraws_the_input_line(void) {
    fixtureSetUp(LOG_RING_MAX_LINES);
    bindWireToConsole();

    for (const char* p = "system.stat"; *p != '\0'; ++p) {
        embeddedCliReceiveChar(g_cli, *p);
    }
    embeddedCliProcess(g_cli);
    serialStubReset();

    paLogLine("[I][dome] link up");
    consoleSerialDrainLogs();

    const int logAt = wireIndexOf("link up");
    TEST_ASSERT_TRUE_MESSAGE(logAt >= 0, "the drained line never reached the wire");
    const int crAt = wireIndexOf("\r");
    TEST_ASSERT_TRUE_MESSAGE(crAt >= 0 && crAt < logAt,
                             "the input line was not cleared before the log text");
    const char* promptThenBuffer = strstr(wire() + logAt, "> system.stat");
    TEST_ASSERT_NOT_NULL_MESSAGE(promptThenBuffer,
                                 "the prompt and the buffered command were not redrawn after it");
}

// ----------------------------------------------------------------------------
// 3. Order
// ----------------------------------------------------------------------------

// The drain is a cursor over the ring's own write order, so lines reach the
// wire in the order they were logged - including lines from different tasks,
// which is the ordering the old "whoever holds the lock writes" model could
// only get by luck.
void test_the_drain_writes_lines_in_the_order_they_were_logged(void) {
    fixtureSetUp(LOG_RING_MAX_LINES);
    bindWireToConsole();

    paLogLine("[I][a] first");
    paLogLine("[I][b] second");
    paLogLine("[I][c] third");

    TEST_ASSERT_EQUAL_UINT32_MESSAGE(3, consoleSerialDrainLogs(),
                                     "the drain did not write all three pending lines");

    const int first = wireIndexOf("[I][a] first");
    const int second = wireIndexOf("[I][b] second");
    const int third = wireIndexOf("[I][c] third");
    TEST_ASSERT_TRUE_MESSAGE(first >= 0 && second >= 0 && third >= 0,
                             "not every logged line reached the wire");
    TEST_ASSERT_TRUE_MESSAGE(first < second && second < third,
                             "the drain reordered the log lines");
}

// ----------------------------------------------------------------------------
// 4. The one remaining loss is marked
// ----------------------------------------------------------------------------

// Ring eviction - writers overtaking the drain cursor - is the only way a log
// line is lost from the wire under ADR 0037, and it must not be silent: one
// counted marker line, carrying the same `dropped=<n>` token a record's
// closing line uses, before the drain continues with what the ring still has.
void test_ring_eviction_emits_one_counted_marker_before_the_drain_continues(void) {
    fixtureSetUp(4);
    bindWireToConsole();

    // Six lines into a four-line ring: the two oldest are gone.
    paLogLine("[I][x] line-1");
    paLogLine("[I][x] line-2");
    paLogLine("[I][x] line-3");
    paLogLine("[I][x] line-4");
    paLogLine("[I][x] line-5");
    paLogLine("[I][x] line-6");

    consoleSerialDrainLogs();

    const int markerAt = wireIndexOf("[log] dropped=2");
    TEST_ASSERT_TRUE_MESSAGE(markerAt >= 0,
                             "eviction was silent: no counted marker reached the wire");
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, wireOccurrences("[log] dropped="),
                                  "eviction must be marked once, not once per line");

    const int survivorAt = wireIndexOf("[I][x] line-3");
    TEST_ASSERT_TRUE_MESSAGE(survivorAt > markerAt,
                             "the marker must come BEFORE the drain continues");
    TEST_ASSERT_TRUE_MESSAGE(wireIndexOf("[I][x] line-6") >= 0,
                             "the drain stopped instead of continuing after the marker");
    TEST_ASSERT_EQUAL_INT_MESSAGE(-1, wireIndexOf("[I][x] line-1"),
                                  "an evicted line cannot be on the wire");
}

// A drain that has kept up says nothing: the marker is present exactly when
// something was lost, matching the `dropped=` field's own presence rule
// (docs/console-protocol.md 3.3).
void test_no_marker_when_the_drain_kept_up(void) {
    fixtureSetUp(4);
    bindWireToConsole();

    paLogLine("[I][x] line-1");
    paLogLine("[I][x] line-2");
    consoleSerialDrainLogs();

    TEST_ASSERT_EQUAL_INT_MESSAGE(-1, wireIndexOf("dropped="),
                                  "a drain that lost nothing must not report a drop");
}

// ----------------------------------------------------------------------------
// 5. The room policy
// ----------------------------------------------------------------------------

// ADR 0037 supersedes ADR 0036's "logs stay best-effort": a drained log line
// waits for transmit room under the same bound as a record, because the reason
// logs could not wait - a TWDT-subscribed logger blocked on the CDC - is gone
// with the writers that did the waiting.
void test_a_drained_line_waits_for_transmit_room(void) {
    fixtureSetUp(LOG_RING_MAX_LINES);
    bindWireToConsole();

    paLogLine("[I][x] waits like a record");
    consoleSerialDrainLogs();

    TEST_ASSERT_TRUE_MESSAGE(SerialStub::availableForWriteCallCount > 0,
                             "a drained log line must ask for transmit room, not skip the check");
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_a_line_logged_before_the_bind_reaches_the_wire_directly);
    RUN_TEST(test_a_line_logged_after_the_bind_reaches_the_wire_only_through_the_drain);
    RUN_TEST(test_a_drained_line_still_clears_and_redraws_the_input_line);
    RUN_TEST(test_the_drain_writes_lines_in_the_order_they_were_logged);
    RUN_TEST(test_ring_eviction_emits_one_counted_marker_before_the_drain_continues);
    RUN_TEST(test_no_marker_when_the_drain_kept_up);
    RUN_TEST(test_a_drained_line_waits_for_transmit_room);
    return UNITY_END();
}
