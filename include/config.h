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
#ifndef PA_BOARD_S3_MINI
constexpr uint8_t PIN_HOVERBOARD_RX = 17;
#endif

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
#ifndef PA_BOARD_S3_MINI
constexpr uint8_t PIN_AUDIO_TX = 26;
#endif
constexpr uint8_t PIN_AUDIO_RX = 35;  // input-only GPIO — RX only, cannot be TX

// -----------------------------------------------------------------------------
// RC receiver inputs
// RC CH1 = GPIO 15, RC CH2 = GPIO 13, RC CH3 = GPIO 2,
// RC CH4 = GPIO 4, RC CH5 = GPIO 12, RC CH6 = GPIO 27.
// CH1/CH2 also serve as SBUS inputs when rc_input_mode selects SBUS.
// -----------------------------------------------------------------------------
constexpr uint8_t PIN_RC_CH1 = 15;
constexpr uint8_t PIN_RC_CH2 = 13;
#ifndef PA_BOARD_S3_MINI
constexpr uint8_t PIN_RC_CH3 = 2;
#endif
constexpr uint8_t PIN_RC_CH4 = 4;
constexpr uint8_t PIN_RC_CH5 = 12;
#ifndef PA_BOARD_S3_MINI
constexpr uint8_t PIN_RC_CH6 = 27;
#endif

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
#ifndef PA_BOARD_S3_MINI
constexpr uint8_t PIN_ARM1_SERVO = 23;
#endif
constexpr uint8_t PIN_ARM2_SERVO = 5;
constexpr uint8_t PIN_ARM3_SERVO = 19;  // AUX1 — spare servo output
#ifndef PA_BOARD_S3_MINI
constexpr uint8_t PIN_ARM4_SERVO = 18;  // AUX2 — spare servo output
constexpr uint8_t PIN_ARM5_SERVO = 32;  // AUX3 — spare servo output
constexpr uint8_t PIN_DOME_ESC = 25;
#endif

// -----------------------------------------------------------------------------
// I2C
// -----------------------------------------------------------------------------
#ifndef PA_BOARD_S3_MINI
constexpr uint8_t PIN_I2C_SCL = 22;
constexpr uint8_t PIN_I2C_SDA = 21;
#endif

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
#  define PA_LOG_LEVEL 3
#endif


// ---------------------------------------------------------------------------
// S3 Mini GPIO overrides (PA_BOARD_S3_MINI=1)
//
// Source: docs/pin_map.md (Artoo PCB confirmed traces) cross-referenced with
//         LOLIN S3 Mini v1.0.0 schematic double-row pinout.
//
// GPIO numbers that are direct matches (same GPIO at same socket position)
// are NOT listed here — they remain unchanged from the default config.h.
// Only positions where the physical socket carries a different GPIO on S3
// need an override.
//
// SBUS1 UART: Serial1 → Serial0 (UART0 free on S3). See rc_input.cpp.
// AUX1: GPIO19 = USB D− on S3. PA_ENABLE_AUX1=0 prevents any LEDC attach.
// ---------------------------------------------------------------------------
#ifdef PA_BOARD_S3_MINI

// --- Overrides: positions where physical socket carries a different GPIO on S3 ---

// I²C — D1 Mini outer SDA/SCL positions (D2=GPIO21, D1=GPIO22) carry
// GPIO35/GPIO36 on the S3 Mini.
constexpr uint8_t PIN_I2C_SDA       = 35;   // was GPIO21 — left outer pos 5
constexpr uint8_t PIN_I2C_SCL       = 36;   // was GPIO22 — left outer pos 6

// Hoverboard serial — TX (GPIO16) is a direct match at left inner pos 8.
// RX was GPIO17 at left inner pos 7; S3 Mini puts GPIO15 there instead.
// Note: GPIO15 is also PIN_SBUS1_RX — those are different socket positions.
constexpr uint8_t PIN_HOVERBOARD_RX = 15;   // was GPIO17 — left inner pos 7

// Audio TX — D0 (GPIO26) is at right outer pos 4 on the D1 Mini.
// S3 Mini right outer pos 4 carries GPIO7. Audio TX is soft-UART (bit-bang)
// so any output-capable GPIO works; GPIO7 has no peripheral conflicts on S3.
constexpr uint8_t PIN_AUDIO_TX      = 7;    // was GPIO26 — right outer pos 4

// RC inputs — CH3 and CH6 land on different GPIOs at their inner-row positions.
// All remaining RC channels (CH1/CH2/CH4/CH5) are direct matches.
//
// GPIO5 overlap on S3: PIN_RC_CH3 and PIN_ARM2_SERVO both map to GPIO5 (right
// inner pos 7). In SBUS modes CH3 is dormant — no conflict. In standard_pwm
// mode with ARM2 enabled, both compete for GPIO5. Use standard_pwm + ARM2 on
// the classic ESP32 target where PIN_RC_CH3 = GPIO2 and PIN_ARM2_SERVO = GPIO5.
constexpr uint8_t PIN_RC_CH3        = 5;    // was GPIO2  — right inner pos 7
constexpr uint8_t PIN_RC_CH6        = 38;   // was GPIO27 — left inner pos 6

// Servo outputs — ARM1, ARM4, ARM5 land on new GPIOs at their socket positions.
// ARM2 (GPIO5) is a direct match at right inner pos 7 — no override needed.
// AUX1/ARM3 (GPIO19) is disabled regardless (PA_ENABLE_AUX1=0).
constexpr uint8_t PIN_ARM1_SERVO    = 8;    // was GPIO23 — right outer pos 3
constexpr uint8_t PIN_ARM4_SERVO    = 6;    // was GPIO18 — right outer pos 5 (AUX2)
constexpr uint8_t PIN_ARM5_SERVO    = 11;   // was GPIO32 — right inner pos 3 (AUX3)

// Dome ESC — GPIO25 (DAC1) was at right inner pos 8 on the D1 Mini.
// S3 Mini right inner pos 8 carries GPIO3.
constexpr uint8_t PIN_DOME_ESC      = 3;    // was GPIO25 — right inner pos 8

// --- Direct matches — same GPIO at same physical socket position ---
// No constexpr needed; default config.h values above apply.
//
// PIN_HOVERBOARD_TX  = 16  — direct match at left inner pos 8
// PIN_AUDIO_RX       = 35  — direct match at left outer pos 5
// PIN_DOME_TX        = 33  — direct match at left inner pos 3
// PIN_DOME_RX        = 34  — direct match at left inner pos 5
// PIN_SBUS1_RX       = 15  — direct match at left inner pos 7 (UART changes to Serial0)
// PIN_SBUS2_RX       = 13  — direct match at right inner pos 4
// PIN_RC_CH1         = 15  — same as PIN_SBUS1_RX above
// PIN_RC_CH2         = 13  — same as PIN_SBUS2_RX above
// PIN_RC_CH4         = 4   — direct match at right inner pos 6
// PIN_RC_CH5         = 12  — direct match at right inner pos 5
// PIN_ARM2_SERVO     = 5   — direct match at right inner pos 7
// PIN_ARM3_SERVO     = 19  — AUX1 disabled; PA_ENABLE_AUX1=0 ensures no LEDC attach

#endif  // PA_BOARD_S3_MINI

// Compile-time guard: AUX1 is not supported on S3 Mini.
// GPIO19 is USB D− on the S3 Mini — it must never be driven as a servo output.
#if defined(PA_BOARD_S3_MINI) && defined(PA_ENABLE_AUX1) && PA_ENABLE_AUX1
#  error "PA_ENABLE_AUX1 not supported on S3 Mini: GPIO19 = USB D−. Set PA_ENABLE_AUX1=0."
#endif
