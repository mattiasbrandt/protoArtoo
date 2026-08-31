// =============================================================================
// include/api_servo.h
//
// Servo control API endpoint, written against the project-owned WebRequest
// seam (ADR 0021) and bound by the seam route table. Exposed so native tests
// can drive it directly through the host-test backend.
// =============================================================================
#pragma once

#include <stdint.h>

#include "robot_state.h"  // ServoCommand, ServoCommandType, CommandSource
#include "web_request.h"

// Maps an "arm1"/"arm2"/"aux1"/"aux2"/"aux3"/"both" target name (case-
// insensitive) to the ServoCommand::armId it dispatches as (0..4, or 255 for
// "both" - arm1+arm2 only, see robot_state.h's own field comment; aux1..3
// have no broadcast id). Returns -1 for anything else. int16_t, not int8_t:
// 255 truncates to -1 in an int8_t return, which is how "both" once came to
// be rejected as invalid on an endpoint whose own error message offers it.
// Exported (not file-local to api_servo.cpp) so the Controller Console's
// servo.action.open/close/set-position executors (src/console/
// console_module.cpp, ADR 0034) resolve a target the same way
// handleServoPost() does, rather than a second name<->id mapping.
int16_t parseArmId(const char* arm);

// Commit Step (ADR 0034 criterion 1): the handler-owned servoCmdQueue
// enqueue, extracted so the Console reaches the identical submission
// handleServoPost() makes rather than a second copy - src/rc_dispatcher_helpers.cpp
// already has its own near-duplicate of this for the RC dispatch path
// (queueServoCommand(), file-local there), which this does not touch.
struct ServoSubmitOutcome {
    bool ok = false;  // false -> caller reports "Servo command queue full" (503)
};
ServoSubmitOutcome servoSubmitCommand(uint8_t armId, ServoCommandType type, uint16_t positionUs,
                                       CommandSource source);

void handleServoPost(WebRequest& req);
