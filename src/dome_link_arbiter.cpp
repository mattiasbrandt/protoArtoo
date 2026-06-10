// =============================================================================
// src/dome_link_arbiter.cpp
//
// protoR2link Arbiter — transport selection and probe cadence as a pure step
// function over explicit state.
//
// See include/dome_link_arbiter.h and docs/adr/0005-protor2link-arbiter-
// functional-core.md for design rationale.
// =============================================================================

#include "dome_link_arbiter.h"

DomeLinkArbiterActions domeLinkArbiterStep(DomeLinkArbiterState&        s,
                                           const DomeLinkArbiterInputs& in) {
    DomeLinkArbiterActions a = {};
    a.txRoute = DOME_LINK_TRANSPORT_UART;

    // -----------------------------------------------------------------------
    // 1. Latch UART heartbeat timestamp
    // -----------------------------------------------------------------------

    if (in.uartHeartbeatSeen) {
        s.lastUartHeartbeatMs = in.nowMs;
    }

    // -----------------------------------------------------------------------
    // 2. Transport selection
    //
    // KNOWN DEVIATION: the 5 s UART heartbeat timeout makes WiFi the
    // steady-state transport whenever both peers share a network, even though
    // the UART slip ring is the intended primary path (ADR 0003).  Accepted
    // for v1.0.0; resolved in the next arbiter slice (issue #5) by gating
    // WiFi fallback on UART never having established contact this boot.
    // -----------------------------------------------------------------------

    const bool uartFresh =
        s.lastUartHeartbeatMs > 0 &&
        (in.nowMs - s.lastUartHeartbeatMs) < kDomeLinkHeartbeatTimeoutMs;

    const bool initialUartGrace =
        s.lastUartHeartbeatMs == 0 && in.nowMs < kDomeLinkHeartbeatTimeoutMs;

    const bool wantWifi =
        !uartFresh && !initialUartGrace && in.staConnected && in.peerKnown;

    if (!wantWifi) {
        // UART primary path: boot grace, UART active, or heartbeat recovery.
        s.uartProbeWindowUntilMs = 0;
        a.acquireUart            = true;
        a.txRoute                = DOME_LINK_TRANSPORT_UART;
    } else {
        // WiFi fallback path: manage the 30 s / 150 ms slip-ring probe cadence.
        if ((in.nowMs - s.lastUartProbeMs) >= kDomeLinkUartProbeIntervalMs) {
            s.lastUartProbeMs        = in.nowMs;
            s.uartProbeWindowUntilMs = in.nowMs + kDomeLinkUartProbeWindowMs;
            a.acquireUart            = true;
            a.sendUartProbe          = true;
        } else if (s.uartProbeWindowUntilMs != 0 &&
                   (int32_t)(in.nowMs - s.uartProbeWindowUntilMs) >= 0) {
            // Probe window expired — release UART back to audio RX.
            a.releaseUartToAudio     = true;
            s.uartProbeWindowUntilMs = 0;
        }
        a.txRoute = DOME_LINK_TRANSPORT_WIFI;
    }

    // -----------------------------------------------------------------------
    // 3. 1 Hz heartbeat gate
    // -----------------------------------------------------------------------

    if ((in.nowMs - s.lastHeartbeatTxMs) >= kDomeLinkHeartbeatIntervalMs) {
        s.lastHeartbeatTxMs = in.nowMs;
        a.sendHeartbeat     = true;
    }

    return a;
}
