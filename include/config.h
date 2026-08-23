// =============================================================================
// include/config.h
//
// GPIO pin assignments and compile-time constants for protoArtoo.
// Supports multiple controller board variants on different chip targets.
// See docs/pin_map.md and docs/adr/0028-two-layer-board-abstraction.md
//
// PCB serial port legend (from PCB silkscreen):
//   S0 = ESP debug           (UART0, GPIO 1/3)
//   S1 = Hoverboard          (UART1, GPIO 16 TX / 17 RX)
//   S2 = Sound               (GPIO 26 TX / 35 RX)
//   S3 = Dome Control        (UART2, GPIO 33 / 34 RX)
// =============================================================================
#pragma once

#include <stddef.h>
#include <stdint.h>

// =============================================================================
// Board Variant Selection (ADR 0028: Two-Layer Board Abstraction)
// =============================================================================
// PA_BOARD must be defined by platformio.ini for each environment.
// It selects which pin-map and board-specific configuration applies.
//
// Board variant identifiers (compile-time):
#define PA_BOARD_ARTOO_ESP32   1  // artoo.uk Artoo Controller PCB on classic ESP32
#define PA_BOARD_FIREBEETLE2   2  // DFRobot FireBeetle 2 on ESP32-P4

#if !defined(PA_BOARD)
  #error "PA_BOARD must be defined by platformio.ini build_flags for the target environment"
#endif

// Chip target mapping (ADR 0028):
// - PA_BOARD_ARTOO_ESP32 targets ESP32 (classic)
// - PA_BOARD_FIREBEETLE2 targets ESP32-P4
#if PA_BOARD == PA_BOARD_ARTOO_ESP32
  #define PA_CHIP_TARGET_ESP32 1
#elif PA_BOARD == PA_BOARD_FIREBEETLE2
  #define PA_CHIP_TARGET_ESP32P4 1
#else
  #error "PA_BOARD value not recognized"
#endif

// Sentinel value for pins that have not yet been assigned on a board variant.
// Never a valid GPIO on any supported chip. Used to make builds fail loudly
// if code tries to use an unassigned pin, rather than silently configuring
// the wrong GPIO. See static_assert guards below per board variant.
constexpr uint8_t PA_PIN_UNASSIGNED = 0xFF;

// =============================================================================
// Board-Specific GPIO Pin Assignments
// =============================================================================
// Each board variant defines its pin-map below. Pins are board-specific;
// protocol constants (SBUS_*, SPEED_*, etc.) are chip-target specific.

#if PA_BOARD == PA_BOARD_ARTOO_ESP32
// ────────────────────────────────────────────────────────────────────────────
// artoo-esp32: artoo.uk Artoo Controller PCB on classic ESP32 D1 Mini clone
// All pins confirmed by PCB continuity trace on 2026-03-12 (PCB v1.2).
// See docs/pin_map.md for full trace results and revision notes.
// ────────────────────────────────────────────────────────────────────────────

// UART1 (Serial1)  --  Hoverboard motor controller (Gen2.x protocol, PCB S1)
constexpr uint8_t PIN_HOVERBOARD_TX = 16;
constexpr uint8_t PIN_HOVERBOARD_RX = 17;

// -----------------------------------------------------------------------------
// UART2 (Serial2)  --  Dome serial link (AstroPixelsPlus via slip ring, PCB S3)
// Dome control UART (S3, slip ring).
// -----------------------------------------------------------------------------
constexpr uint8_t PIN_DOME_TX = 33;
constexpr uint8_t PIN_DOME_RX = 34;

// -----------------------------------------------------------------------------
// Audio module serial (DY-SV5W, PCB S2)
// TX primary; RX used for status/ACK where the module supports it.
// -----------------------------------------------------------------------------
constexpr uint8_t PIN_AUDIO_TX = 26;
constexpr uint8_t PIN_AUDIO_RX = 35;  // input-only GPIO  --  RX only, cannot be TX

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

constexpr uint8_t PIN_SBUS1_RX = PIN_RC_CH1;  // CH1  --  SBUS #1 (drive)
constexpr uint8_t PIN_SBUS2_RX = PIN_RC_CH2;  // CH2  --  SBUS #2 (dome)

// -----------------------------------------------------------------------------
// Servo outputs (LEDC PWM)
// ARM1 = Utility arm servo #1  --  Top / Left arm (GPIO 23)
// ARM2 = Utility arm servo #2  --  Bottom / Right arm (GPIO 5)
// AUX1 = Spare servo output (GPIO 19, also labelled ARM3)
// AUX2 = Spare servo output (GPIO 18, also labelled ARM4)
// AUX3 = Spare servo output (GPIO 32, also labelled ARM5)
// DOME = Dome rotation ESC (GPIO 25)  --  drives brushless motor, not a servo
// -----------------------------------------------------------------------------
constexpr uint8_t PIN_ARM1_SERVO = 23;
constexpr uint8_t PIN_ARM2_SERVO = 5;
constexpr uint8_t PIN_ARM3_SERVO = 19;  // AUX1  --  spare servo output
constexpr uint8_t PIN_ARM4_SERVO = 18;  // AUX2  --  spare servo output
constexpr uint8_t PIN_ARM5_SERVO = 32;  // AUX3  --  spare servo output
constexpr uint8_t PIN_DOME_ESC = 25;

// AUX LED strip selection values (NVS aux_led_pin)
constexpr uint8_t AUX_LED_PIN_DISABLED = 0;
constexpr uint8_t AUX_LED_PIN_AUX1 = 1;
constexpr uint8_t AUX_LED_PIN_AUX2 = 2;
constexpr uint8_t AUX_LED_PIN_AUX3 = 3;
constexpr uint8_t AUX_LED_PIN_MAX = AUX_LED_PIN_AUX3;
constexpr uint8_t AUX_LED_COUNT_DEFAULT = 1;
constexpr uint8_t AUX_LED_COUNT_MAX = 255;

inline bool auxLedPinSettingValid(uint8_t selection) {
    return selection <= AUX_LED_PIN_MAX;
}

inline uint8_t auxLedSelectionToGpio(uint8_t selection) {
    switch (selection) {
        case AUX_LED_PIN_AUX1:
            return PIN_ARM3_SERVO;
        case AUX_LED_PIN_AUX2:
            return PIN_ARM4_SERVO;
        case AUX_LED_PIN_AUX3:
            return PIN_ARM5_SERVO;
        case AUX_LED_PIN_DISABLED:
        default:
            return 0;
    }
}

// -----------------------------------------------------------------------------
// I2C
// -----------------------------------------------------------------------------
constexpr uint8_t PIN_I2C_SCL = 22;
constexpr uint8_t PIN_I2C_SDA = 21;

#elif PA_BOARD == PA_BOARD_FIREBEETLE2
// ────────────────────────────────────────────────────────────────────────────
// firebeetle2: DFRobot FireBeetle 2 on ESP32-P4 with IO expansion board (DFR1237)
// Chip revision v1.x (360 MHz, 32 MB PSRAM, 16 MB flash)
// Pin assignments from docs/spec-sheets/firebeetle2-esp32-p4-spec-sheet.md
// See that sheet for hardware truth: "Recommended allocation" (UART Lane Plan)
// and "Not available on the IO headers" (GPIO constraints).
// ────────────────────────────────────────────────────────────────────────────
// Pin mapping based on FireBeetle 2 spec sheet "Recommended allocation" table
// (lines 803-814). P4 firmware (#188/#189) will finalize RC/servo assignments
// after confirming design constraints. Firmware peripheral planning tracked in #190.

// UART1 — Hoverboard motor controller
// From spec sheet "Recommended allocation": UART1 = GPIO20/21
// Cost: ADC1_CHANNEL4/5 (per spec sheet: "Default first lane if no analog input")
constexpr uint8_t PIN_HOVERBOARD_TX = 20;  // UART1_TX per spec sheet §Recommended allocation
constexpr uint8_t PIN_HOVERBOARD_RX = 21;  // UART1_RX per spec sheet §Recommended allocation

// UART2 — Dome control link
// From spec sheet "Recommended allocation": UART2 = GPIO22/23
// Cost: ADC1_CHANNEL6/7 (per spec sheet: "Default second lane if no analog input")
constexpr uint8_t PIN_DOME_TX = 22;  // UART2_TX per spec sheet §Recommended allocation
constexpr uint8_t PIN_DOME_RX = 23;  // UART2_RX per spec sheet §Recommended allocation

// Audio UART — DY-SV5W module
// From spec sheet: GPIO34/36 are strapping pins (P3), usable via GPIO matrix with
// unburnt eFuses. Dedicated hardware UART TX/RX paths for audio module.
// CAUTION: Never burn EFUSE_JTAG_SEL_ENABLE or EFUSE_UART_PRINT_CONTROL on this board.
// While both default to 0 (eFuse unburnt), GPIO34/36 strapping roles remain ignored.
// Burning either turns the audio UART pins into live strapping inputs — incompatible with audio.
constexpr uint8_t PIN_AUDIO_TX = 34;  // UART matrix, P3 strapping (JTAG source), GPIO matrix routed
constexpr uint8_t PIN_AUDIO_RX = 36;  // UART matrix, P3 strapping (ROM print), GPIO matrix routed

// RC receiver inputs (SBUS + analog channels)
// Allocation per spec sheet "Recommended allocation" and §Exposed GPIO table.
// All six channels assigned to P2 unimpeachable pins (28-33) to keep the safety-critical
// SBUS input away from strapping conflicts and avoid-list pairs.
constexpr uint8_t PIN_RC_CH1 = 28;  // P2 unimpeachable, SBUS #1 (drive) receiver
constexpr uint8_t PIN_RC_CH2 = 29;  // P2 unimpeachable, SBUS #2 (dome) receiver
constexpr uint8_t PIN_RC_CH3 = 30;  // P2 unimpeachable
constexpr uint8_t PIN_RC_CH4 = 31;  // P2 unimpeachable, spec sheet: "best clean pin in <=36 range"
constexpr uint8_t PIN_RC_CH5 = 32;  // P1-for-I3C, reassignable (protoArtoo does not use I3C)
constexpr uint8_t PIN_RC_CH6 = 33;  // P1-for-I3C, reassignable (protoArtoo does not use I3C)

constexpr uint8_t PIN_SBUS1_RX = PIN_RC_CH1;  // CH1  --  SBUS #1 (drive)
constexpr uint8_t PIN_SBUS2_RX = PIN_RC_CH2;  // CH2  --  SBUS #2 (dome)

// Servo outputs (LEDC PWM)
// Allocation: standard arm servos on LDO-backed pins (49-50 on VDD_IO_6).
// AUX pins (1-3, which drive the optional WS2812B strip via auxLedSelectionToGpio())
// use non-LDO main IO to avoid placing a high-frequency timing-critical line on
// unmeasured LDO rails. AUX1/AUX2 on GPIO4/5 cost JTAG, which is acceptable post-debug.
constexpr uint8_t PIN_ARM1_SERVO = 49;  // LEDC PWM, LDO caution (VDD_IO_6), ADC2_CHANNEL0
constexpr uint8_t PIN_ARM2_SERVO = 50;  // LEDC PWM, LDO caution (VDD_IO_6), ADC2_CHANNEL1
constexpr uint8_t PIN_ARM3_SERVO = 4;   // AUX1, WS2812B strip capable, P3 JTAG MTMS (post-debug)
constexpr uint8_t PIN_ARM4_SERVO = 5;   // AUX2, WS2812B strip capable, P3 JTAG MTDO (post-debug)
constexpr uint8_t PIN_ARM5_SERVO = 51;  // AUX3, WS2812B strip capable, LDO caution (VDD_IO_6)
constexpr uint8_t PIN_DOME_ESC = 48;    // ESC PWM, LDO caution (VDD_IO_5)

// Every pin used by a production peripheral must be assigned before the full
// FireBeetle firmware can compile. This is safety-critical for SBUS because it
// feeds the failsafe/estop path. SBUS1/2 guard RC_CH1/2 through their aliases,
// so the inventory has no duplicate RC_CH1/2 rows that could drift.
#define PA_FIREBEETLE_REQUIRED_PIN(pin, diagnostic) \
    static_assert(pin != PA_PIN_UNASSIGNED, diagnostic);
#include "firebeetle_required_pins.inc"
#undef PA_FIREBEETLE_REQUIRED_PIN

// Guard against duplicate pin assignments (two peripherals on the same GPIO).
// Duplicate detection uses a compile-time set membership pattern:
// static_assert(/* pin not already used */) for each assignment.
static_assert(PIN_RC_CH1 != PIN_RC_CH2, "FireBeetle2: duplicate RC assignment - PIN_RC_CH1 and PIN_RC_CH2 are both assigned to the same GPIO");
static_assert(PIN_RC_CH1 != PIN_RC_CH3, "FireBeetle2: duplicate RC assignment - PIN_RC_CH1 and PIN_RC_CH3 are both assigned to the same GPIO");
static_assert(PIN_RC_CH1 != PIN_RC_CH4, "FireBeetle2: duplicate RC assignment - PIN_RC_CH1 and PIN_RC_CH4 are both assigned to the same GPIO");
static_assert(PIN_RC_CH1 != PIN_RC_CH5, "FireBeetle2: duplicate RC assignment - PIN_RC_CH1 and PIN_RC_CH5 are both assigned to the same GPIO");
static_assert(PIN_RC_CH1 != PIN_RC_CH6, "FireBeetle2: duplicate RC assignment - PIN_RC_CH1 and PIN_RC_CH6 are both assigned to the same GPIO");
static_assert(PIN_RC_CH1 != PIN_AUDIO_TX, "FireBeetle2: duplicate assignment - PIN_RC_CH1 and PIN_AUDIO_TX are both assigned to the same GPIO");
static_assert(PIN_RC_CH1 != PIN_AUDIO_RX, "FireBeetle2: duplicate assignment - PIN_RC_CH1 and PIN_AUDIO_RX are both assigned to the same GPIO");
static_assert(PIN_RC_CH1 != PIN_ARM1_SERVO, "FireBeetle2: duplicate assignment - PIN_RC_CH1 and PIN_ARM1_SERVO are both assigned to the same GPIO");
static_assert(PIN_RC_CH1 != PIN_ARM2_SERVO, "FireBeetle2: duplicate assignment - PIN_RC_CH1 and PIN_ARM2_SERVO are both assigned to the same GPIO");
static_assert(PIN_RC_CH1 != PIN_ARM3_SERVO, "FireBeetle2: duplicate assignment - PIN_RC_CH1 and PIN_ARM3_SERVO are both assigned to the same GPIO");
static_assert(PIN_RC_CH1 != PIN_ARM4_SERVO, "FireBeetle2: duplicate assignment - PIN_RC_CH1 and PIN_ARM4_SERVO are both assigned to the same GPIO");
static_assert(PIN_RC_CH1 != PIN_ARM5_SERVO, "FireBeetle2: duplicate assignment - PIN_RC_CH1 and PIN_ARM5_SERVO are both assigned to the same GPIO");
static_assert(PIN_RC_CH1 != PIN_DOME_ESC, "FireBeetle2: duplicate assignment - PIN_RC_CH1 and PIN_DOME_ESC are both assigned to the same GPIO");
static_assert(PIN_RC_CH2 != PIN_RC_CH3, "FireBeetle2: duplicate RC assignment - PIN_RC_CH2 and PIN_RC_CH3 are both assigned to the same GPIO");
static_assert(PIN_RC_CH2 != PIN_RC_CH4, "FireBeetle2: duplicate RC assignment - PIN_RC_CH2 and PIN_RC_CH4 are both assigned to the same GPIO");
static_assert(PIN_RC_CH2 != PIN_RC_CH5, "FireBeetle2: duplicate RC assignment - PIN_RC_CH2 and PIN_RC_CH5 are both assigned to the same GPIO");
static_assert(PIN_RC_CH2 != PIN_RC_CH6, "FireBeetle2: duplicate RC assignment - PIN_RC_CH2 and PIN_RC_CH6 are both assigned to the same GPIO");
static_assert(PIN_RC_CH2 != PIN_AUDIO_TX, "FireBeetle2: duplicate assignment - PIN_RC_CH2 and PIN_AUDIO_TX are both assigned to the same GPIO");
static_assert(PIN_RC_CH2 != PIN_AUDIO_RX, "FireBeetle2: duplicate assignment - PIN_RC_CH2 and PIN_AUDIO_RX are both assigned to the same GPIO");
static_assert(PIN_RC_CH2 != PIN_ARM1_SERVO, "FireBeetle2: duplicate assignment - PIN_RC_CH2 and PIN_ARM1_SERVO are both assigned to the same GPIO");
static_assert(PIN_RC_CH2 != PIN_ARM2_SERVO, "FireBeetle2: duplicate assignment - PIN_RC_CH2 and PIN_ARM2_SERVO are both assigned to the same GPIO");
static_assert(PIN_RC_CH2 != PIN_ARM3_SERVO, "FireBeetle2: duplicate assignment - PIN_RC_CH2 and PIN_ARM3_SERVO are both assigned to the same GPIO");
static_assert(PIN_RC_CH2 != PIN_ARM4_SERVO, "FireBeetle2: duplicate assignment - PIN_RC_CH2 and PIN_ARM4_SERVO are both assigned to the same GPIO");
static_assert(PIN_RC_CH2 != PIN_ARM5_SERVO, "FireBeetle2: duplicate assignment - PIN_RC_CH2 and PIN_ARM5_SERVO are both assigned to the same GPIO");
static_assert(PIN_RC_CH2 != PIN_DOME_ESC, "FireBeetle2: duplicate assignment - PIN_RC_CH2 and PIN_DOME_ESC are both assigned to the same GPIO");
static_assert(PIN_RC_CH3 != PIN_RC_CH4, "FireBeetle2: duplicate RC assignment - PIN_RC_CH3 and PIN_RC_CH4 are both assigned to the same GPIO");
static_assert(PIN_RC_CH3 != PIN_RC_CH5, "FireBeetle2: duplicate RC assignment - PIN_RC_CH3 and PIN_RC_CH5 are both assigned to the same GPIO");
static_assert(PIN_RC_CH3 != PIN_RC_CH6, "FireBeetle2: duplicate RC assignment - PIN_RC_CH3 and PIN_RC_CH6 are both assigned to the same GPIO");
static_assert(PIN_RC_CH3 != PIN_AUDIO_TX, "FireBeetle2: duplicate assignment - PIN_RC_CH3 and PIN_AUDIO_TX are both assigned to the same GPIO");
static_assert(PIN_RC_CH3 != PIN_AUDIO_RX, "FireBeetle2: duplicate assignment - PIN_RC_CH3 and PIN_AUDIO_RX are both assigned to the same GPIO");
static_assert(PIN_RC_CH3 != PIN_ARM1_SERVO, "FireBeetle2: duplicate assignment - PIN_RC_CH3 and PIN_ARM1_SERVO are both assigned to the same GPIO");
static_assert(PIN_RC_CH3 != PIN_ARM2_SERVO, "FireBeetle2: duplicate assignment - PIN_RC_CH3 and PIN_ARM2_SERVO are both assigned to the same GPIO");
static_assert(PIN_RC_CH3 != PIN_ARM3_SERVO, "FireBeetle2: duplicate assignment - PIN_RC_CH3 and PIN_ARM3_SERVO are both assigned to the same GPIO");
static_assert(PIN_RC_CH3 != PIN_ARM4_SERVO, "FireBeetle2: duplicate assignment - PIN_RC_CH3 and PIN_ARM4_SERVO are both assigned to the same GPIO");
static_assert(PIN_RC_CH3 != PIN_ARM5_SERVO, "FireBeetle2: duplicate assignment - PIN_RC_CH3 and PIN_ARM5_SERVO are both assigned to the same GPIO");
static_assert(PIN_RC_CH3 != PIN_DOME_ESC, "FireBeetle2: duplicate assignment - PIN_RC_CH3 and PIN_DOME_ESC are both assigned to the same GPIO");
static_assert(PIN_RC_CH4 != PIN_RC_CH5, "FireBeetle2: duplicate RC assignment - PIN_RC_CH4 and PIN_RC_CH5 are both assigned to the same GPIO");
static_assert(PIN_RC_CH4 != PIN_RC_CH6, "FireBeetle2: duplicate RC assignment - PIN_RC_CH4 and PIN_RC_CH6 are both assigned to the same GPIO");
static_assert(PIN_RC_CH4 != PIN_AUDIO_TX, "FireBeetle2: duplicate assignment - PIN_RC_CH4 and PIN_AUDIO_TX are both assigned to the same GPIO");
static_assert(PIN_RC_CH4 != PIN_AUDIO_RX, "FireBeetle2: duplicate assignment - PIN_RC_CH4 and PIN_AUDIO_RX are both assigned to the same GPIO");
static_assert(PIN_RC_CH4 != PIN_ARM1_SERVO, "FireBeetle2: duplicate assignment - PIN_RC_CH4 and PIN_ARM1_SERVO are both assigned to the same GPIO");
static_assert(PIN_RC_CH4 != PIN_ARM2_SERVO, "FireBeetle2: duplicate assignment - PIN_RC_CH4 and PIN_ARM2_SERVO are both assigned to the same GPIO");
static_assert(PIN_RC_CH4 != PIN_ARM3_SERVO, "FireBeetle2: duplicate assignment - PIN_RC_CH4 and PIN_ARM3_SERVO are both assigned to the same GPIO");
static_assert(PIN_RC_CH4 != PIN_ARM4_SERVO, "FireBeetle2: duplicate assignment - PIN_RC_CH4 and PIN_ARM4_SERVO are both assigned to the same GPIO");
static_assert(PIN_RC_CH4 != PIN_ARM5_SERVO, "FireBeetle2: duplicate assignment - PIN_RC_CH4 and PIN_ARM5_SERVO are both assigned to the same GPIO");
static_assert(PIN_RC_CH4 != PIN_DOME_ESC, "FireBeetle2: duplicate assignment - PIN_RC_CH4 and PIN_DOME_ESC are both assigned to the same GPIO");
static_assert(PIN_RC_CH5 != PIN_RC_CH6, "FireBeetle2: duplicate RC assignment - PIN_RC_CH5 and PIN_RC_CH6 are both assigned to the same GPIO");
static_assert(PIN_RC_CH5 != PIN_AUDIO_TX, "FireBeetle2: duplicate assignment - PIN_RC_CH5 and PIN_AUDIO_TX are both assigned to the same GPIO");
static_assert(PIN_RC_CH5 != PIN_AUDIO_RX, "FireBeetle2: duplicate assignment - PIN_RC_CH5 and PIN_AUDIO_RX are both assigned to the same GPIO");
static_assert(PIN_RC_CH5 != PIN_ARM1_SERVO, "FireBeetle2: duplicate assignment - PIN_RC_CH5 and PIN_ARM1_SERVO are both assigned to the same GPIO");
static_assert(PIN_RC_CH5 != PIN_ARM2_SERVO, "FireBeetle2: duplicate assignment - PIN_RC_CH5 and PIN_ARM2_SERVO are both assigned to the same GPIO");
static_assert(PIN_RC_CH5 != PIN_ARM3_SERVO, "FireBeetle2: duplicate assignment - PIN_RC_CH5 and PIN_ARM3_SERVO are both assigned to the same GPIO");
static_assert(PIN_RC_CH5 != PIN_ARM4_SERVO, "FireBeetle2: duplicate assignment - PIN_RC_CH5 and PIN_ARM4_SERVO are both assigned to the same GPIO");
static_assert(PIN_RC_CH5 != PIN_ARM5_SERVO, "FireBeetle2: duplicate assignment - PIN_RC_CH5 and PIN_ARM5_SERVO are both assigned to the same GPIO");
static_assert(PIN_RC_CH5 != PIN_DOME_ESC, "FireBeetle2: duplicate assignment - PIN_RC_CH5 and PIN_DOME_ESC are both assigned to the same GPIO");
static_assert(PIN_RC_CH6 != PIN_AUDIO_TX, "FireBeetle2: duplicate assignment - PIN_RC_CH6 and PIN_AUDIO_TX are both assigned to the same GPIO");
static_assert(PIN_RC_CH6 != PIN_AUDIO_RX, "FireBeetle2: duplicate assignment - PIN_RC_CH6 and PIN_AUDIO_RX are both assigned to the same GPIO");
static_assert(PIN_RC_CH6 != PIN_ARM1_SERVO, "FireBeetle2: duplicate assignment - PIN_RC_CH6 and PIN_ARM1_SERVO are both assigned to the same GPIO");
static_assert(PIN_RC_CH6 != PIN_ARM2_SERVO, "FireBeetle2: duplicate assignment - PIN_RC_CH6 and PIN_ARM2_SERVO are both assigned to the same GPIO");
static_assert(PIN_RC_CH6 != PIN_ARM3_SERVO, "FireBeetle2: duplicate assignment - PIN_RC_CH6 and PIN_ARM3_SERVO are both assigned to the same GPIO");
static_assert(PIN_RC_CH6 != PIN_ARM4_SERVO, "FireBeetle2: duplicate assignment - PIN_RC_CH6 and PIN_ARM4_SERVO are both assigned to the same GPIO");
static_assert(PIN_RC_CH6 != PIN_ARM5_SERVO, "FireBeetle2: duplicate assignment - PIN_RC_CH6 and PIN_ARM5_SERVO are both assigned to the same GPIO");
static_assert(PIN_RC_CH6 != PIN_DOME_ESC, "FireBeetle2: duplicate assignment - PIN_RC_CH6 and PIN_DOME_ESC are both assigned to the same GPIO");
static_assert(PIN_AUDIO_TX != PIN_AUDIO_RX, "FireBeetle2: duplicate assignment - PIN_AUDIO_TX and PIN_AUDIO_RX are both assigned to the same GPIO");
static_assert(PIN_AUDIO_TX != PIN_ARM1_SERVO, "FireBeetle2: duplicate assignment - PIN_AUDIO_TX and PIN_ARM1_SERVO are both assigned to the same GPIO");
static_assert(PIN_AUDIO_TX != PIN_ARM2_SERVO, "FireBeetle2: duplicate assignment - PIN_AUDIO_TX and PIN_ARM2_SERVO are both assigned to the same GPIO");
static_assert(PIN_AUDIO_TX != PIN_ARM3_SERVO, "FireBeetle2: duplicate assignment - PIN_AUDIO_TX and PIN_ARM3_SERVO are both assigned to the same GPIO");
static_assert(PIN_AUDIO_TX != PIN_ARM4_SERVO, "FireBeetle2: duplicate assignment - PIN_AUDIO_TX and PIN_ARM4_SERVO are both assigned to the same GPIO");
static_assert(PIN_AUDIO_TX != PIN_ARM5_SERVO, "FireBeetle2: duplicate assignment - PIN_AUDIO_TX and PIN_ARM5_SERVO are both assigned to the same GPIO");
static_assert(PIN_AUDIO_TX != PIN_DOME_ESC, "FireBeetle2: duplicate assignment - PIN_AUDIO_TX and PIN_DOME_ESC are both assigned to the same GPIO");
static_assert(PIN_AUDIO_RX != PIN_ARM1_SERVO, "FireBeetle2: duplicate assignment - PIN_AUDIO_RX and PIN_ARM1_SERVO are both assigned to the same GPIO");
static_assert(PIN_AUDIO_RX != PIN_ARM2_SERVO, "FireBeetle2: duplicate assignment - PIN_AUDIO_RX and PIN_ARM2_SERVO are both assigned to the same GPIO");
static_assert(PIN_AUDIO_RX != PIN_ARM3_SERVO, "FireBeetle2: duplicate assignment - PIN_AUDIO_RX and PIN_ARM3_SERVO are both assigned to the same GPIO");
static_assert(PIN_AUDIO_RX != PIN_ARM4_SERVO, "FireBeetle2: duplicate assignment - PIN_AUDIO_RX and PIN_ARM4_SERVO are both assigned to the same GPIO");
static_assert(PIN_AUDIO_RX != PIN_ARM5_SERVO, "FireBeetle2: duplicate assignment - PIN_AUDIO_RX and PIN_ARM5_SERVO are both assigned to the same GPIO");
static_assert(PIN_AUDIO_RX != PIN_DOME_ESC, "FireBeetle2: duplicate assignment - PIN_AUDIO_RX and PIN_DOME_ESC are both assigned to the same GPIO");
static_assert(PIN_ARM1_SERVO != PIN_ARM2_SERVO, "FireBeetle2: duplicate assignment - PIN_ARM1_SERVO and PIN_ARM2_SERVO are both assigned to the same GPIO");
static_assert(PIN_ARM1_SERVO != PIN_ARM3_SERVO, "FireBeetle2: duplicate assignment - PIN_ARM1_SERVO and PIN_ARM3_SERVO are both assigned to the same GPIO");
static_assert(PIN_ARM1_SERVO != PIN_ARM4_SERVO, "FireBeetle2: duplicate assignment - PIN_ARM1_SERVO and PIN_ARM4_SERVO are both assigned to the same GPIO");
static_assert(PIN_ARM1_SERVO != PIN_ARM5_SERVO, "FireBeetle2: duplicate assignment - PIN_ARM1_SERVO and PIN_ARM5_SERVO are both assigned to the same GPIO");
static_assert(PIN_ARM1_SERVO != PIN_DOME_ESC, "FireBeetle2: duplicate assignment - PIN_ARM1_SERVO and PIN_DOME_ESC are both assigned to the same GPIO");
static_assert(PIN_ARM2_SERVO != PIN_ARM3_SERVO, "FireBeetle2: duplicate assignment - PIN_ARM2_SERVO and PIN_ARM3_SERVO are both assigned to the same GPIO");
static_assert(PIN_ARM2_SERVO != PIN_ARM4_SERVO, "FireBeetle2: duplicate assignment - PIN_ARM2_SERVO and PIN_ARM4_SERVO are both assigned to the same GPIO");
static_assert(PIN_ARM2_SERVO != PIN_ARM5_SERVO, "FireBeetle2: duplicate assignment - PIN_ARM2_SERVO and PIN_ARM5_SERVO are both assigned to the same GPIO");
static_assert(PIN_ARM2_SERVO != PIN_DOME_ESC, "FireBeetle2: duplicate assignment - PIN_ARM2_SERVO and PIN_DOME_ESC are both assigned to the same GPIO");
static_assert(PIN_ARM3_SERVO != PIN_ARM4_SERVO, "FireBeetle2: duplicate assignment - PIN_ARM3_SERVO and PIN_ARM4_SERVO are both assigned to the same GPIO");
static_assert(PIN_ARM3_SERVO != PIN_ARM5_SERVO, "FireBeetle2: duplicate assignment - PIN_ARM3_SERVO and PIN_ARM5_SERVO are both assigned to the same GPIO");
static_assert(PIN_ARM3_SERVO != PIN_DOME_ESC, "FireBeetle2: duplicate assignment - PIN_ARM3_SERVO and PIN_DOME_ESC are both assigned to the same GPIO");
static_assert(PIN_ARM4_SERVO != PIN_ARM5_SERVO, "FireBeetle2: duplicate assignment - PIN_ARM4_SERVO and PIN_ARM5_SERVO are both assigned to the same GPIO");
static_assert(PIN_ARM4_SERVO != PIN_DOME_ESC, "FireBeetle2: duplicate assignment - PIN_ARM4_SERVO and PIN_DOME_ESC are both assigned to the same GPIO");
static_assert(PIN_ARM5_SERVO != PIN_DOME_ESC, "FireBeetle2: duplicate assignment - PIN_ARM5_SERVO and PIN_DOME_ESC are both assigned to the same GPIO");

// Guard against reserved GPIO assignments (pins not available on the board or
// committed to essential boot/debug functions that cannot be reassigned).
// GPIO35 is BOOT strapping, GPIO37/38 are UART0 console (ROM logs, serial download).
static_assert(PIN_RC_CH1 != 35 && PIN_RC_CH1 != 37 && PIN_RC_CH1 != 38, "FireBeetle2: PIN_RC_CH1 is assigned to a reserved GPIO (BOOT/UART0 console)");
static_assert(PIN_RC_CH2 != 35 && PIN_RC_CH2 != 37 && PIN_RC_CH2 != 38, "FireBeetle2: PIN_RC_CH2 is assigned to a reserved GPIO (BOOT/UART0 console)");
static_assert(PIN_RC_CH3 != 35 && PIN_RC_CH3 != 37 && PIN_RC_CH3 != 38, "FireBeetle2: PIN_RC_CH3 is assigned to a reserved GPIO (BOOT/UART0 console)");
static_assert(PIN_RC_CH4 != 35 && PIN_RC_CH4 != 37 && PIN_RC_CH4 != 38, "FireBeetle2: PIN_RC_CH4 is assigned to a reserved GPIO (BOOT/UART0 console)");
static_assert(PIN_RC_CH5 != 35 && PIN_RC_CH5 != 37 && PIN_RC_CH5 != 38, "FireBeetle2: PIN_RC_CH5 is assigned to a reserved GPIO (BOOT/UART0 console)");
static_assert(PIN_RC_CH6 != 35 && PIN_RC_CH6 != 37 && PIN_RC_CH6 != 38, "FireBeetle2: PIN_RC_CH6 is assigned to a reserved GPIO (BOOT/UART0 console)");
static_assert(PIN_AUDIO_TX != 35 && PIN_AUDIO_TX != 37 && PIN_AUDIO_TX != 38, "FireBeetle2: PIN_AUDIO_TX is assigned to a reserved GPIO (BOOT/UART0 console)");
static_assert(PIN_AUDIO_RX != 35 && PIN_AUDIO_RX != 37 && PIN_AUDIO_RX != 38, "FireBeetle2: PIN_AUDIO_RX is assigned to a reserved GPIO (BOOT/UART0 console)");
static_assert(PIN_ARM1_SERVO != 35 && PIN_ARM1_SERVO != 37 && PIN_ARM1_SERVO != 38, "FireBeetle2: PIN_ARM1_SERVO is assigned to a reserved GPIO (BOOT/UART0 console)");
static_assert(PIN_ARM2_SERVO != 35 && PIN_ARM2_SERVO != 37 && PIN_ARM2_SERVO != 38, "FireBeetle2: PIN_ARM2_SERVO is assigned to a reserved GPIO (BOOT/UART0 console)");
static_assert(PIN_ARM3_SERVO != 35 && PIN_ARM3_SERVO != 37 && PIN_ARM3_SERVO != 38, "FireBeetle2: PIN_ARM3_SERVO is assigned to a reserved GPIO (BOOT/UART0 console)");
static_assert(PIN_ARM4_SERVO != 35 && PIN_ARM4_SERVO != 37 && PIN_ARM4_SERVO != 38, "FireBeetle2: PIN_ARM4_SERVO is assigned to a reserved GPIO (BOOT/UART0 console)");
static_assert(PIN_ARM5_SERVO != 35 && PIN_ARM5_SERVO != 37 && PIN_ARM5_SERVO != 38, "FireBeetle2: PIN_ARM5_SERVO is assigned to a reserved GPIO (BOOT/UART0 console)");
static_assert(PIN_DOME_ESC != 35 && PIN_DOME_ESC != 37 && PIN_DOME_ESC != 38, "FireBeetle2: PIN_DOME_ESC is assigned to a reserved GPIO (BOOT/UART0 console)");

// AUX LED strip selection values (NVS aux_led_pin)
constexpr uint8_t AUX_LED_PIN_DISABLED = 0;
constexpr uint8_t AUX_LED_PIN_AUX1 = 1;
constexpr uint8_t AUX_LED_PIN_AUX2 = 2;
constexpr uint8_t AUX_LED_PIN_AUX3 = 3;
constexpr uint8_t AUX_LED_PIN_MAX = AUX_LED_PIN_AUX3;
constexpr uint8_t AUX_LED_COUNT_DEFAULT = 1;
constexpr uint8_t AUX_LED_COUNT_MAX = 255;

inline bool auxLedPinSettingValid(uint8_t selection) {
    return selection <= AUX_LED_PIN_MAX;
}

inline uint8_t auxLedSelectionToGpio(uint8_t selection) {
    switch (selection) {
        case AUX_LED_PIN_AUX1:
            return PIN_ARM3_SERVO;
        case AUX_LED_PIN_AUX2:
            return PIN_ARM4_SERVO;
        case AUX_LED_PIN_AUX3:
            return PIN_ARM5_SERVO;
        case AUX_LED_PIN_DISABLED:
        default:
            return 0;
    }
}

// I2C
// From spec sheet "Exposed GPIO table" (lines 717-718): GPIO7/8 are default I2C pads
// (P2 priority: "any GPIO via GPIO matrix, usable without restriction")
constexpr uint8_t PIN_I2C_SCL = 8;   // I2C clock per spec sheet default (P2 priority)
constexpr uint8_t PIN_I2C_SDA = 7;   // I2C data per spec sheet default (P2 priority)

#else
  #error "PA_BOARD value not recognized in pin-map selection"
#endif  // PA_BOARD

// =============================================================================
// Protocol and Feature Constants (chip-target specific, board-agnostic)
// =============================================================================

// Drive constants
// These constants apply to all chip targets; board-specific pins are defined above.
constexpr int16_t SPEED_LIMIT_MAX = 600;  // Absolute max drive output (never exceeded)
constexpr int16_t SPEED_PRESET_SLOW = 200;
constexpr int16_t SPEED_PRESET_NORMAL = 350;
constexpr int16_t SPEED_PRESET_TURBO = SPEED_LIMIT_MAX;
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
constexpr char NVS_KEY_AUX_LED_PIN[] = "aux_led_pin";
constexpr char NVS_KEY_AUX_LED_COUNT[] = "aux_led_count";
constexpr char DROID_NAME_DEFAULT[] = "protoartoo";
constexpr size_t DROID_NAME_MAX_LEN = 32;

// -----------------------------------------------------------------------------
// WiFi AP
// -----------------------------------------------------------------------------
constexpr char WIFI_AP_SSID[] = "protoArtoo";
constexpr char WIFI_AP_IP[] = "192.168.4.1";

// Default AP Credential (ADR 0015): the documented bootstrap password an
// Unprovisioned Controller uses for WiFi Provisioning and Network Recovery
// Mode. Public and shared by design  --  it is a bootstrap credential, not a
// security boundary  --  and operator-changeable through Device WiFi Settings.
constexpr char WIFI_DEFAULT_AP_PASSWORD[] = "protoArtoo1";

// -----------------------------------------------------------------------------
// WiFi hostname / mDNS
// -----------------------------------------------------------------------------
// Keep the LAN hostname lowercase for resolver compatibility. AP mode does not
// advertise mDNS; this hostname is used only when STA WiFi is active.
constexpr char WIFI_MDNS_HOST[] = "artoo";
#ifndef PA_FIRMWARE_VERSION
constexpr char PA_FIRMWARE_VERSION[] = "v0.0.0-dev";
#endif

// -----------------------------------------------------------------------------
// Log levels
// -----------------------------------------------------------------------------
constexpr uint8_t PA_LOG_LEVEL_ERROR = 1;
constexpr uint8_t PA_LOG_LEVEL_WARN = 2;
constexpr uint8_t PA_LOG_LEVEL_INFO = 3;
constexpr uint8_t PA_LOG_LEVEL_DEBUG = 4;

// PA_LOG_LEVEL controls USB debug serial verbosity on UART0.
// - PA_LOG_LEVEL_ERROR (1): loss of function only  --  init/mount failures, failed
//   allocations, unrecoverable driver errors, watchdog-reset detection
// - PA_LOG_LEVEL_WARN  (2): errors plus safety warnings  --  failsafe layer triggers,
//   SBUS watchdog fired, hardware failsafe asserted, estop events, rejected unsafe
//   commands. The recommended minimum: "faults only" means this tier, so a quiet log
//   still reports failsafe activity.
// - PA_LOG_LEVEL_INFO  (3): normal boot health, service bring-up, state transitions
// - PA_LOG_LEVEL_DEBUG (4): verbose development logging, including lower-priority events
// Set via -DPA_LOG_LEVEL=N in platformio.ini build_flags. Defaults to DEBUG if unset.
// This is only the boot default until NVS config loads; the runtime level is the
// operator's saved logLevel (Setup page).
#ifndef PA_LOG_LEVEL
#define PA_LOG_LEVEL 4
#endif
