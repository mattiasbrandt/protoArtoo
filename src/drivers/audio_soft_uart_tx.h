// =============================================================================
// src/drivers/audio_soft_uart_tx.h
//
// Internal shared soft-UART TX primitives for audio drivers.
//
// Provides softUartTxBegin() and softUartTxByte() as inline helpers.
// Both AUDIO_SOFT_UART and AUDIO_CHIRP drivers include this header to avoid
// duplicating bit-bang logic without creating a cross-driver dependency.
//
// Not a public header — only audio driver .cpp files should include this.
// =============================================================================
#pragma once

#include <Arduino.h>

#include "config.h"

// Bit period for 9600 baud: 1,000,000 / 9600 ≈ 104 µs
static constexpr uint32_t SOFT_UART_BIT_US = 104;

// -----------------------------------------------------------------------------
// softUartTxBegin()
// Configure PIN_AUDIO_TX as a digital output and set idle HIGH.
// UART idle state is logic HIGH — must be called once before any byte TX.
// -----------------------------------------------------------------------------
inline void softUartTxBegin() {
    pinMode(PIN_AUDIO_TX, OUTPUT);
    digitalWrite(PIN_AUDIO_TX, HIGH);
}

// -----------------------------------------------------------------------------
// softUartTxByte()
// Transmit one byte via software UART: start bit, 8 data bits LSB-first,
// stop bit. Uses delayMicroseconds() for bit timing at 9600 baud.
// -----------------------------------------------------------------------------
inline void softUartTxByte(uint8_t b) {
    // Start bit — pull low for one bit period
    digitalWrite(PIN_AUDIO_TX, LOW);
    delayMicroseconds(SOFT_UART_BIT_US);

    // 8 data bits, LSB first
    for (int i = 0; i < 8; i++) {
        digitalWrite(PIN_AUDIO_TX, (b >> i) & 0x01);
        delayMicroseconds(SOFT_UART_BIT_US);
    }

    // Stop bit — return high for one bit period
    digitalWrite(PIN_AUDIO_TX, HIGH);
    delayMicroseconds(SOFT_UART_BIT_US);
}

// -----------------------------------------------------------------------------
// softUartTxString()
// Transmit a null-terminated ASCII string byte by byte.
// -----------------------------------------------------------------------------
inline void softUartTxString(const char* s) {
    while (s && *s) {
        softUartTxByte((uint8_t)*s++);
    }
}
