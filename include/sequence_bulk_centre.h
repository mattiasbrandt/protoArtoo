// =============================================================================
// include/sequence_bulk_centre.h
//
// Put every Servo Output back to centre, one press, paced by the droid
// (#318, #365; GLOSSARY.md "Cadence Floor") -- and, on the same cursor, the boot
// pass that sends each Output home at power-up as its boot behaviour says
// (ADR 0052, #414).
//
// One operator standing at the bench chooses this, which is what makes it
// legitimate: it is never emitted automatically, and that is the line ADR 0043
// and ADR 0049 drew when they refused many-at-once motion with nobody deciding.
// What the operator asks for is the whole act; the EXPANSION -- which Outputs,
// in what order, how far apart -- is the Sequence Coordinator's and lives here,
// never in the browser, because a safe pace a page holds is one a hand-edited
// or imported client could walk around (GLOSSARY.md "Sequence Coordinator").
//
// The boot pass is the other thing this cursor runs, and it is the same act
// asked a different way: nobody pressed, so each row's own boot behaviour
// decides whether it goes home at all (sequenceBulkCentreRowStep()). It is
// generated motion like the press, so it takes the same Floor, the same one-row
// -per-tick cursor and the same halt rule, rather than a second pacing machine
// beside this one (ADR 0052: "the boot pass is generated, so the Cadence Floor
// paces it").
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
// knows its number. This default is the dome's figure adopted DELIBERATELY AND
// EXPLICITLY as a stand-in until the body's own is taken (#355 carries that
// measurement, and it needs a built body). Do not restate it anywhere as a body
// figure, and do not quietly let it become one by copying it into a comment
// that drops this paragraph.
//
// THE FLOOR IS A STORED SETTING, and this constant is only what a droid nobody
// has set holds (#453; GLOSSARY.md "Cadence Floor" has said settable from the
// start). It lives on the droid as the Setting `cadenceFloorMs`
// (src/config_settings.cpp): GET /api/config reports it at
// `servo.cadenceFloorMs`, POST /api/config and the Controller Console's
// `servo.config.cadence-floor` set it, and the Sequence Coordinator reads it
// from the config cache every time it paces something
// (configCacheCadenceFloorMs()). Setting it does not make it measured: every
// surface that shows the number still says the body's own is unmeasured.
//
// The rules below take the floor as an argument, so they stay pure and say
// nothing about where the number came from.
//
// It bounds what the Coordinator GENERATES and nothing else: the bulk centre,
// the boot pass, a pose, a body Gesture's expansion, a flutter's legs. It is
// not retrofitted onto authored steps: the `:SE` Factory routines fire nine
// BODY_CLOSE steps at t=0 (kSe31Steps), and flooring those would change what
// seven shipped sequences do, on a number nobody has measured for the body,
// and would rewrite what an author asked for. Pacing what you generated is not
// rewriting what somebody wrote (GLOSSARY.md "Cadence Floor").
// -----------------------------------------------------------------------------
constexpr uint32_t SEQ_CADENCE_FLOOR_MS = 450;

// What the Setting accepts. Never zero: a floor of nothing is no floor, and
// every Output the Coordinator generated would start together. Both ends are
// the model's own rather than a measurement of the body, which nobody has
// taken: the shortest and the longest servo move a sequence may ask for
// (PC_SM_MOVE_MIN and PC_SM_MOVE_MAX, include/protocol_check.h; src/
// config_settings.cpp holds each pair equal).
constexpr uint32_t SEQ_CADENCE_FLOOR_MIN_MS = 50;
constexpr uint32_t SEQ_CADENCE_FLOOR_MAX_MS = 5000;

// The floor to pace by, given what is stored. A value outside what the Setting
// accepts cannot be saved and is repaired on load, so this is the last line
// rather than the rule: whatever reaches here, the floor in use is never zero.
inline uint32_t sequenceCadenceFloorInUse(uint32_t storedMs) {
    return (storedMs < SEQ_CADENCE_FLOOR_MIN_MS || storedMs > SEQ_CADENCE_FLOOR_MAX_MS)
               ? SEQ_CADENCE_FLOOR_MS
               : storedMs;
}

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
// does not get refused; it gets the floor and the sweep carries on.
//
// `floorMs` is the floor in use, handed in by the Coordinator from the stored
// Setting.
//
// Nothing here rewrites the throw. The Output is still asked to move exactly
// as far, exactly as fast, as its own Motion Profile says -- the only thing
// this decides is when the NEXT one is allowed to start (#365 criterion 5).
// -----------------------------------------------------------------------------
inline uint32_t sequenceCadenceSpacingMs(uint16_t throwMs, uint32_t floorMs) {
    return ((uint32_t)throwMs > floorMs) ? (uint32_t)throwMs : floorMs;
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
// sweep ends. `src` is who asked, kept so the log line can say; the commands
// the run sends are the Coordinator's own, SRC_SEQ. `kind` is
// which of the two acts this is.
//
// `awaitOutput` is the Output the last started row moved, by its address. The
// next row waits for it: `dueMs` -- that Output's own full-throw time, floored
// -- is the earliest the next row is looked at, and it starts only once
// ServoTask no longer reports that Output moving
// (sequenceBulkCentreAwaitCheck()), so one servo actuates at a time even when
// a move outlasts its throw. `awaitRelease` says
// the row was "go home and release": once the Output has SETTLED its drive
// comes off, and the next row waits behind that too. Nothing blocks on either:
// the run looks again each tick, like a row.
//
// The counters survive the run ending, whatever ended it, so an estop that cut
// a sweep short can still be reported as "four of nine" rather than as nothing.
// Starting a run is what zeroes them.
// -----------------------------------------------------------------------------
enum SeqBulkCentreKind : uint8_t {
    SEQ_BULK_CENTRE_PRESS = 0,  // an operator asked: every row with travel goes to centre
    SEQ_BULK_CENTRE_BOOT,       // power-up: each row does what its boot behaviour says
};

// -----------------------------------------------------------------------------
// The pace every generated motion keeps (#440, #438, #453)
//
// A pose, a body Gesture and a flutter all put motion out one Output at a
// time, on the Coordinator's own cursor rather than a sequence's: the next
// motion waits the last one's spacing -- the Cadence Floor after a dome panel,
// the Output's own full throw, floored, after a body Output -- and, after a
// body Output, until ServoTask stops reporting that Output moving. These are
// that rule, once, over the two fields a run keeps (`dueMs`, `awaitOutput`).
// Nothing awaited is SERVO_OUTPUT_NONE: a run never starts two Outputs at once,
// so `both` is never what it waits on.
// -----------------------------------------------------------------------------

// Whether the Output the last motion moved has stopped, given what ServoTask
// reports. Clears the wait when it has.
inline bool sequencePaceAwaitDone(ServoOutputAddress* awaitOutput, bool outputMoving) {
    if (awaitOutput == nullptr || *awaitOutput == SERVO_OUTPUT_NONE) return true;
    if (outputMoving) return false;
    *awaitOutput = SERVO_OUTPUT_NONE;
    return true;
}

// Whether the pace lets a motion start at nowMs: the Output the last one moved
// has stopped, and its spacing has run.
inline bool sequencePaceOpen(uint32_t dueMs, ServoOutputAddress* awaitOutput, bool outputMoving,
                             uint32_t nowMs) {
    return sequencePaceAwaitDone(awaitOutput, outputMoving) && (int32_t)(nowMs - dueMs) >= 0;
}

// A due time that has passed is moved up to now, which changes nothing about
// whether the pace is open. It is for a pace that outlives its runs, as the one
// a sequence's Gestures and flutters share does: millis() wraps, and a due time
// left standing for 2^31 ms (about 25 days) would read as still to come, holding
// every generated motion off until it came round again. Called on every tick,
// a passed due time is never more than a tick old.
inline void sequencePaceKeepRecent(uint32_t* dueMs, uint32_t nowMs) {
    if (dueMs != nullptr && (int32_t)(nowMs - *dueMs) >= 0) *dueMs = nowMs;
}

// A motion was dealt with at nowMs. A motion that STARTED holds the next one
// off: a dome panel by the Cadence Floor (`floorMs`, the one in use), a body
// Output by its own throw, floored, and until it stops (`output`). One that did
// not start - a Part
// nothing drives, passed over - holds nothing off, because nothing moved; nor
// does a command that moves nothing (`moves` false: a sound, a light).
inline void sequencePaceMotion(uint32_t* dueMs, ServoOutputAddress* awaitOutput, uint32_t nowMs,
                               bool started, bool moves, bool bodyOutput, uint16_t throwMs,
                               ServoOutputAddress output, uint32_t floorMs) {
    if (dueMs == nullptr || awaitOutput == nullptr) return;
    if (!started || !moves) {
        *dueMs = nowMs;
        return;
    }
    if (bodyOutput) {
        *dueMs = nowMs + sequenceCadenceSpacingMs(throwMs, floorMs);
        *awaitOutput = output;
    } else {
        *dueMs = nowMs + floorMs;
    }
}

struct SeqBulkCentreRun {
    bool     active;
    uint8_t  nextRow;
    uint32_t dueMs;
    uint8_t  centred;     // Outputs this run has started moving
    uint8_t  skipped;     // rows it passed over, with nothing to centre
    uint8_t  src;         // CommandSource of the operator who asked
    uint8_t  kind;        // SeqBulkCentreKind
    ServoOutputAddress awaitOutput;  // the Output the next row waits on, or SERVO_OUTPUT_NONE
    bool     awaitRelease;  // and that Output is owed a release once it has settled
};

// -----------------------------------------------------------------------------
// sequenceBulkCentreRowStep()
// What the run does with one row: whether it sends it to centre, and whether it
// then owes it a release.
//
// A row with no travel (a light) is passed over by either act. A press centres
// every other row. The boot pass asks the row:
//
//   limp          -- passed over, and costs the pass no time. Nothing is
//                    commanded, so the Part stays wherever it was left. Limp is
//                    every row's default (servoOutputRowDefaults()) and no
//                    capture changes it, so calibrating an Output is never the
//                    act that makes it move at power-up.
//   home-hold     -- to its recorded centre, and held there.
//   home-release  -- to its recorded centre, then its drive comes off.
//
// Where a home row goes, and whether this image can drive it at all, is
// sequenceBodyCentrePlan()'s answer, the same as for a press: one plan, so an
// Output the press refuses is refused here for the same reason.
// -----------------------------------------------------------------------------
struct SeqBulkCentreRowStep {
    bool centre;        // send this row to its recorded centre
    bool releaseAfter;  // and take its drive off once the move has had its time
};

inline SeqBulkCentreRowStep sequenceBulkCentreRowStep(const SeqBulkCentreRun& run,
                                                      const ServoOutputRow& row) {
    SeqBulkCentreRowStep step = {false, false};
    if (!sequenceBulkCentreHasTravel(row)) {
        return step;
    }
    if (run.kind != SEQ_BULK_CENTRE_BOOT) {
        step.centre = true;
        return step;
    }
    switch (row.boot) {
        case SERVO_BOOT_HOME_HOLD:
            step.centre = true;
            break;
        case SERVO_BOOT_HOME_RELEASE:
            step.centre = true;
            step.releaseAfter = true;
            break;
        case SERVO_BOOT_LIMP:
        default:
            break;
    }
    return step;
}

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
//
// A press also supersedes a boot pass still going: the operator's word is the
// later one, and a release the pass owed is dropped with it -- the press is
// about to send that Output to centre and hold it there anyway. So is the wait
// on an Output still moving: the press begins with the first row at once, the
// way any second press does.
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
    run->kind = SEQ_BULK_CENTRE_PRESS;
    run->awaitOutput = SERVO_OUTPUT_NONE;
    run->awaitRelease = false;
}

// -----------------------------------------------------------------------------
// sequenceBootPassStart()
// Power-up: start the boot pass, or refuse it.
//
// A droid that comes up with the estop latched -- a TWDT reset latches it on
// the way up (failsafe_boot_twdt.h) -- runs NO boot pass. Not a paused one and
// not one deferred to the estop clearing: the estop has let go of every Output
// (ADR 0043), and a pass that started when the halt cleared would drive Parts
// the moment somebody reached in to find out why the droid reset. The same
// holds for Sleep Mode, which releases every Output on the same rule. It is the
// rule a pressed back-to-centre already keeps under either halt: refused, not
// queued.
//
// Who asked is SRC_INTERNAL, the one CommandSource that names the firmware
// acting on its own at boot; the moves themselves go as SRC_SEQ, like every
// run's. Returns whether the pass started.
// -----------------------------------------------------------------------------
inline bool sequenceBootPassStart(SeqBulkCentreRun* run, uint32_t nowMs, bool estopLatched,
                                  bool sleepMode) {
    if (run == nullptr) {
        return false;
    }
    if (estopLatched || sleepMode) {
        run->active = false;
        run->awaitOutput = SERVO_OUTPUT_NONE;
        run->awaitRelease = false;
        return false;
    }
    sequenceBulkCentreStart(run, nowMs, SRC_INTERNAL);
    run->kind = SEQ_BULK_CENTRE_BOOT;
    return true;
}

// -----------------------------------------------------------------------------
// sequenceBulkCentreEnd()
// The run is over, whoever ended it: an estop, Sleep Mode, a stop, a sequence
// taking over, or the last row. Nothing is commanded on the way out -- an
// estop releases every Output on its own edge (ADR 0043), and driving anything
// to a position as a run ends would be exactly the park ADR 0043 replaced.
//
// A release the boot pass still owed is dropped too, for the same reason:
// nothing is sent as a run ends. Under a halt the Output has already been let
// go; after a stop or a sequence taking over it stays where the pass put it,
// held, until whatever took over moves it.
// -----------------------------------------------------------------------------
inline void sequenceBulkCentreEnd(SeqBulkCentreRun* run) {
    if (run != nullptr) {
        run->active = false;
        run->awaitOutput = SERVO_OUTPUT_NONE;
        run->awaitRelease = false;
    }
}

// -----------------------------------------------------------------------------
// sequenceBulkCentreAwait()
// The row just started moved this Output: the next row waits for it, and a
// "go home and release" row is owed its release once it has settled. Call it
// before sequenceBulkCentreAdvance(), so a last row that owes a release keeps
// the run alive until the release has gone.
// -----------------------------------------------------------------------------
inline void sequenceBulkCentreAwait(SeqBulkCentreRun* run, ServoOutputAddress output,
                                    bool release) {
    if (run != nullptr && run->active) {
        run->awaitOutput = output;
        run->awaitRelease = release;
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
// sequenceBulkCentreAwaitCheck()
// Whether the Output the last started row moved has finished with, given where
// ServoTask says it is -- and, for a "go home and release" row, whether its
// release goes now.
//
// The run's `dueMs` -- the Output's full-throw time, floored -- is the EARLIEST
// moment, never the trigger. A move can outlast one full throw: an overshoot
// goes out to its aim and then settles back (servoMotionSettleBack(),
// include/servo_motion_ramp.h), and a move that starts outside the recorded
// ends, where the calibration dial can leave an Output, is longer than a full
// throw. Starting the next row at the throw time would put two Outputs in
// motion together, which is what the Cadence Floor is for (GLOSSARY.md: one
// servo actuating at a time); a release then would cut the drive part way
// through the settle, leaving the Part wherever it had got to. So both wait, a
// tick at a time, until ServoTask no longer reports a move in progress. Equal
// widths are not the test: an overshoot passes through its target on the way
// out. If something else keeps the Output moving -- a dial, a body view's
// press -- the run waits for that too.
//
// An Output with no pulse on it has nothing to release -- ServoTask refused the
// move (an Output switched off, or one that carries a light), or something else
// already let it go -- so the release is dropped rather than sent: a release
// command would re-label why it is limp.
// -----------------------------------------------------------------------------
enum SeqBulkCentreAwait : uint8_t {
    SEQ_AWAIT_WAIT = 0,  // not yet: too early, or the Output is still moving
    SEQ_AWAIT_DONE,      // nothing to wait on any more: the next row may go
    SEQ_AWAIT_RELEASE,   // settled and driven: take its drive off, then go on
    SEQ_AWAIT_DROP,      // a release was owed, but nothing is driven there any more
};

inline SeqBulkCentreAwait sequenceBulkCentreAwaitCheck(const SeqBulkCentreRun& run,
                                                       uint32_t nowMs,
                                                       const ServoCommandedPosition& at) {
    if (!sequenceBulkCentreRowDue(run, nowMs)) {
        return SEQ_AWAIT_WAIT;
    }
    if (run.awaitOutput == SERVO_OUTPUT_NONE) {
        return SEQ_AWAIT_DONE;
    }
    if (at.moving) {
        return SEQ_AWAIT_WAIT;
    }
    if (!run.awaitRelease) {
        return SEQ_AWAIT_DONE;
    }
    return at.pulsing ? SEQ_AWAIT_RELEASE : SEQ_AWAIT_DROP;
}

// -----------------------------------------------------------------------------
// sequenceBulkCentreAwaitOver()
// The Output the run was waiting on has finished: settled, released, or with
// nothing left to release. The run ends here when the cursor had already
// passed the last row -- a release was all it was still alive for.
// -----------------------------------------------------------------------------
inline void sequenceBulkCentreAwaitOver(SeqBulkCentreRun* run, uint8_t rowCount) {
    if (run == nullptr || !run->active) {
        return;
    }
    run->awaitOutput = SERVO_OUTPUT_NONE;
    run->awaitRelease = false;
    if (run->nextRow >= rowCount) {
        run->active = false;
    }
}

// -----------------------------------------------------------------------------
// sequenceBulkCentreAdvance()
// The row whose turn it was has been dealt with; move the cursor on.
//
// `started` says whether that row was actually commanded. A started row spaces
// the next one by sequenceCadenceSpacingMs(), on the Cadence Floor in use
// (`floorMs`); a skipped one spaces nothing, because nothing moved and there is
// no inrush to hold apart.
//
// `rowCount` is re-read from the live table on every step rather than captured
// at the start, so a table that shrank under the run ends it here instead of
// walking past the end of it. A run that still owes a release stays alive past
// its last row until sequenceBulkCentreAwaitOver() says it has gone; one that
// only awaits a move ends, since there is no next row to hold back.
// -----------------------------------------------------------------------------
inline void sequenceBulkCentreAdvance(SeqBulkCentreRun* run, uint8_t rowCount, uint32_t nowMs,
                                      bool started, uint16_t throwMs, uint32_t floorMs) {
    if (run == nullptr || !run->active) {
        return;
    }
    if (started) {
        run->centred++;
        run->dueMs = nowMs + sequenceCadenceSpacingMs(throwMs, floorMs);
    } else {
        run->skipped++;
    }
    run->nextRow++;
    if (run->nextRow >= rowCount && !run->awaitRelease) {
        run->active = false;
        run->awaitOutput = SERVO_OUTPUT_NONE;
    }
}
