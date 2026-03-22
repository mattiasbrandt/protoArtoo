// =============================================================================
// src/drivers/audio_chirp.cpp
//
// AudioDriver implementation for the CHIRP Audio Trigger board.
//
// Sends ASCII commands over software UART TX on PIN_AUDIO_TX (GPIO 26) at
// 9600 baud. CHIRP must be pre-configured to 9600 baud via CHIRP.INI on the
// SD card root (#BAUD_RATE 9600) — see include/audio_chirp.h for details.
//
// Commands sent (fire-and-forget, no response parsing):
//   playTrack(n)  → "PLAY:n,1,A\n"   — Bank 1, Page A, stream 0
//   stop()        → "STOP\n"          — stop all streams
//   setVolume(v)  → "VOL:N\n"         — N = v * 99 / 30
//
// Volume scaling: AudioDriver interface normalises to 0–30; CHIRP uses 0–99.
// The scaling formula is integer arithmetic: (v * CHIRP_VOL_MAX) / 30.
// At v=30 → 99, v=15 → 49, v=0 → 0. No floating point used.
// =============================================================================

#include "audio_chirp.h"

#include <stdio.h>  // snprintf

#include "audio_soft_uart_tx.h"  // shared soft-UART bit-bang primitives

// -----------------------------------------------------------------------------
// begin()
// Configure PIN_AUDIO_TX as a digital output and set idle HIGH.
// -----------------------------------------------------------------------------
void AudioDriverChirp::begin(uint8_t vol) {
    (void)vol;  // CHIRP driver has no volume control
    softUartTxBegin();
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
    uint8_t chirpVol = (uint8_t)((uint16_t)vol * CHIRP_VOL_MAX / 30);
    char cmd[10];
    snprintf(cmd, sizeof(cmd), "VOL:%u", (unsigned)chirpVol);
    sendCommand(cmd);
}
