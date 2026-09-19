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

// Maps a target word to the ServoCommand::armId it dispatches as. The word is
// the running board's label for an Output - ARM1..ARM5 on the Artoo PCB,
// GPIO 49 / GPIO 50 / GPIO 4 / GPIO 5 / GPIO 51 on the FireBeetle 2 - matched
// without regard to case or spaces (include/board_outputs.h), or "both", which
// keeps its meaning: 255, the first two Outputs together (robot_state.h's own
// field comment; the other three have no broadcast id). Returns -1 for anything
// else, including the old protoArtoo-wide words arm1..aux3 where they are not
// this board's label: there is no alias (ADR 0033 Amendment 2026-09-19).
// int16_t, not int8_t: 255 truncates to -1 in an int8_t return, which is how
// "both" once came to be rejected as invalid on an endpoint whose own error
// message offers it. Exported (not file-local to api_servo.cpp) so the
// Controller Console's servo.action.* executors (include/
// console_direct_action_servo.h, ADR 0036) resolve a target the same way
// handleServoPost() does, rather than a second name<->id mapping.
int16_t parseArmId(const char* arm);

// Commit Step (ADR 0036 criterion 1): the handler-owned servoCmdQueue
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

// POST /api/servo/centre - put every Servo Output back to its recorded centre
// (#318, #365). One press from the output-first table, and the droid paces the
// sweep itself: the Sequence Coordinator expands it into one Output move at a
// time, no closer together than the Cadence Floor
// (include/sequence_bulk_centre.h), so the convenience cannot brown out the
// shared servo rail.
//
// Its own route rather than an action on POST /api/servo, because it takes no
// arm and queues no servo command: it signals the Coordinator through a
// transient flag and the expansion is the Coordinator's, which is the whole
// reason the pace cannot be walked around from a browser. The body is empty;
// there is nothing for a caller to decide.
void handleServoCentrePost(WebRequest& req);
