// =============================================================================
// src/drivers/audio_soft_uart.cpp
//
// DY-SV5W driver — aligned to DYPlayerArduino library + BetterDuino reference.
//
// Root cause of prior "random playback" bug:
//   The driver was sending opcodes 0x0D (getPlayingSound query) and 0x06
//   (next track) instead of 0x07 (playSpecified). It was also dual-sending
//   both end-marker and checksum frame wrappers, producing 4 frames per
//   command — two of which triggered "next track" on the module.
//   See tasks/phase4-tasks.md T10 for full troubleshooting history.
//
// Authoritative command table (DYPlayerArduino DYPlayer.cpp):
//   0x02 = play/resume       0x03 = pause         0x04 = stop
//   0x05 = previous          0x06 = next           0x07 = playSpecified
//   0x0B = setPlayingDevice  0x0D = getPlayingSound (QUERY, not play!)
//   0x13 = setVolume (0-30)  0x1A = setEq
//
// Frame format (checksum dialect — the only one used by DYPlayer library):
//   [0xAA] [CMD] [LEN] [DATA...] [CHECKSUM]
//   CHECKSUM = (sum of ALL preceding bytes including 0xAA) & 0xFF
//
// Transport:
//   Software UART TX on S2 TX pin (GPIO26) at 9600 8N1.
//   Independent of UART2 so SBUS2 and dome serial can run concurrently.
//
// References:
//   - DYPlayerArduino: github.com/SnijderC/dyern (bundled in Padawan360 repo)
//   - BetterDuinoFirmwareV4: MDuinoSoundDYPlayer in src/MDuinoSound.cpp
//   - Padawan360: Imperiallandm/Padawan360_mega_maestro_DYSV5W
// =============================================================================

#include "audio_soft_uart.h"

#include <Arduino.h>

#include "audio_soft_uart_tx.h"  // shared software UART TX primitives
// -----------------------------------------------------------------------------
// sendCommand()
// Send one DY-SV5W checksum frame: [payload bytes][CHECKSUM] over soft UART.
// TX is wrapped in a critical section to reduce bit-timing jitter.
// A 100 ms delay follows each command for module processing time (matches
// BetterDuino MDuinoSoundDYPlayer::sendCommand).
// Send ONCE only — dual-sending causes the module to misinterpret follow-up
// bytes as a new command (previously identified root cause of silent audio).
// -----------------------------------------------------------------------------
static portMUX_TYPE s_audioTxMux = portMUX_INITIALIZER_UNLOCKED;

static void sendCommand(const uint8_t* payload, uint8_t len) {
    uint8_t sum = 0;
    for (uint8_t i = 0; i < len; i++) {
        sum = (uint8_t)(sum + payload[i]);
    }

    taskENTER_CRITICAL(&s_audioTxMux);
    for (uint8_t i = 0; i < len; i++) {
        softUartTxByte(payload[i]);
    }
    softUartTxByte(sum);
    taskEXIT_CRITICAL(&s_audioTxMux);

    delay(100);
}

// Wrapper satisfying AudioDriver private method signature
void AudioDriverSoftUart::sendFrame(const uint8_t* data, uint8_t len) {
    sendCommand(data, len);
}

// -----------------------------------------------------------------------------
// begin()
// Configure software UART TX on the audio GPIO. Wait for module boot, then
// initialise using the same DYPlayer command sequence.
// Runs inside AudioTask on Core 0 — blocking here is acceptable.
// -----------------------------------------------------------------------------
void AudioDriverSoftUart::begin() {
    softUartTxBegin();

    // DY-SV5W needs ~1-1.5 s after power-on to boot and enumerate SD.
    delay(1500);

    // Select SD card as playback device (opcode 0x0B, SD = 0x01)
    // DYPlayer::setPlayingDevice: {0xAA, 0x0B, 0x01, device}
    uint8_t selectSd[] = {0xAA, 0x0B, 0x01, 0x01};
    sendCommand(selectSd, sizeof(selectSd));

    // Set EQ to Normal (opcode 0x1A, Normal = 0x00)
    // BetterDuino MDuinoSoundDYPlayer::init sends this first
    uint8_t eqNormal[] = {0xAA, 0x1A, 0x01, 0x00};
    sendCommand(eqNormal, sizeof(eqNormal));
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
