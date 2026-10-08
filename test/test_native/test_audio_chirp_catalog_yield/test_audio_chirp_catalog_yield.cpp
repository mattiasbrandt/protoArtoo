// =============================================================================
// test_audio_chirp_catalog_yield
//
// Regression coverage for issue #15: CHIRP catalog refresh must yield even when
// every GMAN/GNME response is already waiting in RX. The ordinary readFrame()
// no-data delay path is not exercised in that case.
//
// Also the other half of that cooperation (#397 work item 13): yielding lets
// the REST of the core run, but it never hands the calling task its own command
// queue back, so a Stop sat queued behind a name walk that can occupy the task
// for minutes. The stalled-module fixture below is what that looks like -- the
// module answers GMAN and then answers no name at all, which is the 450 ms per
// sound case -- and the walk has to be able to give up inside it.
// =============================================================================

#include <unity.h>

#include <stdint.h>
#include <string.h>

#include "../../../include/audio_chirp.h"
#include "../../../include/audio_serial_io.h"
#include "../../../include/dome_uart_test_hooks.h"

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
    int gnmeCommands;
    bool stalledModule;  // answers GMAN, then never answers a name

    void reset() {
        memset(txBuf, 0, sizeof(txBuf));
        memset(cmdBuf, 0, sizeof(cmdBuf));
        memset(rxBuf, 0, sizeof(rxBuf));
        txCount = cmdLen = rxCount = rxPos = delayCallCount = gnmeCommands = 0;
        fakeTimeMs = 0;
        stalledModule = false;
    }

    void appendRx(const char* s) {
        for (; s && *s && rxCount < RX_BUF; ++s) {
            rxBuf[rxCount++] = (uint8_t)*s;
        }
    }

    void finishCommand() {
        cmdBuf[cmdLen] = '\0';
        // The module's real wire format: MDAT carries a bank count, and a Bank
        // 1 NAME reply carries no page field at all (CHIRP serial_commands.cpp
        // handleGman/handleGnme). A fixture with "NAME:1,A,..." in it cannot
        // catch the page handling this driver depends on.
        if (strncmp(cmdBuf, "GNME:", 5) == 0) {
            ++gnmeCommands;
        }
        if (stalledModule) {
            if (strcmp(cmdBuf, "GMAN") == 0) {
                appendRx("MDAT:1\nBANK:1,1A_general,50\nMSUM:2712847316\nMEND\n");
            }
            cmdLen = 0;
            memset(cmdBuf, 0, sizeof(cmdBuf));
            return;
        }
        if (strcmp(cmdBuf, "GMAN") == 0) {
            appendRx("MDAT:1\nBANK:1,1A_general,2\nMSUM:2712847316\nMEND\n");
        } else if (strcmp(cmdBuf, "GNME:1,A,1") == 0) {
            appendRx("NAME:1,,1,first.wav\n");
        } else if (strcmp(cmdBuf, "GNME:1,A,2") == 0) {
            appendRx("NAME:1,,2,second.wav\n");
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

// The walk asks this once per sound. Standing in for AudioTask's predicate,
// which answers "a stop was enqueued, or the droid is going to sleep".
static int g_interruptAfterGnmeCommands = 0;  // 0 = never interrupt

static bool interruptAfterSomeNames(void* /*ctx*/) {
    return g_interruptAfterGnmeCommands > 0 &&
           g_io.gnmeCommands >= g_interruptAfterGnmeCommands;
}

static bool txContains(const char* needle) {
    char copy[ScriptedChirpIO::TX_BUF + 1];
    const int n = (g_io.txCount < ScriptedChirpIO::TX_BUF) ? g_io.txCount : ScriptedChirpIO::TX_BUF;
    memcpy(copy, g_io.txBuf, (size_t)n);
    copy[n] = '\0';
    return strstr(copy, needle) != nullptr;
}

void setUp() {
    g_io.reset();
    g_interruptAfterGnmeCommands = 0;
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

void test_chirp_unparsed_bank_slots_retain_page_default() {
    // Regression test for issue #196: unparsed bank slots should retain page='A' default.
    // The memset() in loadManifestBanks() discarded this, leaving unparsed slots with page='\0'.
    // Value-initialization restores the declared default.

    AudioDriverChirp drv;
    drv.setIO(makeScriptedIO());

    // Refresh catalog (calls loadManifestBanks internally, which clears and populates m_catalogBanks)
    // The scripted IO in finishCommand() returns a manifest with only 1 bank: BANK:1,1A_general,2
    TEST_ASSERT_TRUE(drv.refreshCatalog());

    // Get the banks array
    const AudioCatalogBank* banks = drv.getCatalogBanks();
    TEST_ASSERT_NOT_NULL(banks);

    uint8_t parsedCount = drv.getCatalogBankCount();
    TEST_ASSERT_EQUAL_UINT8(1, parsedCount);

    // Check that the parsed bank has the correct page value derived from dirName
    // BANK:1,1A_general,2 -> page = 'A' (from "1A_general")
    TEST_ASSERT_EQUAL_CHAR('A', banks[0].page);

    // The key assertion: unparsed slots (beyond parsedCount) should have page='A' (the default)
    // With memset bug: page would be '\0' (all bytes zeroed)
    // With value-init fix: page is 'A' (declared default preserved)
    TEST_ASSERT_EQUAL_CHAR('A', banks[1].page);  // First unparsed slot (beyond parsedCount)
}

// -----------------------------------------------------------------------------
// A stalled name walk gives up when its caller needs the task back
// -----------------------------------------------------------------------------

void test_a_stalled_name_walk_stops_when_the_caller_asks() {
    g_io.stalledModule = true;
    g_interruptAfterGnmeCommands = 2;

    AudioDriverChirp drv;
    drv.setIO(makeScriptedIO());
    drv.setCatalogInterrupt(interruptAfterSomeNames, nullptr);

    TEST_ASSERT_FALSE(drv.refreshCatalog());
    TEST_ASSERT_EQUAL_INT_MESSAGE(
        AudioCatalogRefreshOutcome::Interrupted, drv.lastCatalogRefreshOutcome(),
        "an interrupted walk has to be distinguishable from a module that did not answer at all");
    TEST_ASSERT_EQUAL_INT_MESSAGE(
        2, g_io.gnmeCommands,
        "the bank holds 50 sounds at 450 ms each; the walk must give up at the next sound, not "
        "at the end of the catalog");
}

void test_an_interrupted_walk_is_not_a_refreshed_catalog() {
    g_io.stalledModule = true;
    g_interruptAfterGnmeCommands = 1;

    AudioDriverChirp drv;
    drv.setIO(makeScriptedIO());
    drv.setCatalogInterrupt(interruptAfterSomeNames, nullptr);

    TEST_ASSERT_FALSE(drv.refreshCatalog());
    TEST_ASSERT_FALSE_MESSAGE(
        drv.isCatalogReady(),
        "the entry array was already overwritten in place, so there is no earlier catalog left "
        "to keep and a half-walked one must not be served as a refreshed one");
}

void test_the_walk_writes_nothing_but_its_own_queries_when_interrupted() {
    g_io.stalledModule = true;
    g_interruptAfterGnmeCommands = 1;

    AudioDriverChirp drv;
    drv.setIO(makeScriptedIO());
    drv.setCatalogInterrupt(interruptAfterSomeNames, nullptr);
    TEST_ASSERT_FALSE(drv.refreshCatalog());

    TEST_ASSERT_FALSE_MESSAGE(
        txContains("STOP"),
        "the audio TX path has exactly one writer; the driver reports the interruption and lets "
        "that writer act on it, rather than acting on it from inside the walk");
}

void test_an_uninterrupted_walk_still_runs_to_the_end() {
    AudioDriverChirp drv;
    drv.setIO(makeScriptedIO());
    drv.setCatalogInterrupt(interruptAfterSomeNames, nullptr);  // armed, never fires

    TEST_ASSERT_TRUE(drv.refreshCatalog());
    TEST_ASSERT_EQUAL_INT(AudioCatalogRefreshOutcome::Complete, drv.lastCatalogRefreshOutcome());
    TEST_ASSERT_EQUAL_UINT16(2, drv.getCatalogEntryCount());
}

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;

    UNITY_BEGIN();
    RUN_TEST(test_chirp_catalog_refresh_yields_with_immediate_rx);
    RUN_TEST(test_chirp_unparsed_bank_slots_retain_page_default);
    RUN_TEST(test_a_stalled_name_walk_stops_when_the_caller_asks);
    RUN_TEST(test_an_interrupted_walk_is_not_a_refreshed_catalog);
    RUN_TEST(test_the_walk_writes_nothing_but_its_own_queries_when_interrupted);
    RUN_TEST(test_an_uninterrupted_walk_still_runs_to_the_end);
    return UNITY_END();
}
