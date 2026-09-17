// =============================================================================
// test/test_native/test_audio_chirp_manifest/test_audio_chirp_manifest.cpp
//
// CHIRP manifest handling against the module's real wire format (#397).
//
// The fixtures are this droid's card, from the spec sheet: one Bank 1
// directory holding 24 grouped sounds, plus 13 Bank 2-6 directories. That is
// what breaks GMAN: handleGman() enqueues sdBankCount + 4 = 17 lines into the
// module's 15-slot reply ring, which drops the OLDEST, so MDAT and BANK:1 are
// gone before the host reads a byte (CHIRP serial_commands.cpp,
// serial_queue.cpp). BANK:1 is the only line carrying Bank 1's count.
//
// LIST is the way back: handleList() writes straight to the UART and cannot be
// dropped, and its Bank 1 section opens with "Sounds: <n>".
// =============================================================================

#include <unity.h>

#include <stdint.h>
#include <string.h>

#include "../../../include/audio_chirp.h"
#include "../../../include/audio_serial_io.h"
#include "../../../include/dome_link.h"

extern DomeUartOwner g_test_dome_uart_owner;

// The 13 Bank 2-6 directories on this card: twelve Bank 2 pages and 3A_system.
static const char* kGmanSdBankLines =
    "BANK:2,2A_music,10\n"
    "BANK:2,2B_alert,4\n"
    "BANK:2,2C_chat,7\n"
    "BANK:2,2D_happy,5\n"
    "BANK:2,2E_sad,3\n"
    "BANK:2,2F_scream,2\n"
    "BANK:2,2G_whistle,6\n"
    "BANK:2,2H_misc,8\n"
    "BANK:2,2I_proc,4\n"
    "BANK:2,2J_razz,3\n"
    "BANK:2,2K_sing,5\n"
    "BANK:2,2L_hum,2\n"
    "BANK:3,3A_system,9\n";

// What the queue overflow leaves of the reply: no MDAT, no BANK:1.
static const char* kGmanTruncatedTail =
    "MSUM:2712847316\n"
    "MEND\n";

// handleList()'s dump, verbatim in shape: a leading blank line from
// println("\n=== Bank 1 (Flash) ==="), the count, up to ten grouped Bank 1
// names, then one line per SD bank and a trailing blank line.
static const char* kListDumpBank1 =
    "\n=== Bank 1 (Flash) ===\n"
    "Sounds: 24\n"
    "   1. general01 (3 variants)\n"
    "   2. happy (1 variants)\n"
    "   3. sad (2 variants)\n"
    "  ... and 21 more\n";

static const char* kListDumpSdSection =
    "\n=== Banks 2-6 (SD) ===\n"
    "Bank 2A: 2A_music (10 files)\n"
    "Bank 2B: 2B_alert (4 files)\n"
    "Bank 3A: 3A_system (9 files)\n"
    "\n";

enum ScriptMode {
    SCRIPT_GMAN_TRUNCATED = 0,  // this droid's card: MDAT and BANK:1 dropped
    SCRIPT_GMAN_COMPLETE,       // a small card whose whole reply fits the queue
    SCRIPT_LIST_WITHOUT_COUNT,  // truncated GMAN, and LIST answers without a count
};

struct ScriptedChirpIO {
    static constexpr int TX_BUF = 512;
    static constexpr int RX_BUF = 2048;
    static constexpr int CMD_BUF = 40;

    char txBuf[TX_BUF];
    int txCount;
    char cmdBuf[CMD_BUF];
    int cmdLen;
    uint8_t rxBuf[RX_BUF];
    int rxCount;
    int rxPos;
    uint32_t fakeTimeMs;
    ScriptMode mode;
    int listCommands;

    void reset() {
        memset(txBuf, 0, sizeof(txBuf));
        memset(cmdBuf, 0, sizeof(cmdBuf));
        memset(rxBuf, 0, sizeof(rxBuf));
        txCount = cmdLen = rxCount = rxPos = listCommands = 0;
        fakeTimeMs = 0;
        mode = SCRIPT_GMAN_TRUNCATED;
    }

    void appendRx(const char* s) {
        for (; s != nullptr && *s != '\0' && rxCount < RX_BUF; ++s) {
            rxBuf[rxCount++] = (uint8_t)*s;
        }
    }

    void finishCommand() {
        cmdBuf[cmdLen] = '\0';
        if (strcmp(cmdBuf, "GMAN") == 0) {
            if (mode == SCRIPT_GMAN_COMPLETE) {
                appendRx("MDAT:3\n");
                appendRx("BANK:1,1A_general,24\n");
                appendRx("BANK:2,2A_music,10\n");
                appendRx("BANK:3,3A_system,9\n");
                appendRx(kGmanTruncatedTail);
            } else {
                appendRx(kGmanSdBankLines);
                appendRx(kGmanTruncatedTail);
            }
        } else if (strcmp(cmdBuf, "LIST") == 0) {
            ++listCommands;
            if (mode == SCRIPT_LIST_WITHOUT_COUNT) {
                // A dump with the heading but no count line: nothing to recover.
                appendRx("\n=== Bank 1 (Flash) ===\n");
                appendRx(kListDumpSdSection);
            } else {
                appendRx(kListDumpBank1);
                appendRx(kListDumpSdSection);
            }
        }
        cmdLen = 0;
        memset(cmdBuf, 0, sizeof(cmdBuf));
    }
};

static ScriptedChirpIO g_io;

static void scriptedWriteByte(uint8_t b) {
    if (g_io.txCount < ScriptedChirpIO::TX_BUF - 1) {
        g_io.txBuf[g_io.txCount++] = (char)b;
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
    g_io.fakeTimeMs += ms;
}

static uint32_t scriptedMillisNow() {
    return g_io.fakeTimeMs;
}

static AudioSerialIO makeScriptedIO() {
    return AudioSerialIO{scriptedWriteByte, scriptedRxAvailable, scriptedRxRead, scriptedDelayMs,
                         scriptedMillisNow};
}

static bool txContains(const char* needle) {
    return strstr(g_io.txBuf, needle) != nullptr;
}

void setUp() {
    g_io.reset();
    g_test_dome_uart_owner = DOME_UART_NONE;
}

void tearDown() {
    g_test_dome_uart_owner = DOME_UART_NONE;
}

// -----------------------------------------------------------------------------
// Truncated GMAN: the count comes back from LIST
// -----------------------------------------------------------------------------

void test_truncated_gman_recovers_bank1_count_from_list() {
    g_io.mode = SCRIPT_GMAN_TRUNCATED;
    AudioDriverChirp drv;
    drv.setIO(makeScriptedIO());

    TEST_ASSERT_TRUE(drv.begin(15));

    AudioModuleState ms{};
    drv.getCachedState(ms);
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(
        24, ms.totalTracks,
        "Total tracks must be Bank 1's count recovered from LIST, not the 0 a dropped BANK:1 left");
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, g_io.listCommands,
                                  "a truncated manifest must ask LIST exactly once");
}

void test_recovered_bank1_is_first_and_claims_no_directory() {
    g_io.mode = SCRIPT_GMAN_TRUNCATED;
    AudioDriverChirp drv;
    drv.setIO(makeScriptedIO());
    TEST_ASSERT_TRUE(drv.begin(15));

    // 13 SD rows off the wire plus the recovered Bank 1 row.
    TEST_ASSERT_EQUAL_UINT8(14, drv.getCatalogBankCount());

    const AudioCatalogBank* banks = drv.getCatalogBanks();
    TEST_ASSERT_NOT_NULL(banks);
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(1, banks[0].bank,
                                    "the recovered Bank 1 belongs at the front of the bank list");
    TEST_ASSERT_EQUAL_UINT16(24, banks[0].count);
    TEST_ASSERT_EQUAL_STRING_MESSAGE(
        "", banks[0].dirName,
        "LIST does not print Bank 1's directory, so no directory may be invented for it");
    // The SD rows that did arrive keep their own identity and order.
    TEST_ASSERT_EQUAL_UINT8(2, banks[1].bank);
    TEST_ASSERT_EQUAL_STRING("2A_music", banks[1].dirName);
    TEST_ASSERT_EQUAL_CHAR('A', banks[1].page);
    TEST_ASSERT_EQUAL_UINT8(3, banks[13].bank);
    TEST_ASSERT_EQUAL_STRING("3A_system", banks[13].dirName);
}

void test_recovery_consumes_the_whole_list_dump() {
    g_io.mode = SCRIPT_GMAN_TRUNCATED;
    AudioDriverChirp drv;
    drv.setIO(makeScriptedIO());
    TEST_ASSERT_TRUE(drv.begin(15));

    TEST_ASSERT_EQUAL_INT(1, g_io.listCommands);
    TEST_ASSERT_EQUAL_INT_MESSAGE(
        0, scriptedRxAvailable(),
        "the rest of the LIST dump must be drained here; left in the buffer it is read as the "
        "first GNME replies and costs those entries their names");
}

void test_list_without_a_count_leaves_total_tracks_unknown() {
    g_io.mode = SCRIPT_LIST_WITHOUT_COUNT;
    AudioDriverChirp drv;
    drv.setIO(makeScriptedIO());

    TEST_ASSERT_TRUE(drv.begin(15));

    AudioModuleState ms{};
    drv.getCachedState(ms);
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(0, ms.totalTracks,
                                     "nothing to recover means unknown, never a made-up count");
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(13, drv.getCatalogBankCount(),
                                    "the SD banks that did arrive are still usable");
    TEST_ASSERT_TRUE_MESSAGE(ms.linkOk, "a truncated manifest is still a live link");
}

// -----------------------------------------------------------------------------
// Complete GMAN: nothing is asked of LIST
// -----------------------------------------------------------------------------

void test_complete_manifest_does_not_ask_list() {
    g_io.mode = SCRIPT_GMAN_COMPLETE;
    AudioDriverChirp drv;
    drv.setIO(makeScriptedIO());

    TEST_ASSERT_TRUE(drv.begin(15));

    AudioModuleState ms{};
    drv.getCachedState(ms);
    TEST_ASSERT_EQUAL_UINT16(24, ms.totalTracks);
    TEST_ASSERT_EQUAL_UINT8(3, drv.getCatalogBankCount());
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, g_io.listCommands,
                                  "a complete BANK:1 line must not cost a LIST dump");
    TEST_ASSERT_FALSE(txContains("LIST"));

    const AudioCatalogBank* banks = drv.getCatalogBanks();
    TEST_ASSERT_EQUAL_STRING_MESSAGE("1A_general", banks[0].dirName,
                                     "an observed Bank 1 directory is kept as observed");
}

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;

    UNITY_BEGIN();
    RUN_TEST(test_truncated_gman_recovers_bank1_count_from_list);
    RUN_TEST(test_recovered_bank1_is_first_and_claims_no_directory);
    RUN_TEST(test_recovery_consumes_the_whole_list_dump);
    RUN_TEST(test_list_without_a_count_leaves_total_tracks_unknown);
    RUN_TEST(test_complete_manifest_does_not_ask_list);
    return UNITY_END();
}
