// =============================================================================
// include/failsafe_boot_twdt.h
//
// Pure decision logic for boot-time TWDT reset detection and estop arming.
// Extracted from main.cpp for native testability (ADR 0005).
// =============================================================================
#pragma once

#include <cstdint>

#ifdef ARDUINO
#include <esp_system.h>
#else
// Native test stub: enum for reset reason
typedef enum {
    ESP_RST_UNKNOWN = 0,
    ESP_RST_POWERON = 1,
    ESP_RST_EXT = 2,
    ESP_RST_SW = 3,
    ESP_RST_PANIC = 4,
    ESP_RST_INT_WDT = 5,
    ESP_RST_TASK_WDT = 6,
    ESP_RST_WDT = 7,
    ESP_RST_DEEPSLEEP = 8,
    ESP_RST_BROWNOUT = 9,
    ESP_RST_SDIO = 10,
    ESP_RST_USB_JTAG_SERIAL = 11,
} esp_reset_reason_t;
#endif

// Boot TWDT reset detection decision.
// Returns true if TWDT_RESET failsafe should be armed at boot.
// SAFETY: If reset reason is ESP_RST_TASK_WDT, previous boot watchdog reset
// left the robot in potentially dangerous state; boot with estop active.
bool bootTwdtResetDecision(esp_reset_reason_t resetReason);
