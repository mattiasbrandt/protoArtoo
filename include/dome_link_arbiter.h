// =============================================================================
// include/dome_link_arbiter.h
//
// protoR2link Arbiter — pure decision module for dome-link transport selection
// and probe cadence.
//
// Design (ADR 0005):
//   - domeLinkArbiterStep() is a pure function over explicit state: no side
//     effects, no hardware access, no Arduino/FreeRTOS dependencies.
//   - The task loop (dome_link.cpp) gathers inputs, calls this function, and
//     executes the returned actions through the existing concrete transport
//     functions. The arbiter decides WHEN; the shell does HOW.
//   - Native timeline tests assert on DomeLinkArbiterActions directly — no
//     fakes, no mocks needed.
//
// This header is Arduino-free and compiles in the native test environment.
// =============================================================================
#pragma once

#include <stdint.h>

#include "dome_link_transport.h"

// ---------------------------------------------------------------------------
// Timing constants (moved here from dome_link.cpp)
// ---------------------------------------------------------------------------

constexpr uint32_t kDomeLinkHeartbeatIntervalMs  = 1000;
constexpr uint32_t kDomeLinkHeartbeatTimeoutMs   = 5000;
constexpr uint32_t kDomeLinkUartProbeIntervalMs  = 30000;
constexpr uint32_t kDomeLinkUartProbeWindowMs    = 150;

// ---------------------------------------------------------------------------
// Arbiter state (default-init == boot state)
// ---------------------------------------------------------------------------

struct DomeLinkArbiterState {
    uint32_t lastUartHeartbeatMs;    // 0 = never seen this boot
    uint32_t lastUartProbeMs;        // 0 = never probed
    uint32_t uartProbeWindowUntilMs; // 0 = not in probe window
    uint32_t lastHeartbeatTxMs;      // 0 = never sent
};

// ---------------------------------------------------------------------------
// Arbiter inputs (gathered by the task shell each tick)
// ---------------------------------------------------------------------------

struct DomeLinkArbiterInputs {
    uint32_t nowMs;
    bool     uartHeartbeatSeen; // #APHB received on UART this tick
    bool     staConnected;      // WiFi STA connected
    bool     peerKnown;         // dome peer IP resolved
};

// ---------------------------------------------------------------------------
// Arbiter actions (executed by the task shell after each step)
// ---------------------------------------------------------------------------

struct DomeLinkArbiterActions {
    DomeLinkTransport txRoute;            // route queue drain and heartbeat TX
    bool              acquireUart;        // shell calls acquireDomeUart()
    bool              releaseUartToAudio; // shell calls releaseUartToAudioRx()
    bool              sendUartProbe;      // shell prints MD_BODY_HB over UART
    bool              sendHeartbeat;      // shell sends 1 Hz #PAHB on txRoute
};

// ---------------------------------------------------------------------------
// Step function
// ---------------------------------------------------------------------------

DomeLinkArbiterActions domeLinkArbiterStep(DomeLinkArbiterState&       s,
                                           const DomeLinkArbiterInputs& in);
