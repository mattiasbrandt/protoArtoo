// =============================================================================
// include/sequence_bulk_centre.h
//
// Put every Servo Output back to centre, one press, paced by the droid
// (#318, #365; CONTEXT.md "Cadence Floor").
//
// One operator standing at the bench chooses this, which is what makes it
// legitimate: it is never emitted automatically, and that is the line ADR 0043
// and ADR 0049 drew when they refused many-at-once motion with nobody deciding.
// What the operator asks for is the whole act; the EXPANSION -- which Outputs,
// in what order, how far apart -- is the Sequence Coordinator's and lives here,
// never in the browser, because a safe pace a page holds is one a hand-edited
// or imported client could walk around (CONTEXT.md "Sequence Coordinator").
//
// The run is a cursor over this droid's own Servo Output rows, read one at a
// time from the live table at the moment each row's turn comes. Nothing is
// authored into the catalog and nothing is cached: a row saved between two
// steps is read as it now stands, the same rule a Body Step's Part-to-Output
// join already follows (include/sequence_body_step.h).
//
// Pure: no NVS, no FreeRTOS, no Arduino, no clock. The Coordinator owns the
// state and passes millis() and the rows; this header owns the rules, so they
// can be tested away from the 10 ms task loop that applies them -- the same
// split include/servo_hold.h made for the calibration dial's two bounds.
// =============================================================================

#pragma once

#include <stdint.h>

#include "output_wire.h"          // outputWireCentreable() - whether a row has travel
#include "sequence_body_step.h"   // SeqBodyStepPlan, sequenceBodyCentrePlan()
#include "servo_output_row.h"     // ServoOutputRow - this droid's own wiring

// -----------------------------------------------------------------------------
// The Cadence Floor
//
// The minimum spacing the Sequence Coordinator holds between body Outputs it
// starts when it expands something itself. Overlapping servo inrush is what
// browns a controller out, and the fix that established the number closes ring
// panels one at a time rather than as a group.
//
// PROVENANCE, AND IT MATTERS: 450 ms is the DOME's figure, measured on DOME
// hardware on 2026-06-17/-18, where seven ring servos share the dome supply
// (src/tasks/sequence_catalog.cpp, DM:RESET's staggered ring closes). THE
// BODY'S CADENCE FLOOR IS UNMEASURED. The body has thirteen to twenty-three
// servos on a shared rail and has never been browned out on a bench, so nobody
// knows its number. This constant is the dome's figure adopted DELIBERATELY AND
// EXPLICITLY as a stand-in until the body's own is taken (#355 carries that
// measurement, and it needs a built body). Do not restate it anywhere as a body
// figure, and do not quietly let it become one by copying it into a comment
// that drops this paragraph.
//
// CONTEXT.md says the number is settable. It is not settable yet, and that is
// deliberate: a builder cannot be asked to set a number nobody has measured.
// Whoever takes the body measurement is the one who decides whether it becomes
// a stored field or a better constant.
//
// It bounds THIS expansion and nothing else. It is not retrofitted onto
// authored steps: the `:SE` Factory routines fire nine BODY_CLOSE steps at
// t=0 (kSe31Steps), and flooring those would change what seven shipped
// sequences do, on a number nobody has measured for the body, and would rewrite
// what an author asked for. Pacing what you generated is not rewriting what
// somebody wrote (CONTEXT.md "Cadence Floor").
// -----------------------------------------------------------------------------
constexpr uint32_t SEQ_CADENCE_FLOOR_MS = 450;

// -----------------------------------------------------------------------------
// sequenceCadenceSpacingMs()
// How long after starting one Output the next may start.
//
// The Output's own physics is the first answer: `throw_ms` is how long a full
// throw of that Output takes, which is what its builder set and what ADR 0052
// stores in place of a rate. One servo actuating at a time is the rail rule, so
// a slow door holds the next Output off for as long as it is moving.
//
// The Cadence Floor is the second answer, and it FLOORS SILENTLY RATHER THAN
// REFUSING (r2d2-astromech-simulator v1.79.0, src/js/maestro/blocks.js:180 --
// blockMinTravelMs() returns the rail's pace for a duration faster than the
// rail allows, with no error dialog). A 200 ms throw does not get a warning and
// does not get refused; it gets 450 ms and the sweep carries on.
//
// Nothing here rewrites the throw. The Output is still asked to move exactly
// as far, exactly as fast, as its own Motion Profile says -- the only thing
// this decides is when the NEXT one is allowed to start (#365 criterion 5).
// -----------------------------------------------------------------------------
inline uint32_t sequenceCadenceSpacingMs(uint16_t throwMs) {
    return ((uint32_t)throwMs > SEQ_CADENCE_FLOOR_MS) ? (uint32_t)throwMs
                                                      : SEQ_CADENCE_FLOOR_MS;
}

// -----------------------------------------------------------------------------
// sequenceBulkCentreHasTravel()
// Whether a bulk centre covers this row at all.
//
// A light has no centre to go back to. "Back to centre" is a motion act, and
// inventing a position for a part that does not move would decide something
// #320 deliberately left open, so a row with nothing to travel is skipped and
// counted rather than driven to a number that means nothing. Which rows those
// are is outputWireCentreable()'s answer (include/output_wire.h), beside the
// two other things a wire's Light Type decides and the reason each differs.
// -----------------------------------------------------------------------------
inline bool sequenceBulkCentreHasTravel(const ServoOutputRow& row) {
    return outputWireCentreable(row);
}

// -----------------------------------------------------------------------------
// The run
//
// `nextRow` is the index of the row whose turn it is, `dueMs` the earliest it
// may start, and the two counters are what the Coordinator reports when the
// sweep ends. `src` is who pressed, kept so the log line can say.
//
// The counters survive the run ending, whatever ended it, so an estop that cut
// a sweep short can still be reported as "four of nine" rather than as nothing.
// Starting a run is what zeroes them.
// -----------------------------------------------------------------------------
struct SeqBulkCentreRun {
    bool     active;
    uint8_t  nextRow;
    uint32_t dueMs;
    uint8_t  centred;  // Outputs this run has started moving
    uint8_t  skipped;  // rows it passed over, with nothing to centre
    uint8_t  src;      // CommandSource of the operator who asked
};

// -----------------------------------------------------------------------------
// sequenceBulkCentreStart()
// The operator pressed. The first row is due at once -- the Floor spaces
// Outputs from each other, and there is nothing yet to space this one from.
//
// Restarting supersedes whatever was in flight rather than queueing behind it:
// the second press is the operator's latest word about what they want the droid
// to do, and two overlapping sweeps is the many-at-once shape the Floor exists
// to prevent.
// -----------------------------------------------------------------------------
inline void sequenceBulkCentreStart(SeqBulkCentreRun* run, uint32_t nowMs, uint8_t src) {
    if (run == nullptr) {
        return;
    }
    run->active = true;
    run->nextRow = 0;
    run->dueMs = nowMs;
    run->centred = 0;
    run->skipped = 0;
    run->src = src;
}

// -----------------------------------------------------------------------------
// sequenceBulkCentreEnd()
// The run is over, whoever ended it: an estop, Sleep Mode, a stop, a sequence
// taking over, or the last row. Nothing is commanded on the way out -- an
// estop releases every Output on its own edge (ADR 0043), and driving anything
// to a position as a run ends would be exactly the park ADR 0043 replaced.
// -----------------------------------------------------------------------------
inline void sequenceBulkCentreEnd(SeqBulkCentreRun* run) {
    if (run != nullptr) {
        run->active = false;
    }
}

// -----------------------------------------------------------------------------
// sequenceBulkCentreRowDue()
// Whether the row whose turn it is may be dealt with now. Unsigned subtraction
// handles millis() wrapping, the way every other due-time test here does.
// -----------------------------------------------------------------------------
inline bool sequenceBulkCentreRowDue(const SeqBulkCentreRun& run, uint32_t nowMs) {
    return run.active && (int32_t)(nowMs - run.dueMs) >= 0;
}

// -----------------------------------------------------------------------------
// sequenceBulkCentreAdvance()
// The row whose turn it was has been dealt with; move the cursor on.
//
// `started` says whether that row was actually commanded. A started row spaces
// the next one by sequenceCadenceSpacingMs(); a skipped one spaces nothing,
// because nothing moved and there is no inrush to hold apart.
//
// `rowCount` is re-read from the live table on every step rather than captured
// at the start, so a table that shrank under the run ends it here instead of
// walking past the end of it.
// -----------------------------------------------------------------------------
inline void sequenceBulkCentreAdvance(SeqBulkCentreRun* run, uint8_t rowCount, uint32_t nowMs,
                                      bool started, uint16_t throwMs) {
    if (run == nullptr || !run->active) {
        return;
    }
    if (started) {
        run->centred++;
        run->dueMs = nowMs + sequenceCadenceSpacingMs(throwMs);
    } else {
        run->skipped++;
    }
    run->nextRow++;
    if (run->nextRow >= rowCount) {
        run->active = false;
    }
}
