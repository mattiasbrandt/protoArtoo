// =============================================================================
// test/test_native/test_audio_chirp_uart_guard/test_audio_chirp_uart_guard.cpp
//
// Tests for AudioDriverChirp UART2 ownership guard (T49 Slice C).
//
// Verifies that CHIRP RX work bails out cleanly when DomeLink owns UART2,
// while TX-side startup still completes so playback commands remain available.
//
// Test seam: g_test_dome_uart_owner in native_test_stubs.cpp controls the
// return value of domeUartOwnedBy() without requiring FreeRTOS or robotState.
// =============================================================================

#include <unity.h>

#include <stdint.h>
#include <string.h>

#include "../../../include/audio_serial_io.h"
#include "../../../include/audio_chirp.h"
#include "../../../include/dome_link.h"

// Seam declared in native_test_stubs.cpp — set DOME_UART_DOME to simulate
// DomeLink holding UART2; reset to DOME_UART_NONE in tearDown.
extern DomeUartOwner g_test_dome_uart_owner;

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

static bool txContains(const char* needle) {
    const size_t needleLen = strlen(needle);
    if (needleLen == 0 || g_rec.txCount < (int)needleLen) {
        return false;
    }
    for (int i = 0; i <= g_rec.txCount - (int)needleLen; ++i) {
        if (memcmp(&g_rec.txBuf[i], needle, needleLen) == 0) {
            return true;
        }
    }
    return false;
}

void setUp()    { g_rec.reset(); g_test_dome_uart_owner = DOME_UART_NONE; }
void tearDown() { g_test_dome_uart_owner = DOME_UART_NONE; }

// =============================================================================
// queryModuleState() guard
// =============================================================================

void test_query_emits_no_bytes_when_dome_owns_uart() {
    AudioDriverChirp drv;
    drv.setIO(makeIO());

    g_test_dome_uart_owner = DOME_UART_DOME;
    AudioModuleState ms{};
    drv.queryModuleState(ms);

    TEST_ASSERT_EQUAL_INT_MESSAGE(0, g_rec.txCount,
        "queryModuleState must not emit bytes when dome owns UART2");
}

void test_query_emits_stat_command_when_uart_available() {
    AudioDriverChirp drv;
    drv.setIO(makeIO());

    g_test_dome_uart_owner = DOME_UART_NONE;
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
// begin() RX guard
// =============================================================================

void test_begin_returns_true_sends_volume_and_skips_gman_when_dome_owns_uart() {
    AudioDriverChirp drv;
    drv.setIO(makeIO());

    g_test_dome_uart_owner = DOME_UART_DOME;
    bool result = drv.begin(15);

    TEST_ASSERT_TRUE_MESSAGE(result,
        "begin must complete TX-side init when DomeLink owns UART2");
    TEST_ASSERT_GREATER_THAN_INT_MESSAGE(0, g_rec.delayCallCount,
        "begin must still wait for CHIRP boot before applying volume");
    TEST_ASSERT_GREATER_THAN_INT_MESSAGE(0, g_rec.txCount,
        "begin must send boot volume when DomeLink owns UART2");
    TEST_ASSERT_FALSE_MESSAGE(txContains("GMAN"),
        "begin must skip GMAN when DomeLink owns UART2");
}

void test_begin_returns_true_calls_delay_and_queries_gman_when_uart_available() {
    AudioDriverChirp drv;
    drv.setIO(makeIO());

    g_test_dome_uart_owner = DOME_UART_NONE;
    bool result = drv.begin(15);

    TEST_ASSERT_TRUE_MESSAGE(result,
        "begin must return true when UART2 is available (init complete)");
    TEST_ASSERT_GREATER_THAN_INT_MESSAGE(0, g_rec.delayCallCount,
        "begin must call delayMs when UART2 is available");
    TEST_ASSERT_TRUE_MESSAGE(txContains("GMAN"),
        "begin must query GMAN when UART2 is available");
}

// =============================================================================
// classifyRxStatus() CHIRP override
// =============================================================================

void test_classify_rx_status_available_when_link_ok() {
    AudioDriverChirp drv;
    drv.setIO(makeIO());

    // linkOk=true must return AVAILABLE regardless of UART ownership
    g_test_dome_uart_owner = DOME_UART_DOME;
    TEST_ASSERT_EQUAL_UINT8(AUDIO_RX_AVAILABLE, drv.classifyRxStatus(true));

    g_test_dome_uart_owner = DOME_UART_NONE;
    TEST_ASSERT_EQUAL_UINT8(AUDIO_RX_AVAILABLE, drv.classifyRxStatus(true));
}

void test_classify_rx_status_blocked_when_dome_owns_uart() {
    AudioDriverChirp drv;
    drv.setIO(makeIO());

    g_test_dome_uart_owner = DOME_UART_DOME;
    TEST_ASSERT_EQUAL_UINT8(AUDIO_RX_BLOCKED_BY_DOME_UART, drv.classifyRxStatus(false));
}

void test_classify_rx_status_no_response_when_uart_available() {
    AudioDriverChirp drv;
    drv.setIO(makeIO());

    g_test_dome_uart_owner = DOME_UART_NONE;
    TEST_ASSERT_EQUAL_UINT8(AUDIO_RX_NO_RESPONSE, drv.classifyRxStatus(false));
}

// =============================================================================

int main(int argc, char** argv) {
    UNITY_BEGIN();
    RUN_TEST(test_query_emits_no_bytes_when_dome_owns_uart);
    RUN_TEST(test_query_emits_stat_command_when_uart_available);
    RUN_TEST(test_begin_returns_true_sends_volume_and_skips_gman_when_dome_owns_uart);
    RUN_TEST(test_begin_returns_true_calls_delay_and_queries_gman_when_uart_available);
    RUN_TEST(test_classify_rx_status_available_when_link_ok);
    RUN_TEST(test_classify_rx_status_blocked_when_dome_owns_uart);
    RUN_TEST(test_classify_rx_status_no_response_when_uart_available);
    return UNITY_END();
}
