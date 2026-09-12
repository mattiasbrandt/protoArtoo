// =============================================================================
// include/sequence_body_step.h
//
// What a Body Step resolves to on the droid in front of it (ADR 0049, #349).
//
// A Body Step names a Part; an Output Address is where the lead plugs in. The
// Sequence Coordinator joins the two at dispatch, every time, from the Servo
// Output rows the builder's own droid stores - never from anything cached at
// save or at discovery. That is what makes authoring before wiring work: wire
// the arm, let an Output record the Part, and the same saved step starts moving
// it with nothing re-authored (include/droid_part_availability.h).
//
// This header owns the DECISION and none of the side effects: given the engine's
// action and the row that drives the Part (or nullptr when none does), it says
// what the Coordinator should command and what it should report. The row walk
// and the queue send are the task adapter's, because the live table is handed
// out one row at a time (configCacheReadServoOutput). Same split ADR 0014 made
// for sequence_dispatcher_step.h.
//
// Pure: no NVS, no FreeRTOS, no Arduino String. It reads a row it is handed.
// =============================================================================

#pragma once

#include <stdint.h>

#include "console_module.h"           // ConsoleReason - the Availability Reason set
#include "droid_part_availability.h"  // droidPartAvailabilityFromRow()
#include "sequence_engine.h"          // SeqAction, SeqBodyShape
#include "servo_helpers.h"            // servo_ledc_channel_to_arm_id()
#include "servo_output_row.h"         // ServoOutputRow - this droid's own wiring

// -----------------------------------------------------------------------------
// What the Coordinator should do with one body action.
//
// `reason` is what to report, and CONSOLE_REASON_NONE is the only value that
// comes with `drive`. A reported reason is never a refusal of the sequence: the
// step is inert and the rest of the choreography carries on, because a Part
// nothing drives yet is the normal state of a build in progress (#301).
// -----------------------------------------------------------------------------
struct SeqBodyStepPlan {
    ConsoleReason reason;    // NONE when the Output can be commanded
    bool          drive;     // true => command armId to targetUs
    uint8_t       armId;     // ServoCommand::armId for the Output Address
    uint16_t      targetUs;  // where to drive it, already clamped by the row
};

// -----------------------------------------------------------------------------
// seqBodyTargetUs()
// Where a shape and a how-far land on one Output's Endpoint Pair.
//
// How-far is measured along the shape's OWN direction of travel, from the end it
// starts from: an open goes that far from `close` towards `open`, and a close
// that far from `open` towards `close`. So the full throw puts each shape on its
// own end, and a half throw puts both on the middle - which is the same
// position, correctly, because half-open and half-closed are one place.
//
// A flutter resolves where an open does, because a flutter ENDS OPEN (ADR 0049).
//
// The pair is directional and stays that way: a reversed linkage is
// `open < close` and nothing else records it, so this reads the two fields as
// stored rather than sorting them - sorting here would be the invert flag the
// model refuses, arriving by the back door (ADR 0041). Then the row's own
// component band bounds the result, because every door onto a row goes through
// that clamp.
// -----------------------------------------------------------------------------
inline uint16_t seqBodyTargetUs(const ServoOutputRow& row, SeqBodyShape shape,
                                uint8_t howFarPct) {
    const int32_t span = (int32_t)row.open_us - (int32_t)row.close_us;
    // Round half away from zero, so a percentage of a reversed span rounds the
    // same distance as it would on a forward one.
    const int32_t bias = (span >= 0) ? 50 : -50;
    const int32_t travelled = (span * (int32_t)howFarPct + bias) / 100;
    const int32_t target = (shape == BODY_SHAPE_CLOSE)
                               ? ((int32_t)row.open_us - travelled)
                               : ((int32_t)row.close_us + travelled);
    const uint16_t bounded = (target < 0) ? 0
                             : (target > 0xFFFF) ? (uint16_t)0xFFFF
                                                 : (uint16_t)target;
    return servoOutputClampPulse(row, bounded);
}

// -----------------------------------------------------------------------------
// sequenceBodyStepPlan()
// The whole decision for one SEQ_ACT_BODY_MOVE action.
//
// `row` is the Output that drives the action's Part, or nullptr when the caller's
// search found none. Passing a row for a Part it does not drive is the caller's
// bug and not checked here: the search is the caller's half of the contract.
// -----------------------------------------------------------------------------
inline SeqBodyStepPlan sequenceBodyStepPlan(const SeqAction& act,
                                            const ServoOutputRow* row) {
    SeqBodyStepPlan plan = { CONSOLE_REASON_NONE, false, 0, 0 };

    plan.reason = droidPartAvailabilityFromRow(act.payload, row != nullptr);
    // The null test is not a second case -- a null row can only come back as
    // part-not-assigned or unknown-argument, both caught by the first clause.
    // It is what makes the dereference below provably safe to a reader and to
    // static analysis, rather than safe by a chain of reasoning about the line
    // above.
    if (plan.reason != CONSOLE_REASON_NONE || row == nullptr) {
        return plan;
    }

    // An expander adds a driver and rows; until one exists, an Output addressed
    // to a driver this image does not carry cannot be commanded, and saying
    // "not in this build" is the honest answer rather than silence. Same for a
    // channel that is not a servo output: LEDC's DOME channel drives a brushless
    // ESC, so no row should be addressed there and none can be driven there.
    uint8_t armId = 0;
    if (row->driver != SERVO_DRIVER_LEDC ||
        !servo_ledc_channel_to_arm_id(row->channel, &armId)) {
        plan.reason = CONSOLE_REASON_NOT_IN_THIS_BUILD;
        return plan;
    }

    plan.armId = armId;
    plan.targetUs = seqBodyTargetUs(*row, (SeqBodyShape)act.bodyShape, act.bodyHowFar);
    plan.drive = true;
    return plan;
}
