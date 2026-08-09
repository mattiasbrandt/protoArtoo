// =============================================================================
// src/drivers/audio_soft_uart_tx.h
//
// Internal shared soft-UART TX primitives for audio drivers.
//
// Provides softUartTxBegin() and softUartTxByte() as inline helpers.
// Both AUDIO_SOFT_UART and AUDIO_CHIRP drivers include this header to avoid
// duplicating bit-bang logic without creating a cross-driver dependency.
//
// Not a public header  --  only audio driver .cpp files should include this.
//
// Interrupt safety
// ----------------
// softUartTxByte() wraps each byte transmission in a portMUX critical section
// (portENTER_CRITICAL / portEXIT_CRITICAL). This is required because
// delayMicroseconds() is not interrupt-safe: the FreeRTOS tick ISR (1 ms) and
// WiFi radio ISRs on Core 0 can stretch a bit period mid-byte, producing
// corrupted frames at the DY-SV5W receiver.
//
// The critical section blocks Core 0 only (AudioTask runs on Core 0). Core 1
// real-time tasks (DriveTask, SBUSInputTask, DomeLinkTask) are unaffected.
// Duration: ~1.04 ms per byte, ~5 ms per 4-byte audio command. Audio commands
// are infrequent (at most a few per second), so this is safe for the application.
//
// s_softUartMux is defined as a file-scope static here. Since PA_AUDIO_DRIVER
// selects exactly one backend at compile time, this header is included by at most
// one translation unit, so the static definition is never duplicated.
// =============================================================================
#pragma once

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/portmacro.h>

#include "config.h"

// Bit period for 9600 baud: 1,000,000 / 9600 ~= 104 us
static constexpr uint32_t SOFT_UART_BIT_US = 104;

// portMUX for Core 0 critical section around each byte transmission.
// One instance per driver TU (safe  --  at most one driver compiled at a time).
static portMUX_TYPE s_softUartMux = portMUX_INITIALIZER_UNLOCKED;

// -----------------------------------------------------------------------------
// softUartTxBegin()
// Configure PIN_AUDIO_TX as a digital output and set idle HIGH.
// UART idle state is logic HIGH  --  must be called once before any byte TX.
// -----------------------------------------------------------------------------
inline void softUartTxBegin() {
    pinMode(PIN_AUDIO_TX, OUTPUT);
    digitalWrite(PIN_AUDIO_TX, HIGH);
}

// -----------------------------------------------------------------------------
// softUartTxByte()
// Transmit one byte via software UART: start bit, 8 data bits LSB-first,
// stop bit. Uses delayMicroseconds() for bit timing at 9600 baud.
//
// Wrapped in a portMUX critical section  --  see file header for rationale.
// Core 0 is non-preemptible for the duration (~1.04 ms).
// -----------------------------------------------------------------------------
inline void softUartTxByte(uint8_t b) {
    portENTER_CRITICAL(&s_softUartMux);

    // Start bit  --  pull low for one bit period
    digitalWrite(PIN_AUDIO_TX, LOW);
    delayMicroseconds(SOFT_UART_BIT_US);

    // 8 data bits, LSB first
    for (int i = 0; i < 8; i++) {
        digitalWrite(PIN_AUDIO_TX, (b >> i) & 0x01);
        delayMicroseconds(SOFT_UART_BIT_US);
    }

    // Stop bit  --  return high for one bit period
    digitalWrite(PIN_AUDIO_TX, HIGH);
    delayMicroseconds(SOFT_UART_BIT_US);

    portEXIT_CRITICAL(&s_softUartMux);
}

// -----------------------------------------------------------------------------
// softUartTxString()
// Transmit a null-terminated ASCII string byte by byte.
// Each byte is individually interrupt-protected via softUartTxByte().
// -----------------------------------------------------------------------------
inline void softUartTxString(const char* s) {
    while (s && *s) {
        softUartTxByte((uint8_t)*s++);
    }
}
