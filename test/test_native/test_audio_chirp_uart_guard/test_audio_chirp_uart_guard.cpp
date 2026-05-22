// =============================================================================
// test/test_native/test_audio_chirp_uart_guard/test_audio_chirp_uart_guard.cpp
//
// Tests for AudioDriverChirp UART2 ownership guard (T49 Slice C).
//
// Verifies that begin() and queryModuleState() bail out cleanly when the dome
// probe window holds UART2, rather than issuing commands into a driver that may
// be mid-teardown on the other core.
//
// Test seam: g_test_dome_uart_owned in native_test_stubs.cpp controls the
// return value of domeUartOwnedBy() without requiring FreeRTOS or robotState.
// =============================================================================

#include <unity.h>

#include <stdint.h>
#include <string.h>

#include "../../../include/audio_serial_io.h"
#include "../../../include/audio_chirp.h"

// Seam declared in native_test_stubs.cpp — set true to simulate an active
// dome probe window holding UART2.
extern bool g_test_dome_uart_owned;

// =============================================================================
// Minimal recording IO
// Records TX bytes and delay calls; RX always empty (simulates no module reply).
// millisNow() advances by 200 ms per call so all 300 ms query timeouts
// expire within 2-3 iterations.
// =============================================================================

struct MinRec {
    static constexpr int BUF = 64;
    uint8_t  txBuf[BUF];
    int      txCount;
    int      delayCallCount;
    uint32_t fakeTimeMs;

    void reset() {
        memset(txBuf, 0, sizeof(txBuf));
        txCount = delayCallCount = 0;
        fakeTimeMs = 0;
    }
};

static MinRec g_rec;

static void     r_writeByte(uint8_t b)  { if (g_rec.txCount < MinRec::BUF) g_rec.txBuf[g_rec.txCount++] = b; }
static int      r_rxAvailable()         { return 0; }
static int      r_rxRead()              { return -1; }
static void     r_delayMs(uint32_t ms)  { ++g_rec.delayCallCount; g_rec.fakeTimeMs += ms; }
static uint32_t r_millisNow()           { return (g_rec.fakeTimeMs += 200); }

static AudioSerialIO makeIO() {
    return AudioSerialIO{r_writeByte, r_rxAvailable, r_rxRead, r_delayMs, r_millisNow};
}

void setUp()    { g_rec.reset(); g_test_dome_uart_owned = false; }
void tearDown() { g_test_dome_uart_owned = false; }

// =============================================================================
// queryModuleState() guard
// =============================================================================

void test_query_emits_no_bytes_when_dome_owns_uart() {
    AudioDriverChirp drv;
    drv.setIO(makeIO());

    g_test_dome_uart_owned = true;
    AudioModuleState ms{};
    drv.queryModuleState(ms);

    TEST_ASSERT_EQUAL_INT_MESSAGE(0, g_rec.txCount,
        "queryModuleState must not emit bytes when dome owns UART2");
}

void test_query_emits_stat_command_when_uart_available() {
    AudioDriverChirp drv;
    drv.setIO(makeIO());

    g_test_dome_uart_owned = false;
    AudioModuleState ms{};
    drv.queryModuleState(ms);

    // sendCommand("STAT:0") emits 'S','T','A','T',':','0','\n' = 7 bytes
    const char expected[] = "STAT:0\n";
    TEST_ASSERT_EQUAL_INT_MESSAGE((int)strlen(expected), g_rec.txCount,
        "queryModuleState must emit exactly STAT:0\\n when UART2 is available");
    TEST_ASSERT_EQUAL_MEMORY_MESSAGE(expected, g_rec.txBuf, (int)strlen(expected),
        "queryModuleState TX content must be STAT:0\\n");
}

// =============================================================================
// begin() guard
// =============================================================================

void test_begin_returns_false_and_skips_io_when_dome_owns_uart() {
    AudioDriverChirp drv;
    drv.setIO(makeIO());

    g_test_dome_uart_owned = true;
    bool result = drv.begin(15);

    TEST_ASSERT_FALSE_MESSAGE(result,
        "begin must return false when dome owns UART2 (deferred init)");
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, g_rec.delayCallCount,
        "begin must not call delayMs when dome owns UART2");
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, g_rec.txCount,
        "begin must not emit any bytes when dome owns UART2");
}

void test_begin_returns_true_and_calls_delay_when_uart_available() {
    AudioDriverChirp drv;
    drv.setIO(makeIO());

    g_test_dome_uart_owned = false;
    bool result = drv.begin(15);

    TEST_ASSERT_TRUE_MESSAGE(result,
        "begin must return true when UART2 is available (init complete)");
    TEST_ASSERT_GREATER_THAN_INT_MESSAGE(0, g_rec.delayCallCount,
        "begin must call delayMs when UART2 is available");
}

// =============================================================================

int main(int argc, char** argv) {
    UNITY_BEGIN();
    RUN_TEST(test_query_emits_no_bytes_when_dome_owns_uart);
    RUN_TEST(test_query_emits_stat_command_when_uart_available);
    RUN_TEST(test_begin_returns_false_and_skips_io_when_dome_owns_uart);
    RUN_TEST(test_begin_returns_true_and_calls_delay_when_uart_available);
    return UNITY_END();
}
