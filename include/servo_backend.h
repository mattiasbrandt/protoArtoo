// =============================================================================
// include/servo_backend.h
//
// The servo backend seam (#444, from #306): what puts a width on an Output's
// pin and takes it off again, reached by the Output's address.
//
// WHERE THE LINE IS. Everything a servo command passes through before it gets
// here is ServoTask's, and no backend can reach it:
//
//   - the component clamp (ADR 0041)            configCacheClampServoOutputPulse()
//   - the Motion Profile ramp (ADR 0052)        include/servo_motion_ramp.h
//   - the calibration dial's hold and its two
//     bounds (ADR 0064)                         include/servo_hold.h
//   - the Output Release after a move
//     arrives (ADR 0043, #443)                  include/servo_release.h
//   - the estop and Sleep Mode release
//     (ADR 0043)                                include/servo_halt.h
//
// A backend is handed a channel and a width that are already decided. It
// attaches a channel, writes a width and takes the pulse off - three verbs and
// nothing else - and what it IS is data on its catalogue row below, the shape
// the Foot Drive seam set (include/drive_backend.h): which Component Member it
// is, the smallest step it can put on a pin, how many Outputs it has, and
// whether it sits on a bus that can drop.
//
// DISPATCH IS A SWITCH, NOT A TABLE OF POINTERS. ServoTask's stack is a
// measured chain walked off the linked image (ADR 0040), and a call through a
// function pointer is one that walk cannot follow without a hand stitch in
// tools/task_stack_recipes.json. So each verb names its member's function
// directly, and is forced inline so the seam adds no frame of its own to a
// chain with no headroom.
//
// SLOTS. ServoTask keeps one state per Output it can drive, and its status
// mirror (RobotState::servoCommanded) one entry per Output, each in the same
// order. That index - the slot - is ServoTask's and the mirror's own, and
// nobody else's: every reader asks by Output Address, and servoOutputSlotOf()
// is the one place an address becomes a slot. Members' Outputs are numbered
// one after another; LEDC's come first, in the board's own table order.
//
// Pure apart from the three verbs, which name the LEDC driver's functions
// (src/drivers/ledc_pwm.cpp): no config cache, no lock, no heap. The mapping
// half compiles in the native build.
// =============================================================================
#pragma once

#include <stddef.h>
#include <stdint.h>

#include "board_outputs.h"         // BOARD_OUTPUTS - the LEDC member's Outputs
#include "component_registry.h"    // componentPartExists() - a profile cites its row
#include "ledc_pwm.h"              // the LEDC member's three verbs and its timer facts
#include "servo_output_address.h"  // ServoOutputAddress, ServoOutputDriver

// -----------------------------------------------------------------------------
// The catalogue: one row per member, indexed by its driver
// -----------------------------------------------------------------------------
struct ServoBackendProfile {
    const char* id;            // Component Member identifier, a Body servo controller row
    ServoOutputDriver driver;  // the half of an Output Address that names this member
    uint16_t resolutionNs;     // the smallest width step it can put on a pin
    uint8_t outputCount;       // how many Outputs it drives
    bool onABus;               // it can drop off a bus, leaving its Outputs unreachable
};

// The LEDC member's Component Registry id, named once so the profile and the
// row it stands for cannot be two different spellings.
inline constexpr char kServoBackendLedcRegistryId[] = "esp32_gpio_ledc";
static_assert(componentPartExists(kServoBackendLedcRegistryId),
              "the servo backend cites a product id no Component Registry row declares");

inline constexpr ServoBackendProfile kServoBackends[SERVO_DRIVER_COUNT] = {
    // The ESP32's own PWM peripheral on the board's GPIO Outputs. One duty
    // step is the period over the timer's full count, the same arithmetic
    // pulseUsToDuty() converts with (include/ledc_pwm.h).
    {
        kServoBackendLedcRegistryId,
        SERVO_DRIVER_LEDC,
        (uint16_t)((PWM_PERIOD_US * 1000UL) / LEDC_DUTY_MAX),
        (uint8_t)BOARD_OUTPUT_COUNT,
        false,
    },
};

namespace servo_backend_detail {
constexpr bool rowsAreInDriverOrder() {
    for (uint8_t i = 0; i < SERVO_DRIVER_COUNT; ++i) {
        if (kServoBackends[i].driver != (ServoOutputDriver)i) {
            return false;
        }
    }
    return true;
}

constexpr uint8_t outputTotal() {
    uint16_t total = 0;
    for (uint8_t i = 0; i < SERVO_DRIVER_COUNT; ++i) {
        total += kServoBackends[i].outputCount;
    }
    return (uint8_t)total;
}
}  // namespace servo_backend_detail

static_assert(servo_backend_detail::rowsAreInDriverOrder(),
              "kServoBackends is indexed by driver: row i must be driver i");

// The profile for an address's driver, or nullptr for a driver this image has
// no member for.
inline const ServoBackendProfile* servoBackendProfileOf(ServoOutputDriver driver) {
    return driver < SERVO_DRIVER_COUNT ? &kServoBackends[driver] : nullptr;
}

// -----------------------------------------------------------------------------
// The LEDC member's Outputs are the board's own
//
// BOARD_OUTPUTS (include/board_outputs.h) is the board's list of its GPIO
// Outputs and the LEDC channel each is driven on, so for this member the board
// index and the Output Address are one fact read two ways. A caller that
// starts from the board's own numbering - the wired ticks, which are stored per
// board Output; Marcduino's panel number; an RC arm - comes here for the
// address, and a caller holding an address comes here for the board index.
// -----------------------------------------------------------------------------

// The Output Address of BOARD_OUTPUTS[boardIndex], or SERVO_OUTPUT_NONE past
// the table.
inline ServoOutputAddress boardOutputAddress(size_t boardIndex) {
    if (boardIndex >= BOARD_OUTPUT_COUNT) {
        return SERVO_OUTPUT_NONE;
    }
    return {SERVO_DRIVER_LEDC, BOARD_OUTPUTS[boardIndex].channel};
}

// Which of the board's Outputs this address is, or BOARD_OUTPUT_COUNT when it
// is none of them: another driver's address, LEDC_CH_DOME (a brushless ESC,
// not an Output), SERVO_OUTPUT_BOTH_ARMS (two of them) or SERVO_OUTPUT_NONE.
inline size_t boardOutputIndexOf(ServoOutputAddress output) {
    if (output.driver != SERVO_DRIVER_LEDC) {
        return BOARD_OUTPUT_COUNT;
    }
    for (size_t index = 0; index < BOARD_OUTPUT_COUNT; ++index) {
        if (BOARD_OUTPUTS[index].channel == output.channel) {
            return index;
        }
    }
    return BOARD_OUTPUT_COUNT;
}

// SERVO_OUTPUT_BOTH_ARMS is the board's first two Outputs.
static_assert(BOARD_OUTPUT_COUNT >= 2, "`both` names the board's first two Outputs");

// -----------------------------------------------------------------------------
// Slots - ServoTask's own index, and its mirror's
// -----------------------------------------------------------------------------
constexpr uint8_t SERVO_OUTPUT_SLOT_COUNT = servo_backend_detail::outputTotal();
constexpr uint8_t SERVO_OUTPUT_SLOT_NONE = 0xFF;
// ServoTask keeps what it snapshots at start one bit per slot.
static_assert(SERVO_OUTPUT_SLOT_COUNT <= 32, "a slot mask is a uint32_t");

// The slot an Output Address is driven from, or SERVO_OUTPUT_SLOT_NONE for an
// address no member of this image drives - which is also what the two
// sentinels answer, so a caller never has to test for them first.
inline uint8_t servoOutputSlotOf(ServoOutputAddress output) {
    switch (output.driver) {
        case SERVO_DRIVER_LEDC: {
            const size_t index = boardOutputIndexOf(output);
            return index < BOARD_OUTPUT_COUNT ? (uint8_t)index : SERVO_OUTPUT_SLOT_NONE;
        }
        default:
            return SERVO_OUTPUT_SLOT_NONE;
    }
}

// The Output Address a slot drives, or SERVO_OUTPUT_NONE past the last one.
inline ServoOutputAddress servoOutputSlotAddress(uint8_t slot) {
    return slot < BOARD_OUTPUT_COUNT ? boardOutputAddress(slot) : SERVO_OUTPUT_NONE;
}

// -----------------------------------------------------------------------------
// The three verbs
//
// Each answers false for an address no member drives, and otherwise whatever
// its member's driver answers; the LEDC driver logs its own failures
// (src/drivers/ledc_pwm.cpp).
// -----------------------------------------------------------------------------

// Configure an Output's channel with no pulse on it, so the next write drives
// it: a Find by Moving run taking a free Output (include/servo_run.h).
inline __attribute__((always_inline)) bool servoBackendAttach(ServoOutputAddress output) {
    switch (output.driver) {
        case SERVO_DRIVER_LEDC:
            return ledcPwmAttach(output.channel);
        default:
            return false;
    }
}

// Put one width on an Output's pin.
inline __attribute__((always_inline)) bool servoBackendWrite(ServoOutputAddress output,
                                                             uint16_t pulseUs) {
    switch (output.driver) {
        case SERVO_DRIVER_LEDC:
            return ledcPwmSetPulseWidth(output.channel, pulseUs);
        default:
            return false;
    }
}

// Take the pulse off an Output's pin, so the servo goes limp where it is
// (ADR 0043). The channel stays configured for the next write.
inline __attribute__((always_inline)) bool servoBackendRelease(ServoOutputAddress output) {
    switch (output.driver) {
        case SERVO_DRIVER_LEDC:
            return ledcPwmRelease(output.channel);
        default:
            return false;
    }
}
