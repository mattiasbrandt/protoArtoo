// =============================================================================
// include/api_servo.h
//
// Servo control API endpoint, written against the project-owned WebRequest
// seam (ADR 0021) and bound by the seam route table. Exposed so native tests
// can drive it directly through the host-test backend.
// =============================================================================
#pragma once

#include <stddef.h>
#include <stdint.h>

#include "robot_state.h"  // ServoCommand, ServoCommandType, CommandSource
#include "servo_output_address.h"  // ServoOutputAddress - how every helper here names an Output
#include "web_request.h"

// Resolves a target word to the Output Address a ServoCommand carries (#444).
// The word is the running board's label for one of its GPIO Outputs -
// ARM1..ARM5 on the Artoo PCB, GPIO 49 / GPIO 50 / GPIO 4 / GPIO 5 / GPIO 51
// on the FireBeetle 2 - matched without regard to case or spaces
// (include/board_outputs.h), and it moves that Output (ADR 0033 Amendment
// 2026-09-19); or "both", which keeps its meaning: SERVO_OUTPUT_BOTH_ARMS, the
// first two Outputs together (the other three have no broadcast). False, with
// `*output` untouched, for anything else, including the old protoArtoo-wide
// words arm1..aux3 where they are not this board's label: there is no alias.
// Exported (not file-local to api_servo.cpp) so the Controller Console's
// servo.action.* executors (include/console_direct_action_servo.h, ADR 0036)
// resolve a target the same way handleServoPost() does, rather than a second
// word<->Output mapping.
bool servoParseTarget(const char* word, ServoOutputAddress* output);

// Commit Step (ADR 0036 criterion 1): the handler-owned servoCmdQueue
// enqueue, extracted so the Console reaches the identical submission
// handleServoPost() makes rather than a second copy - src/rc_dispatcher_helpers.cpp
// already has its own near-duplicate of this for the RC dispatch path
// (queueServoCommand(), file-local there), which this does not touch.
struct ServoSubmitOutcome {
    bool ok = false;  // false -> caller reports "Servo command queue full" (503)
};
ServoSubmitOutcome servoSubmitCommand(ServoOutputAddress output, ServoCommandType type,
                                      uint16_t positionUs, CommandSource source);

// Whether nothing drives the Output(s) `output` names this boot, and if so the
// sentence to refuse with (#364). A wired tick saved after boot is only read at
// the next start (ADR 0027), so an Output can be ticked and still have nothing
// behind it; queueing a command for it would answer `ok` for a move ServoTask
// drops. The running truth is ServoTask's boot snapshot
// (servoTaskDrivesOutput(), include/servo_task.h), never the saved tick.
//
// SERVO_OUTPUT_BOTH_ARMS, the ARM1+ARM2 broadcast, is refused when either of the
// two is not driven, naming that one. The sentence says what would put a servo
// on it: a restart, where the saved tick and the wire's component would have
// ServoTask drive it; otherwise that it carries a light, or is not wired.
// Returns false, and leaves `reason` alone, when every named Output is driven.
bool servoOutputUndriven(ServoOutputAddress output, char* reason, size_t reasonSize);

// Whether this command is a Find by Moving run's on a free Output (#411): a
// nudge on one a run may take (servoTaskMayTakeForRun()), which takes it for
// the run, or a release on one a run holds (servoTaskRunHolds()) or may take.
// Such a command is not refused for being undriven: ServoTask drives a free
// Output for the length of a run, bounded like the dial's hold. Every other
// command on an undriven Output still is.
//
// A release on a free Output the run does not hold YET is still one, because
// Stop must win over a nudge that is queued and not taken (#411 slice 4): the
// nudge that takes the Output is on servoCmdQueue ahead of the release, so
// ServoTask takes it and lets go of it in order, in one drain when both are
// waiting - and LEDC applies a duty only at the next PWM cycle, so the pin
// never carries that take. Refusing it, as this once did, left the queued
// nudge pulsing for its whole out-and-back after Stop. Where no nudge was
// queued, the Output is limp already and stays so: the ok is for what the
// caller asked, an Output with no pulse. A release on an Output a Part is on
// and no run holds is still refused.
bool servoCommandIsARunsOnAFreeOutput(ServoOutputAddress output, ServoCommandType type);

// Refuses with 409 and servoOutputUndriven()'s sentence an act on an Output
// nothing drives this boot; every action is checked, pulses off included.
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
//
// The answer names the Outputs the sweep will pass over because nothing drives
// them (#364): `{"ok":true,"skipped":["ARM3"]}`, empty when none.
void handleServoCentrePost(WebRequest& req);

// The Outputs a press of back to centre passes over although they have travel,
// because nothing moves them this boot - ServoTask does not drive the Output
// (servoTaskDrivesOutput()), or this image has no driver for its address
// (sequenceBodyCentrePlan()). The Sequence Coordinator skips exactly these rows
// (src/tasks/sequence_dispatcher.cpp centreOneOutput()); this is the same two
// questions asked ahead of the run, so the route and the Console can name them.
// `visit` is handed each one's name - its board label, else its Output Address
// - in table order. Returns how many.
size_t servoCentreSkipped(void (*visit)(const char* name, void* ctx), void* ctx);
