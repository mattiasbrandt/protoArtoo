// =============================================================================
// src/dome_link_arbiter.cpp
//
// protoR2link Arbiter  --  transport selection, probe cadence, and sleep-sync
// decisions as a pure step function over explicit state.
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
        s.uartEverSeen        = true;
    }

    // -----------------------------------------------------------------------
    // 2. Transport selection
    //
    // WiFi fallback is allowed only when UART has never established contact
    // this boot. Once UART contact is seen, a heartbeat loss means the dome
    // is off or rebooting  --  stay on UART and keep probing (via the normal
    // 1 Hz heartbeat) rather than silently migrating to WiFi (ADR 0005,
    // resolves the WiFi-steady-state deviation accepted for v1.0.0).
    // -----------------------------------------------------------------------

    const bool uartFresh =
        s.lastUartHeartbeatMs > 0 &&
        (in.nowMs - s.lastUartHeartbeatMs) < kDomeLinkHeartbeatTimeoutMs;

    const bool initialUartGrace =
        s.lastUartHeartbeatMs == 0 && in.nowMs < kDomeLinkHeartbeatTimeoutMs;

    const bool wantWifi = !uartFresh && !initialUartGrace &&
                          in.staConnected && in.peerKnown &&
                          !s.uartEverSeen;

    if (!wantWifi) {
        s.uartProbeWindowUntilMs = 0;
        a.acquireUart            = true;
        a.txRoute                = DOME_LINK_TRANSPORT_UART;
    } else {
        if ((in.nowMs - s.lastUartProbeMs) >= kDomeLinkUartProbeIntervalMs) {
            s.lastUartProbeMs        = in.nowMs;
            s.uartProbeWindowUntilMs = in.nowMs + kDomeLinkUartProbeWindowMs;
            a.acquireUart            = true;
            a.sendUartProbe          = true;
        } else if (s.uartProbeWindowUntilMs != 0 &&
                   (int32_t)(in.nowMs - s.uartProbeWindowUntilMs) >= 0) {
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

    // -----------------------------------------------------------------------
    // 4. Sleep-sync state machine
    //
    // Body sleep state is ground truth. Send #PASL/#PAWU on every fresh dome
    // connect (sleepSynced resets on disconnect) and on sleep-mode change
    // while connected. Polling avoids a stale #PASL firing on reconnect.
    // -----------------------------------------------------------------------

    if (!in.domeConnected) {
        s.sleepSynced = false;
    } else if (!s.sleepSynced || in.bodySleeping != s.lastSyncedSleepMode) {
        a.sleepSync           = in.bodySleeping ? SleepSyncAction::SendSleep
                                                : SleepSyncAction::SendWake;
        s.sleepSynced         = true;
        s.lastSyncedSleepMode = in.bodySleeping;
    }

    return a;
}
