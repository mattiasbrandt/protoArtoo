// =============================================================================
// test/test_native/test_console_cdc_settle/test_console_cdc_settle.cpp
//
// #275: the Console task makes no call into the USB CDC transport for
// CONSOLE_CDC_SETTLE_MS after a genuine plugged edge, and the #260 attach
// debounce then fires exactly once. Drives include/console_cdc_settle.h the
// way src/tasks/console_task.cpp does -- one poll per CONSOLE_POLL_IDLE_MS,
// plugged read from the transport first -- against the SerialStub
// (test/stubs/include/Arduino.h): pluggedValue stands in for
// HWCDC::isPlugged(), connectedValue for `Serial` as a bool, and boolCallCount
// is the count of `Serial` reads the unit made, which is the property under
// test. What is behaviour here: every `Serial` read the unit withholds is a
// packet the driver does not commit during enumeration (see the header).
// =============================================================================

#include <unity.h>

#include <Arduino.h>

#include "console_cdc_settle.h"

static constexpr uint32_t POLL_MS = 10;  // console_task.cpp's CONSOLE_POLL_IDLE_MS

static ConsoleHostPresence g_st;
static uint32_t g_nowMs;

// One Console-task poll: the caller reads cable presence, then asks the unit.
static ConsoleHostPoll poll(void) {
    g_nowMs += POLL_MS;
    return consoleHostPresencePoll(&g_st, SerialStub::isPlugged(), g_nowMs);
}

static int pollsInWindow(void) {
    return (int)(CONSOLE_CDC_SETTLE_MS / POLL_MS);
}

void setUp(void) {
    serialStubReset();
    g_nowMs = 1000;
}

void tearDown(void) {}

// A live session -- plugged and connected -- whose cable is pulled long enough
// to be a real detach, then put back. The state every replug row starts from.
static void detachAfterLiveSession(void) {
    SerialStub::pluggedValue = true;
    SerialStub::connectedValue = true;
    consoleHostPresenceInit(&g_st);
    TEST_ASSERT_EQUAL(CONSOLE_HOST_POLL_RUN, poll());
    TEST_ASSERT_EQUAL(CONSOLE_HOST_POLL_RUN, poll());

    // Cable out: SOF stops, the watchdog drops plugged, the driver's latch
    // drops connected on the next isCDC_Connected() read.
    SerialStub::pluggedValue = false;
    SerialStub::connectedValue = false;
    for (int i = 0; i < 20; i++) {
        TEST_ASSERT_EQUAL(CONSOLE_HOST_POLL_RUN, poll());
    }
}

// The direct acceptance: a genuine edge opens the window, and while it is open
// the unit reads `Serial` exactly zero times, for exactly the window's length.
void test_genuine_edge_holds_off_every_serial_read_for_the_window(void) {
    detachAfterLiveSession();

    // Replug: SOF resumes. The host has not configured the device yet, so
    // `connected` would still read false -- but the point is that nobody asks.
    SerialStub::pluggedValue = true;
    const int readsBefore = SerialStub::boolCallCount;

    int held = 0;
    ConsoleHostPoll r = poll();
    while (r == CONSOLE_HOST_POLL_HOLD) {
        held++;
        TEST_ASSERT_TRUE_MESSAGE(held <= pollsInWindow() + 1, "the window must close");
        r = poll();
    }

    TEST_ASSERT_EQUAL_INT_MESSAGE(readsBefore, SerialStub::boolCallCount - 1,
                                  "no `Serial` read may happen while the window is open; the one "
                                  "read is the first poll after it closed");
    TEST_ASSERT_EQUAL_INT_MESSAGE(pollsInWindow(), held,
                                  "the window is CONSOLE_CDC_SETTLE_MS of polls, no more, no less");
}

// After the window the #260 debounce runs unchanged: two consecutive connected
// polls, one attach edge, then ordinary polls.
void test_after_the_window_the_attach_debounce_fires_exactly_once(void) {
    detachAfterLiveSession();
    SerialStub::pluggedValue = true;

    ConsoleHostPoll r = poll();
    while (r == CONSOLE_HOST_POLL_HOLD) {
        r = poll();
    }
    // The first poll after the window read `Serial` (still false: the host
    // has not spoken yet) and ran.
    TEST_ASSERT_EQUAL(CONSOLE_HOST_POLL_RUN, r);

    // The host reads the endpoint: the driver's latch goes true.
    SerialStub::connectedValue = true;
    int attached = 0;
    for (int i = 0; i < 10; i++) {
        if (poll() == CONSOLE_HOST_POLL_ATTACHED) {
            attached++;
        }
    }
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, attached, "one replug, one banner");
}

// The SOF watchdog's documented few-ms flap is one unplugged poll at most, and
// must not cost a window.
void test_one_poll_flap_starts_no_window(void) {
    SerialStub::pluggedValue = true;
    SerialStub::connectedValue = true;
    consoleHostPresenceInit(&g_st);
    TEST_ASSERT_EQUAL(CONSOLE_HOST_POLL_RUN, poll());

    SerialStub::pluggedValue = false;
    TEST_ASSERT_EQUAL(CONSOLE_HOST_POLL_RUN, poll());
    SerialStub::pluggedValue = true;

    const int readsBefore = SerialStub::boolCallCount;
    for (int i = 0; i < 5; i++) {
        TEST_ASSERT_EQUAL(CONSOLE_HOST_POLL_RUN, poll());
    }
    TEST_ASSERT_EQUAL_INT_MESSAGE(readsBefore + 5, SerialStub::boolCallCount,
                                  "every poll after a flap still reads `Serial` once");
}

// A cold boot with the cable in: plugged from the first sample, never an edge,
// and every poll reads `Serial` exactly once -- the count console_task.cpp
// made before #275 -- so the proven cold-boot attach is untouched.
void test_no_edge_leaves_every_call_count_as_before(void) {
    SerialStub::pluggedValue = true;
    SerialStub::connectedValue = true;
    consoleHostPresenceInit(&g_st);
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, SerialStub::boolCallCount,
                                  "init reads `Serial` once for the debounce baseline");

    for (int i = 0; i < 50; i++) {
        TEST_ASSERT_EQUAL(CONSOLE_HOST_POLL_RUN, poll());
    }
    TEST_ASSERT_EQUAL_INT(1 + 50, SerialStub::boolCallCount);
}

// A boot with no host: the task polls unplugged until someone attaches, so
// the FIRST attach is an edge by the ordinary rule and gets the window -- the
// other place the wedge was measured.
void test_first_attach_after_a_no_host_boot_gets_the_window(void) {
    SerialStub::pluggedValue = false;
    SerialStub::connectedValue = false;
    consoleHostPresenceInit(&g_st);
    for (int i = 0; i < 5; i++) {
        TEST_ASSERT_EQUAL(CONSOLE_HOST_POLL_RUN, poll());
    }

    SerialStub::pluggedValue = true;
    const int readsBefore = SerialStub::boolCallCount;
    TEST_ASSERT_EQUAL(CONSOLE_HOST_POLL_HOLD, poll());
    TEST_ASSERT_EQUAL_INT(readsBefore, SerialStub::boolCallCount);
}

// A second bus reset inside enumeration -- unplugged for several polls while
// the window is open -- restarts the window from the last edge rather than
// letting the first window expire mid-enumeration.
void test_second_reset_inside_the_window_restarts_it(void) {
    detachAfterLiveSession();
    SerialStub::pluggedValue = true;

    // Half the window in.
    for (int i = 0; i < pollsInWindow() / 2; i++) {
        TEST_ASSERT_EQUAL(CONSOLE_HOST_POLL_HOLD, poll());
    }
    // Reset: SOF gone for five polls.
    SerialStub::pluggedValue = false;
    for (int i = 0; i < 5; i++) {
        TEST_ASSERT_EQUAL(CONSOLE_HOST_POLL_HOLD, poll());
    }
    SerialStub::pluggedValue = true;

    // A whole window again from here, not the remaining half.
    int held = 0;
    ConsoleHostPoll r = poll();
    while (r == CONSOLE_HOST_POLL_HOLD) {
        held++;
        r = poll();
    }
    TEST_ASSERT_EQUAL_INT(pollsInWindow(), held);
}

// A one-poll gap inside an open window neither cancels nor restarts it.
void test_one_poll_gap_inside_the_window_changes_nothing(void) {
    detachAfterLiveSession();
    SerialStub::pluggedValue = true;

    for (int i = 0; i < 10; i++) {
        TEST_ASSERT_EQUAL(CONSOLE_HOST_POLL_HOLD, poll());
    }
    SerialStub::pluggedValue = false;
    TEST_ASSERT_EQUAL(CONSOLE_HOST_POLL_HOLD, poll());
    SerialStub::pluggedValue = true;

    int held = 0;
    ConsoleHostPoll r = poll();
    while (r == CONSOLE_HOST_POLL_HOLD) {
        held++;
        r = poll();
    }
    // 10 + 1 polls already spent of the window.
    TEST_ASSERT_EQUAL_INT(pollsInWindow() - 11, held);
}

// The window survives a millis() wrap: unsigned subtraction, one window.
void test_window_survives_millis_wrap(void) {
    detachAfterLiveSession();
    g_nowMs = 0xFFFFFFFFu - 3 * POLL_MS;
    SerialStub::pluggedValue = true;

    int held = 0;
    ConsoleHostPoll r = poll();
    while (r == CONSOLE_HOST_POLL_HOLD) {
        held++;
        TEST_ASSERT_TRUE_MESSAGE(held <= pollsInWindow() + 1, "a wrap must not hold for ever");
        r = poll();
    }
    TEST_ASSERT_EQUAL_INT(pollsInWindow(), held);
}

// edgeThisPoll is the signal console_task.cpp keys the stale-`serial_in_empty`
// register clear off (#275 critic pass 1): true only on the genuine debounced
// edge (window open), true again on a restart, and NEVER on a one-poll flap -
// which is what keeps the clear from touching a live IN-empty during active TX.
void test_edge_signal_fires_only_on_the_genuine_debounced_edge(void) {
    detachAfterLiveSession();
    // No edge during the detach's steady unplugged polls.
    TEST_ASSERT_FALSE(g_st.edgeThisPoll);

    SerialStub::pluggedValue = true;
    poll();  // the genuine edge: window opens
    TEST_ASSERT_TRUE_MESSAGE(g_st.edgeThisPoll, "the genuine plugged edge must signal edgeThisPoll");

    // Not again while the window simply holds.
    for (int i = 0; i < 3; i++) {
        poll();
        TEST_ASSERT_FALSE_MESSAGE(g_st.edgeThisPoll, "holding is not an edge");
    }
}

void test_edge_signal_never_fires_on_a_one_poll_flap(void) {
    SerialStub::pluggedValue = true;
    SerialStub::connectedValue = true;
    consoleHostPresenceInit(&g_st);
    poll();
    SerialStub::pluggedValue = false;
    poll();  // one unplugged poll only
    SerialStub::pluggedValue = true;
    for (int i = 0; i < 5; i++) {
        poll();
        TEST_ASSERT_FALSE_MESSAGE(g_st.edgeThisPoll, "a one-poll flap is not a genuine edge");
    }
}

void test_edge_signal_fires_again_on_a_restart(void) {
    detachAfterLiveSession();
    SerialStub::pluggedValue = true;
    poll();
    TEST_ASSERT_TRUE(g_st.edgeThisPoll);  // first edge
    // Second bus reset inside the window: unplugged for several polls, then back.
    SerialStub::pluggedValue = false;
    for (int i = 0; i < 5; i++) poll();
    SerialStub::pluggedValue = true;
    poll();
    TEST_ASSERT_TRUE_MESSAGE(g_st.edgeThisPoll, "a second reset must re-signal the edge to re-clear the stale bit");
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_genuine_edge_holds_off_every_serial_read_for_the_window);
    RUN_TEST(test_after_the_window_the_attach_debounce_fires_exactly_once);
    RUN_TEST(test_one_poll_flap_starts_no_window);
    RUN_TEST(test_no_edge_leaves_every_call_count_as_before);
    RUN_TEST(test_first_attach_after_a_no_host_boot_gets_the_window);
    RUN_TEST(test_second_reset_inside_the_window_restarts_it);
    RUN_TEST(test_one_poll_gap_inside_the_window_changes_nothing);
    RUN_TEST(test_window_survives_millis_wrap);
    RUN_TEST(test_edge_signal_fires_only_on_the_genuine_debounced_edge);
    RUN_TEST(test_edge_signal_never_fires_on_a_one_poll_flap);
    RUN_TEST(test_edge_signal_fires_again_on_a_restart);
    return UNITY_END();
}
