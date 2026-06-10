// =============================================================================
// test/test_native/test_dome_link_arbiter/test_dome_link_arbiter.cpp
//
// Native timeline tests for the protoR2link Arbiter (ADR 0005).
//
// Slice 1 (issue #3): transport selection, probe cadence, and heartbeat gate.
// =============================================================================

#include <unity.h>

#include "dome_link_arbiter.h"

// =============================================================================
// Helpers
// =============================================================================

static DomeLinkArbiterInputs makeInputs(uint32_t nowMs,
                                        bool uartHbSeen   = false,
                                        bool staConnected = false,
                                        bool peerKnown    = false) {
    DomeLinkArbiterInputs in = {};
    in.nowMs             = nowMs;
    in.uartHeartbeatSeen = uartHbSeen;
    in.staConnected      = staConnected;
    in.peerKnown         = peerKnown;
    return in;
}

void setUp() {}
void tearDown() {}

// =============================================================================
// Boot grace period
// =============================================================================

void test_boot_grace_stays_uart_before_timeout() {
    DomeLinkArbiterState s = {};

    // No UART heartbeat, no WiFi — within 5 s grace window.
    DomeLinkArbiterActions a = domeLinkArbiterStep(s, makeInputs(0));
    TEST_ASSERT_EQUAL_INT(DOME_LINK_TRANSPORT_UART, (int)a.txRoute);
    TEST_ASSERT_TRUE(a.acquireUart);
    TEST_ASSERT_FALSE(a.sendUartProbe);
    TEST_ASSERT_FALSE(a.releaseUartToAudio);

    a = domeLinkArbiterStep(s, makeInputs(kDomeLinkHeartbeatTimeoutMs - 1));
    TEST_ASSERT_EQUAL_INT(DOME_LINK_TRANSPORT_UART, (int)a.txRoute);
    TEST_ASSERT_TRUE(a.acquireUart);
}

// =============================================================================
// UART steady state
// =============================================================================

void test_uart_steady_state_while_heartbeat_fresh() {
    DomeLinkArbiterState s = {};

    // First heartbeat arrives at t=1000.
    DomeLinkArbiterActions a = domeLinkArbiterStep(s, makeInputs(1000, /*uartHbSeen=*/true));
    TEST_ASSERT_EQUAL_INT(DOME_LINK_TRANSPORT_UART, (int)a.txRoute);
    TEST_ASSERT_TRUE(a.acquireUart);
    TEST_ASSERT_FALSE(a.sendUartProbe);

    // Heartbeat seen again at t=2000 — still UART, no probe.
    a = domeLinkArbiterStep(s, makeInputs(2000, true));
    TEST_ASSERT_EQUAL_INT(DOME_LINK_TRANSPORT_UART, (int)a.txRoute);
    TEST_ASSERT_FALSE(a.sendUartProbe);
    TEST_ASSERT_FALSE(a.releaseUartToAudio);

    // t=5999, last HB at 2000 — within 5 s window (5999-2000=3999 < 5000).
    a = domeLinkArbiterStep(s, makeInputs(5999));
    TEST_ASSERT_EQUAL_INT(DOME_LINK_TRANSPORT_UART, (int)a.txRoute);
}

// =============================================================================
// WiFi fallback (the known deviation, pinned by issue #3)
//
// KNOWN DEVIATION: when UART was never seen this boot and WiFi peers are
// reachable, the 5 s timeout causes WiFi to become the steady-state transport.
// Resolved in slice 2 (issue #5) by gating WiFi fallback on uartEverSeen.
// =============================================================================

void test_wifi_fallback_after_heartbeat_timeout() {
    DomeLinkArbiterState s = {};

    // Grace ends at kDomeLinkHeartbeatTimeoutMs — fallback to WiFi.
    DomeLinkArbiterActions a = domeLinkArbiterStep(
        s, makeInputs(kDomeLinkHeartbeatTimeoutMs, false, /*sta=*/true, /*peer=*/true));
    TEST_ASSERT_EQUAL_INT(DOME_LINK_TRANSPORT_WIFI, (int)a.txRoute);
    TEST_ASSERT_FALSE(a.acquireUart);
}

void test_wifi_not_used_without_sta_connection() {
    DomeLinkArbiterState s = {};

    // Grace expired but no WiFi — stays UART.
    DomeLinkArbiterActions a = domeLinkArbiterStep(
        s, makeInputs(kDomeLinkHeartbeatTimeoutMs, false, /*sta=*/false, /*peer=*/false));
    TEST_ASSERT_EQUAL_INT(DOME_LINK_TRANSPORT_UART, (int)a.txRoute);
    TEST_ASSERT_TRUE(a.acquireUart);
}

// =============================================================================
// Probe cadence (30 s interval / 150 ms window)
// =============================================================================

void test_probe_fires_after_30s_in_wifi_mode() {
    DomeLinkArbiterState s = {};

    // Settle into WiFi mode at t=5000.
    domeLinkArbiterStep(s, makeInputs(5000, false, true, true));

    // Probe fires when nowMs - lastUartProbeMs (0) >= 30000.
    DomeLinkArbiterActions a = domeLinkArbiterStep(
        s, makeInputs(kDomeLinkUartProbeIntervalMs, false, true, true));
    TEST_ASSERT_EQUAL_INT(DOME_LINK_TRANSPORT_WIFI, (int)a.txRoute);
    TEST_ASSERT_TRUE(a.acquireUart);
    TEST_ASSERT_TRUE(a.sendUartProbe);
    TEST_ASSERT_FALSE(a.releaseUartToAudio);
    TEST_ASSERT_EQUAL_UINT32(kDomeLinkUartProbeIntervalMs + kDomeLinkUartProbeWindowMs,
                             s.uartProbeWindowUntilMs);
}

void test_probe_window_expires_and_releases_uart() {
    DomeLinkArbiterState s = {};

    // Trigger probe at t=30000.
    domeLinkArbiterStep(s, makeInputs(kDomeLinkUartProbeIntervalMs, false, true, true));
    TEST_ASSERT_TRUE(s.uartProbeWindowUntilMs != 0);

    // One tick before expiry — no release yet.
    DomeLinkArbiterActions a = domeLinkArbiterStep(
        s, makeInputs(kDomeLinkUartProbeIntervalMs + kDomeLinkUartProbeWindowMs - 1,
                      false, true, true));
    TEST_ASSERT_FALSE(a.releaseUartToAudio);

    // At expiry — release fires.
    a = domeLinkArbiterStep(
        s, makeInputs(kDomeLinkUartProbeIntervalMs + kDomeLinkUartProbeWindowMs,
                      false, true, true));
    TEST_ASSERT_TRUE(a.releaseUartToAudio);
    TEST_ASSERT_FALSE(a.acquireUart);
    TEST_ASSERT_EQUAL_UINT32(0, s.uartProbeWindowUntilMs);
}

void test_probe_interval_repeats_after_30s() {
    DomeLinkArbiterState s = {};

    // First probe at t=30000, window expires.
    domeLinkArbiterStep(s, makeInputs(kDomeLinkUartProbeIntervalMs, false, true, true));
    domeLinkArbiterStep(
        s, makeInputs(kDomeLinkUartProbeIntervalMs + kDomeLinkUartProbeWindowMs,
                      false, true, true));

    // Second probe at t=60000.
    DomeLinkArbiterActions a = domeLinkArbiterStep(
        s, makeInputs(2 * kDomeLinkUartProbeIntervalMs, false, true, true));
    TEST_ASSERT_TRUE(a.sendUartProbe);
    TEST_ASSERT_TRUE(a.acquireUart);
}

// =============================================================================
// #APHB recovery from WiFi to UART
// =============================================================================

void test_aphb_recovery_switches_back_to_uart() {
    DomeLinkArbiterState s = {};

    // Enter WiFi, trigger probe at t=30000.
    domeLinkArbiterStep(s, makeInputs(5000, false, true, true));
    domeLinkArbiterStep(s, makeInputs(kDomeLinkUartProbeIntervalMs, false, true, true));

    // #APHB arrives within the probe window.
    DomeLinkArbiterActions a = domeLinkArbiterStep(
        s, makeInputs(kDomeLinkUartProbeIntervalMs + 50, /*uartHbSeen=*/true, true, true));
    TEST_ASSERT_EQUAL_INT(DOME_LINK_TRANSPORT_UART, (int)a.txRoute);
    TEST_ASSERT_TRUE(a.acquireUart);
    TEST_ASSERT_FALSE(a.releaseUartToAudio);
    TEST_ASSERT_EQUAL_UINT32(0, s.uartProbeWindowUntilMs);
}

// =============================================================================
// 1 Hz heartbeat gate
// =============================================================================

void test_heartbeat_fires_at_1hz() {
    DomeLinkArbiterState s = {};

    DomeLinkArbiterActions a = domeLinkArbiterStep(s, makeInputs(999));
    TEST_ASSERT_FALSE(a.sendHeartbeat);

    a = domeLinkArbiterStep(s, makeInputs(1000));
    TEST_ASSERT_TRUE(a.sendHeartbeat);

    a = domeLinkArbiterStep(s, makeInputs(1999));
    TEST_ASSERT_FALSE(a.sendHeartbeat);

    a = domeLinkArbiterStep(s, makeInputs(2000));
    TEST_ASSERT_TRUE(a.sendHeartbeat);
}

// =============================================================================
// Test runner
// =============================================================================

int main() {
    UNITY_BEGIN();

    RUN_TEST(test_boot_grace_stays_uart_before_timeout);
    RUN_TEST(test_uart_steady_state_while_heartbeat_fresh);
    RUN_TEST(test_wifi_fallback_after_heartbeat_timeout);
    RUN_TEST(test_wifi_not_used_without_sta_connection);
    RUN_TEST(test_probe_fires_after_30s_in_wifi_mode);
    RUN_TEST(test_probe_window_expires_and_releases_uart);
    RUN_TEST(test_probe_interval_repeats_after_30s);
    RUN_TEST(test_aphb_recovery_switches_back_to_uart);
    RUN_TEST(test_heartbeat_fires_at_1hz);

    return UNITY_END();
}
