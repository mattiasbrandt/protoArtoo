// =============================================================================
// include/servo_component_helpers.h
//
// Pure helpers for servo component type conversion.
// No Arduino, no FreeRTOS, no queues  --  safe to include in native unit tests.
//
// Extracted from src/web/api_estop.cpp so type conversion can be exercised
// without hardware dependencies.
// =============================================================================
#pragma once

#include <stdint.h>

#include <cstring>

// Forward declare ServoComponentType enum for native tests (defined in robot_state.h for firmware)
// This avoids pulling in Arduino.h for native tests
#ifndef ARDUINO_ARCH_ESP32
enum ServoComponentType : uint8_t {
    SERVO_COMP_NONE = 0,
    SERVO_COMP_MG996R = 1,
    SERVO_COMP_MG90S = 2,
    SERVO_COMP_RGB = 3
};
#endif

// -----------------------------------------------------------------------------
// servoCompTypeToString()
// Convert ServoComponentType enum to string representation.
//
//   SERVO_COMP_MG996R -> "mg996r"
//   SERVO_COMP_MG90S  -> "mg90s"
//   SERVO_COMP_RGB    -> "rgb"
//   SERVO_COMP_NONE   -> "none"
// -----------------------------------------------------------------------------
inline const char* servoCompTypeToString(ServoComponentType t) {
    switch (t) {
        case SERVO_COMP_MG996R:
            return "mg996r";
        case SERVO_COMP_MG90S:
            return "mg90s";
        case SERVO_COMP_RGB:
            return "rgb";
        default:
            return "none";
    }
}

// -----------------------------------------------------------------------------
// parseServoCompType()
// Parse string representation back to ServoComponentType enum.
//
//   "mg996r" -> SERVO_COMP_MG996R
//   "mg90s"  -> SERVO_COMP_MG90S
//   "rgb"    -> SERVO_COMP_RGB
//   "none"   -> SERVO_COMP_NONE
//   nullptr or unknown -> SERVO_COMP_NONE
// -----------------------------------------------------------------------------
inline ServoComponentType parseServoCompType(const char* s) {
    if (s == nullptr)
        return SERVO_COMP_NONE;
    if (strcmp(s, "mg996r") == 0)
        return SERVO_COMP_MG996R;
    if (strcmp(s, "mg90s") == 0)
        return SERVO_COMP_MG90S;
    if (strcmp(s, "rgb") == 0)
        return SERVO_COMP_RGB;
    return SERVO_COMP_NONE;
}
