// =============================================================================
// include/config.h
//
// GPIO pin assignments and compile-time constants for protoArtoo.
// Supports multiple controller board variants on different chip targets.
// See docs/pin_map.md and docs/adr/0028-two-layer-board-abstraction.md
//
// artoo-esp32 PCB serial port legend, read off that board's silkscreen. It is
// one Board Variant's wiring, not a project-wide fact: firebeetle2 has no S1,
// S2 or S3 header and routes all three signals to other GPIO. Where a signal
// is routed on the board being built is a Board Lane (include/board_lanes.inc),
// and the silkscreen text is a Board Component Label (include/component_labels.inc).
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
  // The guard below reads CONFIG_HEAP_TRACING, which only sdkconfig.h defines.
  // Without it the macro reads as 0 and the build #errors with tracing ON (#467).
  #include <sdkconfig.h>
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
// artoo-esp32: artoo.uk Artoo Controller PCB on a classic-generation dual-header ESP32 board clone
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
// Servo outputs (LEDC PWM), named by what the Artoo PCB prints beside each
// (include/component_labels.inc; ADR 0033 Amendment 2026-09-19)
// ARM1 = Utility arm servo #1  --  Top / Left arm (GPIO 23)
// ARM2 = Utility arm servo #2  --  Bottom / Right arm (GPIO 5)
// ARM3 = Servo output that can carry the LED strip (GPIO 19)
// ARM4 = Servo output that can carry the LED strip (GPIO 18)
// ARM5 = Servo output that can carry the LED strip (GPIO 32)
// DOME = Dome rotation ESC (GPIO 25)  --  a brushed-motor ESC, not a servo
// -----------------------------------------------------------------------------
constexpr uint8_t PIN_ARM1_SERVO = 23;
constexpr uint8_t PIN_ARM2_SERVO = 5;
constexpr uint8_t PIN_ARM3_SERVO = 19;  // ARM3  --  can carry the LED strip
constexpr uint8_t PIN_ARM4_SERVO = 18;  // ARM4  --  can carry the LED strip
constexpr uint8_t PIN_ARM5_SERVO = 32;  // ARM5  --  can carry the LED strip
constexpr uint8_t PIN_DOME_ESC = 25;

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
//   UART4  unclaimed by the firmware (borrowed by bench/p4_rt_bench.cpp)
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
// Allocation: the first two Outputs on LDO-backed pins (49-50 on VDD_IO_6). The
// three that can carry the optional WS2812B strip (include/board_outputs.h
// `lightCapable`) use non-LDO main IO where they can, to avoid placing
// a high-frequency timing-critical line on unmeasured LDO rails. GPIO 4 and 5
// cost JTAG, which is acceptable post-debug. Each is named by the GPIO number
// the shield prints (include/component_labels.inc; ADR 0033 Amendment 2026-09-19).
constexpr uint8_t PIN_ARM1_SERVO = 49;  // LEDC PWM, LDO caution (VDD_IO_6), ADC2_CHANNEL0
constexpr uint8_t PIN_ARM2_SERVO = 50;  // LEDC PWM, LDO caution (VDD_IO_6), ADC2_CHANNEL1
constexpr uint8_t PIN_ARM3_SERVO = 4;   // GPIO 4, WS2812B strip capable, P3 JTAG MTMS (post-debug)
constexpr uint8_t PIN_ARM4_SERVO = 5;   // GPIO 5, WS2812B strip capable, P3 JTAG MTDO (post-debug)
constexpr uint8_t PIN_ARM5_SERVO = 51;  // GPIO 51, WS2812B strip capable, LDO caution (VDD_IO_6)
constexpr uint8_t PIN_DOME_ESC = 48;    // ESC PWM, LDO caution (VDD_IO_5)

// I2C
// From spec sheet §Exposed GPIO table (lines 908-909): GPIO8 is "Board default SCL",
// GPIO7 is "Board default SDA". Header table (lines 826-827): J7 = 8/SCL, J1 = 7/SDA.
constexpr uint8_t PIN_I2C_SCL = 8;   // I2C clock, board default
constexpr uint8_t PIN_I2C_SDA = 7;   // I2C data, board default

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

// -----------------------------------------------------------------------------
// Board Lane coherence guards (GLOSSARY.md "Board Lane").
//
// Every lane in include/board_lanes.inc is reported to the browser in the
// identity manifest, so an unrouted lane would put PA_PIN_UNASSIGNED (255) on
// an operator's screen as a GPIO number. Fail the build instead: a lane that
// is declared is a lane that is routed. A shared TX/RX pin is the other way a
// lane row can be wrong by construction -- one wire cannot be both ends.
// -----------------------------------------------------------------------------
#define PA_BOARD_LANE(name, uart_port, tx_pin, rx_pin)                             \
    static_assert((tx_pin) != PA_PIN_UNASSIGNED,                                   \
        "board lane " #name " declares an unassigned TX pin");                     \
    static_assert((rx_pin) != PA_PIN_UNASSIGNED,                                   \
        "board lane " #name " declares an unassigned RX pin");                     \
    static_assert((tx_pin) != (rx_pin),                                            \
        "board lane " #name " routes TX and RX to the same GPIO");
#include "board_lanes.inc"
#undef PA_BOARD_LANE

// =============================================================================
// Protocol and Feature Constants (chip-target specific, board-agnostic)
// =============================================================================

// Drive constants
// These constants apply to all chip targets; board-specific pins are defined
// above. A drive controller's own wire settings -- baud, framing, how long its
// far end tolerates a gap -- are not here: they belong to the backend that
// speaks them, in its catalogue row in include/drive_backend.h.
constexpr int16_t SPEED_LIMIT_MAX = 600;  // Absolute max drive output (never exceeded)
constexpr int16_t SPEED_PRESET_SLOW = 200;
constexpr int16_t SPEED_PRESET_NORMAL = 350;
constexpr int16_t SPEED_PRESET_TURBO = SPEED_LIMIT_MAX;
constexpr uint32_t DRIVE_FREQ_HZ = 50;  // Zero-frame continuity rate, every drive backend
// The tick period DriveTask actually sleeps, derived so the number has one
// home: include/drive_backend.h static_asserts it against the active backend's
// declared continuity deadline. Integer division truncates, which errs toward
// a SHORTER period -- feeding the far end sooner than it asked, never later.
constexpr uint16_t DRIVE_FRAME_PERIOD_MS = (uint16_t)(1000 / DRIVE_FREQ_HZ);

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

// The calibration dial's hold on a Servo Output (ADR 0064, #364). While a dial
// has an Output the Part stays driven so the builder can look and listen, and
// firmware bounds that hold in two ways, neither of which a page can extend:
//
//   SERVO_HOLD_EXPIRY_MS   how long after the last hold command for that
//                          Output the pulse comes off. The same shape as
//                          WEB_DRIVE_TIMEOUT_MS above -- firmware observing
//                          arrivals, not a page asserting liveness -- so a
//                          closed lid or a dropped link is caught in seconds.
//                          The page keeps a hold alive at one command a second,
//                          so 3 s is three missed beats, not one late one.
//   SERVO_HOLD_CEILING_MS  the most a dial holds from when it took the Output,
//                          however many commands keep arriving. Ten minutes:
//                          long enough to fight one stubborn linkage, short
//                          enough that a bench left at lunchtime is not driving
//                          a servo all afternoon (ADR 0064's considered options).
//
// Both are judged in ServoTask (include/servo_hold.h is the rule), which
// releases the Output and says why (ServoLimpReason, include/robot_state.h).
// Not configurable on purpose: one more number a builder can set wrong, for a
// bound that has no reason to differ between droids.
constexpr uint32_t SERVO_HOLD_EXPIRY_MS = 3000;
constexpr uint32_t SERVO_HOLD_CEILING_MS = 600000;

// -----------------------------------------------------------------------------
// Watchdog
// -----------------------------------------------------------------------------
constexpr uint32_t WATCHDOG_TIMEOUT_S = 3;  // ESP32 TWDT timeout

// -----------------------------------------------------------------------------
// Task stacks (chip-target specific)
// -----------------------------------------------------------------------------
// EVERY project-created task has a Recorded Chain and a compile-enforced floor,
// on both chip arms (ADR 0040). Thirteen of them: the ten created in
// src/main.cpp, plus WebEvents and the ArduinoOTA task (src/web/web_server.cpp)
// and HostedRecovery (src/web/web_network_manager_hosted.cpp, which exists only
// where PA_CAP_HOSTED_WIFI is 1, so twelve tasks on artoo-esp32 and thirteen on
// the ESP32-P4). loopTask is sized by ARDUINO_LOOP_STACK_SIZE in platformio.ini
// and stays outside.
//
// The figures are not written here. tools/task_stack_recipes.json is their one
// home (ADR 0040, amended 2026-09-27): per task and per chip, the chain the
// product image walks, the task's stack, the reason wherever that stack is not
// what the rule below gives, and why the chain is as deep as it is today.
// include/task_stack_figures.h is generated from it and declares every
// `*_MEASURED_CHAIN_BYTES` and `*_STACK_BYTES` constant for the selected chip
// target. A re-derivation is a command rather than an edit of this file:
//
//   python3 tools/check_task_stack_chains.py --rewrite --chip esp32
//
// builds and walks the chip's product image, records every chain, prints the
// stack the rule wants against the stack each task has and what taking it
// costs, and moves a stack only when told to (--accept): a raise is the
// operator's decision. Without --rewrite the same tool is the slice gate's
// row, re-walking the recipes and failing a chain that outgrew its figure.
//
// Task stacks differ per chip target. The cause is not the boards, and it is
// not a general "RISC-V frames are wider": the deepest call chain under several
// of these tasks runs through newlib, whose float-formatting frames are much
// wider on RISC-V (_svfprintf_r 800 -> 1152 B, _dtoa_r 160 -> 416) while the
// P4's allocator frames are smaller and partly cancel it (#245). The artoo-esp32
// image links newlib nano printf and the ESP32-P4 keeps full newlib, so a
// chain that reaches a formatted log line is shallower on artoo-esp32.
//
// SIZING RULE: the stack holds the measured worst-case static chain plus 25%,
// rounded up to the next 512 bytes (taskStackByTheRule() in the generated
// header). Two things make that a rule rather than a preference:
//
//  - It reproduces, from the measurement alone, the size #245 arrived at by
//    judgement: that chain is 3152 B, and 3152 * 1.25 = 3940 -> 4096.
//  - 25% of every chain here is several hundred bytes, more than the interrupt
//    cost the chain figures deliberately exclude. The RISC-V exception frame is
//    RV_STK_FRMSZ = 160 B (37 words aligned to 16, riscv/rvruntime-frames.h),
//    and vectors.S allocates it with save_general_regs on the *interrupted
//    task's* stack before any switch to the ISR stack -- so a nested pair of
//    interrupts costs 320 B here, on top of every chain.
//
// Every chain is a LOWER bound: an indirect call the recipe does not stitch is
// not followed, and a cycle in the call graph is cut. Read the margin as cover
// for what the measurement cannot see, not as slack to spend. The Xtensa walk
// is the weaker of the two: objdump prints about a third of the artoo image's
// function bodies as data, and tools/stack_usage_report.py decodes those from a
// copy of the image without .xt.prop -- tools/check_task_stack_chains.py prints
// how many it recovered and how many it could not. That asymmetry is what
// makes the re-walk safe to fail a build on: it can MISS growth and cannot
// report FALSE growth.
//
// Which arms get the rule, and why the two chips answer differently:
//
//  - ESP32-P4: every arm is exactly the rule applied to its own chain. The
//    board has the free heap to buy the margin.
//  - artoo-esp32: an arm may sit ABOVE the rule, where an earlier decision
//    raised it past it on evidence the rule does not carry, or DECLINE it on
//    #248's reason: the margin costs heap on the scarce chip, for cover the
//    Xtensa walk cannot confirm. Declining the rule never declines the floor:
//    every arm still covers its own chain, and the static_asserts below are
//    what say so.
//
// Which arm is which is derived from its two figures every time it is needed,
// never stored, so it cannot disagree with them; the generated header names it
// beside each stack, and the recipe carries the reason for every departure.
//
// Not a lever: moving a task's large locals into static storage to shorten its
// chain. It was rejected for the Console's config write (#269) -- 1892 B of
// .bss to save stack, when the copies themselves could go, and they went.
#include "task_stack_figures.h"

// The floor is compile-enforced rather than promised by the comment above,
// because a comment is what let ConsoleTask stand 4 KB below its own chain until
// it took both boards down (#226). A recipe that lowers a stack below its chain,
// or records a chain past its stack, fails the compile here -- on both chips, in
// every environment that includes this header.
//
// This is half the guard. It fixes the constant to the chain; nothing here can
// notice the CHAIN growing, because the chain is itself a recorded number. That
// half is tools/check_task_stack_chains.py, which re-walks every recipe in
// tools/task_stack_recipes.json against a linked image (ADR 0040).
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
static_assert(REACTION_TASK_STACK_BYTES >= REACTION_TASK_MEASURED_CHAIN_BYTES,
              "REACTION_TASK_STACK_BYTES is below ReactionTask's measured worst-case static "
              "chain");
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
// Retired with #413, and read once more on the way out. They were the single
// lit wire and its LED count, one pair for the whole droid; a Light Type and
// its settings now live on the Output that carries them (ADR 0067). A
// controller upgrading still holds them, so configDeserializeServoOutputs()
// reads them onto the row they were about and configSaveServoOutputs() removes
// them once that row is safely down. Nothing writes them.
constexpr char NVS_KEY_RETIRED_AUX_LED_PIN[] = "aux_led_pin";
constexpr char NVS_KEY_RETIRED_AUX_LED_COUNT[] = "aux_led_count";
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
