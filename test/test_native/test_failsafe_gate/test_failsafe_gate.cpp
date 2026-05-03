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

void test_trigger_twdt_reset_makes_failsafe_active() {
    TEST_ASSERT_FALSE(failsafeIsActive());
    failsafeTrigger(FailsafeLayer::TWDT_RESET);
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

void test_clear_twdt_reset_deactivates_failsafe() {
    failsafeTrigger(FailsafeLayer::TWDT_RESET);
    TEST_ASSERT_TRUE(failsafeIsActive());
    failsafeClear(FailsafeLayer::TWDT_RESET);
    TEST_ASSERT_FALSE(failsafeIsActive());
}

// --- Test 3: failsafeClear(ESTOP) is a no-op; state remains active ---

void test_clear_estop_is_noop() {
    failsafeTrigger(FailsafeLayer::ESTOP);
    TEST_ASSERT_TRUE(failsafeIsActive());
    failsafeClear(FailsafeLayer::ESTOP);  // Should be no-op
    TEST_ASSERT_TRUE(failsafeIsActive());
}

// --- Test 4: failsafeClearEstop() clears both ESTOP and TWDT_RESET ---

void test_clear_estop_clears_estop_layer() {
    failsafeTrigger(FailsafeLayer::ESTOP);
    TEST_ASSERT_TRUE(failsafeIsActive());
    failsafeClearEstop();
    TEST_ASSERT_FALSE(failsafeIsActive());
}

void test_clear_estop_clears_twdt_reset_layer() {
    failsafeTrigger(FailsafeLayer::TWDT_RESET);
    TEST_ASSERT_TRUE(failsafeIsActive());
    failsafeClearEstop();
    TEST_ASSERT_FALSE(failsafeIsActive());
}

void test_clear_estop_clears_both_estop_and_twdt_when_both_active() {
    failsafeTrigger(FailsafeLayer::ESTOP);
    failsafeTrigger(FailsafeLayer::TWDT_RESET);
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
    // SBUS_HW = 0, SBUS_WATCHDOG = 1, WEB_TIMEOUT = 2, TWDT_RESET = 3, ESTOP = 4
    failsafeTrigger(FailsafeLayer::WEB_TIMEOUT);      // priority 2
    failsafeTrigger(FailsafeLayer::TWDT_RESET);       // priority 3
    failsafeTrigger(FailsafeLayer::SBUS_WATCHDOG);    // priority 1 (highest when all active)

    FailsafeLayer reason = failsafeActiveReason();
    TEST_ASSERT_EQUAL_INT((int)reason, (int)FailsafeLayer::SBUS_WATCHDOG);
}

void test_active_reason_skips_inactive_lower_priority() {
    failsafeTrigger(FailsafeLayer::TWDT_RESET);       // priority 3
    failsafeTrigger(FailsafeLayer::ESTOP);            // priority 4

    FailsafeLayer reason = failsafeActiveReason();
    TEST_ASSERT_EQUAL_INT((int)reason, (int)FailsafeLayer::TWDT_RESET);  // Returns 3, not 4
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

void test_twdt_reset_mirror_updates() {
    TEST_ASSERT_FALSE(robotState.estop);
    failsafeTrigger(FailsafeLayer::TWDT_RESET);
    TEST_ASSERT_TRUE(robotState.estop);  // TWDT_RESET also sets estop
    failsafeClear(FailsafeLayer::TWDT_RESET);
    TEST_ASSERT_FALSE(robotState.estop);
}

void test_estop_and_twdt_both_set_estop_mirror() {
    TEST_ASSERT_FALSE(robotState.estop);
    failsafeTrigger(FailsafeLayer::TWDT_RESET);
    TEST_ASSERT_TRUE(robotState.estop);
    failsafeTrigger(FailsafeLayer::ESTOP);
    TEST_ASSERT_TRUE(robotState.estop);

    // Clear TWDT but ESTOP still active
    failsafeClear(FailsafeLayer::TWDT_RESET);
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

int main() {
    UNITY_BEGIN();

    // Test 1: Trigger makes failsafe active
    RUN_TEST(test_trigger_sbus_hw_makes_failsafe_active);
    RUN_TEST(test_trigger_sbus_watchdog_makes_failsafe_active);
    RUN_TEST(test_trigger_web_timeout_makes_failsafe_active);
    RUN_TEST(test_trigger_twdt_reset_makes_failsafe_active);
    RUN_TEST(test_trigger_estop_makes_failsafe_active);

    // Test 2: Clear non-latching layer deactivates failsafe
    RUN_TEST(test_clear_sbus_hw_deactivates_failsafe);
    RUN_TEST(test_clear_sbus_watchdog_deactivates_failsafe);
    RUN_TEST(test_clear_web_timeout_deactivates_failsafe);
    RUN_TEST(test_clear_twdt_reset_deactivates_failsafe);

    // Test 3: failsafeClear(ESTOP) is a no-op
    RUN_TEST(test_clear_estop_is_noop);

    // Test 4: failsafeClearEstop() clears both ESTOP and TWDT_RESET
    RUN_TEST(test_clear_estop_clears_estop_layer);
    RUN_TEST(test_clear_estop_clears_twdt_reset_layer);
    RUN_TEST(test_clear_estop_clears_both_estop_and_twdt_when_both_active);

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
    RUN_TEST(test_twdt_reset_mirror_updates);
    RUN_TEST(test_estop_and_twdt_both_set_estop_mirror);

    // Test 9: failsafeUpdateWebTimeout edge-detect
    RUN_TEST(test_update_web_timeout_true_triggers_layer);
    RUN_TEST(test_update_web_timeout_repeated_true_does_not_retrigger);
    RUN_TEST(test_update_web_timeout_false_when_active_clears_layer);
    RUN_TEST(test_update_web_timeout_false_when_inactive_is_noop);

    return UNITY_END();
}
