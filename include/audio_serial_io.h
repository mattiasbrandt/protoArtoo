// =============================================================================
// include/audio_serial_io.h
//
// AudioSerialIO  --  injectable I/O seam for audio drivers.
//
// Separates protocol framing (what bytes to send, when to wait, what to read)
// from the transport mechanism (softUartTxByte, HardwareSerial, vTaskDelay).
//
// Usage:
//   - Production: driver's begin() initialises m_io with real hardware adapters
//     if setIO() was not called first.
//   - Tests: call driver.setIO(recordingIO) before begin() to capture TX bytes,
//     inject RX responses, and use an instant-return delay.
//
// All fields default to nullptr. Drivers assert that writeByte is non-null
// before any protocol I/O (caught at begin() time).
// =============================================================================
#pragma once

#include <stdint.h>

struct AudioSerialIO {
    void     (*writeByte)(uint8_t b) = nullptr;   // TX: write one byte
    int      (*rxAvailable)()        = nullptr;   // RX: bytes available in buffer
    int      (*rxRead)()             = nullptr;   // RX: read one byte (-1 if empty)
    void     (*delayMs)(uint32_t ms) = nullptr;   // blocking delay (yield-safe in production)
    uint32_t (*millisNow)()          = nullptr;   // current time in ms
};
