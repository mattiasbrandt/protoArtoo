// =============================================================================
// test/test_native/test_failsafe_boot_twdt/test_failsafe_boot_twdt.cpp
//
// TWDT reset -> estop on boot regression test.
// Verifies that boot-time TWDT reset detection and estop arming works correctly.
// SAFETY: Reset-reason -> failsafeTrigger(TWDT_RESET) wiring must activate
// estop if previous boot crashed due to watchdog timeout.
// =============================================================================

#include <stdint.h>

#include <unity.h>

#include "failsafe_boot_twdt.h"

void setUp() {}
void tearDown() {}

// Normal power-on: no TWDT estop
void test_power_on_no_twdt_estop() {
    bool decision = bootTwdtResetDecision(ESP_RST_POWERON);
    TEST_ASSERT_FALSE(decision);
}

// External reset: no TWDT estop
void test_external_reset_no_twdt_estop() {
    bool decision = bootTwdtResetDecision(ESP_RST_EXT);
    TEST_ASSERT_FALSE(decision);
}

// Software reset: no TWDT estop
void test_software_reset_no_twdt_estop() {
    bool decision = bootTwdtResetDecision(ESP_RST_SW);
    TEST_ASSERT_FALSE(decision);
}

// Panic reset: no TWDT estop
void test_panic_reset_no_twdt_estop() {
    bool decision = bootTwdtResetDecision(ESP_RST_PANIC);
    TEST_ASSERT_FALSE(decision);
}

// Interrupt watchdog: no TWDT estop (different watchdog)
void test_int_wdt_reset_no_twdt_estop() {
    bool decision = bootTwdtResetDecision(ESP_RST_INT_WDT);
    TEST_ASSERT_FALSE(decision);
}

// Task watchdog reset: ARM TWDT ESTOP
void test_task_wdt_reset_arm_twdt_estop() {
    bool decision = bootTwdtResetDecision(ESP_RST_TASK_WDT);
    TEST_ASSERT_TRUE(decision);
}

// Brownout reset: no TWDT estop
void test_brownout_reset_no_twdt_estop() {
    bool decision = bootTwdtResetDecision(ESP_RST_BROWNOUT);
    TEST_ASSERT_FALSE(decision);
}

// Unknown reset: no TWDT estop (default safe)
void test_unknown_reset_no_twdt_estop() {
    bool decision = bootTwdtResetDecision(ESP_RST_UNKNOWN);
    TEST_ASSERT_FALSE(decision);
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_power_on_no_twdt_estop);
    RUN_TEST(test_external_reset_no_twdt_estop);
    RUN_TEST(test_software_reset_no_twdt_estop);
    RUN_TEST(test_panic_reset_no_twdt_estop);
    RUN_TEST(test_int_wdt_reset_no_twdt_estop);
    RUN_TEST(test_task_wdt_reset_arm_twdt_estop);
    RUN_TEST(test_brownout_reset_no_twdt_estop);
    RUN_TEST(test_unknown_reset_no_twdt_estop);
    return UNITY_END();
}
