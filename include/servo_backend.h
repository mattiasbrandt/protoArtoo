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
// attaches a channel, writes a width and takes the pulse off - three verbs -
// and what it IS is data on its catalogue row below, the shape the Foot Drive
// seam set (include/drive_backend.h): which Component Member it is, the
// smallest step it can put on a pin, how many Outputs it has, and whether it
// sits on a bus that can drop.
//
// A FOURTH CALL, FOR A MEMBER WHOSE WRITES LEAVE THIS CORE. LEDC's verbs put
// the width on the pin as they are called. The PCA9685's only record what
// each channel should be, because ServoTask is a Core 1 real-time loop and an
// I2C transaction is not something it may wait on (include/pca9685.h); a task
// on Core 0 does the bus writes. servoBackendCommit() is where ServoTask hands
// that member a frame's changes, once a frame and once straight after a halt
// releases every Output - so the halt's sixteen releases leave as one write.
// It changes no width and decides nothing, and LEDC has nothing to do in it.
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
// Pure apart from the verbs, which name each member's driver functions
// (src/drivers/ledc_pwm.cpp, src/drivers/pca9685.cpp): no config cache, no
// lock, no heap. The mapping half is constexpr, so a slot known at compile time
// costs a reader nothing, and compiles in the native build.
// =============================================================================
#pragma once

#include <stddef.h>
#include <stdint.h>

#include "board_outputs.h"         // BOARD_OUTPUTS - the LEDC member's Outputs
#include "component_registry.h"    // componentPartExists() - a profile cites its row
#include "ledc_pwm.h"              // the LEDC member's three verbs and its timer facts
#include "pca9685.h"               // the PCA9685 member's verbs, its channel count and step
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
inline constexpr char kServoBackendPca9685RegistryId[] = "pca9685";
static_assert(componentPartExists(kServoBackendPca9685RegistryId),
              "the servo backend cites a product id no Component Registry row declares");

// Whether a resolved Body servo controller member is the PCA9685 - the one
// question the boot path and ServoTask ask of this boot's choice, answered
// against the id its profile cites so the two cannot spell it differently.
inline bool servoBackendMemberIsPca9685(const ComponentPartEntry* member) {
    return member != nullptr &&
           component_registry_detail::idEquals(member->id, kServoBackendPca9685RegistryId);
}

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
    // The PCA9685 on the I2C header: sixteen channels at a 12-bit count of a
    // 50 Hz frame, 4.88 us a step - sixteen times coarser than LEDC, half a
    // degree on a 180 degree servo (spec sheet 7.7). On a bus, so it can drop
    // off it, and its Outputs are then reported unreachable (ADR 0043).
    {
        kServoBackendPca9685RegistryId,
        SERVO_DRIVER_PCA9685,
        PCA9685_TICK_NS,
        PCA9685_CHANNEL_COUNT,
        true,
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

// The first slot a driver's Outputs take: every Output of the drivers before
// it comes first.
constexpr uint8_t firstSlotOf(ServoOutputDriver driver) {
    uint16_t first = 0;
    for (uint8_t i = 0; i < SERVO_DRIVER_COUNT && i < (uint8_t)driver; ++i) {
        first += kServoBackends[i].outputCount;
    }
    return (uint8_t)first;
}

constexpr uint8_t outputTotal() { return firstSlotOf(SERVO_DRIVER_COUNT); }
}  // namespace servo_backend_detail

static_assert(servo_backend_detail::rowsAreInDriverOrder(),
              "kServoBackends is indexed by driver: row i must be driver i");
// The board's own Outputs are the first slots, in the board's order: `both`
// (the first two), the lit mask and the LEDC init mask are all read by slot
// with that assumed (include/servo_helpers.h, src/tasks/servo_task.cpp).
static_assert(SERVO_DRIVER_LEDC == 0 && servo_backend_detail::firstSlotOf(SERVO_DRIVER_LEDC) == 0,
              "the board's own Outputs take the first slots");

// The profile for an address's driver, or nullptr for a driver this image has
// no member for.
constexpr const ServoBackendProfile* servoBackendProfileOf(ServoOutputDriver driver) {
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
constexpr ServoOutputAddress boardOutputAddress(size_t boardIndex) {
    if (boardIndex >= BOARD_OUTPUT_COUNT) {
        return SERVO_OUTPUT_NONE;
    }
    return ServoOutputAddress{SERVO_DRIVER_LEDC, BOARD_OUTPUTS[boardIndex].channel};
}

// Which of the board's Outputs this address is, or BOARD_OUTPUT_COUNT when it
// is none of them: another driver's address, LEDC_CH_DOME (a brushed ESC,
// not an Output), SERVO_OUTPUT_BOTH_ARMS (two of them) or SERVO_OUTPUT_NONE.
constexpr size_t boardOutputIndexOf(ServoOutputAddress output) {
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
//
// Every member's slots exist in every image, whichever member a builder has
// chosen: the choice is read at start (ADR 0027), and an expander that was not
// chosen, or does not answer, is an Output ServoTask does not drive, exactly
// as a board Output with no Part is - not an address with nowhere to live.
constexpr uint8_t servoOutputSlotOf(ServoOutputAddress output) {
    switch (output.driver) {
        case SERVO_DRIVER_LEDC: {
            const size_t index = boardOutputIndexOf(output);
            return index < BOARD_OUTPUT_COUNT ? (uint8_t)index : SERVO_OUTPUT_SLOT_NONE;
        }
        case SERVO_DRIVER_PCA9685:
            return output.channel < PCA9685_CHANNEL_COUNT
                       ? (uint8_t)(servo_backend_detail::firstSlotOf(SERVO_DRIVER_PCA9685) +
                                   output.channel)
                       : SERVO_OUTPUT_SLOT_NONE;
        default:
            return SERVO_OUTPUT_SLOT_NONE;
    }
}

// The Output Address a slot drives, or SERVO_OUTPUT_NONE past the last one.
constexpr ServoOutputAddress servoOutputSlotAddress(uint8_t slot) {
    if (slot < BOARD_OUTPUT_COUNT) {
        return boardOutputAddress(slot);
    }
    if (slot < SERVO_OUTPUT_SLOT_COUNT) {
        return ServoOutputAddress{
            SERVO_DRIVER_PCA9685,
            (uint8_t)(slot - servo_backend_detail::firstSlotOf(SERVO_DRIVER_PCA9685))};
    }
    return SERVO_OUTPUT_NONE;
}
static_assert(servo_backend_detail::firstSlotOf(SERVO_DRIVER_PCA9685) == BOARD_OUTPUT_COUNT &&
                  SERVO_OUTPUT_SLOT_COUNT == BOARD_OUTPUT_COUNT + PCA9685_CHANNEL_COUNT,
              "servoOutputSlotAddress() reads every slot past the board's as the PCA9685's");

// What a log line calls the Output in a slot: what the board prints beside it,
// or - for an Output no board prints a name for, an expander's - its address.
// The same rule every surface names an Output by (data/outputs.js `name`). A
// static string either way, so a caller on ServoTask's measured chain passes a
// pointer, exactly the bytes the slot number it replaced cost, and formats
// nothing into its own frame.
inline const char* servoOutputSlotName(uint8_t slot) {
    if (slot < BOARD_OUTPUT_COUNT) {
        const char* label = boardOutputLabel(BOARD_OUTPUTS[slot]);
        return label != nullptr ? label : "no output";
    }
    const ServoOutputAddress output = servoOutputSlotAddress(slot);
    return output.driver == SERVO_DRIVER_PCA9685 ? pca9685OutputName(output.channel)
                                                 : "no output";
}

// -----------------------------------------------------------------------------
// The three verbs
//
// Each answers false for an address no member drives, and otherwise whatever
// its member's driver answers; the LEDC driver logs its own failures
// (src/drivers/ledc_pwm.cpp). The PCA9685's never log and never touch the bus:
// they record, and answer whether the expander is answering - its sender task
// logs a failed write, on Core 0 (src/drivers/pca9685.cpp).
// -----------------------------------------------------------------------------

// Configure an Output's channel with no pulse on it, so the next write drives
// it: a Find by Moving run taking a free Output (include/servo_run.h).
inline __attribute__((always_inline)) bool servoBackendAttach(ServoOutputAddress output) {
    switch (output.driver) {
        case SERVO_DRIVER_LEDC:
            return ledcPwmAttach(output.channel);
        case SERVO_DRIVER_PCA9685:
            return pca9685Attach(output.channel);
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
        case SERVO_DRIVER_PCA9685:
            return pca9685Write(output.channel, pulseUs);
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
        case SERVO_DRIVER_PCA9685:
            return pca9685Release(output.channel);
        default:
            return false;
    }
}

// Hand every member whose writes leave this core what this frame changed
// (above, "A FOURTH CALL"). Every member, not one address: it is the frame's
// end, not an Output's.
inline __attribute__((always_inline)) void servoBackendCommit() { pca9685Commit(); }
