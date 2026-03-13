// =============================================================================
// include/config.h
//
// GPIO pin assignments and compile-time constants for protoArtoo.
// All pins confirmed by PCB continuity trace on 2026-03-12 (PCB v1.2).
// See docs/pin_map.md for full trace results and revision notes.
//
// PCB serial port legend (from PCB silkscreen):
//   S0 = ESP debug  (UART0, GPIO 1/3)
//   S1 = Hoverboard (UART1, GPIO 16 TX / 17 RX)
//   S2 = Sound      (GPIO 26 TX / 35 RX)
//   S3 = Dome       (UART2, GPIO 33 TX / 34 RX)
// =============================================================================
#pragma once

// -----------------------------------------------------------------------------
// UART1 (Serial1) — Hoverboard motor controller (Gen2.x protocol, PCB S1)
// -----------------------------------------------------------------------------
#define PIN_HOVERBOARD_TX 16
#define PIN_HOVERBOARD_RX 17

// -----------------------------------------------------------------------------
// UART2 (Serial2) — Dome serial link (AstroPixelsPlus via slip ring, PCB S3)
// Phase 4 only — not used in Phase 1.
// -----------------------------------------------------------------------------
#define PIN_DOME_TX 33
#define PIN_DOME_RX 34

// -----------------------------------------------------------------------------
// Audio module serial (DY-SV5W, PCB S2)
// TX primary; RX available for status/ACK (optional, Phase 4).
// -----------------------------------------------------------------------------
#define PIN_AUDIO_TX 26
#define PIN_AUDIO_RX 35  // input-only GPIO — RX only, cannot be TX

// -----------------------------------------------------------------------------
// SBUS receivers
// SBUS #1 (drive): GPIO 15 — decoded by the custom RMT-based SBUS driver
// SBUS #2 (dome):  GPIO 13 — second RMT SBUS input, dome behavior deferred to Phase 3
// -----------------------------------------------------------------------------
#define PIN_SBUS1_RX 15  // Drive receiver (CH1=speed, CH2=steer, CH8=limit)
#define PIN_SBUS2_RX 13  // Dome-spin receiver — Phase 3 (UART conflict in Phase 1)

// -----------------------------------------------------------------------------
// Servo outputs (LEDC PWM)
// -----------------------------------------------------------------------------
#define PIN_ARM1_SERVO 23
#define PIN_ARM2_SERVO 5
#define PIN_ARM3_SERVO 19  // Spare servo output
#define PIN_ARM4_SERVO 18  // Spare servo output
#define PIN_ARM5_SERVO 32  // Spare servo output
#define PIN_DOME_ESC 25

// -----------------------------------------------------------------------------
// I2C
// -----------------------------------------------------------------------------
#define PIN_I2C_SCL 22
#define PIN_I2C_SDA 21

// -----------------------------------------------------------------------------
// Drive constants
// -----------------------------------------------------------------------------
#define SPEED_LIMIT_MAX 600  // Absolute max drive output (never exceeded)
#define HOVERBOARD_BAUD 115200
#define DRIVE_FREQ_HZ 50  // Frame rate for hoverboard UART

// -----------------------------------------------------------------------------
// SBUS constants
// -----------------------------------------------------------------------------
#define SBUS_MIN 172         // HOTRC SBUS-A raw minimum
#define SBUS_MAX 1811        // HOTRC SBUS-A raw maximum
#define SBUS_TIMEOUT_MS 200  // Watchdog timeout for drive receiver

// -----------------------------------------------------------------------------
// Web API constants
// -----------------------------------------------------------------------------
#define WEB_DRIVE_TIMEOUT_MS 500  // Web drive command expiry

// -----------------------------------------------------------------------------
// Watchdog
// -----------------------------------------------------------------------------
#define WATCHDOG_TIMEOUT_S 3  // ESP32 TWDT timeout

// -----------------------------------------------------------------------------
// NVS
// -----------------------------------------------------------------------------
#define NVS_NAMESPACE "proto"

// -----------------------------------------------------------------------------
// WiFi AP
// -----------------------------------------------------------------------------
#define WIFI_AP_SSID "protoArtoo"
#define WIFI_AP_IP "192.168.4.1"
#define PA_FIRMWARE_VERSION "v0.1.0-phase2-dev"

// -----------------------------------------------------------------------------
#define PA_LOG_LEVEL_ERROR 1
#define PA_LOG_LEVEL_INFO 2
#define PA_LOG_LEVEL_DEBUG 3

#ifndef PA_LOG_LEVEL
// PA_LOG_LEVEL controls USB debug serial verbosity on UART0.
// - PA_LOG_LEVEL_ERROR: faults only (watchdog resets, mount failures, etc.)
// - PA_LOG_LEVEL_INFO: normal boot health and service bring-up
// - PA_LOG_LEVEL_DEBUG: verbose development logging, including lower-priority events
#define PA_LOG_LEVEL PA_LOG_LEVEL_DEBUG
#endif
