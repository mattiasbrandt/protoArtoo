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

// This is the FIRST of nine #error guards that fire together when config.h is
// compiled with no PA_BOARD -- which is what an editor's linter does, since it
// has no platformio.ini env. Every later guard ("... not recognized in
// capability selection", "task stack sizes have no value for this chip
// target", "UART controller count has no value for this chip target", and so
// on) is a cascade from this one, not nine separate faults. Fix this one and
// the rest go with it: point the linter at an env, or define PA_BOARD,
// PA_LOG_LEVEL and PA_HEAP_PROFILE in its compile flags.
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

// Board Capability Gates (ADR 0029). Each Board Variant defines every gate
// as 0 or 1; the manifest expansion below makes an omitted declaration or a
// non-binary value a compile-time error without emitting code or data.
// Capability values are invariant PCB topology facts, never runtime state or
// C6/provisioning health — they declare what the board's silicon can do.
//
// PA_CAP_DEDICATED_AUDIO_UART declares that the board has a hardware UART
// controller to spare for the audio module, so audio does not have to borrow
// the dome link's. It is a count fact, not a wiring fact: the classic ESP32 has
// three HP UARTs (SOC_UART_HP_NUM = 3) against the ESP32-P4's five, and with
// UART0 spent on the console and UART1 on the drive backend, artoo-esp32 has
// exactly one controller left for two consumers. Everything that follows from
// that -- audio RX sharing the dome controller through domeUartAcquire(), and
// audio TX being a software bit-bang because there is no spare TX -- is gated
// on this capability rather than repeated per call site (#254).
#if PA_BOARD == PA_BOARD_ARTOO_ESP32
  #define PA_CAP_NATIVE_WIFI 1
  #define PA_CAP_HOSTED_WIFI 0
  #define PA_CAP_DRIVE_BACKEND_HOVERBOARD 1
  #define PA_CAP_DEDICATED_AUDIO_UART 0  // 3 HP UARTs: audio shares the dome link's controller
#elif PA_BOARD == PA_BOARD_FIREBEETLE2
  #define PA_CAP_NATIVE_WIFI 0
  #define PA_CAP_HOSTED_WIFI 1  // Declared here before its consumers (#188, #189) to gate the capability early
  #define PA_CAP_DRIVE_BACKEND_HOVERBOARD 1
  #define PA_CAP_DEDICATED_AUDIO_UART 1  // 5 HP UARTs: audio gets UART_PORT_AUDIO to itself
#else
  #error "PA_BOARD value not recognized in capability selection"
#endif

#define PA_BOARD_CAPABILITY(name) \
    static_assert((name) == 0 || (name) == 1, #name " must be defined as 0 or 1");
#include "board_capabilities.inc"
#undef PA_BOARD_CAPABILITY

// PlatformIO firmware and native-test builds must supply every Build Feature
// Flag. Expand the manifest in the production compile path so missing or
// non-binary values fail at compile time; the plain-host config.h pin probes
// intentionally exercise only board declarations.
// Build flags are always defined as 0 or 1 and tested with #if (never #ifdef);
// using #ifdef on a 0-valued flag would compile the feature in, inverting the gate.
#if ARDUINO || PA_NATIVE_TEST_STUBS
  #define PA_BUILD_FLAG(name) \
      static_assert((name) == 0 || (name) == 1, #name " must be defined as 0 or 1");
  #include "build_flags.inc"
  #undef PA_BUILD_FLAG
#endif

// Heap tracing (PA_HEAP_TRACING) is a troubleshooting-only Build Feature Flag
// SEPARATE from PA_HEAP_PROFILE and additionally requires SDK CONFIG_HEAP_TRACING.
// It gates system.action.profiler-trace-start/stop — a distinct operator feature.
// Keep invalid images from compiling even if an environment is configured by hand.
// Every checked-in PlatformIO environment leaves this at 0 deliberately.
#if PA_HEAP_TRACING
  #if !PA_HEAP_PROFILE
    #error "PA_HEAP_TRACING=1 requires PA_HEAP_PROFILE=1"
  #endif
  #if !CONFIG_HEAP_TRACING
    #error "PA_HEAP_TRACING=1 requires CONFIG_HEAP_TRACING enabled"
  #endif
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

// -----------------------------------------------------------------------------
// UART controller allocation (the Arduino HardwareSerial index, not a GPIO).
//
// The classic ESP32 has three HP UART controllers (SOC_UART_HP_NUM = 3, the
// Arduino core's soc/esp32/soc_caps.h). UART0 is the USB debug console on PCB
// S0, UART1 is traced to the drive backend on S1, and that leaves ONE
// controller for two consumers -- the dome link on S3 and the audio module's
// RX on S2. Hence PA_CAP_DEDICATED_AUDIO_UART == 0 here, and hence the two
// workarounds that follow from it and are load-bearing on this board:
//   - the dome/audio ownership handoff (domeUartAcquire/domeUartRelease), and
//   - the audio TX software bit-bang (src/drivers/audio_soft_uart_tx.h),
//     because the one shared controller's TX is committed to the dome link.
// Neither is a design preference; both are what three controllers force.
// -----------------------------------------------------------------------------
constexpr uint8_t UART_PORT_DRIVE = 1;  // Serial1, PCB S1
constexpr uint8_t UART_PORT_DOME  = 2;  // Serial2, PCB S3
constexpr uint8_t UART_PORT_AUDIO = 2;  // shared with the dome link -- S2 RX only, no spare TX

// UART1 (Serial1)  --  Drive backend (hoverboard motor controller, Gen2.x protocol, PCB S1)
// This board's UART to the drive backend is locked by PCB trace to hoverboard (one UART, no spares).
// See ADR 0029 (amended 2026-08-26): artoo-esp32's capability set is {hoverboard}, forced by topology.
constexpr uint8_t PIN_DRIVE_TX = 16;
constexpr uint8_t PIN_DRIVE_RX = 17;

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

// -----------------------------------------------------------------------------
// UART controller allocation (the Arduino HardwareSerial index, not a GPIO).
//
// The ESP32-P4 has five HP UART controllers (SOC_UART_HP_NUM = 5, the Arduino
// core's soc/esp32p4/soc_caps.h) plus one LP_UART the spec sheet rules out on
// this board. Five is the reason this chip was chosen, so the allocation is
// one controller per consumer rather than the share three controllers force on
// artoo-esp32:
//
//   UART0  IDF console (CONFIG_ESP_CONSOLE_UART_NUM=0; Serial is USB CDC here)
//   UART1  drive backend            UART_PORT_DRIVE
//   UART2  dome link, permanently   UART_PORT_DOME
//   UART3  audio module, TX and RX  UART_PORT_AUDIO
//   UART4  unclaimed by the firmware (borrowed by bringup/p4_rt_bench.cpp)
//
// This costs no GPIO. UART0-UART4 route TX/RX to any pin through the GPIO
// matrix (spec sheet "UART Lane Plan"), so audio keeps the two pins it already
// owns and no RC channel or analog lane moves. The Lane Plan's suggestion of
// GPIO32/33 for UART3 is advice for picking pins fresh, not a constraint --
// those are RC channels 5 and 6 on this board (#254).
// -----------------------------------------------------------------------------
constexpr uint8_t UART_PORT_DRIVE = 1;
constexpr uint8_t UART_PORT_DOME  = 2;
constexpr uint8_t UART_PORT_AUDIO = 3;

// UART1 — Drive backend (default: hoverboard motor controller, Gen2.x protocol)
// firebeetle2 has the UART headroom artoo-esp32 lacks: this is this board's
// default wiring, not a universal fact. A different serial drive backend
// (e.g. a Sabertooth/Cytron-class motor driver) could be wired here instead
// on a given build. See ADR 0029's 2026-08-26 amendment.
// From spec sheet "Recommended allocation": UART1 = GPIO20/21
// Cost: ADC1_CHANNEL4/5 (per spec sheet: "Default first lane if no analog input")
constexpr uint8_t PIN_DRIVE_TX = 20;  // UART1_TX per spec sheet §Recommended allocation
constexpr uint8_t PIN_DRIVE_RX = 21;  // UART1_RX per spec sheet §Recommended allocation

// UART2 — Dome control link
// From spec sheet "Recommended allocation": UART2 = GPIO22/23
// Cost: ADC1_CHANNEL6/7 (per spec sheet: "Default second lane if no analog input")
constexpr uint8_t PIN_DOME_TX = 22;  // UART2_TX per spec sheet §Recommended allocation
constexpr uint8_t PIN_DOME_RX = 23;  // UART2_RX per spec sheet §Recommended allocation

// Audio UART — DY-SV5W module, on UART_PORT_AUDIO above
// From spec sheet: GPIO34/36 are strapping pins (P3), usable via GPIO matrix with
// unburnt eFuses. Both directions are real hardware UART on this board: TX on
// GPIO34 and RX on GPIO36 are two ends of one dedicated controller, not a
// bit-bang output plus a borrowed RX (PA_CAP_DEDICATED_AUDIO_UART, #254).
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

// I2C
// From spec sheet §Exposed GPIO table (lines 908-909): GPIO8 is "Board default SCL",
// GPIO7 is "Board default SDA". Header table (lines 826-827): J7 = 8/SCL, J1 = 7/SDA.
constexpr uint8_t PIN_I2C_SCL = 8;   // I2C clock, board default
constexpr uint8_t PIN_I2C_SDA = 7;   // I2C data, board default

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

// FireBeetle 2 pin coherence guards — constexpr-driven inventory-driven checks.
//
// These checks verify two invariants from include/firebeetle_required_pins.inc:
// 1. All production GPIO pins are distinct (no duplicates).
// 2. All production GPIO pins are routed by the DFR1237 shield to IO headers.
//
// DESIGN: Adding a row to the inventory automatically gains two static_assert
// checks (routing + uniqueness) with zero new code. kFirebeetleProductionPins
// is built from the inventory at compile time, and constexpr predicates iterate
// both arrays to generate the checks per row. kFirebeetleRoutedPins is a fixed
// board fact (the GPIO the DFR1237 physically routes) and does not grow.

constexpr uint8_t kFirebeetleProductionPins[] = {
#define PA_FIREBEETLE_REQUIRED_PIN(pin, diagnostic) pin,
#include "firebeetle_required_pins.inc"
#undef PA_FIREBEETLE_REQUIRED_PIN
};

constexpr uint8_t kFirebeetleRoutedPins[] = {
    4, 5, 7, 8, 20, 21, 22, 23, 28, 29, 30, 31, 32, 33, 34, 36, 48, 49, 50, 51, 52,
};

constexpr bool firebeetlePinIsRouted(uint8_t pin) {
    for (uint8_t routed : kFirebeetleRoutedPins) {
        if (routed == pin) { return true; }
    }
    return false;
}

constexpr int firebeetlePinUseCount(uint8_t pin) {
    int uses = 0;
    for (uint8_t assigned : kFirebeetleProductionPins) {
        if (assigned == pin) { ++uses; }
    }
    return uses;
}

// Per-row guard expansion: produces two static_asserts per inventory line.
#define PA_FIREBEETLE_REQUIRED_PIN(pin, diagnostic)                              \
    static_assert(firebeetlePinIsRouted(pin),                                    \
        "firebeetle2: " #pin " is assigned to a GPIO the DFR1237 does not route" \
        " to the IO headers");                                                   \
    static_assert(firebeetlePinUseCount(pin) == 1,                               \
        "firebeetle2: " #pin " shares its GPIO with another production"          \
        " peripheral");
#include "firebeetle_required_pins.inc"
#undef PA_FIREBEETLE_REQUIRED_PIN


#else
  #error "PA_BOARD value not recognized in pin-map selection"
#endif  // PA_BOARD

// -----------------------------------------------------------------------------
// UART controller allocation coherence guards.
//
// Highest HP UART controller index each chip target exposes, from the Arduino
// core's soc_caps.h: SOC_UART_HP_NUM is 3 on ESP32 and 5 on ESP32-P4, so the
// last valid index is 2 and 4 respectively. Duplicated here rather than
// included because config.h is read by the plain-host probes in
// test/test_tools/, which have no chip headers on the include path. Without
// this bound a board claiming a controller its chip does not have compiles
// clean and fails only at runtime: HardwareSerial::begin() rejects
// _uart_nr >= SOC_UART_NUM with a log_e and returns, so the lane is simply
// silent. (No line cite: the two chip targets pin different Arduino core
// versions, so that guard sits at a different line in each.)
//
// `#if defined` rather than `#if`: PA_CHIP_TARGET_* are presence macros defined
// only for the selected chip, not 0/1 gates -- see "Chip target mapping" above.
#if defined(PA_CHIP_TARGET_ESP32P4)
constexpr uint8_t UART_PORT_MAX = 4;
#elif defined(PA_CHIP_TARGET_ESP32)
constexpr uint8_t UART_PORT_MAX = 2;
#else
  #error "UART_PORT_MAX has no value for this chip target: add a branch above carrying that chip's SOC_UART_HP_NUM - 1, next to its entry in the Chip target mapping ladder"
#endif

static_assert(UART_PORT_DRIVE <= UART_PORT_MAX,
    "UART_PORT_DRIVE names a UART controller this chip target does not have");
static_assert(UART_PORT_DOME <= UART_PORT_MAX,
    "UART_PORT_DOME names a UART controller this chip target does not have");
static_assert(UART_PORT_AUDIO <= UART_PORT_MAX,
    "UART_PORT_AUDIO names a UART controller this chip target does not have");

// UART0 is the console on both chip targets and is never a firmware lane.
static_assert(UART_PORT_DRIVE != 0 && UART_PORT_DOME != 0 && UART_PORT_AUDIO != 0,
    "UART0 is the console lane and must not be allocated to a firmware consumer");

// The drive lane is never shared with anything.
static_assert(UART_PORT_DRIVE != UART_PORT_DOME && UART_PORT_DRIVE != UART_PORT_AUDIO,
    "the drive backend must own its UART controller outright");

// The audio module borrows the dome link's controller EXACTLY when the board
// does not give it one of its own. This is the guard that stops the two facts
// drifting apart: flipping PA_CAP_DEDICATED_AUDIO_UART without moving
// UART_PORT_AUDIO would gate the ownership handoff out while both consumers
// still sat on one controller -- a runtime UART collision that presents as an
// audio lane that intermittently answers. Here it is a build error (#254).
static_assert((UART_PORT_AUDIO == UART_PORT_DOME) == (PA_CAP_DEDICATED_AUDIO_UART == 0),
    "PA_CAP_DEDICATED_AUDIO_UART must agree with the UART controller allocation:"
    " capability 0 means audio shares UART_PORT_DOME, capability 1 means it does not");

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
// Task stacks (chip-target specific)
// -----------------------------------------------------------------------------
// EVERY project-created task has a Measured Chain and a compile-enforced floor
// here, on both chip arms (ADR 0038). Thirteen of them: the ten created in
// src/main.cpp, plus WebEvents and the ArduinoOTA task (src/web/web_server.cpp)
// and HostedRecovery (src/web/web_network_manager_hosted.cpp, which exists only
// where PA_CAP_HOSTED_WIFI is 1, so twelve tasks on artoo-esp32 and thirteen on
// the ESP32-P4). loopTask is sized by ARDUINO_LOOP_STACK_SIZE in platformio.ini
// and stays outside.
//
// The chain is a `*_MEASURED_CHAIN_BYTES` constant rather than a number in this
// comment, and `static_assert(*_STACK_BYTES >= *_MEASURED_CHAIN_BYTES)` below
// makes a stack that no longer covers its own measurement fail at the
// declaration. The recipe that produced each chain -- environment, root
// symbols, the frames stitched by hand across an indirect call, the
// profiler-image substitution -- is tools/task_stack_recipes.json, and
// tools/check_task_stack_chains.py re-walks every one of them from a linked
// image, so a slice that deepens a chain past its constant fails there instead
// of on a board. That is the half #226 found the expensive way: the assert
// stops the CONSTANT being trimmed; only the re-walk notices the CHAIN growing.
//
// Task stacks differ per chip target. The cause is not the boards, and it is
// not a general "RISC-V frames are wider": the deepest call chain under several
// of these tasks runs through newlib, whose float-formatting frames are much
// wider on RISC-V (_svfprintf_r 800 -> 1152 B, _dtoa_r 160 -> 416) while the
// P4's allocator frames are smaller and partly cancel it (#245).
//
// SIZING RULE: the stack holds the measured worst-case static chain plus 25%,
// rounded up to the next 512 bytes. Two things make that a rule rather than a
// preference:
//
//  - It reproduces, from the measurement alone, the size #245 arrived at by
//    judgement: that chain is 3152 B, and 3152 * 1.25 = 3940 -> 4096.
//  - 25% of each chain here is at least 800 B, which covers the interrupt cost
//    the chain figures deliberately exclude. The RISC-V exception frame is
//    RV_STK_FRMSZ = 160 B (37 words aligned to 16, riscv/rvruntime-frames.h),
//    and vectors.S allocates it with save_general_regs on the *interrupted
//    task's* stack before any switch to the ISR stack -- so a nested pair of
//    interrupts costs 320 B here, on top of every number below.
//
// #248 raised DomeTask (3072 was 208 B SHORT of its P4 chain), AuxLedTask and
// SafetyMonitor on the ESP32-P4 by that rule; their chains are the constants
// below now, re-walked at this tip rather than restated from that ticket.
//
// Every chain is a LOWER bound: indirect calls are not followed, and a cycle in
// the call graph is cut. Read the margin as cover for what the measurement
// cannot see, not as slack to spend.
//
// The Xtensa measurement is much weaker than the RISC-V one: objdump emits
// ~37% of the artoo image's function bodies as data rather than instructions
// (2530 of roughly 6800 at this tip -- tools/check_task_stack_chains.py prints
// the exact figure for the image it just read, which moves by a function or two
// between builds), so any artoo chain crossing one is truncated, while the
// ESP32-P4 image decodes whole (none of roughly 7450). Artoo numbers can prove
// an overrun and cannot prove a margin. That asymmetry is exactly what makes
// the re-walk safe to fail a build on: it can MISS growth and cannot report
// FALSE growth.
//
// Which arms get the rule, and why the two chips answer differently:
//
//  - ESP32-P4: every arm is exactly the rule applied to its own chain. The
//    board has the free heap to buy the margin, and #245/#248/#250/#256 already
//    put eight of them there.
//  - artoo-esp32: five arms are the rule, two sit ABOVE it because an earlier
//    ticket deliberately raised them past it, and five DECLINE it on #248's
//    reason -- raising all five costs 5632 B against ~42.7 KB of measured free
//    heap, for margin the Xtensa walk cannot confirm. Each decline is recorded
//    beside its constant. Declining the rule never declines the floor: every
//    arm still covers its own chain, and the static_asserts below are what say
//    so.
//
// `#if defined` rather than `#if`: PA_CHIP_TARGET_* are presence macros defined
// only for the selected chip (see "Chip target mapping" above), not 0/1 Board
// Capability Gates, so `#if` on the undefined one would silently take the wrong
// branch. Keying on the chip target rather than on PA_BOARD also means a second
// board variant on either chip inherits the right size without a new case here.
// DriveTask and DomeLinkTask were sized the same way and for the same reason
// (#250): both exceeded their old stacks on ESP32-P4, and on ESP32 DriveTask
// was at risk.
//
// Why the two chips diverge here at all: DomeLinkTask's own frame is 2256 B on
// RISC-V against far less on Xtensa, because GCC splits an allocation past
// 2032 B into two `addi sp,sp,-N` instructions -- the same split that hid this
// overrun until tools/stack_usage_report.py was taught to accumulate them.
//
// ⚠️ DriveTask's ESP32 arm was raised past its own figure deliberately: that
// figure read as 32 B under the old 4096 and is the floor of an unknown, not
// headroom, so the 50 Hz drive loop is raised on both chips rather than only
// where an overrun is provable (#250). DomeLinkTask's ESP32 arm was held at
// 6144 by the same tight-heap argument that holds it there now.
//
// RCInputTask, AudioTask and WebEvents were sized the same way (#256). These
// three were still single-valued artoo-era literals. WebEvents is the one that
// moved: its own comment already named the risk -- 4096 overflowed on ESP32 in
// _dtoa_r, and that frame is 160 -> 416 B on RISC-V -- and the P4 chain sat
// past the inherited 6144 before the 25% margin.
//
// ESP32 WebEvents was recorded from the profiler image at #256, because the
// product image's body was emitted as data (.xt.prop) then. It decodes in both
// images at this tip and they agree, so the arm is walked from the product
// image now; AudioTask's ESP32 arm is still the profiler image, which is the
// deeper of the two. Both substitutions are recorded per arm in
// tools/task_stack_recipes.json rather than only here.
//
// ConsoleTask, sized the same way (#226). It is the only stack in this block
// whose under-size was reproduced as a device fault rather than inferred from a
// walk, and it was reproduced on BOTH boards: `system.config.log-level
// value=debug` over the serial Console Adapter reboots the FireBeetle 2 with a
// RISC-V "Stack protection fault" (SP 476 B below the 5120 B bounds) and the
// artoo-esp32 with the Xtensa spelling of the same event, "Stack canary
// watchpoint triggered (Console)". The same write over HTTP answers normally on
// both boards: it shares configApply()/configCommitApplied() and every
// ConfigSnapshot copy below them, and differs only in the task it runs on --
// the web server task has 8 KB.
//
// So this is not the P4 chain divergence the rest of this block is about. The
// literal was justified by a measured high-water mark, which is exactly the
// evidence that cannot see a path that has not run yet, and no config write
// could reach this task until the write path landed. The firmware's own
// instrumentation says both boards were already close after one trivial
// command: 1448 B free on artoo-esp32, 900-1124 B on the FireBeetle 2.
//
//                   old    ESP32 chain    ESP32-P4 chain
//   ConsoleTask    5120       9008           9120       <- both chips over, by ~4 KB
//
// The cause is frame depth, and it is provable without leaving project code:
// consoleTask 320 + embeddedCliProcess 80 + onCliCommand 64 +
// consoleExecuteCommand 1888 + consoleWriteScalarConfigField 2064 +
// configCommitApplied 320 + commandedSetStationary 1264 = 6000 B on ESP32
// (6048 on ESP32-P4) before one byte of newlib or ESP-IDF. Not recursion, not a
// VLA, not alloca: every frame on the chain is reported fixed, and the only
// cycles the walk cuts sit in the ESP-IDF heap and log tail underneath it -- a
// cut edge makes the reported total a LOWER bound, so it cannot be where the
// number came from. Three nested frames on the config-write path each carried a
// ConfigSnapshot (944 B) by value --
// consoleWriteScalarConfigField's `working` plus the ConfigCommitOutcome it got
// back (944 + 948 in one frame), and commandedSetStationary's `cfg` -- on top of
// consoleExecuteCommand's own 1888 B and the ~2.3 KB newlib tail that every
// PA_LOG_* from this task pays through embedded-cli's print path.
//
// Reproducing it needs the two halves stitched by hand, because embedded-cli
// reaches the command callback through `cli->onCommand`, an indirect call the
// walker does not follow:
//
//   export PLATFORMIO_BUILD_SRC_FLAGS="-Wall -Wextra -Werror -fstack-usage"
//   make build BUILD_ENV=<env>
//   python3 tools/stack_usage_report.py --env <env> --root onCliCommand
//   python3 tools/stack_usage_report.py --env <env> --root consoleTask --frames embeddedCliProcess
//
// chain = onCliCommand total + consoleTask frame + embeddedCliProcess frame
// (8608 + 320 + 80 on ESP32; 8688 + 336 + 96 on ESP32-P4 when the panic was
// diagnosed). embeddedCliProcess's frame already contains parseCommand and
// onControlInput, which GCC inlines into it -- both are ABSENT as symbols, which
// is what confirms it rather than a missing measurement.
//
// Where on the chain the peak sat, which the bench observed independently: the
// value read back unchanged after the reboot, so the frame blew before the NVS
// write committed. The walk said the same thing -- the deepest point was
// commandedSetStationary and the log emit under it, which configCommitApplied()
// reaches before it opens Preferences. No configuration was ever half-applied by
// that fault.
//
// The raise was the correct first move and was never the resting state: it paid
// the rule on a chain carrying 1892 B of snapshot copies that did not have to be
// there. ADR 0011's 2026-09-04 amendment took them out (#269) -- the Commit Step
// writes its post-commit snapshot back through `working` instead of returning
// one, and the Commanded Mode setters sync the config cache by field instead of
// round-tripping the whole snapshot -- and the chain fell again with them.
//
// The two frames that lost a snapshot each: consoleWriteScalarConfigField
// 2064 -> 1104 (1120 on ESP32-P4) and commandedSetStationary 1264 -> 320. The
// walk's deepest branch is no longer the config write at all -- it now runs
// through the RC trigger dispatch the Console action executor shares
// (processTriggerAction and the newlib tail below it), which is why the chain
// falls by less than the 1904 B those two frames gave back.
//
// The constants below are re-derived from a walk at this tip, not carried from
// either branch: #269 measured 7568/7552 on its own branch and #270 measured
// 8512 on its own, both cutting the same deepest branch, so neither figure
// describes the merged tree -- it is shorter than either. The two chips land on
// the same 512-byte step, which is coincidence and not a reason to merge the
// arms: the chains still differ, and the next field on either side moves them
// independently.
//
// Why the standard margin here and not a smaller one: the chains are LOWER
// bounds in the same two ways the raised ones were -- objdump emits Xtensa bodies
// as data, and the walk cuts cycles in the ESP-IDF heap and log tail -- so 25% is
// buying headroom against what the tool cannot see, not against what it measured.
// The one shrink that was available and rejected at the time, hoisting `working`
// and the commit outcome into the module's static area, stays rejected: 1892 B of
// .bss to save stack was never the trade to make when the copies themselves could
// go, and they now have.
//
// The Console pays the rule on artoo-esp32 where DomeLinkTask above declines it,
// and the difference is the evidence, not the size: DomeLinkTask's raise would
// buy margin no measurement could confirm, while the Console's under-size was two
// reboots on two boards. The same tight-heap argument points the other way.
//
// One block for every per-chip task stack. #248 and #250 each added a pair and
// arrived here by separate branches; keeping two adjacent, identical #if ladders
// would mean a third ticket adds a third, and a reader has to check all of them
// to answer "what is this task's stack on this chip".
//
// Each arm carries the task's chain and its stack, in that order, with the
// derivation in the trailing comment: `rule` where the stack is exactly the
// chain by the rule, `above rule` where an earlier ticket deliberately went
// further, and `rule declined` with the reason where the arm pays the floor
// only. Sorted the same way on both arms so the two are diffable side by side.
#if defined(PA_CHIP_TARGET_ESP32P4)
// Every arm below is exactly the rule applied to its own chain.
constexpr uint32_t DRIVE_TASK_MEASURED_CHAIN_BYTES = 4368;
constexpr uint32_t DRIVE_TASK_STACK_BYTES = 5632;  // rule: 4368 -> 5460 -> 5632
constexpr uint32_t RC_INPUT_TASK_MEASURED_CHAIN_BYTES = 5360;
constexpr uint32_t RC_INPUT_TASK_STACK_BYTES = 7168;  // rule: 5360 -> 6700 -> 7168
constexpr uint32_t SERVO_TASK_MEASURED_CHAIN_BYTES = 3488;
constexpr uint32_t SERVO_TASK_STACK_BYTES = 4608;  // rule: 3488 -> 4360 -> 4608
constexpr uint32_t DOME_TASK_MEASURED_CHAIN_BYTES = 3280;
constexpr uint32_t DOME_TASK_STACK_BYTES = 4608;  // rule: 3280 -> 4100 -> 4608
constexpr uint32_t AUDIO_TASK_MEASURED_CHAIN_BYTES = 4848;
constexpr uint32_t AUDIO_TASK_STACK_BYTES = 6144;  // rule: 4848 -> 6060 -> 6144
constexpr uint32_t AUX_LED_TASK_MEASURED_CHAIN_BYTES = 3984;
constexpr uint32_t AUX_LED_TASK_STACK_BYTES = 5120;  // rule: 3984 -> 4980 -> 5120
constexpr uint32_t DOME_LINK_TASK_MEASURED_CHAIN_BYTES = 7360;
constexpr uint32_t DOME_LINK_TASK_STACK_BYTES = 9216;  // rule: 7360 -> 9200 -> 9216
constexpr uint32_t SAFETY_MONITOR_MEASURED_CHAIN_BYTES = 3184;
constexpr uint32_t SAFETY_MONITOR_STACK_BYTES = 4096;  // rule: 3184 -> 3980 -> 4096
constexpr uint32_t SEQ_DISPATCHER_TASK_MEASURED_CHAIN_BYTES = 4448;
constexpr uint32_t SEQ_DISPATCHER_TASK_STACK_BYTES = 5632;  // rule: 4448 -> 5560 -> 5632
constexpr uint32_t CONSOLE_TASK_MEASURED_CHAIN_BYTES = 7136;
constexpr uint32_t CONSOLE_TASK_STACK_BYTES = 9216;  // rule: 7136 -> 8920 -> 9216
constexpr uint32_t WEB_EVENTS_TASK_MEASURED_CHAIN_BYTES = 5808;
constexpr uint32_t WEB_EVENTS_TASK_STACK_BYTES = 7680;  // rule: 5808 -> 7260 -> 7680
constexpr uint32_t OTA_TASK_MEASURED_CHAIN_BYTES = 4000;
constexpr uint32_t OTA_TASK_STACK_BYTES = 5120;  // rule: 4000 -> 5000 -> 5120
// HostedRecovery exists only where PA_CAP_HOSTED_WIFI is 1, which today is this
// chip alone (src/web/web_network_manager_hosted.cpp is whole-file guarded on
// it), so its pair is declared on this arm only. A future board on another chip
// that turns the capability on fails at the static_assert below rather than
// inheriting a number measured on someone else's silicon.
constexpr uint32_t HOSTED_RECOVERY_TASK_MEASURED_CHAIN_BYTES = 3648;
constexpr uint32_t HOSTED_RECOVERY_TASK_STACK_BYTES = 4608;  // rule: 3648 -> 4560 -> 4608
#elif defined(PA_CHIP_TARGET_ESP32)
constexpr uint32_t DRIVE_TASK_MEASURED_CHAIN_BYTES = 4080;
// above rule (5120): #250 raised the 50 Hz drive loop on both chips rather than
// only where an overrun is provable, because this figure is the floor of an
// unknown. Not lowered to the rule here -- that would undo that decision.
constexpr uint32_t DRIVE_TASK_STACK_BYTES = 5632;
constexpr uint32_t RC_INPUT_TASK_MEASURED_CHAIN_BYTES = 5248;
// above rule (6656): the pre-#256 literal, kept rather than lowered onto a
// Xtensa figure that can prove an overrun and cannot prove a margin.
constexpr uint32_t RC_INPUT_TASK_STACK_BYTES = 7168;
constexpr uint32_t SERVO_TASK_MEASURED_CHAIN_BYTES = 3200;
constexpr uint32_t SERVO_TASK_STACK_BYTES = 4096;  // rule: 3200 -> 4000 -> 4096
constexpr uint32_t DOME_TASK_MEASURED_CHAIN_BYTES = 2992;
// rule declined (4608, +1536 B): #248's tight-heap reason. This is the thinnest
// floor in the block -- 80 B on a lower-bound walk, which is under the cost of
// one interrupt entry -- and it is the pre-existing shipping value, recorded
// here as a known exposure rather than raised by this ticket (#271).
constexpr uint32_t DOME_TASK_STACK_BYTES = 3072;
constexpr uint32_t AUDIO_TASK_MEASURED_CHAIN_BYTES = 4672;
constexpr uint32_t AUDIO_TASK_STACK_BYTES = 6144;  // rule: 4672 -> 5840 -> 6144
constexpr uint32_t AUX_LED_TASK_MEASURED_CHAIN_BYTES = 3504;
// rule declined (4608, +512 B): #248's tight-heap reason. Floor holds by 592 B.
constexpr uint32_t AUX_LED_TASK_STACK_BYTES = 4096;
constexpr uint32_t DOME_LINK_TASK_MEASURED_CHAIN_BYTES = 5872;
// rule declined (7680, +1536 B): #248's tight-heap reason, named on #250. Floor
// holds by 272 B.
constexpr uint32_t DOME_LINK_TASK_STACK_BYTES = 6144;
constexpr uint32_t SAFETY_MONITOR_MEASURED_CHAIN_BYTES = 3088;
// rule: 3088 -> 3860 -> 4096. Raised from 3072 by #271, and this is the one arm
// in the block where the floor did NOT already hold: the artoo profiler image
// (PA_LOG_LEVEL=4, PA_HEAP_PROFILE=1 -- the image you flash when the board is
// already misbehaving) walks 3088 B here against the product image's 2944, and
// the constant is compiled into both. #245 sized the ESP32-P4 arm from the same
// deeper image for the same reason. A floor that fails is not the margin
// question #248 declined; it is an overrun, so the rule is paid.
constexpr uint32_t SAFETY_MONITOR_STACK_BYTES = 4096;
constexpr uint32_t SEQ_DISPATCHER_TASK_MEASURED_CHAIN_BYTES = 4336;
constexpr uint32_t SEQ_DISPATCHER_TASK_STACK_BYTES = 5632;  // rule: 4336 -> 5420 -> 5632
constexpr uint32_t CONSOLE_TASK_MEASURED_CHAIN_BYTES = 7024;
constexpr uint32_t CONSOLE_TASK_STACK_BYTES = 9216;  // rule: 7024 -> 8780 -> 9216
constexpr uint32_t WEB_EVENTS_TASK_MEASURED_CHAIN_BYTES = 5888;
// rule declined (7680, +1536 B): #248's tight-heap reason, named on #256. Floor
// holds by 256 B.
constexpr uint32_t WEB_EVENTS_TASK_STACK_BYTES = 6144;
constexpr uint32_t OTA_TASK_MEASURED_CHAIN_BYTES = 3696;
// rule declined (5120, +1024 B): #248's tight-heap reason, applied to this
// task's first measurement (#271). Floor holds by 400 B.
constexpr uint32_t OTA_TASK_STACK_BYTES = 4096;
#else
  #error "task stack sizes have no value for this chip target"
#endif

// The floor is compile-enforced rather than promised by the comment above,
// because a comment is what let ConsoleTask stand 4 KB below its own chain until
// it took both boards down (#226). A later edit that lowers a stack below its
// chain, or raises a chain past its stack, fails at the declaration -- on both
// chips, in every environment that includes this header.
//
// This is half the guard. It fixes the constant to the chain; nothing here can
// notice the CHAIN growing, because the chain is itself a recorded number. That
// half is tools/check_task_stack_chains.py, which re-walks every recipe in
// tools/task_stack_recipes.json against a linked image (ADR 0038).
static_assert(DRIVE_TASK_STACK_BYTES >= DRIVE_TASK_MEASURED_CHAIN_BYTES,
              "DRIVE_TASK_STACK_BYTES is below DriveTask's measured worst-case static chain");
static_assert(RC_INPUT_TASK_STACK_BYTES >= RC_INPUT_TASK_MEASURED_CHAIN_BYTES,
              "RC_INPUT_TASK_STACK_BYTES is below RCInputTask's measured worst-case static chain");
static_assert(SERVO_TASK_STACK_BYTES >= SERVO_TASK_MEASURED_CHAIN_BYTES,
              "SERVO_TASK_STACK_BYTES is below ServoTask's measured worst-case static chain");
static_assert(DOME_TASK_STACK_BYTES >= DOME_TASK_MEASURED_CHAIN_BYTES,
              "DOME_TASK_STACK_BYTES is below DomeTask's measured worst-case static chain");
static_assert(AUDIO_TASK_STACK_BYTES >= AUDIO_TASK_MEASURED_CHAIN_BYTES,
              "AUDIO_TASK_STACK_BYTES is below AudioTask's measured worst-case static chain");
static_assert(AUX_LED_TASK_STACK_BYTES >= AUX_LED_TASK_MEASURED_CHAIN_BYTES,
              "AUX_LED_TASK_STACK_BYTES is below AuxLedTask's measured worst-case static chain");
static_assert(DOME_LINK_TASK_STACK_BYTES >= DOME_LINK_TASK_MEASURED_CHAIN_BYTES,
              "DOME_LINK_TASK_STACK_BYTES is below DomeLinkTask's measured worst-case static "
              "chain");
static_assert(SAFETY_MONITOR_STACK_BYTES >= SAFETY_MONITOR_MEASURED_CHAIN_BYTES,
              "SAFETY_MONITOR_STACK_BYTES is below SafetyMonitorTask's measured worst-case static "
              "chain");
static_assert(SEQ_DISPATCHER_TASK_STACK_BYTES >= SEQ_DISPATCHER_TASK_MEASURED_CHAIN_BYTES,
              "SEQ_DISPATCHER_TASK_STACK_BYTES is below SequenceDispatcherTask's measured "
              "worst-case static chain");
static_assert(CONSOLE_TASK_STACK_BYTES >= CONSOLE_TASK_MEASURED_CHAIN_BYTES,
              "CONSOLE_TASK_STACK_BYTES is below the Console task's measured "
              "worst-case static chain");
static_assert(WEB_EVENTS_TASK_STACK_BYTES >= WEB_EVENTS_TASK_MEASURED_CHAIN_BYTES,
              "WEB_EVENTS_TASK_STACK_BYTES is below the WebEvents task's measured worst-case "
              "static chain");
static_assert(OTA_TASK_STACK_BYTES >= OTA_TASK_MEASURED_CHAIN_BYTES,
              "OTA_TASK_STACK_BYTES is below the ArduinoOTA task's measured worst-case static "
              "chain");
#if PA_CAP_HOSTED_WIFI
static_assert(HOSTED_RECOVERY_TASK_STACK_BYTES >= HOSTED_RECOVERY_TASK_MEASURED_CHAIN_BYTES,
              "HOSTED_RECOVERY_TASK_STACK_BYTES is below the HostedRecovery task's measured "
              "worst-case static chain");
#endif

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
//
// Per Board Variant (#242): two controllers on one LAN must not contest the
// same mDNS name, and Makefile's `OTA_IP ?= artoo.local` default must not be
// able to resolve to the wrong board. artoo-esp32 keeps "artoo" unchanged --
// existing bookmarks, that Makefile default, and docs/troubleshooting.md's
// http://artoo.local all stay correct. This changes only the default; the
// Droid Name override (system.mdns_use_name, see configResolvedMdnsHostname()
// in src/config_store.cpp) is unaffected.
#if PA_BOARD == PA_BOARD_ARTOO_ESP32
constexpr char WIFI_MDNS_HOST[] = "artoo";
#elif PA_BOARD == PA_BOARD_FIREBEETLE2
constexpr char WIFI_MDNS_HOST[] = "firebeetle2";
#else
  #error "PA_BOARD value not recognized in mDNS hostname selection"
#endif
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
// Set via -DPA_LOG_LEVEL=N in platformio.ini build_flags, per environment.
// This is only the boot default until NVS config loads; the runtime level is the
// operator's saved logLevel (Setup page).
//
// Required, not defaulted (#244). This used to fall back to DEBUG when unset, so an
// environment that forgot to declare it shipped verbose logging silently -- extra
// serial output, timing cost and flash, with nothing to say why. Every environment
// now declares its own value, and a missing one is a build error rather than a quiet
// wrong image. Same reasoning as the PA_BOARD guard above.
#if !defined(PA_LOG_LEVEL)
  #error "PA_LOG_LEVEL must be defined by platformio.ini build_flags for this environment"
#endif

// Build Feature Flag (ADR 0029), always 0 or 1 and tested with #if. Required for the
// same reason as PA_LOG_LEVEL: it is consumed as `#if PA_HEAP_PROFILE`
// (include/api_profiler.h, src/web/api_profiler.cpp), and an undefined macro there
// evaluates to 0 silently -- the profiler would simply vanish from a build that meant
// to have it, with no diagnostic (#244).
#if !defined(PA_HEAP_PROFILE)
  #error "PA_HEAP_PROFILE must be defined (0 or 1) by platformio.ini build_flags for this environment"
#endif
