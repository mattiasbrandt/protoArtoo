// =============================================================================
// src/drivers/sbus_decoder.cpp
//
// RMT-based SBUS decoder — no hardware UART consumed.
// Decodes standard inverted SBUS (100 kbaud, 8E2, inverted logic) using the
// ESP32 RMT peripheral in poll mode.
//
// SBUS electrical: inverted UART — idle line is LOW. Logic 1 = LOW voltage.
// RMT captures transitions; each symbol records duration0/level0 and
// duration1/level1. With tick=1 µs, one SBUS bit = 10 ticks.
//
// Decoding pipeline:
//   RMT symbols → bit stream (inverted: RMT level0 → SBUS logic value)
//   → UART byte extraction (8E2: 1 start + 8 data + 1 parity + 2 stop)
//   → 25-byte frame validation ([0x0F]...[0x00])
//   → 16-channel unpack + flags parse
// =============================================================================
#include "sbus_decoder.h"
#include <Arduino.h>

// SBUS protocol constants
static constexpr float    SBUS_RMT_TICK_NS    = 1000.0f;  // 1 µs per tick
static constexpr uint32_t SBUS_IDLE_TICKS     = 4000;     // 4 ms idle → end of frame
static constexpr int      SBUS_BIT_TICKS      = 10;       // 10 µs per bit at 100 kbaud
static constexpr int      SBUS_BITS_PER_BYTE  = 12;       // 1 start + 8 data + 1 parity + 2 stop
static constexpr int      SBUS_FRAME_BYTES    = 25;
static constexpr uint8_t  SBUS_HEADER         = 0x0F;
static constexpr uint8_t  SBUS_FOOTER         = 0x00;
static constexpr uint8_t  SBUS_FLAG_LOST_FRAME = (1 << 1);
static constexpr uint8_t  SBUS_FLAG_FAILSAFE   = (1 << 2);

// Maximum RMT symbols for a 25-byte SBUS frame:
//   25 bytes × 12 bits/byte = 300 bits / 2 bits per RMT symbol = 150 symbols.
//   RMT_MEM_128 = 128 symbols — sufficient as many consecutive same-level bits
//   merge into a single symbol with large duration.
static constexpr size_t RMT_SYMBOLS = 128;

// -----------------------------------------------------------------------------
// SbusDecoder::begin()
// Initialize RMT RX channel on the given GPIO.
// Returns false if rmtInit fails (e.g., no free RMT channel).
// -----------------------------------------------------------------------------
bool SbusDecoder::begin(int pin) {
    _rmt = rmtInit(pin, RMT_RX_MODE, RMT_MEM_128);
    if (_rmt == nullptr) {
        return false;
    }
    rmtSetTick(_rmt, SBUS_RMT_TICK_NS);
    rmtSetRxThreshold(_rmt, SBUS_IDLE_TICKS);
    rmtBeginReceive(_rmt);
    return true;
}

// -----------------------------------------------------------------------------
// SbusDecoder::read()
// Returns true if a new valid frame was decoded. Call at ~200 Hz.
// -----------------------------------------------------------------------------
bool SbusDecoder::read() {
    if (_rmt == nullptr) {
        return false;
    }
    if (!rmtReceiveCompleted(_rmt)) {
        return false;
    }
    // Stack-allocated symbol buffer — no heap allocation
    rmt_data_t symbols[RMT_SYMBOLS];
    rmtReadData(_rmt, reinterpret_cast<uint32_t*>(symbols), RMT_SYMBOLS);
    rmtBeginReceive(_rmt);  // restart for next frame immediately

    _newData = false;
    _decodeFrame(symbols, RMT_SYMBOLS);
    return _newData;
}

// -----------------------------------------------------------------------------
// SbusDecoder::_decodeFrame()
// Expand RMT symbols into a bit array, extract UART bytes, validate frame.
//
// SBUS uses inverted UART: idle = LOW (RMT level 0). A start bit is a HIGH
// pulse (RMT level 1). Data bits are inverted: RMT level 0 = logic 1,
// RMT level 1 = logic 0.
//
// Each RMT symbol covers 2 runs: (duration0, level0) and (duration1, level1).
// Round each duration to the nearest SBUS bit period (10 ticks) to get the
// number of bit times that run occupies.
// -----------------------------------------------------------------------------
void SbusDecoder::_decodeFrame(rmt_data_t* symbols, size_t count) {
    // 25 bytes × 12 bits/byte = 300 bits maximum. Add margin.
    static constexpr int MAX_BITS = 320;
    uint8_t bits[MAX_BITS] = {};
    int bitIdx = 0;

    for (size_t i = 0; i < count && bitIdx < MAX_BITS; i++) {
        // First run in symbol (duration0, level0)
        int n0 = (static_cast<int>(symbols[i].duration0) + (SBUS_BIT_TICKS / 2)) / SBUS_BIT_TICKS;
        // Invert: RMT level 0 (idle/low on wire) = SBUS logic 1
        uint8_t b0 = symbols[i].level0 ? 0U : 1U;
        for (int j = 0; j < n0 && bitIdx < MAX_BITS; j++) {
            bits[bitIdx++] = b0;
        }

        // Second run in symbol (duration1, level1)
        if (symbols[i].duration1 == 0) {
            break;  // RMT end-of-data marker
        }
        int n1 = (static_cast<int>(symbols[i].duration1) + (SBUS_BIT_TICKS / 2)) / SBUS_BIT_TICKS;
        uint8_t b1 = symbols[i].level1 ? 0U : 1U;
        for (int j = 0; j < n1 && bitIdx < MAX_BITS; j++) {
            bits[bitIdx++] = b1;
        }
    }

    // Extract UART bytes: scan for start bit (logic 0), then read 8 data bits
    // (LSB first), skip parity and 2 stop bits.
    uint8_t frame[SBUS_FRAME_BYTES] = {};
    int byteIdx = 0;
    int b = 0;

    while (b + SBUS_BITS_PER_BYTE <= bitIdx && byteIdx < SBUS_FRAME_BYTES) {
        // Search for start bit (logic 0)
        if (bits[b] != 0) {
            b++;
            continue;
        }
        b++;  // consume start bit
        uint8_t byte_val = 0;
        for (int bit = 0; bit < 8; bit++) {
            byte_val = static_cast<uint8_t>(byte_val | (bits[b++] << bit));
        }
        b += 3;  // skip parity bit + 2 stop bits
        frame[byteIdx++] = byte_val;
    }

    // Validate frame: must be exactly 25 bytes with correct header and footer
    if (byteIdx == SBUS_FRAME_BYTES &&
        frame[0]  == SBUS_HEADER   &&
        frame[24] == SBUS_FOOTER) {
        _parseChannels(frame);
        _newData = true;
    }
}

// -----------------------------------------------------------------------------
// SbusDecoder::_parseChannels()
// Unpack 16 × 11-bit channels from bytes 1–22 (LSB first) and decode flags.
//
// SBUS channel packing: channels are packed sequentially, LSB first, starting
// at byte 1. Each channel occupies 11 bits with no padding between channels.
// -----------------------------------------------------------------------------
void SbusDecoder::_parseChannels(const uint8_t* frame) {
    // Unpack 16 channels × 11 bits from bytes 1–22 using a bit cursor
    const uint8_t* data = frame + 1;  // skip header byte
    int bitPos = 0;

    for (int ch = 0; ch < 16; ch++) {
        int byteIdx = bitPos / 8;
        int bitOff  = bitPos % 8;

        // Read 3 bytes (safe: 16 ch × 11 bits = 176 bits = 22 bytes, all within data[0..21])
        uint32_t raw = static_cast<uint32_t>(data[byteIdx])
                     | (static_cast<uint32_t>(data[byteIdx + 1]) << 8)
                     | (static_cast<uint32_t>(data[byteIdx + 2]) << 16);
        _data.ch[ch] = static_cast<int16_t>((raw >> bitOff) & 0x7FFU);
        bitPos += 11;
    }

    // Flags byte is at index 23 (byte 23 of 25-byte frame, 0-indexed)
    uint8_t flags     = frame[23];
    _data.lost_frame  = (flags & SBUS_FLAG_LOST_FRAME) != 0;
    _data.failsafe    = (flags & SBUS_FLAG_FAILSAFE)   != 0;
}
