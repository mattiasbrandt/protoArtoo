// =============================================================================
// test/test_native/test_dome_link_arbiter/test_dome_link_arbiter.cpp
//
// Native timeline tests for the protoR2link Arbiter (ADR 0005).
//
// Slice 1 (issue #3): transport selection, probe cadence, and heartbeat gate.
// Slice 2 (issue #4): sleep-sync state machine.
// Slice 3 (issue #5): WiFi-fallback gating on UART boot contact.
// =============================================================================

#include <unity.h>

#include "dome_link_arbiter.h"

// =============================================================================
// Helpers
// =============================================================================

static DomeLinkArbiterInputs makeInputs(uint32_t nowMs,
                                        bool uartHbSeen    = false,
                                        bool staConnected  = false,
                                        bool peerKnown     = false,
                                        bool bodySleeping  = false,
                                        bool domeConnected = false) {
    DomeLinkArbiterInputs in = {};
    in.nowMs             = nowMs;
    in.uartHeartbeatSeen = uartHbSeen;
    in.staConnected      = staConnected;
    in.peerKnown         = peerKnown;
    in.bodySleeping      = bodySleeping;
    in.domeConnected     = domeConnected;
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
// WiFi fallback — UART never contacted this boot (uartEverSeen=false)
//
// Old behaviour (slice 1 pin, updated by slice 3 issue #5):
//   The 5 s UART timeout caused WiFi to become the steady-state transport
//   whenever WiFi peers were reachable, regardless of UART history.  This was
//   the known deviation accepted for v1.0.0.
//
// New behaviour (slice 3):
//   WiFi fallback is now allowed only when UART was never seen this boot.
//   This test still passes because uartEverSeen=false in a zero-init state.
// =============================================================================

void test_wifi_fallback_after_heartbeat_timeout() {
    DomeLinkArbiterState s = {};

    // Grace ends, UART never seen, WiFi available — fallback allowed.
    DomeLinkArbiterActions a = domeLinkArbiterStep(
        s, makeInputs(kDomeLinkHeartbeatTimeoutMs, false, /*sta=*/true, /*peer=*/true));
    TEST_ASSERT_EQUAL_INT(DOME_LINK_TRANSPORT_WIFI, (int)a.txRoute);
    TEST_ASSERT_FALSE(a.acquireUart);
    TEST_ASSERT_FALSE(s.uartEverSeen);
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
// Sleep-sync state machine (issue #4)
// =============================================================================

void test_sleep_sync_sent_on_first_connect_awake() {
    DomeLinkArbiterState s = {};

    // Dome connects while body is awake — SendWake fires.
    DomeLinkArbiterActions a = domeLinkArbiterStep(
        s, makeInputs(1000, true, false, false, /*sleeping=*/false, /*domeConn=*/true));
    TEST_ASSERT_EQUAL_INT((int)SleepSyncAction::SendWake, (int)a.sleepSync);
    TEST_ASSERT_TRUE(s.sleepSynced);
    TEST_ASSERT_FALSE(s.lastSyncedSleepMode);

    // Already synced — no re-send next tick.
    a = domeLinkArbiterStep(s, makeInputs(1010, false, false, false, false, true));
    TEST_ASSERT_EQUAL_INT((int)SleepSyncAction::None, (int)a.sleepSync);
}

void test_sleep_sync_sent_on_first_connect_sleeping() {
    DomeLinkArbiterState s = {};

    DomeLinkArbiterActions a = domeLinkArbiterStep(
        s, makeInputs(1000, true, false, false, /*sleeping=*/true, /*domeConn=*/true));
    TEST_ASSERT_EQUAL_INT((int)SleepSyncAction::SendSleep, (int)a.sleepSync);
    TEST_ASSERT_TRUE(s.lastSyncedSleepMode);
}

void test_sleep_sync_resent_on_reconnect() {
    DomeLinkArbiterState s = {};

    // Connect and sync.
    domeLinkArbiterStep(s, makeInputs(1000, true, false, false, false, true));
    TEST_ASSERT_TRUE(s.sleepSynced);

    // Disconnect — sleepSynced resets.
    domeLinkArbiterStep(s, makeInputs(2000, false, false, false, false, /*domeConn=*/false));
    TEST_ASSERT_FALSE(s.sleepSynced);

    // Reconnect — sync fires again.
    DomeLinkArbiterActions a = domeLinkArbiterStep(
        s, makeInputs(3000, false, false, false, false, true));
    TEST_ASSERT_EQUAL_INT((int)SleepSyncAction::SendWake, (int)a.sleepSync);
}

void test_sleep_sync_resent_on_mode_change() {
    DomeLinkArbiterState s = {};

    // Synced awake.
    domeLinkArbiterStep(s, makeInputs(1000, true, false, false, false, true));

    // Body goes to sleep — resend.
    DomeLinkArbiterActions a = domeLinkArbiterStep(
        s, makeInputs(2000, false, false, false, /*sleeping=*/true, true));
    TEST_ASSERT_EQUAL_INT((int)SleepSyncAction::SendSleep, (int)a.sleepSync);
    TEST_ASSERT_TRUE(s.lastSyncedSleepMode);

    // Same mode next tick — no re-send.
    a = domeLinkArbiterStep(s, makeInputs(3000, false, false, false, true, true));
    TEST_ASSERT_EQUAL_INT((int)SleepSyncAction::None, (int)a.sleepSync);
}

void test_sleep_sync_not_sent_while_dome_disconnected() {
    DomeLinkArbiterState s = {};

    DomeLinkArbiterActions a = domeLinkArbiterStep(
        s, makeInputs(1000, false, false, false, true, /*domeConn=*/false));
    TEST_ASSERT_EQUAL_INT((int)SleepSyncAction::None, (int)a.sleepSync);
}

void test_sleep_sync_not_resent_while_already_synced() {
    DomeLinkArbiterState s = {};

    // Connect and sync awake.
    domeLinkArbiterStep(s, makeInputs(1000, true, false, false, false, true));
    TEST_ASSERT_TRUE(s.sleepSynced);

    // Same mode, still connected — no re-send on subsequent ticks.
    DomeLinkArbiterActions a = domeLinkArbiterStep(
        s, makeInputs(2000, false, false, false, false, true));
    TEST_ASSERT_EQUAL_INT((int)SleepSyncAction::None, (int)a.sleepSync);

    a = domeLinkArbiterStep(s, makeInputs(3000, false, false, false, false, true));
    TEST_ASSERT_EQUAL_INT((int)SleepSyncAction::None, (int)a.sleepSync);
}

// =============================================================================
// WiFi-fallback gating on UART boot contact (issue #5)
//
// Once UART establishes contact this boot, WiFi fallback is suppressed. The
// arbiter stays on UART and probes via the normal 1 Hz heartbeat rather than
// switching to WiFi.
// =============================================================================

void test_wifi_fallback_suppressed_after_uart_contact() {
    DomeLinkArbiterState s = {};

    // UART heartbeat received at t=1000 — contact established.
    domeLinkArbiterStep(s, makeInputs(1000, /*uartHbSeen=*/true, true, true));
    TEST_ASSERT_TRUE(s.uartEverSeen);

    // UART goes stale at t=7000 (7000-1000=6000 > 5000 timeout).
    // WiFi peers available — fallback must be suppressed.
    // sendHeartbeat verifies the arbiter continues probing via 1 Hz heartbeat.
    DomeLinkArbiterActions a = domeLinkArbiterStep(
        s, makeInputs(7000, false, /*sta=*/true, /*peer=*/true));
    TEST_ASSERT_EQUAL_INT(DOME_LINK_TRANSPORT_UART, (int)a.txRoute);
    TEST_ASSERT_TRUE(a.acquireUart);
    TEST_ASSERT_TRUE(a.sendHeartbeat);
}

void test_wifi_fallback_allowed_when_uart_never_seen() {
    // uartEverSeen=false (zero-init) — WiFi fallback unchanged.
    DomeLinkArbiterState s = {};

    DomeLinkArbiterActions a = domeLinkArbiterStep(
        s, makeInputs(kDomeLinkHeartbeatTimeoutMs, false, true, true));
    TEST_ASSERT_EQUAL_INT(DOME_LINK_TRANSPORT_WIFI, (int)a.txRoute);
}

void test_uart_recovery_from_wifi_probe_sets_uart_ever_seen() {
    DomeLinkArbiterState s = {};

    // Enter WiFi fallback (UART never seen), probe fires at t=30000.
    domeLinkArbiterStep(s, makeInputs(5000, false, true, true));
    domeLinkArbiterStep(s, makeInputs(kDomeLinkUartProbeIntervalMs, false, true, true));

    // #APHB arrives — recovery to UART, uartEverSeen latches.
    DomeLinkArbiterActions a = domeLinkArbiterStep(
        s, makeInputs(kDomeLinkUartProbeIntervalMs + 50, /*uartHbSeen=*/true, true, true));
    TEST_ASSERT_EQUAL_INT(DOME_LINK_TRANSPORT_UART, (int)a.txRoute);
    TEST_ASSERT_TRUE(s.uartEverSeen);

    // UART goes stale again — WiFi fallback suppressed because uartEverSeen.
    a = domeLinkArbiterStep(
        s, makeInputs(kDomeLinkUartProbeIntervalMs + 50 + kDomeLinkHeartbeatTimeoutMs + 1,
                      false, true, true));
    TEST_ASSERT_EQUAL_INT(DOME_LINK_TRANSPORT_UART, (int)a.txRoute);
}

// =============================================================================
// Test runner
// =============================================================================

int main() {
    UNITY_BEGIN();

    // Slice 1 — transport selection + probe + heartbeat
    RUN_TEST(test_boot_grace_stays_uart_before_timeout);
    RUN_TEST(test_uart_steady_state_while_heartbeat_fresh);
    RUN_TEST(test_wifi_fallback_after_heartbeat_timeout);
    RUN_TEST(test_wifi_not_used_without_sta_connection);
    RUN_TEST(test_probe_fires_after_30s_in_wifi_mode);
    RUN_TEST(test_probe_window_expires_and_releases_uart);
    RUN_TEST(test_probe_interval_repeats_after_30s);
    RUN_TEST(test_aphb_recovery_switches_back_to_uart);
    RUN_TEST(test_heartbeat_fires_at_1hz);

    // Slice 2 — sleep-sync state machine
    RUN_TEST(test_sleep_sync_sent_on_first_connect_awake);
    RUN_TEST(test_sleep_sync_sent_on_first_connect_sleeping);
    RUN_TEST(test_sleep_sync_not_resent_while_already_synced);
    RUN_TEST(test_sleep_sync_resent_on_reconnect);
    RUN_TEST(test_sleep_sync_resent_on_mode_change);
    RUN_TEST(test_sleep_sync_not_sent_while_dome_disconnected);

    // Slice 3 — WiFi fallback gating on UART boot contact
    RUN_TEST(test_wifi_fallback_suppressed_after_uart_contact);
    RUN_TEST(test_wifi_fallback_allowed_when_uart_never_seen);
    RUN_TEST(test_uart_recovery_from_wifi_probe_sets_uart_ever_seen);

    return UNITY_END();
}
