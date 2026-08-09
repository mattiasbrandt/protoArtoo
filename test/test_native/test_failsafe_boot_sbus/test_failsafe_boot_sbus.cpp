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

// SBUS disabled, no channels: no arm
void test_sbus_disabled_no_channel_no_arm() {
    bool decision = bootSbusSafeGuardDecision(false, false);
    TEST_ASSERT_FALSE(decision);
}

// SBUS disabled, channel enabled: no arm (SBUS must be enabled)
void test_sbus_disabled_channel_enabled_no_arm() {
    bool decision = bootSbusSafeGuardDecision(false, true);
    TEST_ASSERT_FALSE(decision);
}

// SBUS enabled, no channels: no arm (no channels configured)
void test_sbus_enabled_no_channel_no_arm() {
    bool decision = bootSbusSafeGuardDecision(true, false);
    TEST_ASSERT_FALSE(decision);
}

// SBUS enabled, channel enabled: ARM failsafe
void test_sbus_enabled_channel_enabled_arm_failsafe() {
    bool decision = bootSbusSafeGuardDecision(true, true);
    TEST_ASSERT_TRUE(decision);
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_sbus_disabled_no_channel_no_arm);
    RUN_TEST(test_sbus_disabled_channel_enabled_no_arm);
    RUN_TEST(test_sbus_enabled_no_channel_no_arm);
    RUN_TEST(test_sbus_enabled_channel_enabled_arm_failsafe);
    return UNITY_END();
}
