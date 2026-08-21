// =============================================================================
// test/test_native/test_failsafe_boot_sbus/test_failsafe_boot_sbus.cpp
//
// SBUS-safe boot default regression test.
// Verifies that boot-time SBUS failsafe arming decision works correctly.
// SAFETY: From zeroed state, boot arming path leaves failsafe active before
// any RC frame is processed.
// =============================================================================

#include <stdint.h>

#include <unity.h>

#include "failsafe_boot_sbus.h"

void setUp() {}
void tearDown() {}

void test_sbus1_watchdog_disabled_does_not_arm() {
    bool decision = bootSbusSafeGuardDecision(false);
    TEST_ASSERT_FALSE(decision);
}

void test_sbus1_watchdog_enabled_arms_failsafe() {
    bool decision = bootSbusSafeGuardDecision(true);
    TEST_ASSERT_TRUE(decision);
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_sbus1_watchdog_disabled_does_not_arm);
    RUN_TEST(test_sbus1_watchdog_enabled_arms_failsafe);
    return UNITY_END();
}
