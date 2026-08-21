// =============================================================================
// src/failsafe_boot_twdt.cpp
//
// Pure decision logic for boot-time TWDT reset detection and estop arming.
// Extracted from main.cpp for native testability (ADR 0005).
// =============================================================================

#include "failsafe_boot_twdt.h"

bool bootTwdtResetDecision(esp_reset_reason_t resetReason) {
    // Arm TWDT_RESET if the previous boot ended with a watchdog timeout.
    // This sets estop so the robot does not move until the operator explicitly
    // clears via POST /api/estop/clear, ensuring recovery from a crash state.
    return resetReason == ESP_RST_TASK_WDT;
}
