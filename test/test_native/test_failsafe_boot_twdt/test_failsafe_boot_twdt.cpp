// =============================================================================
// test/test_native/test_failsafe_boot_twdt/test_failsafe_boot_twdt.cpp
//
// Watchdog reset -> estop on boot regression test.
// Verifies that boot-time watchdog reset detection and estop arming works correctly.
// SAFETY: Reset-reason -> failsafeTrigger(WATCHDOG_RESET) wiring must activate
// estop if previous boot crashed due to any watchdog timeout.
// =============================================================================

#include <stdint.h>

#include <unity.h>

#include "failsafe_boot_twdt.h"

void setUp() {}
void tearDown() {}

// Normal power-on: no watchdog estop
void test_power_on_no_watchdog_estop() {
    bool decision = bootWatchdogResetDecision(ESP_RST_POWERON);
    TEST_ASSERT_FALSE(decision);
}

// External reset: no watchdog estop
void test_external_reset_no_watchdog_estop() {
    bool decision = bootWatchdogResetDecision(ESP_RST_EXT);
    TEST_ASSERT_FALSE(decision);
}

// Software reset: no watchdog estop
void test_software_reset_no_watchdog_estop() {
    bool decision = bootWatchdogResetDecision(ESP_RST_SW);
    TEST_ASSERT_FALSE(decision);
}

// Panic reset: no watchdog estop
void test_panic_reset_no_watchdog_estop() {
    bool decision = bootWatchdogResetDecision(ESP_RST_PANIC);
    TEST_ASSERT_FALSE(decision);
}

// Interrupt watchdog: ARM WATCHDOG ESTOP (any watchdog arms estop per ADR 0031)
void test_int_wdt_reset_arm_watchdog_estop() {
    bool decision = bootWatchdogResetDecision(ESP_RST_INT_WDT);
    TEST_ASSERT_TRUE(decision);
}

// Task watchdog reset: ARM WATCHDOG ESTOP
void test_task_wdt_reset_arm_watchdog_estop() {
    bool decision = bootWatchdogResetDecision(ESP_RST_TASK_WDT);
    TEST_ASSERT_TRUE(decision);
}

// Generic watchdog reset (ESP32-P4 reports this for all WDT types): ARM WATCHDOG ESTOP
void test_generic_wdt_reset_arm_watchdog_estop() {
    bool decision = bootWatchdogResetDecision(ESP_RST_WDT);
    TEST_ASSERT_TRUE(decision);
}

// Brownout reset: no watchdog estop
void test_brownout_reset_no_watchdog_estop() {
    bool decision = bootWatchdogResetDecision(ESP_RST_BROWNOUT);
    TEST_ASSERT_FALSE(decision);
}

// Unknown reset: no watchdog estop (default safe)
void test_unknown_reset_no_watchdog_estop() {
    bool decision = bootWatchdogResetDecision(ESP_RST_UNKNOWN);
    TEST_ASSERT_FALSE(decision);
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_power_on_no_watchdog_estop);
    RUN_TEST(test_external_reset_no_watchdog_estop);
    RUN_TEST(test_software_reset_no_watchdog_estop);
    RUN_TEST(test_panic_reset_no_watchdog_estop);
    RUN_TEST(test_int_wdt_reset_arm_watchdog_estop);
    RUN_TEST(test_task_wdt_reset_arm_watchdog_estop);
    RUN_TEST(test_generic_wdt_reset_arm_watchdog_estop);
    RUN_TEST(test_brownout_reset_no_watchdog_estop);
    RUN_TEST(test_unknown_reset_no_watchdog_estop);
    return UNITY_END();
}
