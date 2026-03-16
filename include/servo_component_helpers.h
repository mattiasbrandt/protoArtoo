// =============================================================================
// include/servo_component_helpers.h
//
// Pure helpers for servo component type conversion and default calibration.
// No Arduino, no FreeRTOS, no queues — safe to include in native unit tests.
//
// Extracted from src/web/api_estop.cpp so type conversion and default
// logic can be exercised without hardware dependencies.
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
//   SERVO_COMP_MG996R → "mg996r"
//   SERVO_COMP_MG90S  → "mg90s"
//   SERVO_COMP_RGB    → "rgb"
//   SERVO_COMP_NONE   → "none"
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
//   "mg996r" → SERVO_COMP_MG996R
//   "mg90s"  → SERVO_COMP_MG90S
//   "rgb"    → SERVO_COMP_RGB
//   "none"   → SERVO_COMP_NONE
//   nullptr or unknown → SERVO_COMP_NONE
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

// -----------------------------------------------------------------------------
// servoTypeDefaultOpen()
// Return default "open" pulse width in microseconds for a given servo type.
//
//   MG996R: 2000 µs (standard hobby servo range)
//   MG90S:  2500 µs (micro servo full range)
//   RGB/none: 1500 µs (neutral position)
// -----------------------------------------------------------------------------
inline uint16_t servoTypeDefaultOpen(ServoComponentType t) {
    if (t == SERVO_COMP_MG996R)
        return 2000;
    if (t == SERVO_COMP_MG90S)
        return 2500;
    return 1500;  // neutral for rgb/none
}

// -----------------------------------------------------------------------------
// servoTypeDefaultClose()
// Return default "close" pulse width in microseconds for a given servo type.
//
//   MG996R: 1000 µs (standard hobby servo range)
//   MG90S:  500 µs (micro servo full range)
//   RGB/none: 1500 µs (neutral position)
// -----------------------------------------------------------------------------
inline uint16_t servoTypeDefaultClose(ServoComponentType t) {
    if (t == SERVO_COMP_MG996R)
        return 1000;
    if (t == SERVO_COMP_MG90S)
        return 500;
    return 1500;  // neutral for rgb/none
}

// -----------------------------------------------------------------------------
// isValidServoCompType()
// Validate that a type value is within the valid enum range.
//
// Valid range: SERVO_COMP_NONE (0) through SERVO_COMP_RGB (3)
// Returns false for values > SERVO_COMP_RGB
// -----------------------------------------------------------------------------
inline bool isValidServoCompType(uint8_t rawValue) {
    return rawValue <= SERVO_COMP_RGB;
}

// -----------------------------------------------------------------------------
// clampServoCompType()
// Clamp an arbitrary value to a valid ServoComponentType.
//
// Values > SERVO_COMP_RGB are clamped to SERVO_COMP_NONE
// This is useful for sanitizing NVS/config values
// -----------------------------------------------------------------------------
inline ServoComponentType clampServoCompType(uint8_t rawValue) {
    if (rawValue <= SERVO_COMP_RGB)
        return static_cast<ServoComponentType>(rawValue);
    return SERVO_COMP_NONE;
}
