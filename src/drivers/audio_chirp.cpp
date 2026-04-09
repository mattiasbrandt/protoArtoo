// =============================================================================
// src/drivers/audio_chirp.cpp
//
// AudioDriver implementation for the CHIRP Audio Trigger board.
//
// TX commands are sent over software UART on PIN_AUDIO_TX (GPIO 26) at 9600 baud.
// RX responses are read from HardwareSerial(2) on PIN_AUDIO_RX (GPIO 35) for
// manifest and status queries.
//
// CHIRP must be pre-configured to 9600 baud via CHIRP.INI on the SD card root:
//   #BAUD_RATE 9600
//
// Commands used:
//   playTrack(n)        -> "PLAY:n,1,A\n"  (Bank 1, Page A)
//   stop()              -> "STOP\n"
//   setVolume(v)        -> "VOL:N\n" where N = v * 99 / 30
//   begin() bootstrap   -> "GMAN\n" for Bank 1 track count
//   queryModuleState()  -> "STAT\n" for stream activity
// =============================================================================

#include "audio_chirp.h"

#include <Arduino.h>
#include <stdio.h>   // snprintf, sscanf
#include <string.h>  // strcmp, strncmp, strstr

#include "audio_soft_uart_tx.h"  // shared soft-UART bit-bang primitives
#include "config.h"
#include "logging.h"
#include "robot_state.h"

static HardwareSerial s_chirpSerial(2);
static const char* TAG = "ChirpDrv";
static uint32_t s_lastNoRspDiagMs = 0;

// Read one \r\n-terminated ASCII line from s_chirpSerial into buf (null-terminated).
// Returns number of characters written (excluding null). Stops at '\n', '\r' discarded.
// Yields Core 0 while waiting to keep WiFi/web tasks responsive.
static uint8_t readLine(char* buf, uint8_t maxLen, uint32_t timeoutMs) {
    if (maxLen == 0) {
        return 0;
    }

    uint32_t start = millis();
    uint8_t pos = 0;
    while ((uint32_t)(millis() - start) < timeoutMs && pos < maxLen - 1u) {
        if (s_chirpSerial.available()) {
            char c = (char)s_chirpSerial.read();
            if (c == '\n') {
                break;
            }
            if (c != '\r') {
                buf[pos++] = c;
            }
        } else {
            vTaskDelay(pdMS_TO_TICKS(1));
        }
    }
    buf[pos] = '\0';
    return pos;
}

// Parse CHIRP status lines.
// Current firmware format: "STAT:playing,<file>,<vol>" or "STAT:idle,,0".
// Legacy integration notes used "S:<stream>,ply/idle,..."; accept both for
// robustness while bench hardware is being validated.
static bool parseChirpStatusLine(const char* line, uint8_t* playStateOut) {
    if (line == nullptr || playStateOut == nullptr) {
        return false;
    }

    if (strncmp(line, "STAT:", 5) == 0) {
        const char* state = line + 5;
        if (strncmp(state, "playing,", 8) == 0) {
            *playStateOut = 0x01;
            return true;
        }
        if (strncmp(state, "idle,", 5) == 0) {
            *playStateOut = 0x00;
            return true;
        }
    }

    if (strncmp(line, "S:", 2) == 0) {
        if (strstr(line, ",ply,") != nullptr) {
            *playStateOut = 0x01;
            return true;
        }
        if (strstr(line, ",idle,") != nullptr) {
            *playStateOut = 0x00;
            return true;
        }
    }

    return false;
}

// -----------------------------------------------------------------------------
// begin()
// Configures CHIRP TX/RX, applies boot volume, then queries GMAN to cache
// Bank 1 track count.
// -----------------------------------------------------------------------------
void AudioDriverChirp::begin(uint8_t vol) {
    // same UART2 RX path as DY-SV5W; see T66 for dome-link contention handling.
    // SBUS2 is now RMT-based; UART2 is only contended by dome link.
    s_chirpSerial.begin(9600, SERIAL_8N1, PIN_AUDIO_RX, -1);
    softUartTxBegin();

    // CHIRP boots, mounts SD, and optionally syncs Bank 1 to flash; 2 s covers
    // most cases. First boot after SD card change may need more time.
    delay(2000);

    // Drain boot chatter before issuing our own queries.
    while (s_chirpSerial.available()) {
        s_chirpSerial.read();
    }

    // Apply NVS-configured boot volume before any playback.
    setVolume(vol);

    m_totalTracks = 0;
    m_linkOk = false;
    m_playState = 0xFF;

    sendCommand("GMAN");

    uint32_t startMs = millis();
    bool gotValidGmanLine = false;
    uint32_t rxBytes = 0;
    char line[96];
    while ((uint32_t)(millis() - startMs) < 1500u) {
        uint8_t n = readLine(line, (uint8_t)sizeof(line), 60u);
        if (n == 0) {
            continue;
        }
        rxBytes += n;

        bool isGmanLine = (strncmp(line, "MDAT:", 5) == 0) || (strncmp(line, "BANK:", 5) == 0) ||
                          (strncmp(line, "MSUM:", 5) == 0) || (strcmp(line, "MEND") == 0);
        if (isGmanLine) {
            gotValidGmanLine = true;
        }

        if (strncmp(line, "BANK:1,", 7) == 0) {
            uint16_t bank1Count = 0;
            if (sscanf(line, "BANK:%*u,%*[^,],%hu", &bank1Count) == 1) {
                m_totalTracks = bank1Count;
            }
        }

        if (strcmp(line, "MEND") == 0) {
            break;
        }
    }

    m_linkOk = gotValidGmanLine;

    PA_LOG_INFO(TAG, "init — vol=%u Bank1 sounds=%u link=%s", (unsigned)vol,
                (unsigned)m_totalTracks, m_linkOk ? "OK" : "no response");
    if (!m_linkOk) {
        if (rxBytes == 0) {
            PA_LOG_WARN(TAG,
                        "No CHIRP RX bytes during GMAN bootstrap. Verify CHIRP TX->S2 RX (GPIO35), common GND, and baud=9600.");
        } else {
            PA_LOG_WARN(TAG,
                        "CHIRP RX activity seen (%u bytes) but no valid GMAN frame. Verify CHIRP baud/protocol settings.",
                        (unsigned)rxBytes);
        }
    }
}

// -----------------------------------------------------------------------------
// sendCommand()
// Transmit a null-terminated ASCII string followed by '\n'.
// -----------------------------------------------------------------------------
void AudioDriverChirp::sendCommand(const char* cmd) {
    softUartTxString(cmd);
    softUartTxByte('\n');
}

// -----------------------------------------------------------------------------
// playTrack()
// Play a track by 1-based index in Bank 1, Page A on stream 0.
// Format: "PLAY:index,1,A\n"
// Track 0 is invalid — silently ignored.
// -----------------------------------------------------------------------------
void AudioDriverChirp::playTrack(uint16_t track) {
    if (track == 0) {
        return;
    }
    m_lastTrack = track;  // cache for currentTrack reporting
    // Buffer sized for "PLAY:65535,1,A" (14 chars) + null
    char cmd[20];
    snprintf(cmd, sizeof(cmd), "PLAY:%u,1,A", (unsigned)track);
    sendCommand(cmd);
}

// -----------------------------------------------------------------------------
// stop()
// Stop all active streams.
// Format: "STOP\n"
// -----------------------------------------------------------------------------
void AudioDriverChirp::stop() {
    sendCommand("STOP");
}

// -----------------------------------------------------------------------------
// setVolume()
// Set global volume. vol is 0–30 (clamped by AudioTask before this call).
// Scales to CHIRP native range 0–99: N = (vol * CHIRP_VOL_MAX) / 30.
// Format: "VOL:N\n"
// -----------------------------------------------------------------------------
void AudioDriverChirp::setVolume(uint8_t vol) {
    uint8_t chirpVol = (uint8_t)((uint16_t)vol * CHIRP_VOL_MAX / 30u);
    char cmd[10];
    snprintf(cmd, sizeof(cmd), "VOL:%u", (unsigned)chirpVol);
    sendCommand(cmd);
}

// -----------------------------------------------------------------------------
// queryModuleState()
// Query CHIRP stream status and map to AudioModuleState.
// Returns true if at least one valid status line is received.
// -----------------------------------------------------------------------------
bool AudioDriverChirp::queryModuleState(AudioModuleState& out) {
    bool uart2Contended;
    taskENTER_CRITICAL(&robotStateMux);
    uart2Contended = robotState.cfg_enable_s3_dome_ctrl;
    taskEXIT_CRITICAL(&robotStateMux);
    if (uart2Contended) {
        PA_LOG_DEBUG(TAG, "UART2 contended — returning cached module state");
        getCachedState(out);
        return false;
    }

    out.linkOk = false;
    out.playState = 0xFF;
    out.device       = 0x03;          // CHIRP: Bank 1 on flash, Banks 2–6 on SD
    out.totalTracks  = m_totalTracks;
    out.currentTrack = m_lastTrack;   // last index sent via playTrack()

    while (s_chirpSerial.available()) {
        s_chirpSerial.read();
    }

    // Query stream 0 state; CHIRP responds with "STAT:playing,..." or "STAT:idle,,0".
    sendCommand("STAT:0");

    uint32_t startMs = millis();
    char line[64];
    uint32_t rxBytes = 0;
    uint16_t unparsableLines = 0;
    while ((uint32_t)(millis() - startMs) < 300u) {
        uint8_t n = readLine(line, (uint8_t)sizeof(line), 40u);
        if (n == 0) {
            continue;
        }
        rxBytes += n;
        uint8_t parsedPlayState = 0xFF;
        if (!parseChirpStatusLine(line, &parsedPlayState)) {
            ++unparsableLines;
            continue;
        }

        if (parsedPlayState == 0x01) {
            out.playState = 0x01;
            out.linkOk = true;
        } else if (parsedPlayState == 0x00) {
            if (out.playState != 0x01) {
                out.playState = 0x00;
            }
            out.linkOk = true;
        }
    }

    m_linkOk = out.linkOk;
    if (out.playState != 0xFF) {
        m_playState = out.playState;
    }

    if (!out.linkOk) {
        uint32_t now = millis();
        if ((uint32_t)(now - s_lastNoRspDiagMs) > 5000u) {
            if (rxBytes == 0) {
                PA_LOG_WARN(TAG,
                            "No CHIRP RX bytes during STAT query. Verify return path CHIRP TX->S2 RX and shared GND.");
            } else {
                PA_LOG_WARN(TAG,
                            "CHIRP RX activity seen (%u bytes) but no valid STAT line (%u unparsable lines).",
                            (unsigned)rxBytes, (unsigned)unparsableLines);
            }
            s_lastNoRspDiagMs = now;
        }
    }

    return out.linkOk;
}

// -----------------------------------------------------------------------------
// getCachedState()
// Returns the cached CHIRP state with no UART traffic.
// -----------------------------------------------------------------------------
void AudioDriverChirp::getCachedState(AudioModuleState& out) const {
    out.linkOk = m_linkOk;
    out.playState = m_playState;
    out.device       = 0x03;          // CHIRP: Bank 1 flash-backed, Banks 2–6 SD
    out.totalTracks  = m_totalTracks;
    out.currentTrack = m_lastTrack;   // last track index sent to playTrack()
}
