// =============================================================================
// test/test_native/test_audio_io_seam/test_audio_io_seam.cpp
//
// Native integration tests for the AudioSerialIO seam introduced in T41.
//
// Tests instantiate each concrete driver, inject a RecordingSerialIO, and
// verify that the correct byte sequences are emitted for playTrack(), stop(),
// setVolume(), and begin(). No GPIO or hardware is required.
//
// RecordingSerialIO design:
//   AudioSerialIO uses raw function pointers so a capture-free static-global
//   harness is the only clean approach. g_rec holds all recorder state; the
//   four static callbacks delegate to it.
//
// Time model:
//   millisNow() advances fakeTimeMs by 200 ms per call so all send-loop
//   timeouts (300-500 ms) expire within 2-3 iterations — no busy-wait in tests.
//   delayMs() advances fakeTimeMs by the requested ms (delay is recorded but
//   does not block). Combined, every driver timeout expires in ≤3 loop ticks.
//
// DY-SV5W frame format reminder:
//   [0xAA][CMD][LEN][DATA...][SM]   SM = low 8 bits of sum of all payload bytes.
// =============================================================================

#include <unity.h>

#include <stdint.h>
#include <string.h>

#include "../../../include/audio_serial_io.h"
#include "../../../include/audio_dy_sv5w.h"
#include "../../../include/audio_chirp.h"
#include "../../../include/audio_mp3trigger.h"

// =============================================================================
// RecordingSerialIO
// =============================================================================

struct RecorderState {
    static constexpr int BUF = 256;
    uint8_t  txBuf[BUF];
    int      txCount;
    uint8_t  rxBuf[BUF];
    int      rxCount;
    int      rxPos;
    uint32_t delayTotalMs;
    int      delayCallCount;
    uint32_t fakeTimeMs;

    void reset() {
        memset(txBuf, 0, sizeof(txBuf));
        txCount = rxCount = rxPos = delayCallCount = 0;
        delayTotalMs = fakeTimeMs = 0;
    }

    void injectRx(const uint8_t* data, int len) {
        rxPos = 0;
        rxCount = 0;
        for (int i = 0; i < len && rxCount < BUF; ++i) {
            rxBuf[rxCount++] = data[i];
        }
    }

    // Build an ASCII RX response: useful for MP3Trigger 'S0'/'S1' responses.
    void injectRxString(const char* s) {
        rxPos = 0;
        rxCount = 0;
        for (; s && *s && rxCount < BUF; ++s) {
            rxBuf[rxCount++] = (uint8_t)*s;
        }
    }
};

static RecorderState g_rec;

static void     rec_writeByte(uint8_t b)  { if (g_rec.txCount < RecorderState::BUF) g_rec.txBuf[g_rec.txCount++] = b; }
static int      rec_rxAvailable()         { return g_rec.rxCount - g_rec.rxPos; }
static int      rec_rxRead()              { return (g_rec.rxPos < g_rec.rxCount) ? g_rec.rxBuf[g_rec.rxPos++] : -1; }
static void     rec_delayMs(uint32_t ms)  { g_rec.delayTotalMs += ms; ++g_rec.delayCallCount; g_rec.fakeTimeMs += ms; }
static uint32_t rec_millisNow()           { return (g_rec.fakeTimeMs += 200); }

static AudioSerialIO makeRecordingIO() {
    return AudioSerialIO{rec_writeByte, rec_rxAvailable, rec_rxRead, rec_delayMs, rec_millisNow};
}

// =============================================================================
// Helpers
// =============================================================================

// Compute DY-SV5W checksum (SM = low 8 bits of sum of all payload bytes).
static uint8_t dy_sm(const uint8_t* payload, uint8_t len) {
    uint8_t s = 0;
    for (uint8_t i = 0; i < len; i++) { s = (uint8_t)(s + payload[i]); }
    return s;
}

void setUp()    { g_rec.reset(); }
void tearDown() {}

// =============================================================================
// DY-SV5W tests
// =============================================================================

// playTrack(1) → frame [AA 07 02 00 01 SM] + 100 ms delay
void test_dysv5w_play_track_byte_sequence() {
    AudioDriverDySv5w drv;
    drv.setIO(makeRecordingIO());

    drv.playTrack(1);

    // AA 07 02 00 01 → SM = 0xB4
    const uint8_t expected[] = {0xAA, 0x07, 0x02, 0x00, 0x01, 0xB4};
    TEST_ASSERT_EQUAL_INT_MESSAGE(6, g_rec.txCount,
                                  "playTrack(1) must emit exactly 6 bytes");
    TEST_ASSERT_EQUAL_HEX8_ARRAY(expected, g_rec.txBuf, 6);
}

// playTrack(256) → 2-byte big-endian track, frame [AA 07 02 01 00 SM]
void test_dysv5w_play_track_high_byte() {
    AudioDriverDySv5w drv;
    drv.setIO(makeRecordingIO());

    drv.playTrack(256);

    const uint8_t expected[] = {0xAA, 0x07, 0x02, 0x01, 0x00, 0xB4};
    TEST_ASSERT_EQUAL_INT(6, g_rec.txCount);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(expected, g_rec.txBuf, 6);
}

// playTrack(0) → silent no-op; no bytes emitted
void test_dysv5w_play_track_zero_is_nop() {
    AudioDriverDySv5w drv;
    drv.setIO(makeRecordingIO());

    drv.playTrack(0);

    TEST_ASSERT_EQUAL_INT_MESSAGE(0, g_rec.txCount,
                                  "playTrack(0) must emit no bytes");
}

// stop() → frame [AA 04 00 AE] + 100 ms delay
void test_dysv5w_stop_byte_sequence() {
    AudioDriverDySv5w drv;
    drv.setIO(makeRecordingIO());

    drv.stop();

    // AA 04 00 → SM = 0xAE
    const uint8_t expected[] = {0xAA, 0x04, 0x00, 0xAE};
    TEST_ASSERT_EQUAL_INT_MESSAGE(4, g_rec.txCount,
                                  "stop() must emit exactly 4 bytes");
    TEST_ASSERT_EQUAL_HEX8_ARRAY(expected, g_rec.txBuf, 4);
}

// setVolume(0) → [AA 13 01 00 BE]
//   SM = 0xAA+0x13+0x01+0x00 = 0xBE
void test_dysv5w_set_volume_0_byte_sequence() {
    AudioDriverDySv5w drv;
    drv.setIO(makeRecordingIO());

    drv.setVolume(0);

    const uint8_t expected[] = {0xAA, 0x13, 0x01, 0x00, 0xBE};
    TEST_ASSERT_EQUAL_INT(5, g_rec.txCount);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(expected, g_rec.txBuf, 5);
}

// setVolume(15) → [AA 13 01 0F CD]
//   SM = 0xAA+0x13+0x01+0x0F = 170+19+1+15 = 205 = 0xCD
void test_dysv5w_set_volume_15_byte_sequence() {
    AudioDriverDySv5w drv;
    drv.setIO(makeRecordingIO());

    drv.setVolume(15);

    const uint8_t expected[] = {0xAA, 0x13, 0x01, 0x0F, 0xCD};
    TEST_ASSERT_EQUAL_INT(5, g_rec.txCount);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(expected, g_rec.txBuf, 5);
}

// setVolume(30) → [AA 13 01 1E DC]
//   SM = 0xAA+0x13+0x01+0x1E = 170+19+1+30 = 220 = 0xDC
void test_dysv5w_set_volume_max_byte_sequence() {
    AudioDriverDySv5w drv;
    drv.setIO(makeRecordingIO());

    drv.setVolume(30);

    const uint8_t expected[] = {0xAA, 0x13, 0x01, 0x1E, 0xDC};
    TEST_ASSERT_EQUAL_INT(5, g_rec.txCount);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(expected, g_rec.txBuf, 5);
}

// sendCommand posts a 100 ms delay via m_io.delayMs after every frame.
void test_dysv5w_command_posts_100ms_delay() {
    AudioDriverDySv5w drv;
    drv.setIO(makeRecordingIO());

    drv.playTrack(1);

    TEST_ASSERT_EQUAL_INT_MESSAGE(1, g_rec.delayCallCount,
                                  "one sendCommand must call delayMs once");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(100, g_rec.delayTotalMs,
                                     "the delay must be exactly 100 ms");
}

// DY-SV5W checksum helper self-test (verifies our test expectations are correct)
void test_dysv5w_checksum_formula() {
    const uint8_t stop_payload[] = {0xAA, 0x04, 0x00};
    TEST_ASSERT_EQUAL_HEX8(0xAE, dy_sm(stop_payload, 3));

    const uint8_t play1_payload[] = {0xAA, 0x07, 0x02, 0x00, 0x01};
    TEST_ASSERT_EQUAL_HEX8(0xB4, dy_sm(play1_payload, 5));

    const uint8_t vol15_payload[] = {0xAA, 0x13, 0x01, 0x0F};
    TEST_ASSERT_EQUAL_HEX8(0xCD, dy_sm(vol15_payload, 4));
}

// begin() uses the injected IO when setIO() was called first.
// Evidence: TX bytes are recorded (production IO would call softUartTxByte
// which is a no-op in native builds and bypasses the recorder entirely).
void test_dysv5w_begin_uses_injected_io() {
    AudioDriverDySv5w drv;
    drv.setIO(makeRecordingIO());

    drv.begin(10);

    TEST_ASSERT_TRUE_MESSAGE(g_rec.txCount > 0,
                              "begin() must use injected IO, not production IO");
    // First 4 bytes are the Q_DEV_ONLINE query frame: AA 09 00 B3
    const uint8_t q_dev[] = {0xAA, 0x09, 0x00, 0xB3};
    TEST_ASSERT_EQUAL_HEX8_ARRAY(q_dev, g_rec.txBuf, 4);
}

// =============================================================================
// MP3 Trigger tests
// =============================================================================

// playTrack(5) → ['t', 0x05]
void test_mp3trigger_play_track_byte_sequence() {
    AudioDriverMp3Trigger drv;
    drv.setIO(makeRecordingIO());

    drv.playTrack(5);

    TEST_ASSERT_EQUAL_INT_MESSAGE(2, g_rec.txCount, "playTrack(5) must emit 2 bytes");
    TEST_ASSERT_EQUAL_HEX8('t', g_rec.txBuf[0]);
    TEST_ASSERT_EQUAL_HEX8(0x05, g_rec.txBuf[1]);
}

// playTrack(255) → ['t', 0xFF]
void test_mp3trigger_play_track_255() {
    AudioDriverMp3Trigger drv;
    drv.setIO(makeRecordingIO());

    drv.playTrack(255);

    TEST_ASSERT_EQUAL_INT(2, g_rec.txCount);
    TEST_ASSERT_EQUAL_HEX8('t', g_rec.txBuf[0]);
    TEST_ASSERT_EQUAL_HEX8(0xFF, g_rec.txBuf[1]);
}

// playTrack(256) → dropped; no bytes emitted (overflow guard)
void test_mp3trigger_play_track_256_dropped() {
    AudioDriverMp3Trigger drv;
    drv.setIO(makeRecordingIO());

    drv.playTrack(256);

    TEST_ASSERT_EQUAL_INT_MESSAGE(0, g_rec.txCount,
                                  "playTrack(256) must emit no bytes — overflow guard");
}

// playTrack(0) → dropped; no bytes emitted
void test_mp3trigger_play_track_zero_dropped() {
    AudioDriverMp3Trigger drv;
    drv.setIO(makeRecordingIO());

    drv.playTrack(0);

    TEST_ASSERT_EQUAL_INT_MESSAGE(0, g_rec.txCount,
                                  "playTrack(0) must emit no bytes");
}

// stop() → ['t', 0xFE]  (MP3TRIGGER_STOP_TRACK = 254)
void test_mp3trigger_stop_byte_sequence() {
    AudioDriverMp3Trigger drv;
    drv.setIO(makeRecordingIO());

    drv.stop();

    TEST_ASSERT_EQUAL_INT_MESSAGE(2, g_rec.txCount, "stop() must emit 2 bytes");
    TEST_ASSERT_EQUAL_HEX8('t', g_rec.txBuf[0]);
    TEST_ASSERT_EQUAL_HEX8(0xFE, g_rec.txBuf[1]);
}

// setVolume(0) → ['v', 0xFF]  (vol=0 → nativeVol=(30-0)*255/30=255)
void test_mp3trigger_set_volume_0_byte_sequence() {
    AudioDriverMp3Trigger drv;
    drv.setIO(makeRecordingIO());

    drv.setVolume(0);

    TEST_ASSERT_EQUAL_INT(2, g_rec.txCount);
    TEST_ASSERT_EQUAL_HEX8('v', g_rec.txBuf[0]);
    TEST_ASSERT_EQUAL_HEX8(0xFF, g_rec.txBuf[1]);
}

// setVolume(30) → ['v', 0x00]  (vol=30 → nativeVol=(30-30)*255/30=0 = maximum)
void test_mp3trigger_set_volume_max_byte_sequence() {
    AudioDriverMp3Trigger drv;
    drv.setIO(makeRecordingIO());

    drv.setVolume(30);

    TEST_ASSERT_EQUAL_INT(2, g_rec.txCount);
    TEST_ASSERT_EQUAL_HEX8('v', g_rec.txBuf[0]);
    TEST_ASSERT_EQUAL_HEX8(0x00, g_rec.txBuf[1]);
}

// setVolume(15) → ['v', 0x7F]  (vol=15 → nativeVol=(30-15)*255/30=127)
void test_mp3trigger_set_volume_mid_byte_sequence() {
    AudioDriverMp3Trigger drv;
    drv.setIO(makeRecordingIO());

    drv.setVolume(15);

    TEST_ASSERT_EQUAL_INT(2, g_rec.txCount);
    TEST_ASSERT_EQUAL_HEX8('v', g_rec.txBuf[0]);
    TEST_ASSERT_EQUAL_HEX8(0x7F, g_rec.txBuf[1]);
}

// begin() uses the injected IO: at minimum the S0 query ('S','0') must appear.
void test_mp3trigger_begin_uses_injected_io() {
    AudioDriverMp3Trigger drv;
    drv.setIO(makeRecordingIO());

    drv.begin(10);

    TEST_ASSERT_TRUE_MESSAGE(g_rec.txCount >= 2,
                              "begin() must use injected IO and send at least S0 query");
    TEST_ASSERT_EQUAL_HEX8_MESSAGE('S', g_rec.txBuf[0], "first TX byte must be 'S' (S0 query)");
    TEST_ASSERT_EQUAL_HEX8_MESSAGE('0', g_rec.txBuf[1], "second TX byte must be '0' (S0 query)");
}

// Without S0 response begin() does NOT send the S1 query — linkOk gate.
// TX order without link: ['S','0'] then ['v', nativeVol] only (no S1).
// nativeVol for vol=10: (30-10)*255/30 = 170 = 0xAA.
void test_mp3trigger_begin_no_s1_without_link() {
    AudioDriverMp3Trigger drv;
    drv.setIO(makeRecordingIO());

    drv.begin(10);

    // Expect exactly 4 bytes: S0 query then setVolume — no S1 when no link.
    TEST_ASSERT_EQUAL_INT_MESSAGE(4, g_rec.txCount,
                                  "no S0 response → only S0 query + setVolume (no S1)");
    TEST_ASSERT_EQUAL_HEX8('S', g_rec.txBuf[0]);
    TEST_ASSERT_EQUAL_HEX8('0', g_rec.txBuf[1]);
    TEST_ASSERT_EQUAL_HEX8('v', g_rec.txBuf[2]);
    TEST_ASSERT_EQUAL_HEX8(0xAA, g_rec.txBuf[3]);  // nativeVol = 170
}

// =============================================================================
// CHIRP tests
// =============================================================================

// playTrack(3) → "PLAY:3,1,A\n"
void test_chirp_play_track_byte_sequence() {
    AudioDriverChirp drv;
    drv.setIO(makeRecordingIO());

    drv.playTrack(3);

    const char* expected = "PLAY:3,1,A\n";
    int len = (int)strlen(expected);
    TEST_ASSERT_EQUAL_INT_MESSAGE(len, g_rec.txCount,
                                  "playTrack(3) must emit 'PLAY:3,1,A\\n'");
    TEST_ASSERT_EQUAL_MEMORY(expected, g_rec.txBuf, (size_t)len);
}

// playTrack(0) → silent no-op
void test_chirp_play_track_zero_is_nop() {
    AudioDriverChirp drv;
    drv.setIO(makeRecordingIO());

    drv.playTrack(0);

    TEST_ASSERT_EQUAL_INT_MESSAGE(0, g_rec.txCount,
                                  "playTrack(0) must emit no bytes");
}

// stop() → "STOP\n"
void test_chirp_stop_byte_sequence() {
    AudioDriverChirp drv;
    drv.setIO(makeRecordingIO());

    drv.stop();

    const char* expected = "STOP\n";
    int len = (int)strlen(expected);
    TEST_ASSERT_EQUAL_INT_MESSAGE(len, g_rec.txCount,
                                  "stop() must emit 'STOP\\n'");
    TEST_ASSERT_EQUAL_MEMORY(expected, g_rec.txBuf, (size_t)len);
}

// setVolume(15) → "VOL:49\n"  (15*99/30 = 49)
void test_chirp_set_volume_15_byte_sequence() {
    AudioDriverChirp drv;
    drv.setIO(makeRecordingIO());

    drv.setVolume(15);

    const char* expected = "VOL:49\n";
    int len = (int)strlen(expected);
    TEST_ASSERT_EQUAL_INT_MESSAGE(len, g_rec.txCount,
                                  "setVolume(15) must emit 'VOL:49\\n'");
    TEST_ASSERT_EQUAL_MEMORY(expected, g_rec.txBuf, (size_t)len);
}

// setVolume(0) → "VOL:0\n"
void test_chirp_set_volume_0_byte_sequence() {
    AudioDriverChirp drv;
    drv.setIO(makeRecordingIO());

    drv.setVolume(0);

    const char* expected = "VOL:0\n";
    int len = (int)strlen(expected);
    TEST_ASSERT_EQUAL_INT(len, g_rec.txCount);
    TEST_ASSERT_EQUAL_MEMORY(expected, g_rec.txBuf, (size_t)len);
}

// setVolume(30) → "VOL:99\n"  (30*99/30 = 99)
void test_chirp_set_volume_max_byte_sequence() {
    AudioDriverChirp drv;
    drv.setIO(makeRecordingIO());

    drv.setVolume(30);

    const char* expected = "VOL:99\n";
    int len = (int)strlen(expected);
    TEST_ASSERT_EQUAL_INT(len, g_rec.txCount);
    TEST_ASSERT_EQUAL_MEMORY(expected, g_rec.txBuf, (size_t)len);
}

// playTrackBanked(2, 3, 'B') → "PLAY:2,3,B\n"
void test_chirp_play_track_banked() {
    AudioDriverChirp drv;
    drv.setIO(makeRecordingIO());

    drv.playTrackBanked(2, 3, 'B');

    const char* expected = "PLAY:2,3,B\n";
    int len = (int)strlen(expected);
    TEST_ASSERT_EQUAL_INT(len, g_rec.txCount);
    TEST_ASSERT_EQUAL_MEMORY(expected, g_rec.txBuf, (size_t)len);
}

// playTrackBanked with lowercase page — normalizePage converts to uppercase
void test_chirp_play_track_banked_lowercase_page_normalized() {
    AudioDriverChirp drv;
    drv.setIO(makeRecordingIO());

    drv.playTrackBanked(1, 2, 'b');  // 'b' → 'B'

    const char* expected = "PLAY:1,2,B\n";
    int len = (int)strlen(expected);
    TEST_ASSERT_EQUAL_INT(len, g_rec.txCount);
    TEST_ASSERT_EQUAL_MEMORY(expected, g_rec.txBuf, (size_t)len);
}

// begin() uses the injected IO: VOL command then GMAN query must be recorded.
void test_chirp_begin_uses_injected_io() {
    AudioDriverChirp drv;
    drv.setIO(makeRecordingIO());

    drv.begin(0);

    // begin() sends setVolume() → "VOL:0\n" then loadManifestBanks → "GMAN\n"
    TEST_ASSERT_TRUE_MESSAGE(g_rec.txCount > 0,
                              "begin() must use injected IO, not production IO");

    // First command is the boot volume: "VOL:0\n"
    const char* vol_prefix = "VOL:";
    TEST_ASSERT_EQUAL_MEMORY_MESSAGE(vol_prefix, g_rec.txBuf, 4,
                                     "first TX command in begin() must be VOL:");
}

// =============================================================================

int main(int argc, char** argv) {
    UNITY_BEGIN();

    // DY-SV5W
    RUN_TEST(test_dysv5w_checksum_formula);
    RUN_TEST(test_dysv5w_play_track_byte_sequence);
    RUN_TEST(test_dysv5w_play_track_high_byte);
    RUN_TEST(test_dysv5w_play_track_zero_is_nop);
    RUN_TEST(test_dysv5w_stop_byte_sequence);
    RUN_TEST(test_dysv5w_set_volume_0_byte_sequence);
    RUN_TEST(test_dysv5w_set_volume_15_byte_sequence);
    RUN_TEST(test_dysv5w_set_volume_max_byte_sequence);
    RUN_TEST(test_dysv5w_command_posts_100ms_delay);
    RUN_TEST(test_dysv5w_begin_uses_injected_io);

    // MP3 Trigger
    RUN_TEST(test_mp3trigger_play_track_byte_sequence);
    RUN_TEST(test_mp3trigger_play_track_255);
    RUN_TEST(test_mp3trigger_play_track_256_dropped);
    RUN_TEST(test_mp3trigger_play_track_zero_dropped);
    RUN_TEST(test_mp3trigger_stop_byte_sequence);
    RUN_TEST(test_mp3trigger_set_volume_0_byte_sequence);
    RUN_TEST(test_mp3trigger_set_volume_max_byte_sequence);
    RUN_TEST(test_mp3trigger_set_volume_mid_byte_sequence);
    RUN_TEST(test_mp3trigger_begin_uses_injected_io);
    RUN_TEST(test_mp3trigger_begin_no_s1_without_link);

    // CHIRP
    RUN_TEST(test_chirp_play_track_byte_sequence);
    RUN_TEST(test_chirp_play_track_zero_is_nop);
    RUN_TEST(test_chirp_stop_byte_sequence);
    RUN_TEST(test_chirp_set_volume_15_byte_sequence);
    RUN_TEST(test_chirp_set_volume_0_byte_sequence);
    RUN_TEST(test_chirp_set_volume_max_byte_sequence);
    RUN_TEST(test_chirp_play_track_banked);
    RUN_TEST(test_chirp_play_track_banked_lowercase_page_normalized);
    RUN_TEST(test_chirp_begin_uses_injected_io);

    return UNITY_END();
}
