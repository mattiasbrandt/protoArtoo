// =============================================================================
// include/sbus_flags.h
//
// Pure SBUS flags-byte parsing — no hardware, no FreeRTOS.
// Extracted for testability. Used by SbusDecoder and native tests.
//
// SBUS flags byte bit assignments (per SBUS specification):
//   bit0 = CH17 digital channel
//   bit1 = CH18 digital channel
//   bit2 = lost_frame (single frame missed — not a failsafe condition)
//   bit3 = failsafe   (receiver has lost TX signal)
// =============================================================================
#pragma once
#include <stdbool.h>
#include <stdint.h>

static const uint8_t SBUS_FLAG_CH17 = (1u << 0);
static const uint8_t SBUS_FLAG_CH18 = (1u << 1);
static const uint8_t SBUS_FLAG_LOST_FRAME = (1u << 2);
static const uint8_t SBUS_FLAG_FAILSAFE = (1u << 3);

struct SbusFlags {
    bool ch17;
    bool ch18;
    bool lost_frame;
    bool failsafe;
};

inline SbusFlags parseSbusFlags(uint8_t flags_byte) {
    SbusFlags f;
    f.ch17 = (flags_byte & SBUS_FLAG_CH17) != 0;
    f.ch18 = (flags_byte & SBUS_FLAG_CH18) != 0;
    f.lost_frame = (flags_byte & SBUS_FLAG_LOST_FRAME) != 0;
    f.failsafe = (flags_byte & SBUS_FLAG_FAILSAFE) != 0;
    return f;
}
