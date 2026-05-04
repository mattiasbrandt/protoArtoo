// =============================================================================
// src/drivers/audio_mp3trigger.cpp
//
// AudioDriver implementation for the SparkFun MP3 Trigger v2.x.
//
// TX commands are sent over software UART on PIN_AUDIO_TX (GPIO 26) at 9600
// baud via audio_soft_uart_tx.h. RX query responses are read from
// HardwareSerial(2) on PIN_AUDIO_RX (GPIO 35), opened RX-only (TX pin = -1).
// UART2 is shared with dome link (S3) and SBUS2; queryModuleState() checks for
// contention and returns cached state when UART2 is not available.
//
// Wire protocol (source-verified: BetterDuino MDuinoSound.cpp, Padawan360,
// SparkFun MP3 Trigger v2.4 Hookup Guide):
//
//   Transmit (ESP32 → module):
//     'S'+'0'  — query firmware version string
//     'S'+'1'  — query SD track count
//     't'+N    — play track N by filename prefix NNNxxxx.MP3 (N = uint8_t 1–255)
//     'v'+V    — set volume: 0=loudest, 255=silent (VS1053 inverted register)
//     'O'      — toggle play/pause (not used directly — see stop() below)
//
//   Receive (module → ESP32):
//     "=MP3 Trigger v2.NN\r\n"  — S0 version response
//     "=NNN\r\n"                 — S1 track-count response (strip '=' before parsing)
//     'X'                        — track finished naturally
//     'x'                        — playback cancelled
//     'E'                        — hardware error
//
// stop() plays track 254 (community standard blank track). This is the approach
// used by BetterDuino and SHADOW_MD and is more reliable than 'O' toggle
// because it works regardless of current module play state. Operator SD root
// must contain 254XXXX.MP3 (all R2 community packs include it).
//
// Volume mapping: VS1053 register is inverted.
//   nativeVol = (30 - vol) * 255 / 30
//   vol=0 → 255 (silent), vol=30 → 0 (maximum), vol=15 → 127.
//
// Baud rate: 9600 (community standard). Factory default is 38400; set via baud
// init file on SD root (see docs/sound_playback.md §2.3 for details).
// No changes to audio_soft_uart_tx.h are required — SOFT_UART_BIT_US=104 is
// already calibrated for 9600 baud.
// =============================================================================

#include "audio_mp3trigger.h"

#include <Arduino.h>
#include <stdio.h>  // sscanf

#include "audio_soft_uart_tx.h"
#include "config.h"
#include "logging.h"

static const char* TAG = "Mp3TrgDrv";
static HardwareSerial s_mp3Serial(2);

// Production IO adapters
static void mp3WriteByte(uint8_t b)    { softUartTxByte(b); }
static int  mp3RxAvailable()           { return s_mp3Serial.available(); }
static int  mp3RxRead()                { return s_mp3Serial.read(); }
static void mp3DelayMs(uint32_t ms)    { vTaskDelay(pdMS_TO_TICKS(ms)); }
static uint32_t mp3MillisNow()         { return (uint32_t)millis(); }

static const AudioSerialIO kMp3ProductionIO {
    mp3WriteByte, mp3RxAvailable, mp3RxRead, mp3DelayMs, mp3MillisNow,
};

// Read one '\r\n'-terminated ASCII response line via m_io. '\r' discarded;
// reading stops at '\n' or timeout. Yields Core 0 while waiting.
uint8_t AudioDriverMp3Trigger::readLine(char* buf, uint8_t maxLen,
                                        uint32_t timeoutMs) {
    if (maxLen == 0) { return 0; }
    uint32_t start = m_io.millisNow();
    uint8_t  pos   = 0;
    while ((uint32_t)(m_io.millisNow() - start) < timeoutMs && pos < maxLen - 1u) {
        if (m_io.rxAvailable()) {
            char c = (char)m_io.rxRead();
            if (c == '\n') { break; }
            if (c != '\r') { buf[pos++] = c; }
        } else {
            m_io.delayMs(1);
        }
    }
    buf[pos] = '\0';
    return pos;
}

// Drain stale RX, transmit a 2-byte query (b0, b1) via m_io, read one line.
uint8_t AudioDriverMp3Trigger::sendQuery(uint8_t b0, uint8_t b1,
                                         char* buf, uint8_t maxLen,
                                         uint32_t timeoutMs) {
    while (m_io.rxAvailable()) { (void)m_io.rxRead(); }
    m_io.writeByte(b0);
    m_io.writeByte(b1);
    return readLine(buf, maxLen, timeoutMs);
}

// -----------------------------------------------------------------------------
// begin()
// Open UART2 RX-only on PIN_AUDIO_RX, configure soft-UART TX, wait for module
// boot, then query firmware version (S0) and track count (S1) before applying
// the NVS-configured boot volume. Blocking — runs in AudioTask on Core 0.
// -----------------------------------------------------------------------------
void AudioDriverMp3Trigger::begin(uint8_t vol) {
    if (!m_io.writeByte) { m_io = kMp3ProductionIO; }

    // Hardware init — no-ops in native test builds.
    s_mp3Serial.begin(9600, SERIAL_8N1, PIN_AUDIO_RX, -1);
    softUartTxBegin();

    // MP3 Trigger mounts the SD card on power-on. 1 s covers cold boot.
    m_io.delayMs(1000);

    // Drain any bytes emitted during boot.
    while (m_io.rxAvailable()) { (void)m_io.rxRead(); }

    // -------------------------------------------------------------------------
    // S0 — firmware version query. Response: "=MP3 Trigger v2.NN\r\n".
    // Receiving any response with a leading '=' confirms the TX path is alive.
    // If no response: check wiring and SD baud init file (factory = 38400).
    // -------------------------------------------------------------------------
    char line[48];
    uint8_t n = sendQuery('S', '0', line, (uint8_t)sizeof(line), 500);
    if (n > 1 && line[0] == '=') {
        m_linkOk = true;
        PA_LOG_INFO(TAG, "link OK — version: %s", line + 1);  // strip leading '='
    } else {
        PA_LOG_WARN(TAG,
                    "no response to S0 query — check wiring and SD baud init "
                    "file (factory default is 38400; community standard is 9600)");
    }

    // -------------------------------------------------------------------------
    // S1 — track count query. Response: "=NNN\r\n".
    // Strip '=' prefix before parsing. Only attempted if link is confirmed to
    // avoid spurious data confusing the simple uint16_t parser.
    // -------------------------------------------------------------------------
    if (m_linkOk) {
        n = sendQuery('S', '1', line, (uint8_t)sizeof(line), 500);
        if (n > 1 && line[0] == '=') {
            uint16_t count = 0;
            if (sscanf(line + 1, "%hu", &count) == 1) {
                m_totalTracks = count;
            }
            PA_LOG_INFO(TAG, "total tracks = %u", (unsigned)m_totalTracks);
        } else {
            PA_LOG_WARN(TAG, "no response to S1 query — total tracks unknown");
        }
    }

    setVolume(vol);
    PA_LOG_INFO(TAG, "init done — vol=%u link=%s tracks=%u", (unsigned)vol,
                m_linkOk ? "OK" : "no response", (unsigned)m_totalTracks);
}

// -----------------------------------------------------------------------------
// playTrack()
// Send 't' + uint8_t(track) to play by filename prefix (NNNxxxx.MP3).
//
// Track 0: silently ignored — audio_driver.h interface contract.
// Track > 255: logged and dropped. The VS1053 't' command is a single byte;
//   casting 256 → uint8_t(0x00) would silently play the wrong track. AudioTask
//   may also supply a uint16_t track number from user input; guard it here.
// -----------------------------------------------------------------------------
void AudioDriverMp3Trigger::playTrack(uint16_t track) {
    if (track == 0) {
        return;
    }
    if (track > 255) {
        PA_LOG_WARN(TAG, "playTrack(%u) exceeds MP3 Trigger maximum (255) — dropped",
                    (unsigned)track);
        return;
    }
    m_lastTrack = track;
    m_io.writeByte('t');
    m_io.writeByte((uint8_t)track);
}

// -----------------------------------------------------------------------------
// stop()
// Play the community-standard silent blank track MP3TRIGGER_STOP_TRACK (254).
// This is the approach used by BetterDuino and SHADOW_MD and is more reliable
// than 'O' toggle because it works regardless of current module play state.
// SD root must contain 254XXXX.MP3 (all R2 community packs include it).
// -----------------------------------------------------------------------------
void AudioDriverMp3Trigger::stop() {
    m_io.writeByte('t');
    m_io.writeByte(MP3TRIGGER_STOP_TRACK);
}

// -----------------------------------------------------------------------------
// setVolume()
// vol is 0–30 (clamped by AudioTask before this call). Scaled to VS1053
// inverted register: nativeVol = (30 - vol) * MP3TRIGGER_VOL_MAX / 30.
//   vol=0  → 255 (silent)
//   vol=30 → 0   (maximum)
//   vol=15 → 127
// Following BetterDuino: practical audible range is 0–100 on the native scale;
// values above ~100 are near-inaudible but technically valid per VS1053 spec.
// -----------------------------------------------------------------------------
void AudioDriverMp3Trigger::setVolume(uint8_t vol) {
    uint8_t nativeVol =
        (uint8_t)((uint32_t)(30u - vol) * MP3TRIGGER_VOL_MAX / 30u);
    m_io.writeByte('v');
    m_io.writeByte(nativeVol);
}

// -----------------------------------------------------------------------------
// queryModuleState()
// UART2 contention check identical to AudioDriverDySv5w and AudioDriverChirp.
// Returns cached state when dome ctrl is active. SBUS2 is RMT-based; it no
// longer contends UART2.
//
// When UART2 is available: drain RX, send S0 (link), then S1 (track count).
// playState and device are always 0xFF — the MP3 Trigger protocol has no
// play-state or device-type query commands. currentTrack is the cached value
// from the last playTrack() call (no live query available).
//
// Only call from AudioTask (Core 0). Blocking up to ~1 s in the worst case.
// -----------------------------------------------------------------------------
bool AudioDriverMp3Trigger::queryModuleState(AudioModuleState& out) {

    out.linkOk       = false;
    out.playState    = 0xFF;   // not queryable in this protocol
    out.device       = 0xFF;   // no device-type concept for MP3 Trigger
    out.totalTracks  = m_totalTracks;
    out.currentTrack = m_lastTrack;

    char line[48];
    uint8_t n;

    // S0 — link health check. Any '='-prefixed response confirms the link.
    n = sendQuery('S', '0', line, (uint8_t)sizeof(line), 500);
    if (n > 1 && line[0] == '=') {
        out.linkOk = true;
        m_linkOk   = true;
    } else {
        m_linkOk = false;
    }

    // S1 — track count update (only when link is confirmed).
    if (out.linkOk) {
        n = sendQuery('S', '1', line, (uint8_t)sizeof(line), 500);
        if (n > 1 && line[0] == '=') {
            uint16_t count = 0;
            if (sscanf(line + 1, "%hu", &count) == 1) {
                m_totalTracks   = count;
                out.totalTracks = count;
            }
        }
    }

    return out.linkOk;
}

// -----------------------------------------------------------------------------
// getCachedState()
// Returns last-known cached state with no UART traffic. Safe to call at any
// time including during playback.
// playState is always 0xFF (no play-state query in this protocol).
// device is always 0xFF (no device-type concept for MP3 Trigger).
// -----------------------------------------------------------------------------
void AudioDriverMp3Trigger::getCachedState(AudioModuleState& out) const {
    out.linkOk       = m_linkOk;
    out.playState    = 0xFF;
    out.device       = 0xFF;
    out.totalTracks  = m_totalTracks;
    out.currentTrack = m_lastTrack;
}
