// =============================================================================
// test/test_native/test_audio_chirp_status/test_audio_chirp_status.cpp
//
// What the CHIRP module actually reports about playback (#397 work item 3).
//
// The fixtures are handleStat()'s real replies (CHIRP serial_commands.cpp):
// "STAT:playing,<full path>,<volume>" or "STAT:idle,,0", printed straight to
// the UART, carrying no stream number and carrying the path handlePlay() built
// -- "/flash/<file>", "/<bank1Dir>/<file>" or "/<bankDir>/<file>", never a
// bare catalog name.
// =============================================================================

#include <unity.h>

#include <stdint.h>
#include <string.h>

#include "../../../include/audio_chirp.h"
#include "../../../include/audio_serial_io.h"
#include "../../../include/dome_link.h"

extern DomeUartOwner g_test_dome_uart_owner;

struct ScriptedChirpIO {
    static constexpr int TX_BUF = 512;
    static constexpr int RX_BUF = 1024;
    static constexpr int CMD_BUF = 40;

    char txBuf[TX_BUF];
    int txCount;
    char cmdBuf[CMD_BUF];
    int cmdLen;
    uint8_t rxBuf[RX_BUF];
    int rxCount;
    int rxPos;
    uint32_t fakeTimeMs;
    // Per-stream STAT script. statReply[n] is what STAT:n answers (nullptr =
    // the stream never answers); statPrefix[n] is anything the module emits
    // before it, which is how a queued notification arrives mid-poll.
    const char* statReply[3];
    const char* statPrefix[3];
    int statQueries;

    void reset() {
        memset(txBuf, 0, sizeof(txBuf));
        memset(cmdBuf, 0, sizeof(cmdBuf));
        memset(rxBuf, 0, sizeof(rxBuf));
        txCount = cmdLen = rxCount = rxPos = statQueries = 0;
        fakeTimeMs = 0;
        for (int i = 0; i < 3; ++i) {
            statReply[i] = "STAT:idle,,0\n";
            statPrefix[i] = nullptr;
        }
    }

    void appendRx(const char* s) {
        for (; s != nullptr && *s != '\0' && rxCount < RX_BUF; ++s) {
            rxBuf[rxCount++] = (uint8_t)*s;
        }
    }

    void finishCommand() {
        cmdBuf[cmdLen] = '\0';
        if (strcmp(cmdBuf, "GMAN") == 0) {
            appendRx("MDAT:2\nBANK:1,1A_general,2\nBANK:2,2A_music,1\nMSUM:9\nMEND\n");
        } else if (strcmp(cmdBuf, "GNME:1,A,1") == 0) {
            appendRx("NAME:1,,1,general01.wav\n");
        } else if (strcmp(cmdBuf, "GNME:1,A,2") == 0) {
            appendRx("NAME:1,,2,happy.wav\n");
        } else if (strcmp(cmdBuf, "GNME:2,A,1") == 0) {
            appendRx("NAME:2,A,1,cantina.mp3\n");
        } else if (strncmp(cmdBuf, "STAT:", 5) == 0) {
            const int stream = cmdBuf[5] - '0';
            ++statQueries;
            if (stream >= 0 && stream < 3) {
                appendRx(statPrefix[stream]);
                appendRx(statReply[stream]);
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

void setUp() {
    g_io.reset();
    g_test_dome_uart_owner = DOME_UART_NONE;
}

void tearDown() {
    g_test_dome_uart_owner = DOME_UART_NONE;
}

// -----------------------------------------------------------------------------
// Play state is every default stream, not stream 0
// -----------------------------------------------------------------------------

void test_all_three_default_streams_are_asked() {
    AudioDriverChirp drv;
    drv.setIO(makeScriptedIO());

    AudioModuleState ms{};
    TEST_ASSERT_TRUE(drv.queryModuleState(ms));

    TEST_ASSERT_EQUAL_INT_MESSAGE(3, g_io.statQueries,
                                  "the module's default configuration is three streams");
    TEST_ASSERT_NOT_NULL(strstr(g_io.txBuf, "STAT:0\n"));
    TEST_ASSERT_NOT_NULL(strstr(g_io.txBuf, "STAT:1\n"));
    TEST_ASSERT_NOT_NULL(strstr(g_io.txBuf, "STAT:2\n"));
}

void test_stream_0_idle_while_stream_1_plays_is_playing() {
    g_io.statReply[0] = "STAT:idle,,0\n";
    g_io.statReply[1] = "STAT:playing,/2A_music/cantina.mp3,40\n";
    g_io.statReply[2] = "STAT:idle,,0\n";

    AudioDriverChirp drv;
    drv.setIO(makeScriptedIO());

    AudioModuleState ms{};
    TEST_ASSERT_TRUE(drv.queryModuleState(ms));
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(
        0x01, ms.playState,
        "a beep ending on stream 0 under running music must not report the module idle");
}

void test_all_streams_idle_is_stopped_with_no_current_track() {
    AudioDriverChirp drv;
    drv.setIO(makeScriptedIO());

    AudioModuleState ms{};
    TEST_ASSERT_TRUE(drv.queryModuleState(ms));
    TEST_ASSERT_EQUAL_UINT8(0x00, ms.playState);
    TEST_ASSERT_EQUAL_UINT16(0, ms.currentTrack);
}

void test_an_unanswered_stream_is_not_an_idle_stream() {
    g_io.statReply[0] = "STAT:idle,,0\n";
    g_io.statReply[1] = nullptr;  // this stream never answers
    g_io.statReply[2] = nullptr;

    AudioDriverChirp drv;
    drv.setIO(makeScriptedIO());

    AudioModuleState ms{};
    TEST_ASSERT_TRUE_MESSAGE(drv.queryModuleState(ms),
                             "one valid reply still proves the link is alive");
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(
        0xFF, ms.playState,
        "silence from two streams is not evidence that they are idle");
}

void test_queued_play_notification_is_not_status() {
    // handlePlay() enqueues "S:<n>,ply,<vol>" BEFORE startStream(), so it can
    // outlive a play that failed on a missing file. It must not decide a poll.
    g_io.statPrefix[0] = "PACK:PLAY\nS:0,ply,80\n";
    g_io.statReply[0] = "STAT:idle,,0\n";

    AudioDriverChirp drv;
    drv.setIO(makeScriptedIO());

    AudioModuleState ms{};
    TEST_ASSERT_TRUE(drv.queryModuleState(ms));
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(0x00, ms.playState,
                                    "a queued ply notification is not a stream's status");
}

// -----------------------------------------------------------------------------
// Current track is an observed catalog entry, never the last commanded index
// -----------------------------------------------------------------------------

void test_sd_bank_path_identifies_its_catalog_entry() {
    AudioDriverChirp drv;
    drv.setIO(makeScriptedIO());
    TEST_ASSERT_TRUE(drv.refreshCatalog());

    g_io.statReply[0] = "STAT:playing,/2A_music/cantina.mp3,40\n";

    AudioModuleState ms{};
    TEST_ASSERT_TRUE(drv.queryModuleState(ms));
    TEST_ASSERT_EQUAL_UINT8(0x01, ms.playState);
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(
        1, ms.currentTrack,
        "the path's directory names the bank and its leaf names the entry");
}

void test_flash_bank1_variant_path_identifies_its_group() {
    AudioDriverChirp drv;
    drv.setIO(makeScriptedIO());
    TEST_ASSERT_TRUE(drv.refreshCatalog());

    // The card holds general01_02.mp3; the module groups it as "general01" and
    // reports the catalog name as "general01.wav" whatever the real extension.
    g_io.statReply[0] = "STAT:playing,/flash/general01_02.mp3,40\n";

    AudioModuleState ms{};
    TEST_ASSERT_TRUE(drv.queryModuleState(ms));
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(
        1, ms.currentTrack,
        "a Bank 1 variant belongs to its group, and the forced .wav is not the file's format");
}

void test_unrecognised_path_while_playing_stays_unidentified() {
    AudioDriverChirp drv;
    drv.setIO(makeScriptedIO());
    TEST_ASSERT_TRUE(drv.refreshCatalog());

    g_io.statReply[0] = "STAT:playing,/9Z_nowhere/mystery.mp3,40\n";

    AudioModuleState ms{};
    TEST_ASSERT_TRUE(drv.queryModuleState(ms));
    TEST_ASSERT_EQUAL_UINT8(0x01, ms.playState);
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(0, ms.currentTrack,
                                     "playing but unidentified is honest; a stale echo is not");
}

void test_current_track_is_not_the_last_commanded_index() {
    AudioDriverChirp drv;
    drv.setIO(makeScriptedIO());
    TEST_ASSERT_TRUE(drv.refreshCatalog());

    drv.playTrackBanked(2, 1, 'A');  // commanded, and the module plays nothing

    AudioModuleState ms{};
    TEST_ASSERT_TRUE(drv.queryModuleState(ms));
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(
        0, ms.currentTrack,
        "an index we sent is a command, not an observation of playback");

    AudioModuleState cached{};
    drv.getCachedState(cached);
    TEST_ASSERT_EQUAL_UINT16(0, cached.currentTrack);
}

void test_stop_clears_play_state_and_current_track() {
    AudioDriverChirp drv;
    drv.setIO(makeScriptedIO());
    TEST_ASSERT_TRUE(drv.refreshCatalog());

    g_io.statReply[0] = "STAT:playing,/2A_music/cantina.mp3,40\n";
    AudioModuleState playing{};
    TEST_ASSERT_TRUE(drv.queryModuleState(playing));
    TEST_ASSERT_EQUAL_UINT16(1, playing.currentTrack);

    drv.stop();

    AudioModuleState cached{};
    drv.getCachedState(cached);
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(0x00, cached.playState,
                                    "a bare STOP stops every stream, so idle is observed");
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(0, cached.currentTrack,
                                     "nothing is playing after Stop, so nothing is current");
}

void test_oversized_playback_path_identifies_nothing() {
    AudioDriverChirp drv;
    drv.setIO(makeScriptedIO());
    TEST_ASSERT_TRUE(drv.refreshCatalog());

    // 66 characters: longer than the module's own char[64] filename, so
    // whatever this is did not come off the card intact. The reply still says
    // the stream is playing.
    g_io.statReply[0] =
        "STAT:playing,/2A_music/cantina_with_a_name_that_overruns_the_modules_buffer.mp3,40\n";

    AudioModuleState ms{};
    TEST_ASSERT_TRUE(drv.queryModuleState(ms));
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(0x01, ms.playState, "the reply still proves playback");
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(0, ms.currentTrack,
                                     "an observation this long identifies nothing");
}

void test_a_reply_too_long_to_frame_is_not_a_status() {
    AudioDriverChirp drv;
    drv.setIO(makeScriptedIO());

    // 103 characters, past the read buffer and so never terminated: the longest
    // reply handleStat() can print is 79. Nothing here is status.
    g_io.statReply[0] =
        "STAT:playing,/2A_music/"
        "cantina_with_an_absurdly_long_name_that_cannot_fit_the_modules_own_buffer.mp3,40\n";
    g_io.statReply[1] = nullptr;
    g_io.statReply[2] = nullptr;

    AudioModuleState ms{};
    drv.queryModuleState(ms);
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(
        0xFF, ms.playState,
        "a frame that never terminated is not evidence of anything, either way");
}

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;

    UNITY_BEGIN();
    RUN_TEST(test_all_three_default_streams_are_asked);
    RUN_TEST(test_stream_0_idle_while_stream_1_plays_is_playing);
    RUN_TEST(test_all_streams_idle_is_stopped_with_no_current_track);
    RUN_TEST(test_an_unanswered_stream_is_not_an_idle_stream);
    RUN_TEST(test_queued_play_notification_is_not_status);
    RUN_TEST(test_sd_bank_path_identifies_its_catalog_entry);
    RUN_TEST(test_flash_bank1_variant_path_identifies_its_group);
    RUN_TEST(test_unrecognised_path_while_playing_stays_unidentified);
    RUN_TEST(test_current_track_is_not_the_last_commanded_index);
    RUN_TEST(test_stop_clears_play_state_and_current_track);
    RUN_TEST(test_oversized_playback_path_identifies_nothing);
    RUN_TEST(test_a_reply_too_long_to_frame_is_not_a_status);
    return UNITY_END();
}
