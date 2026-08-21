// =============================================================================
// include/dome_link_transport.h
//
// DomeLinkTransport enum  --  active transport state for the protoR2link.
//
// Extracted from robot_state.h so dome_link_arbiter.h can use it without
// pulling in the full robot state (Arduino, FreeRTOS, etc.).
// =============================================================================
#pragma once

#include <stdint.h>

enum DomeLinkTransport : uint8_t {
    DOME_LINK_TRANSPORT_DISCONNECTED = 0,
    DOME_LINK_TRANSPORT_UART,
    DOME_LINK_TRANSPORT_WIFI,
};
