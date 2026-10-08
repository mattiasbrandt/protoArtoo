// =============================================================================
// test_audio_chirp_frames
//
// Frame completion and catalog completeness for the CHIRP driver (#397 work
// item 12).
//
// The module's replies are paced by the wire, not delivered whole: a queued
// reply can start arriving near the end of a read window, and at 9600 baud a
// 20-character NAME line takes ~21 ms to finish. A reader that returns whatever
// it has when its window expires hands the parser half a line -- and half of
// "NAME:1,,1,general01.wav" parses, so the card gets a track called "genera".
// Half of "Sounds: 24" is a count of 2.
//
// The scripted I/O below therefore RELEASES BYTES OVER FAKE TIME rather than
// dropping a whole response into the buffer, which is the only way a test can
// land on that boundary at all.
//
// The second half of the suite is the other side of item 12: a catalog that is
// usable is not automatically whole, and what it is missing has to be
// countable rather than only loggable.
// =============================================================================

#include <unity.h>

#include <stdint.h>
#include <string.h>

#include "../../../include/audio_chirp.h"
#include "../../../include/audio_serial_io.h"
#include "../../../include/dome_uart_test_hooks.h"

enum FrameScript {
    SCRIPT_PACED_NAME = 0,      // one Bank 1 name, released one byte per bytePeriodMs
    SCRIPT_PACED_LIST_COUNT,    // truncated GMAN, then a paced LIST dump
    SCRIPT_OVERSIZED_THEN_NAME, // a line longer than any frame, then the real reply
    SCRIPT_COMPLETE_MANIFEST,   // every bank row the module announced arrives
    SCRIPT_TRUNCATED_MANIFEST,  // MDAT and BANK:1 lost to the module's reply queue
    SCRIPT_NO_NAMES,            // banks answer GMAN, no GNME reply ever comes back
    SCRIPT_OVER_THE_ENTRY_CAP,  // one bank with more sounds than the entry array holds
    SCRIPT_BANK1_NO_DIRECTORY,  // "BANK:1,,0": a card with no 1x_ directory
};

struct PacedChirpIO {
    static constexpr int TX_BUF = 512;
    static constexpr int RX_BUF = 16384;
    static constexpr int CMD_BUF = 40;

    char txBuf[TX_BUF];
    int txCount;
    char cmdBuf[CMD_BUF];
    int cmdLen;
    uint8_t rxBuf[RX_BUF];
    uint32_t rxAtMs[RX_BUF];  // when each byte becomes readable
    int rxCount;
    int rxPos;
    uint32_t fakeTimeMs;
    uint32_t lastArrivalMs;
    uint32_t bytePeriodMs;  // 0 = the whole response is readable immediately
    FrameScript mode;
    int listCommands;
    int gnmeCommands;

    void reset() {
        memset(txBuf, 0, sizeof(txBuf));
        memset(cmdBuf, 0, sizeof(cmdBuf));
        memset(rxBuf, 0, sizeof(rxBuf));
        memset(rxAtMs, 0, sizeof(rxAtMs));
        txCount = cmdLen = rxCount = rxPos = listCommands = gnmeCommands = 0;
        fakeTimeMs = 0;
        lastArrivalMs = 0;
        bytePeriodMs = 0;
        mode = SCRIPT_PACED_NAME;
    }

    void appendRx(const char* s) {
        for (; s != nullptr && *s != '\0' && rxCount < RX_BUF; ++s) {
            uint32_t at = fakeTimeMs;
            if (bytePeriodMs > 0) {
                at = (lastArrivalMs > fakeTimeMs ? lastArrivalMs : fakeTimeMs) + bytePeriodMs;
                lastArrivalMs = at;
            }
            rxAtMs[rxCount] = at;
            rxBuf[rxCount++] = (uint8_t)*s;
        }
    }

    int readable() const {
        int n = 0;
        for (int i = rxPos; i < rxCount; ++i) {
            if (rxAtMs[i] > fakeTimeMs) break;
            ++n;
        }
        return n;
    }

    void answerGnme(const char* args) {
        ++gnmeCommands;
        switch (mode) {
            case SCRIPT_PACED_NAME:
            case SCRIPT_OVERSIZED_THEN_NAME:
                if (strcmp(args, "1,A,1") == 0) {
                    if (mode == SCRIPT_OVERSIZED_THEN_NAME) {
                        // Longer than m_rxLine, with its own terminator: the
                        // driver must drop it whole and recover on the next.
                        appendRx(
                            "NAME:1,,1,"
                            "0123456789012345678901234567890123456789"
                            "0123456789012345678901234567890123456789"
                            "0123456789012345678901234567890123456789"
                            "0123456789012345678901234567890123456789\n");
                    }
                    appendRx("NAME:1,,1,general01.wav\n");
                }
                break;
            case SCRIPT_OVER_THE_ENTRY_CAP: {
                unsigned bank = 0, index = 0;
                char page = 'A';
                if (sscanf(args, "%u,%c,%u", &bank, &page, &index) == 3) {
                    char line[48];
                    snprintf(line, sizeof(line), "NAME:%u,%c,%u,s%u.wav", bank, page, index, index);
                    appendRx(line);
                    appendRx("\n");
                }
                break;
            }
            case SCRIPT_COMPLETE_MANIFEST:
            case SCRIPT_TRUNCATED_MANIFEST:
                if (strncmp(args, "2,A,", 4) == 0) {
                    appendRx("NAME:2,A,1,cantina.mp3\n");
                }
                break;
            default:
                break;  // SCRIPT_NO_NAMES and the manifest-only scripts answer nothing
        }
    }

    void finishCommand() {
        cmdBuf[cmdLen] = '\0';
        if (strcmp(cmdBuf, "GMAN") == 0) {
            switch (mode) {
                case SCRIPT_PACED_NAME:
                case SCRIPT_OVERSIZED_THEN_NAME:
                    appendRx("MDAT:1\nBANK:1,1A_general,1\nMSUM:9\nMEND\n");
                    break;
                case SCRIPT_PACED_LIST_COUNT:
                    appendRx("BANK:2,2A_music,1\nMSUM:9\nMEND\n");
                    break;
                case SCRIPT_COMPLETE_MANIFEST:
                    appendRx("MDAT:2\nBANK:1,1A_general,0\nBANK:2,2A_music,1\nMSUM:9\nMEND\n");
                    break;
                case SCRIPT_TRUNCATED_MANIFEST:
                    // No MDAT and no BANK:1: what the module's 15-slot reply
                    // ring leaves of a 13-directory card's manifest.
                    appendRx("BANK:2,2A_music,1\nMSUM:9\nMEND\n");
                    break;
                case SCRIPT_NO_NAMES:
                    appendRx("MDAT:2\nBANK:1,1A_general,2\nBANK:2,2A_music,1\nMSUM:9\nMEND\n");
                    break;
                case SCRIPT_OVER_THE_ENTRY_CAP:
                    appendRx("MDAT:1\nBANK:1,1A_general,301\nMSUM:9\nMEND\n");
                    break;
                case SCRIPT_BANK1_NO_DIRECTORY:
                    appendRx("MDAT:2\nBANK:1,,0\nBANK:2,2A_music,1\nMSUM:9\nMEND\n");
                    break;
            }
        } else if (strncmp(cmdBuf, "GNME:", 5) == 0) {
            answerGnme(cmdBuf + 5);
        } else if (strcmp(cmdBuf, "LIST") == 0) {
            ++listCommands;
            appendRx("\n=== Bank 1 (Flash) ===\nSounds: 24\n   1. general01 (3 variants)\n\n");
        }
        cmdLen = 0;
        memset(cmdBuf, 0, sizeof(cmdBuf));
    }
};

static PacedChirpIO g_io;

static void pacedWriteByte(uint8_t b) {
    if (g_io.txCount < PacedChirpIO::TX_BUF - 1) {
        g_io.txBuf[g_io.txCount++] = (char)b;
    }
    if (b == '\n') {
        g_io.finishCommand();
        return;
    }
    if (g_io.cmdLen < PacedChirpIO::CMD_BUF - 1) {
        g_io.cmdBuf[g_io.cmdLen++] = (char)b;
    }
}

static int pacedRxAvailable() { return g_io.readable(); }

static int pacedRxRead() {
    if (g_io.readable() <= 0) return -1;
    return g_io.rxBuf[g_io.rxPos++];
}

static void pacedDelayMs(uint32_t ms) { g_io.fakeTimeMs += ms; }
static uint32_t pacedMillisNow() { return g_io.fakeTimeMs; }

static AudioSerialIO makePacedIO() {
    return AudioSerialIO{pacedWriteByte, pacedRxAvailable, pacedRxRead, pacedDelayMs,
                         pacedMillisNow};
}

void setUp() {
    g_io.reset();
    g_test_dome_uart_owner = DOME_UART_NONE;
}

void tearDown() {
    g_test_dome_uart_owner = DOME_UART_NONE;
}

// -----------------------------------------------------------------------------
// Frame completion
// -----------------------------------------------------------------------------

// 10 ms per byte puts "NAME:1,,1,general01.wav" (23 bytes) at ~230 ms, well
// past the 120 ms window one readFrame() call waits, so the name can only
// arrive by being held across windows inside the GNME deadline.
void test_a_name_split_across_read_windows_is_one_whole_name() {
    g_io.mode = SCRIPT_PACED_NAME;
    g_io.bytePeriodMs = 10;
    AudioDriverChirp drv;
    drv.setIO(makePacedIO());

    TEST_ASSERT_TRUE(drv.refreshCatalog());
    TEST_ASSERT_EQUAL_UINT16(1, drv.getCatalogEntryCount());
    TEST_ASSERT_EQUAL_STRING_MESSAGE(
        "general01.wav", drv.getCatalogEntries()[0].name,
        "a reply that spans two read windows is one name, never the prefix that had arrived "
        "when the first window expired");
}

void test_a_count_split_across_read_windows_is_the_whole_count() {
    g_io.mode = SCRIPT_PACED_LIST_COUNT;
    g_io.bytePeriodMs = 11;
    AudioDriverChirp drv;
    drv.setIO(makePacedIO());

    TEST_ASSERT_TRUE(drv.begin(15));
    TEST_ASSERT_EQUAL_INT(1, g_io.listCommands);

    AudioModuleState ms{};
    drv.getCachedState(ms);
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(
        24, ms.totalTracks,
        "half of \"Sounds: 24\" is a count of 2; the count must be read from a whole line");
}

void test_an_oversized_frame_is_dropped_and_the_next_one_still_lands() {
    g_io.mode = SCRIPT_OVERSIZED_THEN_NAME;
    AudioDriverChirp drv;
    drv.setIO(makePacedIO());

    TEST_ASSERT_TRUE(drv.refreshCatalog());
    TEST_ASSERT_EQUAL_UINT16(1, drv.getCatalogEntryCount());
    TEST_ASSERT_EQUAL_STRING_MESSAGE(
        "general01.wav", drv.getCatalogEntries()[0].name,
        "an over-long line is consumed through its terminator so the next frame starts clean; "
        "a truncated head of it must never become a track name");
}

// -----------------------------------------------------------------------------
// Catalog completeness
// -----------------------------------------------------------------------------

void test_a_complete_manifest_reports_a_complete_catalog() {
    g_io.mode = SCRIPT_COMPLETE_MANIFEST;
    AudioDriverChirp drv;
    drv.setIO(makePacedIO());

    TEST_ASSERT_TRUE(drv.refreshCatalog());

    AudioCatalogCompleteness c{};
    drv.getCatalogCompleteness(c);
    TEST_ASSERT_TRUE(c.manifestComplete);
    TEST_ASSERT_EQUAL_UINT16(0, c.missingNameCount);
    TEST_ASSERT_FALSE(c.entryCapReached);
}

void test_a_truncated_manifest_is_a_usable_catalog_that_is_not_whole() {
    g_io.mode = SCRIPT_TRUNCATED_MANIFEST;
    AudioDriverChirp drv;
    drv.setIO(makePacedIO());

    TEST_ASSERT_TRUE(drv.refreshCatalog());
    TEST_ASSERT_TRUE_MESSAGE(drv.isCatalogReady(), "what did arrive is still usable");

    AudioCatalogCompleteness c{};
    drv.getCatalogCompleteness(c);
    TEST_ASSERT_FALSE_MESSAGE(
        c.manifestComplete,
        "valid link activity is not manifest completion: banks went missing and the catalog "
        "must say so rather than describing itself as complete");
}

void test_missing_names_are_counted_not_only_logged() {
    g_io.mode = SCRIPT_NO_NAMES;
    AudioDriverChirp drv;
    drv.setIO(makePacedIO());

    TEST_ASSERT_TRUE(drv.refreshCatalog());
    TEST_ASSERT_EQUAL_UINT16(3, drv.getCatalogEntryCount());

    AudioCatalogCompleteness c{};
    drv.getCatalogCompleteness(c);
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(
        3, c.missingNameCount,
        "every entry fell back to index_N, and a count of that is what lets the page say so");
    TEST_ASSERT_EQUAL_STRING("index_1", drv.getCatalogEntries()[0].name);
}

void test_the_entry_cap_is_reported_as_a_limitation() {
    g_io.mode = SCRIPT_OVER_THE_ENTRY_CAP;
    AudioDriverChirp drv;
    drv.setIO(makePacedIO());

    TEST_ASSERT_TRUE(drv.refreshCatalog());
    TEST_ASSERT_EQUAL_UINT16(AUDIO_CATALOG_MAX_ENTRIES, drv.getCatalogEntryCount());

    AudioCatalogCompleteness c{};
    drv.getCatalogCompleteness(c);
    TEST_ASSERT_TRUE_MESSAGE(
        c.entryCapReached,
        "the 301st sound is absent from the catalog with no row to say so; the cap has to "
        "travel with the catalog, not sit in a log line");
}

// -----------------------------------------------------------------------------
// A Bank 1 row the module sent without a directory
// -----------------------------------------------------------------------------

void test_a_bank1_row_without_a_directory_is_still_bank_1() {
    g_io.mode = SCRIPT_BANK1_NO_DIRECTORY;
    AudioDriverChirp drv;
    drv.setIO(makePacedIO());

    TEST_ASSERT_TRUE(drv.begin(15));

    TEST_ASSERT_EQUAL_UINT8_MESSAGE(2, drv.getCatalogBankCount(),
                                    "\"BANK:1,,0\" is the module reporting no 1x_ directory, "
                                    "not a malformed row");
    const AudioCatalogBank* banks = drv.getCatalogBanks();
    TEST_ASSERT_EQUAL_UINT8(1, banks[0].bank);
    TEST_ASSERT_EQUAL_STRING("", banks[0].dirName);
    TEST_ASSERT_EQUAL_INT_MESSAGE(
        0, g_io.listCommands,
        "the count was in the manifest, so nothing may be spent on a LIST dump to recover it");
}

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;

    UNITY_BEGIN();
    RUN_TEST(test_a_name_split_across_read_windows_is_one_whole_name);
    RUN_TEST(test_a_count_split_across_read_windows_is_the_whole_count);
    RUN_TEST(test_an_oversized_frame_is_dropped_and_the_next_one_still_lands);
    RUN_TEST(test_a_complete_manifest_reports_a_complete_catalog);
    RUN_TEST(test_a_truncated_manifest_is_a_usable_catalog_that_is_not_whole);
    RUN_TEST(test_missing_names_are_counted_not_only_logged);
    RUN_TEST(test_the_entry_cap_is_reported_as_a_limitation);
    RUN_TEST(test_a_bank1_row_without_a_directory_is_still_bank_1);
    return UNITY_END();
}
