// =============================================================================
// src/drivers/audio_soft_uart.cpp
//
// Concrete AudioDriver using software UART TX for binary-frame audio modules.
//
// Serial communication uses software bit-bang UART TX on PIN_AUDIO_TX (GPIO 26)
// at 9600 baud 8N1. All three hardware UARTs are reserved (UART0 = debug,
// UART1 = hoverboard, UART2 = dome serial), so software TX is used here.
// At 9600 baud and 240 MHz the ESP32 bit-bangs reliably with delayMicroseconds().
//
// Frame format: 0xAA [CMD] [LEN] [DATA...] 0xAB
//   - CMD  : command byte
//   - LEN  : number of DATA bytes that follow
//   - DATA : payload (omitted if LEN == 0)
//
// ⚠  Command bytes are tuned for the installed audio module firmware.
//    Verify all command values against the physical module during hardware
//    validation — see T09 acceptance checklist.
// =============================================================================

#include "audio_soft_uart.h"

#include <Arduino.h>

#include "config.h"

// Bit period for 9600 baud: 1,000,000 / 9600 ≈ 104 µs
static constexpr uint32_t BAUD_BIT_US = 104;

// Command bytes for the installed audio module.
// ⚠ Verify against physical module firmware during T09 hardware validation.
static constexpr uint8_t CMD_PLAY_INDEX = 0x07;  // Play by file index (2-byte big-endian track)
static constexpr uint8_t CMD_STOP       = 0x04;  // Stop playback
static constexpr uint8_t CMD_SET_VOLUME = 0x13;  // Set volume (0–30)

// -----------------------------------------------------------------------------
// begin()
// Configure PIN_AUDIO_TX as a digital output and set idle HIGH.
// UART idle state is logic HIGH — line must be high before the first byte.
// -----------------------------------------------------------------------------
void AudioDriverSoftUart::begin() {
    pinMode(PIN_AUDIO_TX, OUTPUT);
    digitalWrite(PIN_AUDIO_TX, HIGH);
}

// -----------------------------------------------------------------------------
// softTxByte()
// Transmit one byte via software UART: start bit, 8 data bits LSB-first,
// stop bit. Uses delayMicroseconds() for bit timing at 9600 baud.
// -----------------------------------------------------------------------------
void AudioDriverSoftUart::softTxByte(uint8_t b) {
    // Start bit — pull low for one bit period
    digitalWrite(PIN_AUDIO_TX, LOW);
    delayMicroseconds(BAUD_BIT_US);

    // 8 data bits, LSB first
    for (int i = 0; i < 8; i++) {
        digitalWrite(PIN_AUDIO_TX, (b >> i) & 0x01);
        delayMicroseconds(BAUD_BIT_US);
    }

    // Stop bit — return high for one bit period
    digitalWrite(PIN_AUDIO_TX, HIGH);
    delayMicroseconds(BAUD_BIT_US);
}

// -----------------------------------------------------------------------------
// sendFrame()
// Transmit all bytes of a command frame sequentially.
// -----------------------------------------------------------------------------
void AudioDriverSoftUart::sendFrame(const uint8_t* data, uint8_t len) {
    for (uint8_t i = 0; i < len; i++) {
        softTxByte(data[i]);
    }
}

// -----------------------------------------------------------------------------
// playTrack()
// Play a track by 1-based file index. Track 0 is invalid — silently ignored.
// Frame: AA 07 02 [hi] [lo] AB  (index split as big-endian uint16)
// ⚠ Command bytes require T09 hardware validation.
// -----------------------------------------------------------------------------
void AudioDriverSoftUart::playTrack(uint16_t track) {
    if (track == 0) {
        return;
    }
    uint8_t frame[] = {
        0xAA,
        CMD_PLAY_INDEX,
        0x02,
        static_cast<uint8_t>(track >> 8),
        static_cast<uint8_t>(track & 0xFF),
        0xAB,
    };
    sendFrame(frame, sizeof(frame));
}

// -----------------------------------------------------------------------------
// stop()
// Stop current playback immediately.
// Frame: AA 04 00 AB
// ⚠ Command bytes require T09 hardware validation.
// -----------------------------------------------------------------------------
void AudioDriverSoftUart::stop() {
    uint8_t frame[] = {0xAA, CMD_STOP, 0x00, 0xAB};
    sendFrame(frame, sizeof(frame));
}

// -----------------------------------------------------------------------------
// setVolume()
// Set playback volume. vol is 0–30 (clamped by AudioTask before this call).
// Frame: AA 13 01 [vol] AB
// ⚠ Command bytes require T09 hardware validation.
// -----------------------------------------------------------------------------
void AudioDriverSoftUart::setVolume(uint8_t vol) {
    uint8_t frame[] = {0xAA, CMD_SET_VOLUME, 0x01, vol, 0xAB};
    sendFrame(frame, sizeof(frame));
}
