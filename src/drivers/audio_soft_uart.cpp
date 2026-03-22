// =============================================================================
// src/drivers/audio_soft_uart.cpp
//
// DY-SV5W UART driver — authoritative implementation from module datasheet.
//
// Frame format (checksum dialect — confirmed by datasheet):
//   [0xAA] [CMD] [LEN] [DATA...] [SM]
//   SM = low 8 bits of sum of ALL preceding bytes (0xAA + CMD + LEN + DATA).
//   Example: play   = AA 02 00 AC  (AA+02+00 = AC)
//            stop   = AA 04 00 AE  (AA+04+00 = AE)
//            vol 15 = AA 13 01 0F C7  (AA+13+01+0F = C7)
//
// NOTE: The "0xAB end-marker" previously used was wrong. 0xAB appearing in
//   query frame AA 01 00 AB is coincidence (AA+01+00 = AB). End-marker dialect
//   does not exist in this module's datasheet.
//
// Device codes (Switch Drive / 0x0B):
//   0x00 = USB    0x01 = SD/TF    0x02 = FLASH
//
// Command table (from datasheet):
//   0x01 = queryPlayState    0x02 = play/resume     0x03 = pause
//   0x04 = stop              0x06 = next            0x07 = playSpecified
//   0x09 = queryDeviceOnline 0x0A = queryPlayDrive  0x0B = switchDrive
//   0x0C = queryTotalTracks  0x0D = queryCurrentTrack
//   0x13 = setVolume(0-30)   0x18 = setLoopMode     0x1A = setEq
//
// Default play mode: 02 = single stop (plays once, then stops).
//
// Transport:
//   HardwareSerial(2) on GPIO26 TX / GPIO35 RX, 9600 8N1.
//   UART2 is shared with dome serial link (S3) and SBUS2; only one active
//   at a time. See T65 for the long-term contention resolution.
//
// Diagnostic queries in begin():
//   Runs three queries before and after init commands so the serial log
//   tells us definitively whether TX reaches the module, device is online,
//   and how many tracks are present. Zero-byte responses = TX dead.
//   The detected device type is used to send the correct switchDrive command
//   so FLASH modules are not accidentally switched to SD (which breaks queries).
// =============================================================================

#include "audio_soft_uart.h"

#include <Arduino.h>

#include "config.h"
#include "logging.h"

static const char* TAG = "AudioDrv";
static HardwareSerial s_audioSerial(2);

// -----------------------------------------------------------------------------
// sendCommand()
// Send one DY-SV5W frame: [payload bytes][SM] over UART2.
// SM = low 8 bits of sum of all payload bytes (including 0xAA start byte).
// A 100 ms delay follows each command for module processing time.
// -----------------------------------------------------------------------------
static void sendCommand(const uint8_t* payload, uint8_t len) {
    uint8_t sum = 0;
    for (uint8_t i = 0; i < len; i++) {
        sum = (uint8_t)(sum + payload[i]);
    }
    s_audioSerial.write(payload, len);
    s_audioSerial.write(sum);
    delay(100);
}

// -----------------------------------------------------------------------------
// sendQuery()
// Send a 4-byte query frame (no data bytes) and wait for a response.
// query[]      — exactly 4 bytes: [0xAA, CMD, 0x00, SM].
// expectedLen  — break once this many bytes have been received; avoids
//               burning the full timeoutMs when the module responds quickly.
//               Pass sizeof(response) for the expected response size.
// Returns number of bytes read into buf (max maxLen).
// -----------------------------------------------------------------------------
static uint8_t sendQuery(const uint8_t* query, uint8_t* buf, uint8_t maxLen, uint8_t expectedLen,
                         uint32_t timeoutMs) {
    // Drain stale RX bytes before sending
    while (s_audioSerial.available()) {
        (void)s_audioSerial.read();
    }
    s_audioSerial.write(query, 4);
    uint32_t start = millis();
    uint8_t count = 0;
    while ((uint32_t)(millis() - start) < timeoutMs && count < maxLen) {
        if (s_audioSerial.available()) {
            buf[count++] = (uint8_t)s_audioSerial.read();
            if (count >= expectedLen) {
                break;  // got all expected bytes — don't burn the rest of timeoutMs
            }
        } else {
            // Yield while waiting so Core 0 tasks (WiFi, web) stay responsive.
            vTaskDelay(pdMS_TO_TICKS(1));
        }
    }
    return count;
}

// Wrapper satisfying AudioDriver private method signature
void AudioDriverSoftUart::sendFrame(const uint8_t* data, uint8_t len) {
    sendCommand(data, len);
}

// -----------------------------------------------------------------------------
// begin()
// Open UART2 on the audio GPIO pins, wait for module boot, then init.
// Runs diagnostic queries before and after init to confirm the UART link is
// alive and the SD card is present. Zero-byte query responses mean TX is not
// reaching the module (check DIP switches: CON3=1 CON2=0 CON1=0 for UART mode).
// Runs inside AudioTask on Core 0 — blocking here is acceptable.
// -----------------------------------------------------------------------------
void AudioDriverSoftUart::begin() {
    s_audioSerial.begin(9600, SERIAL_8N1, PIN_AUDIO_RX, PIN_AUDIO_TX);

    // DY-SV5W needs ~1.5 s after power-on to boot and enumerate SD.
    delay(1500);

    // Drain any bytes the module sent during boot (e.g. boot announcement).
    while (s_audioSerial.available()) {
        (void)s_audioSerial.read();
    }

    // -------------------------------------------------------------------------
    // Pre-init diagnostic queries — run BEFORE sending any commands so we
    // see the module's raw power-on state. If all three return 0 bytes the
    // UART TX wire is not reaching the module or DIP mode is wrong.
    // SM values are exact checksums: AA+CMD+00 = SM.
    // -------------------------------------------------------------------------
    uint8_t rsp[8];
    uint8_t n;

    // Query device online (0x09): which storage is detected?
    // Response: AA 09 01 [device] SM   device: 00=USB 01=SD 02=FLASH FF=none
    static const uint8_t Q_DEV_ONLINE[] = {0xAA, 0x09, 0x00, 0xB3};
    n = sendQuery(Q_DEV_ONLINE, rsp, sizeof(rsp), 5,
                  300);  // response: AA 09 01 device SM (5 bytes)
    if (n >= 4) {
        m_device = rsp[3];  // cache for queryModuleState()
        const char* dev = (rsp[3] == 0x00)   ? "USB"
                          : (rsp[3] == 0x01) ? "SD/TF"
                          : (rsp[3] == 0x02) ? "FLASH"
                          : (rsp[3] == 0xFF) ? "NO_DEVICE"
                                             : "?";
        PA_LOG_INFO(TAG, "pre-init: device online = %s (0x%02X)", dev, rsp[3]);
    } else {
        PA_LOG_WARN(TAG,
                    "pre-init: no response to device-online query — "
                    "check DIP (CON3=1 CON2=0 CON1=0) and TX wiring");
    }

    // Query play state (0x01): is module already playing from a prior session?
    // Response: AA 01 01 [state] SM   state: 00=stop 01=playing 02=paused
    static const uint8_t Q_PLAY_STATE[] = {0xAA, 0x01, 0x00, 0xAB};
    n = sendQuery(Q_PLAY_STATE, rsp, sizeof(rsp), 5, 300);  // response: AA 01 01 state SM (5 bytes)
    if (n >= 4) {
        const char* st = (rsp[3] == 0)   ? "stop"
                         : (rsp[3] == 1) ? "playing"
                         : (rsp[3] == 2) ? "paused"
                                         : "unknown";
        PA_LOG_INFO(TAG, "pre-init: play state = %s (0x%02X)", st, rsp[3]);
    } else {
        PA_LOG_WARN(TAG, "pre-init: no response to play-state query");
    }

    // Query total tracks (0x0C): how many files does the module see?
    // Response: AA 0C 02 [HI] [LO] SM
    static const uint8_t Q_TOTAL_TRACKS[] = {0xAA, 0x0C, 0x00, 0xB6};
    n = sendQuery(Q_TOTAL_TRACKS, rsp, sizeof(rsp), 6,
                  300);  // response: AA 0C 02 SN_H SN_L SM (6 bytes)
    if (n >= 5) {
        m_totalTracks = ((uint16_t)rsp[3] << 8) | rsp[4];  // cache for queryModuleState()
        PA_LOG_INFO(TAG, "pre-init: total tracks = %u", (unsigned)m_totalTracks);
    } else {
        PA_LOG_WARN(TAG, "pre-init: no response to total-tracks query (%u bytes)", (unsigned)n);
    }

    // -------------------------------------------------------------------------
    // Init commands — device select (use detected device), EQ normal, then
    // apply NVS volume. (Volume is applied by AudioTask after begin() returns.)
    //
    // IMPORTANT: do NOT hardcode SD/TF here. The module reports its actual
    // storage type via Q_DEV_ONLINE. Sending switchDrive(SD) to a FLASH module
    // causes it to attempt SD card enumeration, fail, and stop responding to
    // queries — while play commands still work (different UART path inside the
    // module). Use the detected m_device to select the right storage, or skip
    // the command if pre-init detection failed (m_device == 0xFF).
    // -------------------------------------------------------------------------

    // Switch to detected device (0x0B). SM = AA+0B+01+[device].
    if (m_device != 0xFF) {
        uint8_t selectDev[] = {0xAA, 0x0B, 0x01, m_device};
        sendCommand(selectDev, sizeof(selectDev));
        PA_LOG_INFO(TAG, "init: switchDrive to 0x%02X", m_device);
    } else {
        PA_LOG_WARN(TAG, "init: skipping switchDrive — device unknown from pre-init query");
    }

    // Set EQ to Normal (0x1A, eq=0x00). SM = AA+1A+01+00 = 0xC5.
    uint8_t eqNormal[] = {0xAA, 0x1A, 0x01, 0x00};
    sendCommand(eqNormal, sizeof(eqNormal));

    // -------------------------------------------------------------------------
    // Post-init confirmation — verify device select took effect.
    // -------------------------------------------------------------------------
    static const uint8_t Q_PLAY_DRIVE[] = {0xAA, 0x0A, 0x00, 0xB4};
    n = sendQuery(Q_PLAY_DRIVE, rsp, sizeof(rsp), 5, 300);  // response: AA 0A 01 drive SM (5 bytes)
    if (n >= 4 && rsp[0] == 0xAA && rsp[1] == 0x0A) {
        m_device = rsp[3];  // update: post-init device is the authoritative drive selection
        const char* dev = (rsp[3] == 0x00)   ? "USB"
                          : (rsp[3] == 0x01) ? "SD/TF"
                          : (rsp[3] == 0x02) ? "FLASH"
                                             : "?";
        PA_LOG_INFO(TAG, "post-init: play drive = %s (0x%02X)", dev, rsp[3]);
    } else {
        PA_LOG_WARN(TAG, "post-init: no response to play-drive query");
    }
}

// -----------------------------------------------------------------------------
// queryModuleState()
// Send live queries for device, play state, and current track.
// Populates 'out' from the responses; returns true if at least one query
// received a valid response (i.e. the UART link is alive).
// Called by AudioTask after begin() and then periodically every ~2 s.
// Blocking up to ~900 ms total (3 × 300 ms timeout) in the worst case.
// Only call from AudioTask (Core 0).
// -----------------------------------------------------------------------------
bool AudioDriverSoftUart::queryModuleState(AudioModuleState& out) {
    uint8_t rsp[8];
    uint8_t n;
    bool gotAny = false;

    out.linkOk = false;
    out.playState = 0xFF;
    out.device = m_device;            // carry forward cached value from begin()
    out.totalTracks = m_totalTracks;  // not re-queried on every poll
    out.currentTrack = 0;

    // Query device online (0x09)
    // Validate rsp[0]==0xAA and rsp[1]==0x09 to reject spontaneous bytes
    // the module emits during playback (status pushes, boot announcements).
    static const uint8_t Q_DEV[] = {0xAA, 0x09, 0x00, 0xB3};
    n = sendQuery(Q_DEV, rsp, sizeof(rsp), 5, 300);  // response: AA 09 01 device SM (5 bytes)
    if (n >= 4 && rsp[0] == 0xAA && rsp[1] == 0x09) {
        out.device = rsp[3];
        m_device = rsp[3];  // keep member in sync
        gotAny = true;
    }

    // Query play state (0x01)
    static const uint8_t Q_PLAY[] = {0xAA, 0x01, 0x00, 0xAB};
    n = sendQuery(Q_PLAY, rsp, sizeof(rsp), 5, 300);  // response: AA 01 01 state SM (5 bytes)
    if (n >= 4 && rsp[0] == 0xAA && rsp[1] == 0x01) {
        out.playState = rsp[3];
        gotAny = true;
    }

    // Query current song (0x0D)
    static const uint8_t Q_SONG[] = {0xAA, 0x0D, 0x00, 0xB7};
    n = sendQuery(Q_SONG, rsp, sizeof(rsp), 6, 300);  // response: AA 0D 02 SN_H SN_L SM (6 bytes)
    if (n >= 5 && rsp[0] == 0xAA && rsp[1] == 0x0D) {
        out.currentTrack = ((uint16_t)rsp[3] << 8) | rsp[4];
        gotAny = true;
    }

    out.linkOk = gotAny;
    return gotAny;
}

// -----------------------------------------------------------------------------
// playTrack()
// Play a track by 1-based file number. Track 0 is silently ignored.
// Opcode 0x07 = playSpecified, LEN=2, DATA = 2-byte big-endian track index.
// DYPlayer::playSpecified: {0xAA, 0x07, 0x02, hi, lo}
// BetterDuino MDuinoSoundDYPlayer::Play: {0xAA, 0x07, 0x02, 0x00, SoundNr}
//
// NOTE: BetterDuino uses uint8_t for track number (max 255). We support
// uint16_t for forward compatibility but clamp to 255 for DY-SV5W modules.
// -----------------------------------------------------------------------------
void AudioDriverSoftUart::playTrack(uint16_t track) {
    if (track == 0) {
        return;
    }

    uint8_t cmd[] = {
        0xAA,
        0x07,  // playSpecified
        0x02,  // LEN = 2 data bytes
        static_cast<uint8_t>(track >> 8),
        static_cast<uint8_t>(track & 0xFF),
    };
    sendCommand(cmd, sizeof(cmd));
}

// -----------------------------------------------------------------------------
// stop()
// Stop current playback. Opcode 0x04.
// DYPlayer::stop: {0xAA, 0x04, 0x00}
// BetterDuino MDuinoSoundDYPlayer::Stop: {0xAA, 0x04, 0x00}
//
// WARNING: 0x02 = play/resume, 0x03 = pause. Neither is stop!
// Prior driver versions sent 0x03+0x02 which paused then immediately resumed.
// -----------------------------------------------------------------------------
void AudioDriverSoftUart::stop() {
    uint8_t cmd[] = {0xAA, 0x04, 0x00};
    sendCommand(cmd, sizeof(cmd));
}

// -----------------------------------------------------------------------------
// setVolume()
// Set playback volume. vol is 0–30 (clamped by AudioTask before this call).
// Opcode 0x13.
// DYPlayer::setVolume: {0xAA, 0x13, 0x01, volume}
// BetterDuino MDuinoSoundDYPlayer::SetVolume: {0xAA, 0x13, 0x01, Volume}
// -----------------------------------------------------------------------------
void AudioDriverSoftUart::setVolume(uint8_t vol) {
    uint8_t cmd[] = {0xAA, 0x13, 0x01, vol};
    sendCommand(cmd, sizeof(cmd));
}
