// =============================================================================
// test/test_native/test_wifi_recovery_gesture/test_wifi_recovery_gesture.cpp
//
// Native unit tests for the pure Network Recovery Mode gesture (ADR 0015).
// See include/wifi_recovery_gesture.h.
// =============================================================================
#include <unity.h>

#include "wifi_recovery_gesture.h"

void setUp(void) {}
void tearDown(void) {}

// A non-power-on reset (watchdog, panic, brownout, software restart) never
// counts toward the gesture, regardless of any prior in-progress count.
void test_non_poweron_reset_resets_count(void) {
    WifiRecoveryGestureInput input;
    input.wasPowerOnReset = false;
    input.priorCycleCount = 2;

    WifiRecoveryGestureResult result = wifiEvaluateRecoveryGesture(input);

    TEST_ASSERT_FALSE(result.recoveryRequested);
    TEST_ASSERT_EQUAL_UINT8(0, result.nextCycleCount);
}

// A fresh power-on reset with no prior count starts counting but does not
// yet trigger recovery.
void test_first_poweron_cycle_does_not_trigger(void) {
    WifiRecoveryGestureInput input;
    input.wasPowerOnReset = true;
    input.priorCycleCount = 0;

    WifiRecoveryGestureResult result = wifiEvaluateRecoveryGesture(input);

    TEST_ASSERT_FALSE(result.recoveryRequested);
    TEST_ASSERT_EQUAL_UINT8(1, result.nextCycleCount);
}

// A second consecutive power-on cycle still does not trigger recovery.
void test_second_poweron_cycle_does_not_trigger(void) {
    WifiRecoveryGestureInput input;
    input.wasPowerOnReset = true;
    input.priorCycleCount = 1;

    WifiRecoveryGestureResult result = wifiEvaluateRecoveryGesture(input);

    TEST_ASSERT_FALSE(result.recoveryRequested);
    TEST_ASSERT_EQUAL_UINT8(2, result.nextCycleCount);
}

// The threshold-th consecutive power-on cycle latches Network Recovery Mode
// and resets the persisted counter so the gesture must be repeated to
// trigger again.
void test_threshold_poweron_cycle_triggers_recovery(void) {
    WifiRecoveryGestureInput input;
    input.wasPowerOnReset = true;
    input.priorCycleCount = WIFI_RECOVERY_GESTURE_THRESHOLD - 1;

    WifiRecoveryGestureResult result = wifiEvaluateRecoveryGesture(input);

    TEST_ASSERT_TRUE(result.recoveryRequested);
    TEST_ASSERT_EQUAL_UINT8(0, result.nextCycleCount);
}

// After the gesture fires, the very next power-on boot starts counting from
// zero again rather than immediately re-triggering.
void test_gesture_does_not_immediately_retrigger(void) {
    WifiRecoveryGestureInput input;
    input.wasPowerOnReset = true;
    input.priorCycleCount = 0;  // as persisted by the triggering boot

    WifiRecoveryGestureResult result = wifiEvaluateRecoveryGesture(input);

    TEST_ASSERT_FALSE(result.recoveryRequested);
    TEST_ASSERT_EQUAL_UINT8(1, result.nextCycleCount);
}

// Rule: ordinary WiFi Client Mode connection trouble, watchdog resets, and
// panic resets must never be mistaken for the recovery gesture. The input
// shape has no "STA disconnected" or reset-reason field beyond the
// power-on classification, so simulating a task watchdog reset (which the
// boot shell classifies as wasPowerOnReset=false) can never latch recovery,
// however many prior cycles were in progress.
void test_watchdog_reset_never_latches_recovery(void) {
    WifiRecoveryGestureInput input;
    input.wasPowerOnReset = false;
    input.priorCycleCount = WIFI_RECOVERY_GESTURE_THRESHOLD - 1;

    WifiRecoveryGestureResult result = wifiEvaluateRecoveryGesture(input);

    TEST_ASSERT_FALSE(result.recoveryRequested);
    TEST_ASSERT_EQUAL_UINT8(0, result.nextCycleCount);
}

// priorCycleCount is persisted NVS state and could in principle hold an
// out-of-range value (corrupted flash, a downgraded firmware that used a
// different threshold, etc). A maximal uint8_t value must still trigger
// recovery on the next power-on cycle rather than wrapping past it.
void test_out_of_range_prior_count_still_triggers_on_next_poweron(void) {
    WifiRecoveryGestureInput input;
    input.wasPowerOnReset = true;
    input.priorCycleCount = 255;

    WifiRecoveryGestureResult result = wifiEvaluateRecoveryGesture(input);

    TEST_ASSERT_TRUE(result.recoveryRequested);
    TEST_ASSERT_EQUAL_UINT8(0, result.nextCycleCount);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_non_poweron_reset_resets_count);
    RUN_TEST(test_first_poweron_cycle_does_not_trigger);
    RUN_TEST(test_second_poweron_cycle_does_not_trigger);
    RUN_TEST(test_threshold_poweron_cycle_triggers_recovery);
    RUN_TEST(test_gesture_does_not_immediately_retrigger);
    RUN_TEST(test_watchdog_reset_never_latches_recovery);
    RUN_TEST(test_out_of_range_prior_count_still_triggers_on_next_poweron);
    return UNITY_END();
}
