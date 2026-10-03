// =============================================================================
// include/sequence_flutter.h
//
// A body flutter, performed (ADR 0049, #453): the Part swings between its
// closed end and the step's how-far point for as long as the flutter lasts,
// and ENDS CLOSED.
//
// A FLUTTER NEVER HOLDS THE ENGINE'S CURSOR. The engine has one timing cursor,
// and a two-second shake held on it would stall a dome step due at t=500. So
// the engine hands a flutter over the moment its step is due and moves on, and
// this run performs it on the Sequence Coordinator's own tick, beside the
// engine -- the shape the bulk centre, the pose press and a Gesture already
// take (include/sequence_bulk_centre.h, include/sequence_gesture.h). The
// sequence's other steps still fire on time. One run performs every flutter:
// a Body Step's, and each member's of a body Gesture.
//
// WHAT A LEG IS. One move of the Part's Output, out to how far or back to the
// closed end, sent as the same ServoCommand a Body Step's move is. Nothing
// here says how fast: each leg moves at the Output's own Motion Profile
// (ADR 0052), or at a Gesture's stated speed and easing where the flutter is a
// Gesture's. How long a leg takes is therefore the Output's answer, handed in
// by the Coordinator (servoMotionArrivalMs()), and the next leg of the same
// Part waits that long and until ServoTask no longer reports the Output
// moving.
//
// WHAT THE CADENCE FLOOR HOLDS APART is legs on DIFFERENT Outputs. A leg is
// generated motion, so it keeps the pace every generated motion keeps
// (sequencePaceMotion()): it starts no sooner after another Output's motion
// than the Floor, or that motion's own time, and not while that Output is
// still moving. Several flutters take turns, ONE WHOLE SWING each -- out, and
// straight back -- so none is starved and at most one Part is ever out. A
// flutter ALONE is not paced by the Floor at all: only its own Output's leg
// time holds its next leg, because the Floor spaces Outputs from each other and
// there is no other.
//
// HOW IT ENDS. An out leg starts only while a whole swing -- out and back --
// still fits in what is left, so the Part is back on its closed end by the
// time the flutter is over. "What is left" runs to the earlier of two moments:
// the flutter's own length, and the end step of the run that fired it. Since
// the back leg follows its out leg directly, with no other flutter's leg and no
// Gesture's move in between, a swing that fitted when it started is not then
// pushed past its limit by anything this task does.
//
// The first swing goes even where the flutter's own length is shorter than one
// swing of its Output: a short flutter is still a flutter. THAT HOLDS ONLY
// INSIDE THE RUN. A first swing that would not be back by the run's end step
// does not start, and the Part stays closed.
//
// The end step is where a run's flutters are cut, and NOTHING IS COMMANDED
// THERE (ADR 0043 for a halt; the same for a normal end). The fit above is why
// that leaves a Part closed. What it cannot cover is the last frames of a back
// leg already sent: ServoTask takes a command up a frame after it is queued,
// so a swing that fitted to the millisecond is still settling when the end
// step comes, and is cut where it has got to -- on its way to the closed end.
// So is a swing delayed by a full servoCmdQueue. The Rehearsal is where a
// builder is told (#441).
//
// Anything else that ends what the Coordinator is doing -- a halt, a stop, a
// later run, a pose, back to centre, a dome resync -- ends the flutters where
// they have got to, out or not, and commands nothing on the way out: a halt
// has already let every Output go. A later move of the same Part ends that
// Part's flutter, and the later move is the one performed.
//
// Pure: no NVS, no FreeRTOS, no Arduino, no clock, no heap. The Coordinator
// owns the run, reads the rows and ServoTask's report, and passes millis().
// =============================================================================

#pragma once

#include <stdint.h>

#include "protocol_check.h"        // PC_SM_MOVE_MIN - the shortest move the model accepts
#include "sequence_bulk_centre.h"  // the pace every generated motion keeps

// How many Parts can flutter at once: one Gesture's worth of members
// (SEQ_GESTURE_MEMBERS_MAX; the Coordinator holds the two together). One more
// is reported and not performed.
constexpr uint8_t SEQ_FLUTTER_PARTS_MAX = 24;
constexpr uint8_t SEQ_FLUTTER_NONE = 0xFF;

// The shortest a leg is taken to last, whatever its Output's profile says: the
// shortest servo move the model accepts, which is also what bounds a flutter's
// own length from below (PC_BODY_FLUTTER_MS_MIN). ServoTask needs a frame of
// its own to take a command up, and until it has, it still reports the Output
// as not moving; a leg timed shorter than that would be replaced before it was
// ever put on the pin.
constexpr uint16_t SEQ_FLUTTER_LEG_MIN_MS = PC_SM_MOVE_MIN;

// -----------------------------------------------------------------------------
// sequenceFlutterLegTimeMs()
// How long one leg holds the Part's next leg off.
//
// `arrivalMs` is the Output's own answer for the leg (servoMotionArrivalMs()).
// Zero is a snap: an Output nobody has measured jumps, and how long the servo
// itself then takes is not something the model knows (ADR 0052). There the
// Cadence Floor stands in, the same answer sequenceCadenceSpacingMs() gives an
// unmeasured throw -- a servo told to reverse every few frames would never
// reach either end.
// -----------------------------------------------------------------------------
inline uint16_t sequenceFlutterLegTimeMs(uint32_t arrivalMs, uint32_t floorMs) {
    uint32_t ms = (arrivalMs == 0) ? floorMs : arrivalMs;
    if (ms < SEQ_FLUTTER_LEG_MIN_MS) ms = SEQ_FLUTTER_LEG_MIN_MS;
    return (ms > 0xFFFFu) ? (uint16_t)0xFFFFu : (uint16_t)ms;
}

// -----------------------------------------------------------------------------
// The run
//
// One entry per fluttering Part, named by its catalog index the way a Gesture
// names its members. The Part is resolved to an Output when each leg goes,
// never before (include/sequence_body_step.h), so `output` is not where the
// Part is wired: it is the Output the LAST leg moved, kept so the next leg can
// wait for it to stop.
//
// `turn` is where the search for the next entry starts, so flutters take turns:
// it stays on an entry that has just gone out, and passes on once it is back.
// `lastLeg` is the entry whose leg was the last generated motion to start, or
// SEQ_FLUTTER_NONE once a Gesture's move has started since: it is how a flutter
// knows its next leg follows its own last one, with no other Output in between.
// -----------------------------------------------------------------------------
struct SeqFlutterEntry {
    bool     active;
    bool     out;       // the last leg went out to how far: the Part is not on its closed end
    bool     swung;     // a leg has gone
    uint8_t  part;      // index into DROID_PART_IDS
    uint8_t  howFar;    // already resolved through seqBodyHowFar()
    uint8_t  easing;    // a Gesture's easing for its legs (ServoEasing + 1); 0 = the Output's own
    uint16_t speedMs;   // a Gesture's full-throw time for its legs; 0 = the Output's own
    uint32_t stopMs;    // when the flutter is over: its length run, or its run's end, the earlier
    uint32_t endAtMs;   // when the run that fired it reaches its end step; 0 = none
    uint32_t dueMs;     // the earliest its next leg may start: when its last leg is over
    ServoOutputAddress output;  // the Output its last leg moved, SERVO_OUTPUT_NONE before the first
};

struct SeqFlutterRun {
    SeqFlutterEntry f[SEQ_FLUTTER_PARTS_MAX];
    uint8_t  turn;
    uint8_t  lastLeg;
    uint16_t legs;      // legs sent since the last flutter started from idle, for the log line
};

// Whether a Part is out: its swing's back leg is the next thing owed. A
// Gesture's body move waits behind it (the Coordinator asks), so nothing comes
// between a swing's two legs.
inline bool sequenceFlutterPartOut(const SeqFlutterRun& run) {
    for (const SeqFlutterEntry& e : run.f) {
        if (e.active && e.out) return true;
    }
    return false;
}

inline bool sequenceFlutterActive(const SeqFlutterRun& run) {
    for (const SeqFlutterEntry& e : run.f) {
        if (e.active) return true;
    }
    return false;
}

// Every flutter is over, where it has got to. Nothing is commanded.
inline void sequenceFlutterEnd(SeqFlutterRun* run) {
    if (run == nullptr) return;
    for (SeqFlutterEntry& e : run->f) e.active = false;
    run->lastLeg = SEQ_FLUTTER_NONE;
}

// A later move of this Part ends its flutter; the later move is the one
// performed. Returns whether one was running.
inline bool sequenceFlutterEndPart(SeqFlutterRun* run, uint8_t part) {
    if (run == nullptr) return false;
    for (SeqFlutterEntry& e : run->f) {
        if (e.active && e.part == part) {
            e.active = false;
            return true;
        }
    }
    return false;
}

// Whether a Part is fluttering. A take holds off it until the swing is over
// (include/take_replay.h, "a step wins").
inline bool sequenceFlutterHasPart(const SeqFlutterRun& run, uint8_t part) {
    for (const SeqFlutterEntry& e : run.f) {
        if (e.active && e.part == part) return true;
    }
    return false;
}

// A generated motion that is not a flutter's leg has started -- a Gesture's
// move. The next leg of any flutter then follows another Output's motion, and
// keeps the pace.
inline void sequenceFlutterOtherMotion(SeqFlutterRun* run) {
    if (run != nullptr) run->lastLeg = SEQ_FLUTTER_NONE;
}

// -----------------------------------------------------------------------------
// sequenceFlutterStart()
// Take a flutter the engine, or a Gesture, has just handed over. A Part already
// fluttering starts again from here: the later word replaces the earlier one.
// False when every entry is taken; the sequence carries on without it.
//
// `runEndAtMs` is when the run that fired it reaches its end step, 0 where no
// run bounds it. The flutter is over by then, however long it says it lasts:
// the end step cuts it and commands nothing, so it has to be closed before it.
//
// No leg goes from here. The first one waits its turn and the pace like every
// other, so two flutters fired on the same tick do not start together.
// -----------------------------------------------------------------------------
inline bool sequenceFlutterStart(SeqFlutterRun* run, uint8_t part, uint8_t howFar,
                                 uint16_t flutterMs, uint16_t speedMs, uint8_t easing,
                                 uint32_t nowMs, uint32_t runEndAtMs) {
    if (run == nullptr) return false;
    if (!sequenceFlutterActive(*run)) run->legs = 0;
    SeqFlutterEntry* slot = nullptr;
    for (SeqFlutterEntry& e : run->f) {
        if (e.active && e.part == part) {
            slot = &e;
            break;
        }
        if (!e.active && slot == nullptr) slot = &e;
    }
    if (slot == nullptr) return false;
    // The entry a leg was last sent from may be this one, re-used: the new
    // flutter's first leg keeps the pace like any first leg.
    if (run->lastLeg == (uint8_t)(slot - run->f)) run->lastLeg = SEQ_FLUTTER_NONE;
    *slot = SeqFlutterEntry{};
    slot->active = true;
    slot->part = part;
    slot->howFar = howFar;
    slot->easing = easing;
    slot->speedMs = speedMs;
    slot->stopMs = nowMs + flutterMs;
    if (runEndAtMs != 0 && (int32_t)(runEndAtMs - slot->stopMs) < 0) {
        slot->stopMs = runEndAtMs;
    }
    slot->endAtMs = runEndAtMs;
    slot->dueMs = nowMs;
    slot->output = SERVO_OUTPUT_NONE;
    return true;
}

// The entry whose turn it is, or -1 when none is fluttering.
inline int8_t sequenceFlutterTurn(const SeqFlutterRun& run) {
    for (uint8_t n = 0; n < SEQ_FLUTTER_PARTS_MAX; ++n) {
        const uint8_t i = (uint8_t)((run.turn + n) % SEQ_FLUTTER_PARTS_MAX);
        if (run.f[i].active) return (int8_t)i;
    }
    return -1;
}

// -----------------------------------------------------------------------------
// sequenceFlutterMayGo()
// Whether the entry whose turn it is may start a leg at nowMs.
//
// Its own last leg has to be over first: its time has run (`dueMs`) and
// ServoTask no longer reports that Output moving (`ownMoving`).
//
// Then the pace. `paceOpen` is whether the pace every generated motion keeps
// lets a motion start now (sequencePaceOpen()). A flutter whose last leg was
// the last generated motion to start is not asked: the next leg is on the same
// Output, and the Cadence Floor spaces Outputs from each other. That is every
// back leg -- the turn stays on a Part that is out -- and, for a flutter on its
// own, the next swing's out leg too.
//
// Between swings it stands aside while a Gesture has a move waiting
// (`othersWaiting`), so a lone flutter's back-to-back swings never shut a
// Gesture's move out: a Gesture's moves are placed in time by its spread, and
// a flutter's have no moment of their own. A Part that is OUT does not stand
// aside: its back leg is what closes it, and a swing that fitted before its
// limit must not be held past it.
// -----------------------------------------------------------------------------
inline bool sequenceFlutterMayGo(const SeqFlutterRun& run, uint8_t idx, uint32_t nowMs,
                                 bool ownMoving, bool paceOpen, bool othersWaiting) {
    if (idx >= SEQ_FLUTTER_PARTS_MAX || !run.f[idx].active) return false;
    const SeqFlutterEntry& e = run.f[idx];
    if ((int32_t)(nowMs - e.dueMs) < 0 || ownMoving) return false;
    const bool followsItsOwn = run.lastLeg == idx && (e.out || !othersWaiting);
    return followsItsOwn || paceOpen;
}

// Whether a whole swing -- out, then back -- starting at atMs is over by
// limitMs.
inline bool sequenceFlutterSwingOverBy(uint32_t limitMs, uint32_t atMs, uint16_t outMs,
                                       uint16_t backMs) {
    const int32_t left = (int32_t)(limitMs - atMs);
    return left > 0 && (uint32_t)left >= (uint32_t)outMs + (uint32_t)backMs;
}

// Whether a whole swing starting at atMs is over by the time the flutter is.
inline bool sequenceFlutterSwingFits(const SeqFlutterEntry& e, uint32_t atMs, uint16_t outMs,
                                     uint16_t backMs) {
    return sequenceFlutterSwingOverBy(e.stopMs, atMs, outMs, backMs);
}

// -----------------------------------------------------------------------------
// sequenceFlutterLeg()
// What the entry does with its turn: a leg out, a leg back, or nothing more.
//
// `outMs` and `backMs` are how long each leg takes on the Output the Part is
// on now (sequenceFlutterLegTimeMs()). A Part that is out comes back, whatever
// the time. A Part on its closed end goes out only while a whole swing still
// fits before the flutter is over. The first swing also goes where the
// flutter's own length is too short for it -- but never where it would not be
// back by the run's end step.
// -----------------------------------------------------------------------------
enum SeqFlutterLeg : uint8_t {
    SEQ_FLUTTER_LEG_OUT = 0,
    SEQ_FLUTTER_LEG_BACK,
    SEQ_FLUTTER_OVER,
};

inline SeqFlutterLeg sequenceFlutterLeg(const SeqFlutterEntry& e, uint32_t nowMs, uint16_t outMs,
                                        uint16_t backMs) {
    if (e.out) return SEQ_FLUTTER_LEG_BACK;
    if (sequenceFlutterSwingFits(e, nowMs, outMs, backMs)) return SEQ_FLUTTER_LEG_OUT;
    const bool insideTheRun =
        e.endAtMs == 0 || sequenceFlutterSwingOverBy(e.endAtMs, nowMs, outMs, backMs);
    return (!e.swung && insideTheRun) ? SEQ_FLUTTER_LEG_OUT : SEQ_FLUTTER_OVER;
}

// The entry is over without a leg: its length has run with the Part closed, or
// nothing can move its Part any more. The turn passes on.
inline void sequenceFlutterDrop(SeqFlutterRun* run, uint8_t idx) {
    if (run == nullptr || idx >= SEQ_FLUTTER_PARTS_MAX) return;
    run->f[idx].active = false;
    run->turn = (uint8_t)((idx + 1) % SEQ_FLUTTER_PARTS_MAX);
}

// -----------------------------------------------------------------------------
// sequenceFlutterSent()
// The leg sequenceFlutterLeg() chose reached ServoTask at nowMs, on `output`.
//
// It holds the Part's own next leg off for its time, and every other generated
// motion off by the pace (`paceDueMs`, `paceAwait`: the Coordinator's one pace
// for what a sequence generates, shared with its Gestures; `floorMs` is the
// Cadence Floor in use). The turn stays on a Part that has gone out, so its
// back leg is the next leg sent, and passes on once it is on its way back. A
// leg back that leaves no room for another swing is the flutter's last: the
// entry ends here, with the Part on its way to its closed end.
// -----------------------------------------------------------------------------
inline void sequenceFlutterSent(SeqFlutterRun* run, uint8_t idx, uint32_t nowMs, SeqFlutterLeg leg,
                                uint16_t outMs, uint16_t backMs, ServoOutputAddress output,
                                uint32_t* paceDueMs, ServoOutputAddress* paceAwait,
                                uint32_t floorMs) {
    if (run == nullptr || idx >= SEQ_FLUTTER_PARTS_MAX || leg == SEQ_FLUTTER_OVER) return;
    SeqFlutterEntry& e = run->f[idx];
    const uint16_t legMs = (leg == SEQ_FLUTTER_LEG_OUT) ? outMs : backMs;
    e.out = (leg == SEQ_FLUTTER_LEG_OUT);
    e.swung = true;
    e.output = output;
    e.dueMs = nowMs + legMs;
    sequencePaceMotion(paceDueMs, paceAwait, nowMs, /*started=*/true, /*moves=*/true,
                       /*bodyOutput=*/true, legMs, output, floorMs);
    run->lastLeg = idx;
    run->turn = e.out ? idx : (uint8_t)((idx + 1) % SEQ_FLUTTER_PARTS_MAX);
    run->legs++;
    if (!e.out && !sequenceFlutterSwingFits(e, e.dueMs, outMs, backMs)) {
        e.active = false;
    }
}
