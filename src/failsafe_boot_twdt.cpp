// =============================================================================
// src/failsafe_boot_twdt.cpp
//
// Pure decision logic for boot-time watchdog reset detection and estop arming.
// Extracted from main.cpp for native testability (ADR 0005).
// =============================================================================

#include "failsafe_boot_twdt.h"

bool bootWatchdogResetDecision(esp_reset_reason_t resetReason) {
    // Arm WATCHDOG_RESET if the previous boot ended with any watchdog timeout.
    // This includes ESP_RST_TASK_WDT (task watchdog), ESP_RST_INT_WDT (interrupt
    // watchdog), and ESP_RST_WDT (generic watchdog). This sets estop so the robot
    // does not move until the operator explicitly clears via POST /api/estop/clear,
    // ensuring recovery from a crash state. See ADR 0031.
    return resetReason == ESP_RST_TASK_WDT ||
           resetReason == ESP_RST_INT_WDT ||
           resetReason == ESP_RST_WDT;
}
