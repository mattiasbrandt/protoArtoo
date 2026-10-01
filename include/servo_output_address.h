// =============================================================================
// include/servo_output_address.h
//
// An Output Address: which driver, and which channel on it, a servo wire plugs
// into (ADR 0041). It is how the whole servo path names an Output - a Servo
// Output row, servoCmdQueue, ServoTask's status mirror, POST /api/servo, the
// Controller Console's servo.action.* and the Body Step and back-to-centre
// plans - so an expander's channels are more Outputs on the same path rather
// than a path of their own (#444).
//
// A leaf, on purpose: include/robot_state.h carries an address in every
// ServoCommand, and include/servo_output_row.h stores one in every row while
// including robot_state.h for the component type, so the type has to sit below
// both of them.
// =============================================================================
#pragma once

#include <stdint.h>

// An expander adds a driver here and rows to the table; it never adds a field
// to the row. `ledc` is the ESP32 PWM peripheral this controller drives today.
enum ServoOutputDriver : uint8_t {
    SERVO_DRIVER_LEDC = 0,
    SERVO_DRIVER_COUNT = 1,
};

// A channel value that is not an address on any driver. Rows past the table's
// count carry it, so a row nobody has addressed cannot read as channel 0.
constexpr uint8_t SERVO_OUTPUT_CHANNEL_UNSET = 0xFF;

struct ServoOutputAddress {
    ServoOutputDriver driver;
    uint8_t channel;
};

inline constexpr bool operator==(ServoOutputAddress a, ServoOutputAddress b) {
    return a.driver == b.driver && a.channel == b.channel;
}

inline constexpr bool operator!=(ServoOutputAddress a, ServoOutputAddress b) {
    return !(a == b);
}

// No Output: what a row nobody has addressed holds, and what a run that is
// waiting on nothing records.
constexpr ServoOutputAddress SERVO_OUTPUT_NONE = {SERVO_DRIVER_LEDC, SERVO_OUTPUT_CHANNEL_UNSET};

// The board's first two Outputs together, the utility arms - `both` on
// POST /api/servo and the Console, Marcduino panel 0 or 99. Not an Output: it
// is two, and ServoTask moves each through its own address
// (include/servo_backend.h, boardOutputAddress(0) and (1)). 0xFE is a channel no driver
// has, and it is not SERVO_OUTPUT_CHANNEL_UNSET, so "both" and "none" cannot be
// read as each other.
constexpr ServoOutputAddress SERVO_OUTPUT_BOTH_ARMS = {SERVO_DRIVER_LEDC, 0xFE};
