// =============================================================================
// include/sbus_unpack.h
//
// Pure SBUS channel-unpacking helper — no hardware, no FreeRTOS.
// Extracted for testability. Used by SbusDecoder and native tests.
//
// SBUS channel packing (per SBUS specification):
//   16 channels × 11 bits packed sequentially, LSB first, starting at byte 1
//   of the 25-byte frame (i.e. the first byte of the 22-byte data payload).
//   No padding between channels. Total: 176 bits = 22 bytes.
//
// Input:  data[22] — bytes 1–22 of the SBUS frame (header byte already skipped)
// Output: ch[16]   — caller-supplied array, filled with 11-bit values (0–2047)
// =============================================================================
#pragma once
#include <stdint.h>

// -----------------------------------------------------------------------------
// sbusUnpackChannels()
// Unpack 16 × 11-bit SBUS channels from a 22-byte packed payload.
//
// Parameters:
//   data  — pointer to the 22-byte channel payload (frame bytes 1–22,
//            i.e. frame + 1 with the 0x0F header already skipped).
//   ch    — output array of 16 int16_t values; each receives a value 0–2047.
//
// Thread safety: pure function, no side effects, no global state.
// -----------------------------------------------------------------------------
inline void sbusUnpackChannels(const uint8_t* data, int16_t* ch) {
    int bitPos = 0;
    for (int i = 0; i < 16; i++) {
        int byteIdx = bitPos / 8;
        int bitOff = bitPos % 8;

        // Read 3 bytes (safe: 16 ch × 11 bits = 176 bits = 22 bytes,
        // all within data[0..21]; worst case byteIdx+2 = 21 at ch15)
        uint32_t raw = static_cast<uint32_t>(data[byteIdx]) |
                       (static_cast<uint32_t>(data[byteIdx + 1]) << 8) |
                       (static_cast<uint32_t>(data[byteIdx + 2]) << 16);
        ch[i] = static_cast<int16_t>((raw >> bitOff) & 0x7FFU);
        bitPos += 11;
    }
}
