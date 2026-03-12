// =============================================================================
// include/sbus_decoder.h
//
// RMT-based SBUS decoder for ESP32.
// Decodes standard inverted SBUS (100kbaud 8E2) on any GPIO using the RMT
// peripheral. No hardware UART consumed.
//
// Hardware: HOTRC SBUS-A receivers on GPIO 15 (drive) and GPIO 13 (dome).
// Protocol: 25-byte frame at 100 Hz. Channels 172–1811, center ~992.
//
// Frame format: [0x0F][22 data bytes][flags byte][0x00]
//   16 channels × 11 bits packed LSB-first into bytes 1–22.
//   Flags byte (index 23): bit1=lost_frame, bit2=failsafe.
//
// RMT configuration:
//   Tick = 1 µs (1000 ns). SBUS bit period = 10 ticks.
//   Idle threshold = 4000 ticks (4 ms) — detects inter-frame gap.
//   Memory = RMT_MEM_128 (128 symbols = 256 RMT items, enough for 25-byte frame).
// =============================================================================
#pragma once
#include <stdint.h>
#include <stddef.h>
#include "esp32-hal-rmt.h"

// -----------------------------------------------------------------------------
// SbusData — decoded SBUS frame
// -----------------------------------------------------------------------------
struct SbusData {
    int16_t ch[16];     // 16 channels, range 172–1811, center ~992
    bool failsafe;      // true when receiver has lost TX signal
    bool lost_frame;    // true when a frame was missed
};

// -----------------------------------------------------------------------------
// SbusDecoder — RMT-based SBUS receiver
//
// Usage:
//   SbusDecoder rx;
//   rx.begin(PIN_SBUS1_RX);
//   // in task loop:
//   if (rx.read()) {
//       SbusData d = rx.data();  // use d.ch[0], d.failsafe, etc.
//   }
// -----------------------------------------------------------------------------
class SbusDecoder {
public:
    // Initialize RMT channel on the given GPIO pin.
    // Call once from task init before entering the task loop.
    // Returns true on success, false if RMT init failed.
    bool begin(int pin);

    // Poll for a new decoded frame. Returns true if a new frame was decoded
    // since the last call. Call at ~200 Hz (every 5 ms) to catch every
    // 100 Hz SBUS frame without missing frames.
    bool read();

    // Returns a copy of the most recently decoded frame.
    // Valid only after at least one successful read() == true.
    SbusData data() const { return _data; }

    // Convenience accessors for the most recently decoded frame.
    bool failsafe()  const { return _data.failsafe; }
    bool lostFrame() const { return _data.lost_frame; }

private:
    rmt_obj_t* _rmt    = nullptr;
    SbusData   _data   = {};
    bool       _newData = false;

    // Expand RMT symbol stream into bit array, assemble UART bytes, validate
    // frame header/footer, and populate _data if valid.
    void _decodeFrame(rmt_data_t* symbols, size_t count);

    // Parse 16 channels and flags from a validated 25-byte SBUS frame.
    void _parseChannels(const uint8_t* frame);
};
