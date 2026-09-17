// =============================================================================
// test/test_native/test_failsafe_gate/test_failsafe_gate.cpp
//
// Native unit tests for FailsafeGate state machine.
// Tests all five failsafe layers: trigger, clear, priority, and latching behavior.
//
// Safety relevance: FailsafeGate must correctly enforce latching (ESTOP),
// independent layer clearing, and priority ordering without deadlocks.
// =============================================================================
#include <cstring>
#include <unity.h>

#include "failsafe_gate.h"
#include "robot_state.h"

// robotState and robotStateMux are provided by native_test_stubs.cpp
extern RobotState robotState;
extern portMUX_TYPE robotStateMux;

// requestStatusBroadcastNow() counts here in the native build
// (src/native_test_stubs.cpp), which is how the tests below observe that a
// failsafe edge asked the event stream to publish. The counter is reset inside
// each test rather than in setUp(): tearDown() clears every layer, and those
// clears are edges too.
extern unsigned g_test_status_broadcast_count;

void setUp() {
    // Reset mocks before each test
    memset(&robotState, 0, sizeof(RobotState));
    robotStateMux = 0;
    failsafeInit(&robotStateMux);
}

void tearDown() {
    // Cleanup after each test
    // Reset all layers by clearing them and ESTOP
    for (int i = 0; i < 5; ++i) {
        failsafeClear((FailsafeLayer)i);
    }
    failsafeClearEstop();
}

// --- Test 1: Trigger a layer — failsafeIsActive() returns true ---

void test_trigger_sbus_hw_makes_failsafe_active() {
    TEST_ASSERT_FALSE(failsafeIsActive());
    failsafeTrigger(FailsafeLayer::SBUS_HW);
    TEST_ASSERT_TRUE(failsafeIsActive());
}

void test_trigger_sbus_watchdog_makes_failsafe_active() {
    TEST_ASSERT_FALSE(failsafeIsActive());
    failsafeTrigger(FailsafeLayer::SBUS_WATCHDOG);
    TEST_ASSERT_TRUE(failsafeIsActive());
}

void test_trigger_web_timeout_makes_failsafe_active() {
    TEST_ASSERT_FALSE(failsafeIsActive());
    failsafeTrigger(FailsafeLayer::WEB_TIMEOUT);
    TEST_ASSERT_TRUE(failsafeIsActive());
}

void test_trigger_watchdog_reset_makes_failsafe_active() {
    TEST_ASSERT_FALSE(failsafeIsActive());
    failsafeTrigger(FailsafeLayer::WATCHDOG_RESET);
    TEST_ASSERT_TRUE(failsafeIsActive());
}

void test_trigger_estop_makes_failsafe_active() {
    TEST_ASSERT_FALSE(failsafeIsActive());
    failsafeTrigger(FailsafeLayer::ESTOP);
    TEST_ASSERT_TRUE(failsafeIsActive());
}

// --- Test 2: Clear a non-latching layer — failsafeIsActive() returns false ---

void test_clear_sbus_hw_deactivates_failsafe() {
    failsafeTrigger(FailsafeLayer::SBUS_HW);
    TEST_ASSERT_TRUE(failsafeIsActive());
    failsafeClear(FailsafeLayer::SBUS_HW);
    TEST_ASSERT_FALSE(failsafeIsActive());
}

void test_clear_sbus_watchdog_deactivates_failsafe() {
    failsafeTrigger(FailsafeLayer::SBUS_WATCHDOG);
    TEST_ASSERT_TRUE(failsafeIsActive());
    failsafeClear(FailsafeLayer::SBUS_WATCHDOG);
    TEST_ASSERT_FALSE(failsafeIsActive());
}

void test_clear_web_timeout_deactivates_failsafe() {
    failsafeTrigger(FailsafeLayer::WEB_TIMEOUT);
    TEST_ASSERT_TRUE(failsafeIsActive());
    failsafeClear(FailsafeLayer::WEB_TIMEOUT);
    TEST_ASSERT_FALSE(failsafeIsActive());
}

void test_clear_watchdog_reset_deactivates_failsafe() {
    failsafeTrigger(FailsafeLayer::WATCHDOG_RESET);
    TEST_ASSERT_TRUE(failsafeIsActive());
    failsafeClear(FailsafeLayer::WATCHDOG_RESET);
    TEST_ASSERT_FALSE(failsafeIsActive());
}

// --- Test 3: failsafeClear(ESTOP) is a no-op; state remains active ---

void test_clear_estop_is_noop() {
    failsafeTrigger(FailsafeLayer::ESTOP);
    TEST_ASSERT_TRUE(failsafeIsActive());
    failsafeClear(FailsafeLayer::ESTOP);  // Should be no-op
    TEST_ASSERT_TRUE(failsafeIsActive());
}

// --- Test 4: failsafeClearEstop() clears both ESTOP and WATCHDOG_RESET ---

void test_clear_estop_clears_estop_layer() {
    failsafeTrigger(FailsafeLayer::ESTOP);
    TEST_ASSERT_TRUE(failsafeIsActive());
    failsafeClearEstop();
    TEST_ASSERT_FALSE(failsafeIsActive());
}

void test_clear_estop_clears_watchdog_reset_layer() {
    failsafeTrigger(FailsafeLayer::WATCHDOG_RESET);
    TEST_ASSERT_TRUE(failsafeIsActive());
    failsafeClearEstop();
    TEST_ASSERT_FALSE(failsafeIsActive());
}

void test_clear_estop_clears_both_estop_and_watchdog_when_both_active() {
    failsafeTrigger(FailsafeLayer::ESTOP);
    failsafeTrigger(FailsafeLayer::WATCHDOG_RESET);
    TEST_ASSERT_TRUE(failsafeIsActive());
    failsafeClearEstop();
    TEST_ASSERT_FALSE(failsafeIsActive());
}

// --- Test 5: Multiple layers active — clearing one still leaves failsafeIsActive() true ---

void test_clear_one_of_multiple_layers_still_active() {
    failsafeTrigger(FailsafeLayer::SBUS_HW);
    failsafeTrigger(FailsafeLayer::SBUS_WATCHDOG);
    TEST_ASSERT_TRUE(failsafeIsActive());

    failsafeClear(FailsafeLayer::SBUS_HW);
    TEST_ASSERT_TRUE(failsafeIsActive());  // Still active because SBUS_WATCHDOG is set

    failsafeClear(FailsafeLayer::SBUS_WATCHDOG);
    TEST_ASSERT_FALSE(failsafeIsActive());  // Now inactive
}

void test_multiple_layers_with_estop_requires_clear_estop() {
    failsafeTrigger(FailsafeLayer::SBUS_HW);
    failsafeTrigger(FailsafeLayer::ESTOP);
    TEST_ASSERT_TRUE(failsafeIsActive());

    failsafeClear(FailsafeLayer::SBUS_HW);
    TEST_ASSERT_TRUE(failsafeIsActive());  // Still active due to ESTOP

    failsafeClear(FailsafeLayer::ESTOP);  // No-op for ESTOP
    TEST_ASSERT_TRUE(failsafeIsActive());  // Still active

    failsafeClearEstop();  // Explicit clear needed
    TEST_ASSERT_FALSE(failsafeIsActive());
}

// --- Test 6: Priority ordering — failsafeActiveReason() returns lowest-index active layer ---

void test_active_reason_returns_sbus_hw_when_all_inactive() {
    // Default when no layer active (shouldn't happen in practice)
    FailsafeLayer reason = failsafeActiveReason();
    TEST_ASSERT_EQUAL_INT((int)reason, (int)FailsafeLayer::SBUS_HW);
}

void test_active_reason_returns_sbus_hw_when_triggered() {
    failsafeTrigger(FailsafeLayer::SBUS_HW);
    FailsafeLayer reason = failsafeActiveReason();
    TEST_ASSERT_EQUAL_INT((int)reason, (int)FailsafeLayer::SBUS_HW);
}

void test_active_reason_returns_highest_priority_when_multiple() {
    // SBUS_HW = 0, SBUS_WATCHDOG = 1, WEB_TIMEOUT = 2, WATCHDOG_RESET = 3, ESTOP = 4
    failsafeTrigger(FailsafeLayer::WEB_TIMEOUT);      // priority 2
    failsafeTrigger(FailsafeLayer::WATCHDOG_RESET);       // priority 3
    failsafeTrigger(FailsafeLayer::SBUS_WATCHDOG);    // priority 1 (highest when all active)

    FailsafeLayer reason = failsafeActiveReason();
    TEST_ASSERT_EQUAL_INT((int)reason, (int)FailsafeLayer::SBUS_WATCHDOG);
}

void test_active_reason_skips_inactive_lower_priority() {
    failsafeTrigger(FailsafeLayer::WATCHDOG_RESET);       // priority 3
    failsafeTrigger(FailsafeLayer::ESTOP);            // priority 4

    FailsafeLayer reason = failsafeActiveReason();
    TEST_ASSERT_EQUAL_INT((int)reason, (int)FailsafeLayer::WATCHDOG_RESET);  // Returns 3, not 4
}

// --- Test 7: All five layers independently trigger and clear ---

void test_all_layers_trigger_independently() {
    for (int i = 0; i < 5; ++i) {
        // Each layer triggers independently
        failsafeTrigger((FailsafeLayer)i);
        TEST_ASSERT_TRUE(failsafeIsActive());

        // Non-ESTOP layers clear independently
        if (i != (int)FailsafeLayer::ESTOP) {
            failsafeClear((FailsafeLayer)i);
            TEST_ASSERT_FALSE(failsafeIsActive());
        } else {
            // ESTOP requires explicit clear
            failsafeClear(FailsafeLayer::ESTOP);  // No-op
            TEST_ASSERT_TRUE(failsafeIsActive());
            failsafeClearEstop();
            TEST_ASSERT_FALSE(failsafeIsActive());
        }
    }
}

// --- Test 8: Mirror fields update correctly ---

void test_sbus_hw_mirror_updates() {
    TEST_ASSERT_FALSE(robotState.sbusHwFailsafe);
    failsafeTrigger(FailsafeLayer::SBUS_HW);
    TEST_ASSERT_TRUE(robotState.sbusHwFailsafe);
    failsafeClear(FailsafeLayer::SBUS_HW);
    TEST_ASSERT_FALSE(robotState.sbusHwFailsafe);
}

void test_sbus_watchdog_mirror_updates() {
    TEST_ASSERT_FALSE(robotState.sbusSignalLost);
    failsafeTrigger(FailsafeLayer::SBUS_WATCHDOG);
    TEST_ASSERT_TRUE(robotState.sbusSignalLost);
    failsafeClear(FailsafeLayer::SBUS_WATCHDOG);
    TEST_ASSERT_FALSE(robotState.sbusSignalLost);
}

void test_web_timeout_mirror_updates() {
    TEST_ASSERT_FALSE(robotState.webDriveExpired);
    failsafeTrigger(FailsafeLayer::WEB_TIMEOUT);
    TEST_ASSERT_TRUE(robotState.webDriveExpired);
    failsafeClear(FailsafeLayer::WEB_TIMEOUT);
    TEST_ASSERT_FALSE(robotState.webDriveExpired);
}

void test_estop_mirror_updates() {
    TEST_ASSERT_FALSE(robotState.estop);
    failsafeTrigger(FailsafeLayer::ESTOP);
    TEST_ASSERT_TRUE(robotState.estop);
    failsafeClearEstop();
    TEST_ASSERT_FALSE(robotState.estop);
}

void test_watchdog_reset_mirror_updates() {
    TEST_ASSERT_FALSE(robotState.estop);
    failsafeTrigger(FailsafeLayer::WATCHDOG_RESET);
    TEST_ASSERT_TRUE(robotState.estop);  // WATCHDOG_RESET also sets estop
    failsafeClear(FailsafeLayer::WATCHDOG_RESET);
    TEST_ASSERT_FALSE(robotState.estop);
}

void test_estop_and_watchdog_both_set_estop_mirror() {
    TEST_ASSERT_FALSE(robotState.estop);
    failsafeTrigger(FailsafeLayer::WATCHDOG_RESET);
    TEST_ASSERT_TRUE(robotState.estop);
    failsafeTrigger(FailsafeLayer::ESTOP);
    TEST_ASSERT_TRUE(robotState.estop);

    // Clear WATCHDOG but ESTOP still active
    failsafeClear(FailsafeLayer::WATCHDOG_RESET);
    TEST_ASSERT_TRUE(robotState.estop);  // Still true due to ESTOP

    // Clear ESTOP
    failsafeClearEstop();
    TEST_ASSERT_FALSE(robotState.estop);
}

// --- Test 9: failsafeUpdateWebTimeout() edge-detect behavior ---

void test_update_web_timeout_true_triggers_layer() {
    TEST_ASSERT_FALSE(failsafeIsActive());
    failsafeUpdateWebTimeout(true);
    TEST_ASSERT_TRUE(failsafeIsActive());
    TEST_ASSERT_TRUE(robotState.webDriveExpired);
    TEST_ASSERT_EQUAL_UINT32(1, robotState.failsafeTriggerCount);
    TEST_ASSERT_EQUAL_INT((int)FS_WEB_TIMEOUT, (int)robotState.failsafeLastTriggerSource);
}

void test_update_web_timeout_repeated_true_does_not_retrigger() {
    failsafeUpdateWebTimeout(true);
    failsafeUpdateWebTimeout(true);
    failsafeUpdateWebTimeout(true);
    TEST_ASSERT_TRUE(failsafeIsActive());
    TEST_ASSERT_EQUAL_UINT32(1, robotState.failsafeTriggerCount);
}

void test_update_web_timeout_false_when_active_clears_layer() {
    failsafeUpdateWebTimeout(true);
    TEST_ASSERT_TRUE(failsafeIsActive());

    failsafeUpdateWebTimeout(false);
    TEST_ASSERT_FALSE(failsafeIsActive());
    TEST_ASSERT_FALSE(robotState.webDriveExpired);
}

void test_update_web_timeout_false_when_inactive_is_noop() {
    TEST_ASSERT_FALSE(failsafeIsActive());
    failsafeUpdateWebTimeout(false);
    TEST_ASSERT_FALSE(failsafeIsActive());
    TEST_ASSERT_EQUAL_UINT32(0, robotState.failsafeTriggerCount);
}

// --- Test 10: every failsafe edge reaches the browser ---
//
// A latching estop that nobody is told about is a droid that reads SAFE on
// every screen while it is held (#346). The event stream publishes on request
// only, and before this group the request was made by the web handlers alone -
// so an estop raised from the radio, an SBUS timeout, or a watchdog-reset latch
// changed robotState and told no client at all.
//
// The request is posted from the one place that owns failsafe state, so every
// layer is covered by construction rather than by remembering to add a call at
// each trigger site. It posts a flag and does no work inline, because
// failsafeTrigger() is reached from the 50 Hz real-time path.

void test_trigger_estop_requests_a_status_broadcast() {
    g_test_status_broadcast_count = 0;
    failsafeTrigger(FailsafeLayer::ESTOP);
    TEST_ASSERT_EQUAL_UINT(1, g_test_status_broadcast_count);
}

void test_trigger_sbus_watchdog_requests_a_status_broadcast() {
    // Not only the estop: an SBUS timeout holds the feet just as hard, and the
    // plate reads it through a mirror field of its own.
    g_test_status_broadcast_count = 0;
    failsafeTrigger(FailsafeLayer::SBUS_WATCHDOG);
    TEST_ASSERT_EQUAL_UINT(1, g_test_status_broadcast_count);
}

void test_repeated_trigger_requests_nothing_further() {
    // The rising edge is the event. dispatchStandardSbusInputs() re-triggers a
    // held layer every frame, and a request per frame would rebuild the whole
    // status payload 50 times a second on Core 0 for a state that has not moved.
    failsafeTrigger(FailsafeLayer::SBUS_HW);
    g_test_status_broadcast_count = 0;
    failsafeTrigger(FailsafeLayer::SBUS_HW);
    failsafeTrigger(FailsafeLayer::SBUS_HW);
    TEST_ASSERT_EQUAL_UINT(0, g_test_status_broadcast_count);
}

void test_clearing_a_layer_requests_a_status_broadcast() {
    // The falling edge matters as much as the rising one: a plate that shows
    // STOPPED after the droid is free again is the same lie pointing the other
    // way, and it is the one that makes an operator distrust the readout.
    failsafeTrigger(FailsafeLayer::WEB_TIMEOUT);
    g_test_status_broadcast_count = 0;
    failsafeClear(FailsafeLayer::WEB_TIMEOUT);
    TEST_ASSERT_EQUAL_UINT(1, g_test_status_broadcast_count);
}

void test_clearing_an_inactive_layer_requests_nothing() {
    // failsafeUpdateWebTimeout(false) runs on every arbiter tick.
    g_test_status_broadcast_count = 0;
    failsafeClear(FailsafeLayer::WEB_TIMEOUT);
    TEST_ASSERT_EQUAL_UINT(0, g_test_status_broadcast_count);
}

void test_clear_estop_requests_a_status_broadcast() {
    failsafeTrigger(FailsafeLayer::ESTOP);
    g_test_status_broadcast_count = 0;
    failsafeClearEstop();
    TEST_ASSERT_EQUAL_UINT(1, g_test_status_broadcast_count);
}

void test_clear_estop_with_nothing_latched_requests_nothing() {
    g_test_status_broadcast_count = 0;
    failsafeClearEstop();
    TEST_ASSERT_EQUAL_UINT(0, g_test_status_broadcast_count);
}

void test_init_requests_nothing() {
    // Boot is not an edge, and there is no client to tell.
    g_test_status_broadcast_count = 0;
    failsafeInit(&robotStateMux);
    TEST_ASSERT_EQUAL_UINT(0, g_test_status_broadcast_count);
}

int main() {
    UNITY_BEGIN();

    // Test 1: Trigger makes failsafe active
    RUN_TEST(test_trigger_sbus_hw_makes_failsafe_active);
    RUN_TEST(test_trigger_sbus_watchdog_makes_failsafe_active);
    RUN_TEST(test_trigger_web_timeout_makes_failsafe_active);
    RUN_TEST(test_trigger_watchdog_reset_makes_failsafe_active);
    RUN_TEST(test_trigger_estop_makes_failsafe_active);

    // Test 2: Clear non-latching layer deactivates failsafe
    RUN_TEST(test_clear_sbus_hw_deactivates_failsafe);
    RUN_TEST(test_clear_sbus_watchdog_deactivates_failsafe);
    RUN_TEST(test_clear_web_timeout_deactivates_failsafe);
    RUN_TEST(test_clear_watchdog_reset_deactivates_failsafe);

    // Test 3: failsafeClear(ESTOP) is a no-op
    RUN_TEST(test_clear_estop_is_noop);

    // Test 4: failsafeClearEstop() clears both ESTOP and WATCHDOG_RESET
    RUN_TEST(test_clear_estop_clears_estop_layer);
    RUN_TEST(test_clear_estop_clears_watchdog_reset_layer);
    RUN_TEST(test_clear_estop_clears_both_estop_and_watchdog_when_both_active);

    // Test 5: Multiple layers
    RUN_TEST(test_clear_one_of_multiple_layers_still_active);
    RUN_TEST(test_multiple_layers_with_estop_requires_clear_estop);

    // Test 6: Priority ordering
    RUN_TEST(test_active_reason_returns_sbus_hw_when_all_inactive);
    RUN_TEST(test_active_reason_returns_sbus_hw_when_triggered);
    RUN_TEST(test_active_reason_returns_highest_priority_when_multiple);
    RUN_TEST(test_active_reason_skips_inactive_lower_priority);

    // Test 7: All layers
    RUN_TEST(test_all_layers_trigger_independently);

    // Test 8: Mirror fields
    RUN_TEST(test_sbus_hw_mirror_updates);
    RUN_TEST(test_sbus_watchdog_mirror_updates);
    RUN_TEST(test_web_timeout_mirror_updates);
    RUN_TEST(test_estop_mirror_updates);
    RUN_TEST(test_watchdog_reset_mirror_updates);
    RUN_TEST(test_estop_and_watchdog_both_set_estop_mirror);

    // Test 9: failsafeUpdateWebTimeout edge-detect
    RUN_TEST(test_update_web_timeout_true_triggers_layer);
    RUN_TEST(test_update_web_timeout_repeated_true_does_not_retrigger);
    RUN_TEST(test_update_web_timeout_false_when_active_clears_layer);
    RUN_TEST(test_update_web_timeout_false_when_inactive_is_noop);

    // Test 10: every failsafe edge reaches the browser
    RUN_TEST(test_trigger_estop_requests_a_status_broadcast);
    RUN_TEST(test_trigger_sbus_watchdog_requests_a_status_broadcast);
    RUN_TEST(test_repeated_trigger_requests_nothing_further);
    RUN_TEST(test_clearing_a_layer_requests_a_status_broadcast);
    RUN_TEST(test_clearing_an_inactive_layer_requests_nothing);
    RUN_TEST(test_clear_estop_requests_a_status_broadcast);
    RUN_TEST(test_clear_estop_with_nothing_latched_requests_nothing);
    RUN_TEST(test_init_requests_nothing);

    return UNITY_END();
}
