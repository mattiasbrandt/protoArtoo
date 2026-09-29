// =============================================================================
// include/board_lane_wire.h
//
// The wire contract each Board Lane speaks (include/board_lanes.inc), where the
// lane itself fixes it: the baud and the Component Protocol on that UART.
//
// A lane's pins say where a signal is routed; this says what travels on it, so
// a page that states the link's fixed facts reads them from the firmware
// (GET /api/identity board_lanes) rather than keeping a copy (#369). The
// driver that opens the UART reads the same row, so the number the page shows
// is the number the controller is configured with.
//
// Kept beside the manifest rather than inside it: tools/check_pin_drift.py
// parses board_lanes.inc strictly, one four-argument row per line, and a wider
// row would be reported unreadable. The serializer joins a lane to its row
// here by the lane's own identifier (kBoardLaneWire_<name>), so a lane added to
// the manifest with no row below is a compile error, not a lane that silently
// reports no contract.
//
// Pure data: no Arduino, no FreeRTOS, included by the native-tested serializer.
// =============================================================================
#pragma once

#include <stdint.h>

struct BoardLaneWire {
    uint32_t baud;         // 0 when protocol is nullptr
    const char* protocol;  // the Component Protocol's wire word; nullptr = not the lane's to say
};

// protoR2link has no Component Member: the Dome Controller at the far end is
// whatever board speaks Marcduino ASCII at 9600 8N1 over the slip ring, on
// every Board Variant (docs/pin_map.md). The contract belongs to the lane.
inline constexpr BoardLaneWire kBoardLaneWire_protor2link = {9600, "marcduino"};

// These two lanes carry none on purpose, and filling them in would be a second
// home for a fact that already has one. Foot Drive's baud and protocol are its
// member's (kDriveBackend, include/drive_backend.h), and the audio lane's are
// whichever sound module is fitted (each driver under src/drivers/audio_*.cpp
// opens its own UART).
inline constexpr BoardLaneWire kBoardLaneWire_drive = {0, nullptr};
inline constexpr BoardLaneWire kBoardLaneWire_audio = {0, nullptr};
