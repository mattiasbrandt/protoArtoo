// =============================================================================
// test_audio_chirp_catalog_yield
//
// Regression coverage for issue #15: CHIRP catalog refresh must yield even when
// every GMAN/GNME response is already waiting in RX. The ordinary readLine()
// no-data delay path is not exercised in that case.
// =============================================================================

#include <unity.h>

#include <stdint.h>
#include <string.h>

#include "../../../include/audio_chirp.h"
#include "../../../include/audio_serial_io.h"
#include "../../../include/dome_link.h"

extern DomeUartOwner g_test_dome_uart_owner;

struct ScriptedChirpIO {
    static constexpr int TX_BUF = 256;
    static constexpr int RX_BUF = 512;
    static constexpr int CMD_BUF = 40;

    uint8_t txBuf[TX_BUF];
    int txCount;
    char cmdBuf[CMD_BUF];
    int cmdLen;
    uint8_t rxBuf[RX_BUF];
    int rxCount;
    int rxPos;
    uint32_t fakeTimeMs;
    int delayCallCount;

    void reset() {
        memset(txBuf, 0, sizeof(txBuf));
        memset(cmdBuf, 0, sizeof(cmdBuf));
        memset(rxBuf, 0, sizeof(rxBuf));
        txCount = cmdLen = rxCount = rxPos = delayCallCount = 0;
        fakeTimeMs = 0;
    }

    void appendRx(const char* s) {
        for (; s && *s && rxCount < RX_BUF; ++s) {
            rxBuf[rxCount++] = (uint8_t)*s;
        }
    }

    void finishCommand() {
        cmdBuf[cmdLen] = '\0';
        if (strcmp(cmdBuf, "GMAN") == 0) {
            appendRx("MDAT:CHIRP\nBANK:1,001A,2\nMEND\n");
        } else if (strcmp(cmdBuf, "GNME:1,A,1") == 0) {
            appendRx("NAME:1,A,1,first.wav\n");
        } else if (strcmp(cmdBuf, "GNME:1,A,2") == 0) {
            appendRx("NAME:1,A,2,second.wav\n");
        }
        cmdLen = 0;
        memset(cmdBuf, 0, sizeof(cmdBuf));
    }
};

static ScriptedChirpIO g_io;

static void scriptedWriteByte(uint8_t b) {
    if (g_io.txCount < ScriptedChirpIO::TX_BUF) {
        g_io.txBuf[g_io.txCount++] = b;
    }
    if (b == '\n') {
        g_io.finishCommand();
        return;
    }
    if (g_io.cmdLen < ScriptedChirpIO::CMD_BUF - 1) {
        g_io.cmdBuf[g_io.cmdLen++] = (char)b;
    }
}

static int scriptedRxAvailable() {
    return g_io.rxCount - g_io.rxPos;
}

static int scriptedRxRead() {
    return (g_io.rxPos < g_io.rxCount) ? g_io.rxBuf[g_io.rxPos++] : -1;
}

static void scriptedDelayMs(uint32_t ms) {
    ++g_io.delayCallCount;
    g_io.fakeTimeMs += ms;
}

static uint32_t scriptedMillisNow() {
    return g_io.fakeTimeMs;
}

static AudioSerialIO makeScriptedIO() {
    return AudioSerialIO{scriptedWriteByte, scriptedRxAvailable, scriptedRxRead,
                         scriptedDelayMs, scriptedMillisNow};
}

void setUp() {
    g_io.reset();
    g_test_dome_uart_owner = DOME_UART_NONE;
}

void tearDown() {
    g_test_dome_uart_owner = DOME_UART_NONE;
}

void test_chirp_catalog_refresh_yields_with_immediate_rx() {
    AudioDriverChirp drv;
    drv.setIO(makeScriptedIO());

    TEST_ASSERT_TRUE(drv.refreshCatalog());
    TEST_ASSERT_TRUE(drv.isCatalogReady());
    TEST_ASSERT_EQUAL_UINT16(2, drv.getCatalogEntryCount());
    TEST_ASSERT_GREATER_OR_EQUAL_INT_MESSAGE(
        6, g_io.delayCallCount,
        "catalog refresh must yield explicitly even when RX never waits");
}

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;

    UNITY_BEGIN();
    RUN_TEST(test_chirp_catalog_refresh_yields_with_immediate_rx);
    return UNITY_END();
}
