// =============================================================================
// include/audio_rx_status.h
//
// AudioRxStatus enum — RX availability state for audio module status reporting.
//
// Extracted from robot_state.h so audio_driver.h can use it without pulling in
// the full robot state (Arduino, FreeRTOS, etc.).
// =============================================================================
#pragma once

#include <stdint.h>

enum AudioRxStatus : uint8_t {
    AUDIO_RX_UNKNOWN = 0,
    AUDIO_RX_AVAILABLE,
    AUDIO_RX_BLOCKED_BY_DOME_UART,
    AUDIO_RX_NO_RESPONSE,
};
