// =============================================================================
// include/marcduino_helpers.h
//
// Pure-math helpers for Marcduino command parsing.
// No Arduino, no FreeRTOS, no queues  --  safe to include in native unit tests.
//
// Extracted from src/drivers/dome_rx_parser.cpp so the mapping and conversion
// logic can be exercised without hardware dependencies.
// =============================================================================
#pragma once

#include <stdint.h>
#include <stdlib.h>  // atoi
#include <string.h>  // strcmp, strlen, strncmp

#include "ledc_pwm.h"  // SERVO_PULSE_MIN_US, SERVO_PULSE_MAX_US

// -----------------------------------------------------------------------------
// marcduino_panel_to_arm_id()
// Map Marcduino panel number to internal armId.
//
//   Panel 1 -> armId 0  (ARM1)
//   Panel 2 -> armId 1  (ARM2)
//   Panel 3 -> armId 2  (AUX1)
//   Panel 4 -> armId 3  (AUX2)
//   Panel 5 -> armId 4  (AUX3)
//   Panel 0 or 99 -> armId 255  (broadcast  --  ARM1+ARM2)
//   Any other value -> 254  (invalid sentinel)
//
// Returns 254 for invalid panel numbers (caller must reject the command).
// Returns 255 for broadcast (panel 0 or 99).
// -----------------------------------------------------------------------------
inline uint8_t marcduino_panel_to_arm_id(int panel) {
    switch (panel) {
        case 1:
            return 0;
        case 2:
            return 1;
        case 3:
            return 2;
        case 4:
            return 3;
        case 5:
            return 4;
        case 0:
        case 99:
            return 255;  // broadcast
        default:
            return 254;  // invalid
    }
}

// -----------------------------------------------------------------------------
// marcduino_panel_to_arm_id_mv()
// Variant for :MV (position) commands  --  broadcast (0/99) is not valid for MV.
//
//   Panel 1-5 -> armId 0-4 (same as above)
//   Any other value -> 254 (invalid sentinel)
// -----------------------------------------------------------------------------
inline uint8_t marcduino_panel_to_arm_id_mv(int panel) {
    switch (panel) {
        case 1:
            return 0;
        case 2:
            return 1;
        case 3:
            return 2;
        case 4:
            return 3;
        case 5:
            return 4;
        default:
            return 254;  // invalid (includes 0 and 99  --  no broadcast for MV)
    }
}

// -----------------------------------------------------------------------------
// marcduino_panel_command_arm_id()
// The Output a panel-family line (:OPnn, :CLnn, :OFnn, :MVnn...) names, read
// the one way both the body handler and the ownership resolver read it
// (include/marcduino_ownership.h). Two readings of one number is how a line
// the resolver calls the body's could reach a handler that refuses it.
//
//   :OP/:CL/:OF  atoi() of everything after the head, via
//                marcduino_panel_to_arm_id() (1-5, and 0/99 broadcast)
//   :MV          exactly two digits after the head, via
//                marcduino_panel_to_arm_id_mv() (1-5, no broadcast)
//
// Returns 254 for a line shorter than five characters, a head that is not one
// of the four, or a number no Output answers to. The value after an :MV
// number is not read here; the handler refuses an :MV with none.
// -----------------------------------------------------------------------------
inline uint8_t marcduino_panel_command_arm_id(const char* line) {
    if (line == nullptr || line[0] != ':' || strlen(line) < 5) {
        return 254;
    }
    if (strncmp(line, ":OP", 3) == 0 || strncmp(line, ":CL", 3) == 0 ||
        strncmp(line, ":OF", 3) == 0) {
        return marcduino_panel_to_arm_id(atoi(line + 3));
    }
    if (strncmp(line, ":MV", 3) == 0) {
        if (line[3] < '0' || line[3] > '9' || line[4] < '0' || line[4] > '9') {
            return 254;
        }
        return marcduino_panel_to_arm_id_mv(((line[3] - '0') * 10) + (line[4] - '0'));
    }
    return 254;
}

// -----------------------------------------------------------------------------
// marcduino_is_body_hash_command()
// The '#' lines the body acts on: the dome's sleep and wake sync, and the
// body's own heartbeat echoed back. Every other '#' line is the dome's.
//
// One home for the three, read by the body handler and the ownership resolver
// alike, so the list the body answers and the list it executes cannot differ.
// -----------------------------------------------------------------------------
inline bool marcduino_is_body_hash_command(const char* line) {
    return line != nullptr &&
           (strcmp(line, "#APSL") == 0 || strcmp(line, "#APWU") == 0 ||
            strcmp(line, "#PAHB") == 0);
}

// -----------------------------------------------------------------------------
// marcduino_mv_value_to_pulse_us()
// Convert `:MVxxdddd` values to servo pulse width in microseconds.
//
// Marcduino direct numeric semantics:
//   - 0000-0180 => degrees across the configured servo pulse range
//   - >0544     => direct microseconds
//
// Inputs are not clamped here. Caller-side validation decides what ranges are
// accepted; this helper only models the conversion rule.
// -----------------------------------------------------------------------------
inline uint16_t marcduino_mv_value_to_pulse_us(int value) {
    if (value > (int)SERVO_PULSE_MIN_US) {
        return (uint16_t)value;
    }

    uint16_t range_us = SERVO_PULSE_MAX_US - SERVO_PULSE_MIN_US;
    return (uint16_t)(SERVO_PULSE_MIN_US + ((value * range_us) / 180));
}

// -----------------------------------------------------------------------------
// marcduino_percent_to_pulse_us()
// Legacy compatibility helper for the older percent-based parser path.
// New `:MV` semantics should use marcduino_mv_value_to_pulse_us() instead.
// -----------------------------------------------------------------------------
inline uint16_t marcduino_percent_to_pulse_us(int pos) {
    uint16_t range_us = SERVO_PULSE_MAX_US - SERVO_PULSE_MIN_US;
    return (uint16_t)(SERVO_PULSE_MIN_US + ((pos * range_us) / 100));
}

// -----------------------------------------------------------------------------
// marcduino_sequence_id_valid()
// Return true if seqId is a valid body sequence (30-36 inclusive).
// -----------------------------------------------------------------------------
inline bool marcduino_sequence_id_valid(int seq_id) {
    return seq_id >= 30 && seq_id <= 36;
}

// -----------------------------------------------------------------------------
// Full-droid sequence decomposition for body-local actions.
//
// audioDollarCmd:
//   - nullptr => no local audio trigger
//   - "$X"    => queue this audio command to AudioTask
//
// bodySeqId:
//   - -1      => no local body routine
//   - 30 / 31 => start that body routine, :SE30 / :SE31 -- a Factory Sequence
//                built from Body Steps (sequenceBodyRoutineName(), ADR 0049)
// -----------------------------------------------------------------------------
struct FullDroidBodyAction {
    const char* audioDollarCmd;
    int         bodySeqId;
};

inline FullDroidBodyAction marcduino_full_droid_body_actions(int seq_id) {
    switch (seq_id) {
        case 1:
            return {"$S", 30};
        case 2:
            return {nullptr, 31};
        case 3:
            return {nullptr, 31};
        case 4:
            return {nullptr, 31};
        case 5:
            return {"$c", 31};
        case 6:
            return {"$F", 30};
        case 7:
            return {"$C", 31};
        case 8:
            return {"$L", 30};
        case 9:
            return {"$D", 31};
        case 15:
            return {"$S", -1};
        case 16:
            return {nullptr, 31};
        default:
            return {nullptr, -1};
    }
}
