// =============================================================================
// include/sequence_gesture.h
//
// A Gesture: one authored move spread across many Parts (ADR 0046, #438).
//
// It is two independent choices -- what each Part does (the Move Shape) and how
// the move travels across the set (the spread) -- with a direction, a start,
// a pace, and speed, easing and how far as parameters. It names its Parts as
// ONE token ("ring"), or as an explicit list of Part ids, and never as the
// panels or Outputs it meant on the day: the set is resolved when the sequence
// RUNS, against the droid as it is then. A Part fitted after the save is in it;
// a Part nothing can move is reported and the rest performs; nothing is ever
// dropped from what was stored.
//
// ORDER comes from where the Parts are, not from how they were listed: the
// catalog's bearing table (include/droid_parts.h, generated), clockwise or
// counter-clockwise from a start. A Part with no bearing -- every body Part
// today -- goes last, in catalog order, and is never refused.
//
// WHO PERFORMS IT is decided by who owns the Parts (Coordinator Resolution): a
// dome set is performed by the dome under Catalog Authority, through one of
// its `$` commands, and a body set is expanded by the Sequence Coordinator into
// individual Output moves paced by the Cadence Floor. The body NEVER expands a
// dome Gesture into individual panel commands (ADR 0046's rejected option: two
// motion authorities for one set of panels). A (shape, spread) pair the dome
// has no command for still saves; the Rehearsal says what happens instead.
//
// This header is the one home of the vocabulary and the rules. The browser
// mirrors them in data/seq_gesture.js, which says so at each table; a change
// here is a change there.
//
// HOW THE PARAMETERS ARE STORED. A Gesture is a step, so it lives in SeqStep and
// SeqStepParams like every other, and SeqStepParams is one flat POD whose
// members mean different things per step type (durationMs is a loop's and a
// dome turn's). A Gesture reuses members the other types leave idle rather
// than growing every step in the catalog and every run buffer by the fields
// only a Gesture has; the accessors below are the only readers, and they name
// what each member means here:
//
//   Gesture meaning     SeqStepParams member   width
//   spread              slotSet                u8
//   direction           pickDistinct           u8
//   start               audioCategory          u8
//   easing              audioFallbackSlot      u8
//   stepMs (the pace)   moveMs                 u16
//   speedMs             jitterMs               u16
//   repeatMs            periodMs               u16
//   extentMs            durationMs             u32
//   shape/howFar/flutterMs  as a Body Step
//
// The set token, or the comma-separated Part ids, is the payload.
//
// Pure: no Arduino, no FreeRTOS, no heap.
// =============================================================================

#pragma once

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "droid_parts.h"           // the Part ids, their bearings and the sets
#include "sequence_bulk_centre.h"  // the Cadence Floor and the pace generated motion keeps
#include "sequence_engine.h"       // SeqStep, SeqStepParams, SeqBodyShape

// -----------------------------------------------------------------------------
// The spread: how a move travels across the set. The id is stored; the browser
// labels each with its timing rule in beats (data/seq_gesture.js), so a picker
// needs no help text. `step` is the Gesture's pace -- one beat of the tempo,
// or a stated number of milliseconds.
//
//   together   every member on the step
//   wave       one member per step, in order, and each stays
//   chase      one member per step, in order, and the one before goes back
//   alternate  every member on one step, and every member back on the next
//   pulse      every member on the step, and back on the half step
//
// "Goes back" is the shape's undo: an open closes, a close opens, and a
// flutter closes -- the close ends a flutter still going, and one already over
// ended closed anyway (ADR 0049, amended 2026-10-02).
// -----------------------------------------------------------------------------
enum SeqGestureSpread : uint8_t {
    GESTURE_SPREAD_TOGETHER  = 0,
    GESTURE_SPREAD_WAVE      = 1,
    GESTURE_SPREAD_CHASE     = 2,
    GESTURE_SPREAD_ALTERNATE = 3,
    GESTURE_SPREAD_PULSE     = 4,
    GESTURE_SPREAD_COUNT     = 5,
};

inline const char* seqGestureSpreadToString(uint8_t spread) {
    switch (spread) {
        case GESTURE_SPREAD_WAVE:      return "wave";
        case GESTURE_SPREAD_CHASE:     return "chase";
        case GESTURE_SPREAD_ALTERNATE: return "alternate";
        case GESTURE_SPREAD_PULSE:     return "pulse";
        case GESTURE_SPREAD_TOGETHER:
        default:                       return "together";
    }
}

// Which way round the droid the order runs, viewed from above.
enum SeqGestureDirection : uint8_t {
    GESTURE_DIR_CLOCKWISE        = 0,
    GESTURE_DIR_COUNTERCLOCKWISE = 1,
    GESTURE_DIR_COUNT            = 2,
};

inline const char* seqGestureDirectionToString(uint8_t direction) {
    return direction == GESTURE_DIR_COUNTERCLOCKWISE ? "ccw" : "cw";
}

// Where the order starts. Bearings are clockwise viewed from above with 0 dead
// astern and 180 dead ahead (DROID_BEARING_DEAD_AHEAD_TENTHS, the catalog's
// convention), so from above, facing the way the droid faces, 90 is its left
// and 270 its right. Every start is measured from the one constant, so a
// change of convention is one edit.
enum SeqGestureStart : uint8_t {
    GESTURE_START_FRONT = 0,
    GESTURE_START_RIGHT = 1,
    GESTURE_START_REAR  = 2,
    GESTURE_START_LEFT  = 3,
    GESTURE_START_COUNT = 4,
};

inline const char* seqGestureStartToString(uint8_t start) {
    switch (start) {
        case GESTURE_START_RIGHT: return "right";
        case GESTURE_START_REAR:  return "rear";
        case GESTURE_START_LEFT:  return "left";
        case GESTURE_START_FRONT:
        default:                  return "front";
    }
}

inline int16_t seqGestureStartBearingTenths(uint8_t start) {
    // A quarter turn clockwise from the front at a time, from the one constant.
    const int16_t quarter = 900;
    const int16_t turns = (start < GESTURE_START_COUNT) ? (int16_t)start : 0;
    return (int16_t)((DROID_BEARING_DEAD_AHEAD_TENTHS + turns * quarter) % 3600);
}

// Easing, the Motion Profile's own three words (CONTEXT.md "Motion Profile").
// GESTURE_EASING_OUTPUT means the Gesture says nothing and the Output's own
// profile applies, which is the default and is stored as absence.
// The values are ServoEasing + 1 on purpose (include/servo_output_row.h), so a
// Gesture's easing reaches ServoCommand::motionEasing as it is stored.
enum SeqGestureEasing : uint8_t {
    GESTURE_EASING_OUTPUT    = 0,
    GESTURE_EASING_NONE      = 1,
    GESTURE_EASING_SOFT      = 2,
    GESTURE_EASING_OVERSHOOT = 3,
    GESTURE_EASING_COUNT     = 4,
};

inline const char* seqGestureEasingToString(uint8_t easing) {
    switch (easing) {
        case GESTURE_EASING_NONE:      return "none";
        case GESTURE_EASING_SOFT:      return "soft";
        case GESTURE_EASING_OVERSHOOT: return "overshoot";
        case GESTURE_EASING_OUTPUT:
        default:                       return "";
    }
}

// A token -> value, walking the spelling function so each vocabulary has one
// home. `count` bounds the walk; an empty spelling (easing's "say nothing") is
// never matched.
inline bool seqGestureTokenFromString(const char* s, const char* (*spell)(uint8_t),
                                      uint8_t count, uint8_t& out) {
    if (s == nullptr || s[0] == '\0') return false;
    for (uint8_t v = 0; v < count; ++v) {
        if (strcmp(spell(v), s) == 0) {
            out = v;
            return true;
        }
    }
    return false;
}

// The pace a Gesture takes when it states none and the sequence has no tempo
// to lend it a beat: one beat at 120 BPM. With a tempo, the default is one
// beat of it, resolved when the sequence is parsed (src/seq_json.cpp).
constexpr uint16_t SEQ_GESTURE_STEP_DEFAULT_MS = 500;

// -----------------------------------------------------------------------------
// The accessors (see the table in the header comment).
// -----------------------------------------------------------------------------
inline uint8_t  seqGestureSpread(const SeqStepParams& p)    { return p.slotSet; }
inline uint8_t  seqGestureDirection(const SeqStepParams& p) { return p.pickDistinct; }
inline uint8_t  seqGestureStart(const SeqStepParams& p)     { return p.audioCategory; }
inline uint8_t  seqGestureEasing(const SeqStepParams& p)    { return p.audioFallbackSlot; }
inline uint16_t seqGestureStepMs(const SeqStepParams& p) {
    return p.moveMs != 0 ? p.moveMs : SEQ_GESTURE_STEP_DEFAULT_MS;
}
// How long each member of a flutter Gesture flutters: what the Gesture states,
// or one step of its own pace when it states none -- the step is the Gesture's
// unit of time, and on it a member's move is over before the next step's.
inline uint16_t seqGestureFlutterMs(const SeqStepParams& p) {
    return p.flutterMs != 0 ? p.flutterMs : seqGestureStepMs(p);
}
inline uint16_t seqGestureSpeedMs(const SeqStepParams& p)   { return p.jitterMs; }
inline uint16_t seqGestureRepeatMs(const SeqStepParams& p)  { return p.periodMs; }
inline uint32_t seqGestureExtentMs(const SeqStepParams& p)  { return p.durationMs; }

inline void seqGestureSetSpread(SeqStepParams& p, uint8_t v)    { p.slotSet = v; }
inline void seqGestureSetDirection(SeqStepParams& p, uint8_t v) { p.pickDistinct = v; }
inline void seqGestureSetStart(SeqStepParams& p, uint8_t v)     { p.audioCategory = v; }
inline void seqGestureSetEasing(SeqStepParams& p, uint8_t v)    { p.audioFallbackSlot = v; }
inline void seqGestureSetStepMs(SeqStepParams& p, uint16_t v)   { p.moveMs = v; }
inline void seqGestureSetSpeedMs(SeqStepParams& p, uint16_t v)  { p.jitterMs = v; }
inline void seqGestureSetRepeatMs(SeqStepParams& p, uint16_t v) { p.periodMs = v; }
inline void seqGestureSetExtentMs(SeqStepParams& p, uint32_t v) { p.durationMs = v; }

// -----------------------------------------------------------------------------
// The members, resolved when the sequence runs.
//
// `payload` is a set token or a comma-separated list of Part ids. The answer is
// indices into DROID_PART_IDS, in the Gesture's own order. An id this build no
// longer declares -- a sequence carried across a firmware update that dropped
// it -- cannot be addressed and is passed over; it stays in what was stored.
// -----------------------------------------------------------------------------
constexpr uint8_t SEQ_GESTURE_MEMBERS_MAX = 24;

// Members are held as bytes; a catalog that outgrows one is a build failure
// here rather than a Part index that wraps.
static_assert(DROID_PART_COUNT <= 255, "a Gesture member index is a byte");

inline bool seqGestureIsList(const char* payload) {
    return payload != nullptr && droidPartSetFind(payload) == nullptr;
}

// Visit every Part id in an explicit list, in the order written. Returns the
// number of ids, and calls `visit` for each with a NUL-terminated copy.
template <typename Visit>
inline uint8_t seqGestureEachListed(const char* payload, Visit visit) {
    uint8_t n = 0;
    const char* p = payload;
    while (p != nullptr && *p != '\0') {
        const char* comma = strchr(p, ',');
        const size_t len = (comma != nullptr) ? (size_t)(comma - p) : strlen(p);
        char id[DROID_PART_ID_MAX_LEN + 1];
        if (len > 0 && len <= DROID_PART_ID_MAX_LEN) {
            memcpy(id, p, len);
            id[len] = '\0';
            visit(id);
        } else {
            visit("");  // too long or empty: never an id, so never known
        }
        ++n;
        p = (comma != nullptr) ? comma + 1 : nullptr;
    }
    return n;
}

// Whether the Gesture's Parts are the dome's: its set's half, or the first
// listed Part's (Protocol Check refuses a list that spans both halves).
inline bool seqGestureIsDome(const char* payload) {
    const DroidPartSet* set = droidPartSetFind(payload);
    if (set != nullptr) return set->dome;
    bool dome = false;
    bool first = true;
    seqGestureEachListed(payload, [&](const char* id) {
        const size_t i = droidPartIndexOf(id);
        if (first && i < DROID_PART_COUNT) dome = DROID_PART_ON_DOME[i];
        first = false;
    });
    return dome;
}

// Clockwise angular distance from the start, in tenths; a Part with no bearing
// sorts after every Part that has one.
inline int32_t seqGestureOrderKey(uint8_t partIndex, uint8_t start, uint8_t direction) {
    const int16_t bearing = DROID_PART_BEARING_TENTHS[partIndex];
    if (bearing == DROID_BEARING_NONE) return 100000 + partIndex;
    const int32_t from = seqGestureStartBearingTenths(start);
    const int32_t cw = ((int32_t)bearing - from + 3600) % 3600;
    return (direction == GESTURE_DIR_COUNTERCLOCKWISE) ? (3600 - cw) % 3600 : cw;
}

// The Gesture's members, in its order. Returns how many were written.
inline uint8_t seqGestureMembers(const SeqStep& step, uint8_t* out, uint8_t cap) {
    uint8_t n = 0;
    const DroidPartSet* set = droidPartSetFind(step.payload);
    if (set != nullptr) {
        for (uint8_t m = 0; m < set->count && n < cap; ++m) out[n++] = set->members[m];
    } else {
        seqGestureEachListed(step.payload, [&](const char* id) {
            const size_t i = droidPartIndexOf(id);
            if (i < DROID_PART_COUNT && n < cap) out[n++] = (uint8_t)i;
        });
    }
    const uint8_t start = seqGestureStart(step.params);
    const uint8_t direction = seqGestureDirection(step.params);
    // Insertion sort, stable, in place: at most SEQ_GESTURE_MEMBERS_MAX entries.
    for (uint8_t a = 1; a < n; ++a) {
        const uint8_t moving = out[a];
        const int32_t key = seqGestureOrderKey(moving, start, direction);
        uint8_t b = a;
        while (b > 0 && seqGestureOrderKey(out[b - 1], start, direction) > key) {
            out[b] = out[b - 1];
            --b;
        }
        out[b] = moving;
    }
    return n;
}

// -----------------------------------------------------------------------------
// One pass of the spread, as moves in time order.
//
// seqGesturePassMoves(spread, n) is how many moves a pass over n members makes,
// and seqGesturePassMove() the k-th of them: which member (its position in the
// Gesture's order), when (ms from the pass start, at `stepMs` a step) and
// whether it is the shape or its undo. Where a chase's next member goes and
// the one before goes back on the same step, the one before goes back first,
// so the chase never has two members out at once.
// -----------------------------------------------------------------------------
struct SeqGestureMove {
    uint8_t  member;
    uint32_t atMs;
    bool     undo;
};

inline uint16_t seqGesturePassMoves(uint8_t spread, uint8_t n) {
    switch (spread) {
        case GESTURE_SPREAD_CHASE:
        case GESTURE_SPREAD_ALTERNATE:
        case GESTURE_SPREAD_PULSE:
            return (uint16_t)(2u * n);
        case GESTURE_SPREAD_TOGETHER:
        case GESTURE_SPREAD_WAVE:
        default:
            return n;
    }
}

inline SeqGestureMove seqGesturePassMove(uint8_t spread, uint8_t n, uint16_t stepMs, uint16_t k) {
    SeqGestureMove m = { 0, 0, false };
    switch (spread) {
        case GESTURE_SPREAD_WAVE:
            m.member = (uint8_t)k;
            m.atMs = (uint32_t)k * stepMs;
            break;
        case GESTURE_SPREAD_CHASE:
            if (k == 0) {
                m.member = 0;
            } else {
                const uint16_t j = (uint16_t)((k + 1) / 2);  // the step this move is on
                m.atMs = (uint32_t)j * stepMs;
                if (k % 2 == 1) {
                    m.member = (uint8_t)(j - 1);  // the one before goes back
                    m.undo = true;
                } else {
                    m.member = (uint8_t)j;
                }
            }
            break;
        case GESTURE_SPREAD_ALTERNATE:
        case GESTURE_SPREAD_PULSE:
            m.member = (uint8_t)(k % (n > 0 ? n : 1));
            m.undo = k >= n;
            if (m.undo) m.atMs = (spread == GESTURE_SPREAD_PULSE) ? stepMs / 2u : stepMs;
            break;
        case GESTURE_SPREAD_TOGETHER:
        default:
            m.member = (uint8_t)k;
            break;
    }
    return m;
}

// The shape a move performs: the Gesture's, or its undo.
inline SeqBodyShape seqGestureMoveShape(SeqBodyShape shape, bool undo) {
    if (!undo) return shape;
    return (shape == BODY_SHAPE_CLOSE) ? BODY_SHAPE_OPEN : BODY_SHAPE_CLOSE;
}

// How many passes the Gesture makes: one, or one per repeat while the pass
// start is inside the extent -- the loop's own arithmetic (seqEnginePeek()).
inline uint32_t seqGesturePasses(const SeqStepParams& p) {
    const uint16_t repeat = seqGestureRepeatMs(p);
    const uint32_t extent = seqGestureExtentMs(p);
    if (repeat == 0 || extent == 0) return 1;
    return (extent + repeat - 1) / repeat;
}

// The passes that start before the run that fired the Gesture reaches its end
// step (#438): a Gesture fired at fireMs in a run ending at endAtMs starts no
// pass at or after endAtMs, whatever its extent says. The first pass always
// counts -- it starts as the Gesture fires, which the engine only does before
// the end. endAtMs 0 is a run with no end step, bounded by the extent alone.
// A pass under way at the end is cut there: sequenceGestureNext() ends the
// entry at endAtMs, mid-pass, so no move goes out at or after it.
// Both clocks are the caller's: absolute ms for a run, sequence ms for a pose.
inline uint32_t seqGesturePassesBefore(const SeqStepParams& p, uint32_t fireMs, uint32_t endAtMs) {
    const uint32_t passes = seqGesturePasses(p);
    const uint16_t repeat = seqGestureRepeatMs(p);
    if (endAtMs == 0 || repeat == 0 || passes <= 1) return passes;
    if ((int32_t)(endAtMs - fireMs) <= 0) return 1;
    const uint32_t before = (endAtMs - fireMs + repeat - 1) / repeat;
    return before < passes ? before : passes;
}

// -----------------------------------------------------------------------------
// The dome's side: Coordinator Resolution onto the connected dome's `$` family.
//
// Our fork's dynamic panel-group commands (AstroPixelsPlus MarcduinoPanel.h
// :286-590 at 61303ad) each take a HEX BITMASK of panels, then optional speed,
// delay and two easings. The bits are the fork's own panel addresses,
// AstroPixelsPlus.ino:359-371 at 61303ad (PANEL_P1 = 1 << 14 ... PANEL_PP3 =
// 1 << 26). A Part with no address there -- a fixed panel, P10, a light --
// has nothing the dome can be told, so it is left out of the mask and the
// Rehearsal names it; it stays in the Gesture.
//
// THE MASK IS THE MEMBERS, BUILT WHEN THE STEP RUNS. It is not narrowed by the
// dome's published layout: every member with an address goes in, and the
// dome's own servo table decides which of them move - a bit with no servo
// behind it is passed over by the dome itself (its channel-0 short circuit).
// Reading the 24 KB layout cache on a measured task stack would buy no
// behaviour the dome does not already give.
//
// WHICH COMMAND. The fork's sequences are ReelTwo's (ServoSequencer.h at
// 23.5.3), and each is a fixed pattern over the mask in the DOME'S OWN SERVO
// ORDER, so a direction and a start cannot reach it:
//   (open, together)  :OP$   every member opens, and stays
//   (close, together) :CL$   every member closes
//   (flutter, together) :OF$ every member flutters, and the dome closes them
//   (open, wave)      :OWC$  open one at a time, and the dome closes them again
//   (open, chase)     :OW$   one at a time, the one before closes
//   (open, alternate) :OC$   all open, then all close
//   (open, pulse)     :OCR$  open and close, repeated quickly
// Every other pair has no command: it saves, the Rehearsal warns, and at run
// the dome is sent nothing. Which pairs the dome performs only approximately,
// and why, is the Rehearsal's to say (data/seq_gesture.js mirrors this table).
// -----------------------------------------------------------------------------
struct SeqGestureDomeAddress {
    const char* part;
    uint8_t     bit;
};

inline constexpr SeqGestureDomeAddress SEQ_GESTURE_DOME_ADDRESSES[] = {
    {"panel1", 14}, {"panel2", 15}, {"panel3", 16}, {"panel4", 17},
    {"panel7", 18}, {"panel11", 19}, {"panel13", 20},
    {"pie1", 21}, {"pie2", 22}, {"pie4", 23}, {"pie5", 24}, {"pie6", 25}, {"pie3", 26},
};

// The fork's address bit for a Part, or -1 when the dome has none for it.
inline int8_t seqGestureDomeBit(uint8_t partIndex) {
    const char* id = droidPartIdAt(partIndex);
    for (const SeqGestureDomeAddress& a : SEQ_GESTURE_DOME_ADDRESSES) {
        if (strcmp(a.part, id) == 0) return (int8_t)a.bit;
    }
    return -1;
}

inline const char* seqGestureDomePrefix(SeqBodyShape shape, uint8_t spread) {
    switch (spread) {
        case GESTURE_SPREAD_TOGETHER:
            return shape == BODY_SHAPE_CLOSE ? ":CL$" : shape == BODY_SHAPE_FLUTTER ? ":OF$" : ":OP$";
        case GESTURE_SPREAD_WAVE:      return shape == BODY_SHAPE_OPEN ? ":OWC$" : nullptr;
        case GESTURE_SPREAD_CHASE:     return shape == BODY_SHAPE_OPEN ? ":OW$" : nullptr;
        case GESTURE_SPREAD_ALTERNATE: return shape == BODY_SHAPE_OPEN ? ":OC$" : nullptr;
        case GESTURE_SPREAD_PULSE:     return shape == BODY_SHAPE_OPEN ? ":OCR$" : nullptr;
        default:                       return nullptr;
    }
}

// The mask of every member the dome has an address for.
inline uint32_t seqGestureDomeMask(const uint8_t* members, uint8_t n) {
    uint32_t mask = 0;
    for (uint8_t i = 0; i < n; ++i) {
        const int8_t bit = seqGestureDomeBit(members[i]);
        if (bit >= 0) mask |= (uint32_t)1u << bit;
    }
    return mask;
}

// The dome command a dome Gesture resolves to, e.g. ":OW$1FC000", written into
// out. False when the pair has no command or no member has an address: the
// dome is then sent nothing. The line is at most 14 characters, well inside
// DOME_TX_LINE_MAX (63). The dome's own speed and delay defaults apply; a
// Gesture's speed and easing are the body's Motion Profile words and have no
// meaning on the dome's sequencer, which is a Rehearsal note, not a command.
inline bool seqGestureDomeCommand(const SeqStep& step, char* out, size_t outLen) {
    if (out == nullptr || outLen == 0) return false;
    out[0] = '\0';
    const char* prefix = seqGestureDomePrefix(seqBodyShape(step.params), seqGestureSpread(step.params));
    if (prefix == nullptr) return false;
    uint8_t members[SEQ_GESTURE_MEMBERS_MAX];
    const uint8_t n = seqGestureMembers(step, members, SEQ_GESTURE_MEMBERS_MAX);
    const uint32_t mask = seqGestureDomeMask(members, n);
    if (mask == 0) return false;
    // The hex is written by hand rather than through snprintf: this runs on
    // the Sequence Coordinator's measured stack chain (ADR 0040), and newlib's
    // formatter drags its float path onto it for one %X.
    char hex[9];
    uint8_t digits = 0;
    for (uint32_t v = mask; v != 0 && digits < 8; v >>= 4) {
        hex[digits++] = "0123456789ABCDEF"[v & 0xFu];
    }
    const size_t prefixLen = strlen(prefix);
    if (prefixLen + digits + 1 > outLen) return false;
    memcpy(out, prefix, prefixLen);
    for (uint8_t d = 0; d < digits; ++d) out[prefixLen + d] = hex[digits - 1 - d];
    out[prefixLen + digits] = '\0';
    return true;
}

// -----------------------------------------------------------------------------
// The run: the Sequence Coordinator's cursor over the Gestures a sequence has
// fired (#438).
//
// A GESTURE NEVER HOLDS THE ENGINE'S CURSOR. The engine has one timing cursor,
// and a paced expansion held on it would stall every step behind it (the
// flutter note on #175: a single cursor holding a 2 s flutter stalls a dome
// step due at t=500). So the engine hands the Gesture over the moment it is due
// and moves on, and this run expands it on the Coordinator's own cursor, the
// shape the pose press and the bulk centre already take: the sequence's other
// steps still fire on time.
//
// A BODY Gesture becomes one Output move per member per step of its spread,
// each no earlier than the spread asks and never closer to the last generated
// motion than the pace every generated motion keeps (sequencePaceMotion()) --
// the Cadence Floor, or the moved Output's own throw, and until it stops --
// WHATEVER THE AUTHORED SPREAD ASKS. The browser never holds that pace. A
// member nothing drives is reported at dispatch and passed over, costing no
// time; the rest performs.
//
// A member's FLUTTER is not one move, so it is not sent from here: when the
// spread puts it, it is handed to the flutter run with the Gesture's flutter
// length, speed and easing (include/sequence_flutter.h, #453), which performs
// the legs on this same pace and ends the member closed.
//
// A DOME Gesture is its one `$` command, sent when each pass is due; the dome
// performs it. Nothing about the dome's motion is paced here, because it is
// the dome's (Catalog Authority).
//
// Everything a pass needs is copied out of the step when the Gesture starts:
// its members are resolved THEN, against the droid as it is, and a Learned
// run's step buffers are freed when its run ends while a Gesture may still be
// repeating. Up to four Gestures run at once; a fifth due while four are still
// running is reported and not performed.
// -----------------------------------------------------------------------------
constexpr uint8_t SEQ_GESTURE_RUNS_MAX = 4;

struct SeqGestureRunEntry {
    bool     active;
    bool     dome;
    uint8_t  members[SEQ_GESTURE_MEMBERS_MAX];
    uint8_t  n;
    uint8_t  shape;
    uint8_t  spread;
    uint8_t  howFar;      // already resolved through seqBodyHowFar()
    uint16_t speedMs;     // a full throw's time for its moves; 0 = each Output's own
    uint8_t  easing;      // SeqGestureEasing (ServoEasing + 1); 0 = each Output's own
    uint16_t flutterMs;   // how long each member flutters, already resolved (seqGestureFlutterMs())
    uint16_t stepMs;
    uint16_t repeatMs;
    uint32_t passes;
    uint32_t fireMs;      // when the engine handed it over
    uint32_t endAtMs;     // when the run that fired it reaches its end step; 0 = none
    uint32_t pass;        // the pass under way
    uint16_t k;           // the next move of that pass
    char     domeCmd[16]; // a dome Gesture's command, or "" when it has none
};

struct SeqGestureRun {
    SeqGestureRunEntry g[SEQ_GESTURE_RUNS_MAX];
    uint32_t dueMs;     // the earliest the next BODY move may start
    ServoOutputAddress awaitOutput;  // SERVO_OUTPUT_NONE when nothing is awaited
    uint16_t sent;
    uint16_t skipped;
};

inline void sequenceGestureEnd(SeqGestureRun* run) {
    if (run == nullptr) return;
    for (SeqGestureRunEntry& e : run->g) e.active = false;
    run->awaitOutput = SERVO_OUTPUT_NONE;
}

inline bool sequenceGestureActive(const SeqGestureRun& run) {
    for (const SeqGestureRunEntry& e : run.g) {
        if (e.active) return true;
    }
    return false;
}

// Take a Gesture the engine has just handed over. Returns false when four are
// already running, and when there is nothing to perform: a dome Gesture the
// dome has no command for, or a Gesture none of whose Parts this build knows.
// Neither is an error in the sequence, which carries on.
inline bool sequenceGestureStart(SeqGestureRun* run, const SeqStep& step, uint32_t nowMs,
                                 uint32_t runEndAtMs = 0) {
    if (run == nullptr) return false;
    SeqGestureRunEntry* slot = nullptr;
    for (SeqGestureRunEntry& e : run->g) {
        if (!e.active) {
            slot = &e;
            break;
        }
    }
    if (slot == nullptr) return false;
    // Built in place, in the free slot, rather than on the caller's frame.
    SeqGestureRunEntry& e = *slot;
    memset(&e, 0, sizeof(e));
    e.dome = seqGestureIsDome(step.payload);
    e.n = seqGestureMembers(step, e.members, SEQ_GESTURE_MEMBERS_MAX);
    e.shape = (uint8_t)seqBodyShape(step.params);
    e.spread = seqGestureSpread(step.params);
    e.howFar = seqBodyHowFar(step.params);
    e.speedMs = seqGestureSpeedMs(step.params);
    e.easing = seqGestureEasing(step.params);
    e.flutterMs = seqGestureFlutterMs(step.params);
    e.stepMs = seqGestureStepMs(step.params);
    e.repeatMs = seqGestureRepeatMs(step.params);
    e.passes = seqGesturePassesBefore(step.params, nowMs, runEndAtMs);
    e.endAtMs = runEndAtMs;
    e.fireMs = nowMs;
    if (e.n == 0) return false;
    if (e.dome && !seqGestureDomeCommand(step, e.domeCmd, sizeof(e.domeCmd))) return false;
    e.active = true;
    return true;
}

// What is due next: a dome Gesture's pass, or a body Gesture's move.
struct SeqGestureNext {
    uint8_t      entry;
    bool         dome;
    uint8_t      part;   // a body move's Part, as an index into DROID_PART_IDS
    SeqBodyShape shape;  // a body move's shape: the Gesture's, or its undo
    uint8_t      howFar;
    uint16_t     speedMs;  // the Gesture's override of the Output's throw time, or 0
    uint8_t      easing;   // the Gesture's override of the Output's easing, or 0
    uint16_t     flutterMs;  // a flutter move's length
    uint32_t     endAtMs;    // when the run that fired the Gesture ends; 0 = none
};

inline uint32_t sequenceGestureDueAt(const SeqGestureRunEntry& e) {
    const uint32_t passAt = e.fireMs + e.pass * (uint32_t)e.repeatMs;
    if (e.dome) return passAt;
    return passAt + seqGesturePassMove(e.spread, e.n, e.stepMs, e.k).atMs;
}

// The next thing due at nowMs, or false when nothing may go yet. A dome pass
// goes as soon as it is due. A body move waits for the moment its spread puts
// it at AND for the pace: `awaitedMoving` is whether ServoTask still reports
// the Output the last body move started (run->awaitOutput) as moving, and the
// spacing after it must have run. The earliest-due item goes first.
//
// `bodyHeld` holds every body move back whatever the pace says: the
// Coordinator sets it while a flutter's Part is out, so the back leg that
// closes it goes before anything else moves (include/sequence_flutter.h). A
// dome pass is not held; the dome's motion is the dome's.
//
// NOTHING GOES OUT AT OR AFTER THE FIRING RUN'S END STEP (#438). A Gesture
// whose run has reached its end is over, mid-pass or not: terminal cleanup is
// the last thing the run moves, and the Coordinator clears suppression right
// after it. So an entry is ended here the moment nowMs reaches its run's end,
// and a move the pace pushed past the end is never sent. The Rehearsal says
// when a pass cannot fit before the end (gesture-cut).
inline bool sequenceGestureNext(SeqGestureRun* run, uint32_t nowMs, bool awaitedMoving,
                                SeqGestureNext* out, bool bodyHeld = false) {
    if (run == nullptr || out == nullptr) return false;
    for (SeqGestureRunEntry& e : run->g) {
        if (e.active && e.endAtMs != 0 && (int32_t)(nowMs - e.endAtMs) >= 0) e.active = false;
    }
    const bool bodyMayGo =
        !bodyHeld && sequencePaceOpen(run->dueMs, &run->awaitOutput, awaitedMoving, nowMs);
    int8_t best = -1;
    uint32_t bestAt = 0;
    for (uint8_t i = 0; i < SEQ_GESTURE_RUNS_MAX; ++i) {
        const SeqGestureRunEntry& e = run->g[i];
        if (!e.active || (!e.dome && !bodyMayGo)) continue;
        const uint32_t at = sequenceGestureDueAt(e);
        if ((int32_t)(nowMs - at) < 0) continue;
        if (best < 0 || (int32_t)(at - bestAt) < 0) {
            best = (int8_t)i;
            bestAt = at;
        }
    }
    if (best < 0) return false;
    const SeqGestureRunEntry& e = run->g[best];
    out->entry = (uint8_t)best;
    out->dome = e.dome;
    out->howFar = e.howFar;
    out->speedMs = e.speedMs;
    out->easing = e.easing;
    out->flutterMs = e.flutterMs;
    out->endAtMs = e.endAtMs;
    if (!e.dome) {
        const SeqGestureMove m = seqGesturePassMove(e.spread, e.n, e.stepMs, e.k);
        out->part = e.members[m.member];
        out->shape = seqGestureMoveShape((SeqBodyShape)e.shape, m.undo);
    } else {
        out->part = 0;
        out->shape = (SeqBodyShape)e.shape;
    }
    return true;
}

// Whether a body Gesture has a move whose moment has come and that the pace is
// still holding back. A flutter asks, and stands aside for it
// (sequenceFlutterMayGo(), include/sequence_flutter.h).
inline bool sequenceGestureBodyDue(const SeqGestureRun& run, uint32_t nowMs) {
    for (const SeqGestureRunEntry& e : run.g) {
        if (e.active && !e.dome && (int32_t)(nowMs - sequenceGestureDueAt(e)) >= 0) return true;
    }
    return false;
}

// The cursor moves past the item just dealt with: the next move of the pass,
// or the next pass, or the Gesture is over.
inline void sequenceGestureAdvance(SeqGestureRunEntry& e) {
    if (!e.dome) {
        e.k++;
        if (e.k < seqGesturePassMoves(e.spread, e.n)) return;
        e.k = 0;
    }
    e.pass++;
    if (e.pass >= e.passes) e.active = false;
}

// The item sequenceGestureNext() handed out has been dealt with at nowMs.
// `started` is whether a body move reached ServoTask; a member nothing drives
// was reported and passed over and holds nothing off. A dome pass holds no
// body move off: the dome's motion is the dome's. `floorMs` is the Cadence
// Floor in use.
inline void sequenceGestureDone(SeqGestureRun* run, const SeqGestureNext& next, uint32_t nowMs,
                                bool started, uint16_t throwMs, ServoOutputAddress output,
                                uint32_t floorMs) {
    if (run == nullptr || next.entry >= SEQ_GESTURE_RUNS_MAX) return;
    SeqGestureRunEntry& e = run->g[next.entry];
    if (!e.active) return;
    if (started) run->sent++;
    else run->skipped++;
    if (!e.dome) {
        sequencePaceMotion(&run->dueMs, &run->awaitOutput, nowMs, started, true, true, throwMs,
                           output, floorMs);
    }
    sequenceGestureAdvance(e);
}

// A member's flutter was handed to the flutter run (include/sequence_flutter.h),
// or reported as one nothing can move. Handing it over starts no motion, so
// the pace is left as it stands: the flutter's legs keep it themselves.
inline void sequenceGestureFlutterHandedOver(SeqGestureRun* run, const SeqGestureNext& next,
                                             bool taken) {
    if (run == nullptr || next.entry >= SEQ_GESTURE_RUNS_MAX) return;
    SeqGestureRunEntry& e = run->g[next.entry];
    if (!e.active) return;
    if (taken) run->sent++;
    else run->skipped++;
    sequenceGestureAdvance(e);
}
