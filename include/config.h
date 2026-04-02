// =============================================================================
// include/config.h
//
// GPIO pin assignments and compile-time constants for protoArtoo.
// All pins confirmed by PCB continuity trace on 2026-03-12 (PCB v1.2).
// See docs/pin_map.md for full trace results and revision notes.
//
// PCB serial port legend (from PCB silkscreen):
//   S0 = ESP debug           (UART0, GPIO 1/3)
//   S1 = Hoverboard          (UART1, GPIO 16 TX / 17 RX)
//   S2 = Sound               (GPIO 26 TX / 35 RX)
//   S3 = Dome Control        (UART2, GPIO 33 / 34 RX)
// =============================================================================
#pragma once

#include <stdint.h>

// -----------------------------------------------------------------------------
// UART1 (Serial1) — Hoverboard motor controller (Gen2.x protocol, PCB S1)
// -----------------------------------------------------------------------------
constexpr uint8_t PIN_HOVERBOARD_TX = 16;
constexpr uint8_t PIN_HOVERBOARD_RX = 17;

// -----------------------------------------------------------------------------
// UART2 (Serial2) — Dome serial link (AstroPixelsPlus via slip ring, PCB S3)
// Phase 4 only — not used in Phase 1.
// -----------------------------------------------------------------------------
constexpr uint8_t PIN_DOME_TX = 33;
constexpr uint8_t PIN_DOME_RX = 34;

// -----------------------------------------------------------------------------
// Audio module serial (DY-SV5W, PCB S2)
// TX primary; RX available for status/ACK (optional, Phase 4).
// -----------------------------------------------------------------------------
constexpr uint8_t PIN_AUDIO_TX = 26;
constexpr uint8_t PIN_AUDIO_RX = 35;  // input-only GPIO — RX only, cannot be TX

// -----------------------------------------------------------------------------
// RC receiver inputs
// RC CH1 = GPIO 15, RC CH2 = GPIO 13, RC CH3 = GPIO 2,
// RC CH4 = GPIO 4, RC CH5 = GPIO 12, RC CH6 = GPIO 27.
// CH1/CH2 also serve as SBUS inputs when rc_input_mode selects SBUS.
// -----------------------------------------------------------------------------
constexpr uint8_t PIN_RC_CH1 = 15;
constexpr uint8_t PIN_RC_CH2 = 13;
constexpr uint8_t PIN_RC_CH3 = 2;
constexpr uint8_t PIN_RC_CH4 = 4;
constexpr uint8_t PIN_RC_CH5 = 12;
constexpr uint8_t PIN_RC_CH6 = 27;

constexpr uint8_t PIN_SBUS1_RX = PIN_RC_CH1;  // CH1 — SBUS #1 (drive)
constexpr uint8_t PIN_SBUS2_RX = PIN_RC_CH2;  // CH2 — SBUS #2 (dome)

// -----------------------------------------------------------------------------
// Servo outputs (LEDC PWM)
// ARM1 = Utility arm servo #1 — Top / Left arm (GPIO 23)
// ARM2 = Utility arm servo #2 — Bottom / Right arm (GPIO 5)
// AUX1 = Spare servo output (GPIO 19, also labelled ARM3)
// AUX2 = Spare servo output (GPIO 18, also labelled ARM4)
// AUX3 = Spare servo output (GPIO 32, also labelled ARM5)
// DOME = Dome rotation ESC (GPIO 25) — drives brushless motor, not a servo
// -----------------------------------------------------------------------------
constexpr uint8_t PIN_ARM1_SERVO = 23;
constexpr uint8_t PIN_ARM2_SERVO = 5;
constexpr uint8_t PIN_ARM3_SERVO = 19;  // AUX1 — spare servo output
constexpr uint8_t PIN_ARM4_SERVO = 18;  // AUX2 — spare servo output
constexpr uint8_t PIN_ARM5_SERVO = 32;  // AUX3 — spare servo output
constexpr uint8_t PIN_DOME_ESC = 25;

// -----------------------------------------------------------------------------
// I2C
// -----------------------------------------------------------------------------
constexpr uint8_t PIN_I2C_SCL = 22;
constexpr uint8_t PIN_I2C_SDA = 21;

// -----------------------------------------------------------------------------
// Drive constants
// -----------------------------------------------------------------------------
constexpr int16_t SPEED_LIMIT_MAX = 600;  // Absolute max drive output (never exceeded)
constexpr uint32_t HOVERBOARD_BAUD = 115200;
constexpr uint32_t DRIVE_FREQ_HZ = 50;  // Frame rate for hoverboard UART

// -----------------------------------------------------------------------------
// SBUS constants
// -----------------------------------------------------------------------------
constexpr uint16_t SBUS_MIN = 172;         // HOTRC SBUS-A raw minimum
constexpr uint16_t SBUS_MAX = 1811;        // HOTRC SBUS-A raw maximum
constexpr uint32_t SBUS_TIMEOUT_MS = 200;  // Watchdog timeout for drive receiver

// -----------------------------------------------------------------------------
// Web API constants
// -----------------------------------------------------------------------------
constexpr uint32_t WEB_DRIVE_TIMEOUT_MS = 500;  // Web drive command expiry

// -----------------------------------------------------------------------------
// Watchdog
// -----------------------------------------------------------------------------
constexpr uint32_t WATCHDOG_TIMEOUT_S = 3;  // ESP32 TWDT timeout

// -----------------------------------------------------------------------------
// NVS
// -----------------------------------------------------------------------------
constexpr char NVS_NAMESPACE[] = "proto";

// -----------------------------------------------------------------------------
// WiFi AP
// -----------------------------------------------------------------------------
constexpr char WIFI_AP_SSID[] = "protoArtoo";
constexpr char WIFI_AP_IP[] = "192.168.4.1";
#ifndef PA_FIRMWARE_VERSION
constexpr char PA_FIRMWARE_VERSION[] = "v0.0.0-dev";
#endif

// -----------------------------------------------------------------------------
// Log levels
// -----------------------------------------------------------------------------
constexpr uint8_t PA_LOG_LEVEL_ERROR = 1;
constexpr uint8_t PA_LOG_LEVEL_INFO = 2;
constexpr uint8_t PA_LOG_LEVEL_DEBUG = 3;

// PA_LOG_LEVEL controls USB debug serial verbosity on UART0.
// - PA_LOG_LEVEL_ERROR (1): faults only (watchdog resets, mount failures, etc.)
// - PA_LOG_LEVEL_INFO  (2): normal boot health and service bring-up
// - PA_LOG_LEVEL_DEBUG (3): verbose development logging, including lower-priority events
// Set via -DPA_LOG_LEVEL=N in platformio.ini build_flags. Defaults to DEBUG if unset.
#ifndef PA_LOG_LEVEL
#define PA_LOG_LEVEL 3
#endif
