// =============================================================================
// include/sequence_engine.h
//
// Pure DM:* sequence step model and cursor engine (ADR 0004, issue #2).
//
// This module is deliberately free of Arduino, FreeRTOS, logging, and RNG
// dependencies so the full execution semantics (flat, toggle, loop, random)
// are natively testable. Time is passed in; randomness is injected via a
// function pointer; emitted work is returned as SeqAction values that the
// dispatcher task maps onto domeQueueTx()/audioQueue*().
//
// Execution model  --  peek/commit:
//   seqEnginePeek()   returns the next due action without consuming it.
//   seqEngineCommit() consumes it after the caller dispatched it successfully.
// If a downstream queue is full the caller simply does not commit; the same
// action is returned again next tick. Scheduling is anchored to absolute step
// times, so retries do not drift the rest of the choreography.
// =============================================================================
#pragma once

#include <stdint.h>

// -----------------------------------------------------------------------------
// Step type.
// -----------------------------------------------------------------------------
enum SeqStepType : uint8_t {
    STEP_END            = 0,  // terminal sentinel; fires at tMs, then auto-reset
    STEP_DOME_CMD       = 1,  // payload -> dome TX queue
    STEP_AUDIO          = 2,  // payload -> audio $-command queue
    STEP_LOOP           = 3,  // repeat next params.bodyCount steps every
                              // params.periodMs while iteration start < durationMs
    STEP_RANDOM         = 4,  // emit a random logical panel intent command
                              // resolved at fire time
    STEP_AUDIO_CATEGORY = 5,  // random track from a config-backed sound category
    STEP_CLEAR_LATCHES  = 6,  // body-internal: reset toggle latches (piesOpen/
                              // ringOpen) at this step's fire time. Factory-only  -- 
                              // there is no JSON/wire form, the seq_json parser
                              // cannot produce it, and serialization omits it, so
                              // it never reaches Protocol Check. Used by DM:RESET
                              // to clear latch state without a group close.
    STEP_DOME_ROTATE    = 7,  // body-owned dome ESC timed rotation. Separate
                              // from STEP_DOME_CMD, which is dome serial/TX.
    STEP_AUDIO_STOP     = 8,  // body-internal: emit a Track Stop (SEQ_ACT_AUDIO_STOP)
                              // at this step's fire time, mid-sequence  --  for an
                              // author who wants a musically sensible cutoff earlier
                              // than the natural SEQ_TERM (ADR 0010), e.g. ROCKMARCH's
                              // 47000 ms cutoff before its 48250 ms TERM. Track Stop,
                              // not the mood-disabling full stop, so it never needs a
                              // dollar command. Factory-only  --  same as
                              // STEP_CLEAR_LATCHES: no JSON/wire form, the seq_json
                              // parser cannot produce it, and serialization omits it,
                              // so it never reaches Protocol Check.
    STEP_BODY           = 9,  // Body Step (ADR 0049): move one body Part. payload
                              // carries the Droid Parts Catalog id -- the Part, not
                              // an Output Address, so a recording survives a
                              // re-address; params carries the Move Shape, how far
                              // it goes as a fraction of that Part's own throw, and
                              // for a flutter how long it goes on. Its own type
                              // rather than a body target smuggled into
                              // STEP_DOME_CMD's payload, on the precedent
                              // STEP_DOME_ROTATE set for a body-owned motion step.
};

// -----------------------------------------------------------------------------
// Effect class bitmask  --  persistent dome/body state a step activates. The
// engine ORs fired steps' classes into activeFx and emits the matching reset
// commands on terminal transitions (ADR 0004 decision 7), only for what was
// actually activated.
// -----------------------------------------------------------------------------
enum SeqEffectClass : uint8_t {
    FX_NONE      = 0,
    FX_LOGIC_PSI = 1 << 0,  // @0T* / @0P*   --  reset with @0T1 + @0P1
    FX_PANEL     = 1 << 1,  // panel opens    --  reset with :CL00 (close + release)
    FX_HOLO      = 1 << 2,  // holo effects   --  reset with *ST00
    FX_AUDIO     = 1 << 3,  // long audio     --  Track Stop on ABNORMAL termination only
                            // (ring-out preserved on normal completion)
    FX_DOME_SEQUENCE = 1 << 4, // legacy :SE##  --  conservatively reset dome effects
    FX_AUDIO_BOUNDED = 1 << 5, // long NAMED TRACK (opt-in, ADR 0010 Bounded Audio)  -- 
                               // Track Stop on normal termination as well as abnormal.
                               // Sibling to FX_AUDIO; SEQ_AUDIO_CAT keeps plain FX_AUDIO
                               // so short category vocalizations always ring out.
};

// -----------------------------------------------------------------------------
// Random logical target sets.
//   RING = P1,P2,P3,P4,P7,P11,P13
//   PIE  = PP1,PP2,PP3,PP4,PP5,PP6
//   ALL  = ring + pie
//   HOLD = reuse the target picked by the previous STEP_RANDOM
// -----------------------------------------------------------------------------
enum SeqSlotSet : uint8_t {
    SLOTSET_RING = 0,
    SLOTSET_PIE  = 1,
    SLOTSET_ALL  = 2,
    SLOTSET_HOLD = 3,
};

enum SeqRandomMode : uint8_t {
    RAND_FLUTTER   = 0,
    RAND_OPEN      = 1,
    RAND_CLOSE     = 2,
};

// -----------------------------------------------------------------------------
// Move Shape  --  what a step says one Part does (ADR 0049, CONTEXT.md).
//
// These are the dome's own three words, so one word means one thing across the
// droid: a builder who has learned that a dome panel opens, closes and flutters
// has learned the body too, and a Gesture that spreads a shape never has to ask
// which half of the droid it is on.
//
// A LIGHT PART STORES THE SAME TOKEN. The surface names them by Part Kind  -- 
// a servo Part reads open/close/flutter and a light Part reads on/off/flash,
// and "how far" reads as travel on one and brightness on the other  --  but the
// stored shape is one token either way, which is what lets a Gesture spread one
// shape across a mixed set. There is deliberately no second vocabulary here to
// translate between.
//
// OPEN is 0 because it is the default: a step that says nothing about its shape
// is an open, so absence and the default are the same value in storage. Every
// reader goes through seqBodyShape() so the default is written once.
// -----------------------------------------------------------------------------
enum SeqBodyShape : uint8_t {
    BODY_SHAPE_OPEN    = 0,
    BODY_SHAPE_CLOSE   = 1,
    BODY_SHAPE_FLUTTER = 2,
    BODY_SHAPE_COUNT   = 3,
};

constexpr SeqBodyShape SEQ_BODY_SHAPE_DEFAULT = BODY_SHAPE_OPEN;

// How far a Body Step moves its Part, as a percentage of that Part's OWN throw
// (the Endpoint Pair recorded on the Output that drives it), so the same step
// means the same gesture on a different linkage and a recalibration changes the
// microseconds without touching the routine.
//
// Zero means the wire said nothing, and absence is the whole throw. A stated
// value below the floor is FLOORED rather than refused: the model has no way to
// mean "does not move", and refusing 3% would be a judgement about intent,
// which is the Rehearsal's and never Protocol Check's (ADR 0044).
constexpr uint8_t SEQ_BODY_HOWFAR_UNSET   = 0;
constexpr uint8_t SEQ_BODY_HOWFAR_DEFAULT = 100;
constexpr uint8_t SEQ_BODY_HOWFAR_FLOOR   = 5;
constexpr uint8_t SEQ_BODY_HOWFAR_MAX     = 100;

// -----------------------------------------------------------------------------
// Per-type step parameters. All-zero for flat steps. Kept as one flat POD
// (not a union) so static tables stay aggregate-initializable on the firmware
// toolchain and the layout maps cleanly to a future serial format.
// -----------------------------------------------------------------------------
struct SeqStepParams {
    uint32_t durationMs;        // LOOP: keep iterating while iterStart < durationMs
    uint16_t periodMs;          // LOOP: iteration period
    uint8_t  bodyCount;         // LOOP: number of body steps following the header
    uint8_t  slotSet;           // RANDOM: SeqSlotSet
    uint16_t pulseMin;          // RANDOM: SeqRandomMode (legacy field name)
    uint16_t pulseMax;          // RANDOM: reserved, must be 0
    uint16_t moveMs;            // RANDOM: servo travel time
    uint16_t jitterMs;          // RANDOM: random 0..jitterMs added to fire time
    uint8_t  pickDistinct;      // RANDOM: avoid slots already picked this run
    uint8_t  audioCategory;     // AUDIO_CATEGORY: AudioPlaybackCategory value
    uint8_t  audioFallbackSlot; // AUDIO_CATEGORY: AudioPlaybackSlot fallback
    int8_t   speedPct;          // DOME_ROTATE: signed -100..100 speed percentage
    uint8_t  audioBounded;      // AUDIO (STEP_AUDIO), Learned Sequences only: parsed
                                 // JSON boundAudio carrier, consumed by
                                 // protocolCheckBranch() to stamp FX_AUDIO_BOUNDED vs
                                 // FX_AUDIO (ADR 0010). Factory catalog entries set
                                 // effectClass directly via SEQ_AUDIO_FX and ignore
                                 // this field. Appended last so existing positional
                                 // catalog-macro initializers stay valid (aggregate
                                 // init zero-fills trailing members).
    // BODY (STEP_BODY), appended last for the same reason audioBounded was, and
    // the rule is the same for whoever comes next: a new member goes on the END
    // of this struct. A sequence saved by a prior build carries no body step at
    // all, so it loads unchanged; a Factory catalog macro that stops short of
    // these three zero-fills them, which is exactly the default each one means.
    uint8_t  shape;             // BODY: SeqBodyShape. Read through seqBodyShape()
    uint8_t  howFar;            // BODY: 0..100 pct of that Part's own throw,
                                 // SEQ_BODY_HOWFAR_UNSET when the wire said nothing.
                                 // Read through seqBodyHowFar()
    uint16_t flutterMs;         // BODY: how long a flutter goes on. Zero on every
                                 // other shape -- Protocol Check refuses a duration
                                 // on a shape that has nowhere to spend it
};

// -----------------------------------------------------------------------------
// seqBodyShape() / seqBodyHowFar()
// The two readers of a Body Step's Move Shape and how-far, and the only place
// either default lives. Total on purpose: both are safe to call on a zero-filled
// params block, and a stored value the vocabulary does not model degrades to the
// default rather than reaching the drive path as a number nobody meant.
//
// seqBodyHowFar() FLOORS rather than refusing (SEQ_BODY_HOWFAR_FLOOR): the model
// has no way to say "does not move", and deciding that 3% was not what the
// author meant is the Rehearsal's judgement, never this one's (ADR 0044).
// -----------------------------------------------------------------------------
inline SeqBodyShape seqBodyShape(const SeqStepParams& p) {
    return (p.shape < BODY_SHAPE_COUNT) ? (SeqBodyShape)p.shape : SEQ_BODY_SHAPE_DEFAULT;
}

inline uint8_t seqBodyHowFar(const SeqStepParams& p) {
    if (p.howFar == SEQ_BODY_HOWFAR_UNSET) {
        return SEQ_BODY_HOWFAR_DEFAULT;
    }
    if (p.howFar < SEQ_BODY_HOWFAR_FLOOR) {
        return SEQ_BODY_HOWFAR_FLOOR;
    }
    return (p.howFar > SEQ_BODY_HOWFAR_MAX) ? SEQ_BODY_HOWFAR_MAX : p.howFar;
}

// -----------------------------------------------------------------------------
// SeqStep  --  POD step, statically allocated, serializable-ready.
//
// tMs:  Milliseconds from sequence start (or from iteration start for steps
//       inside a STEP_LOOP body) when this step fires.
// payload: Dome command (STEP_DOME_CMD), audio $-command (STEP_AUDIO), or the
//          Droid Parts Catalog id of the Part a STEP_BODY moves.
//          64 bytes  --  matches DomeTxCmd.buf.
// -----------------------------------------------------------------------------
struct SeqStep {
    uint32_t      tMs;
    SeqStepType   type;
    uint8_t       effectClass;
    char          payload[64];
    SeqStepParams params;
};

// -----------------------------------------------------------------------------
// Catalog authoring macros  --  keep the positional SeqStepParams ordering in one
// place. The firmware toolchain cannot rely on C++20 designated initializers.
// The non-body lists stop at audioBounded (ADR 0010) and let aggregate init
// zero-fill the body members after it; SEQ_BODY is the one that names them, and
// it stops at flutterMs the same way. Factory catalog entries ignore
// audioBounded and set effectClass directly via SEQ_AUDIO_FX.
// -----------------------------------------------------------------------------
#define SEQ_DOME(t, fx, cmd)  { (t), STEP_DOME_CMD, (uint8_t)(fx), cmd, {} }
#define SEQ_AUDIO(t, cmd)     { (t), STEP_AUDIO, FX_NONE, cmd, {} }
#define SEQ_AUDIO_FX(t, fx, cmd) { (t), STEP_AUDIO, (uint8_t)(fx), cmd, {} }
#define SEQ_AUDIO_CAT(t, cat, fb) \
    { (t), STEP_AUDIO_CATEGORY, FX_AUDIO, "", \
      { 0, 0, 0, 0, 0, 0, 0, 0, 0, (uint8_t)(cat), (uint8_t)(fb), 0, 0 } }
#define SEQ_DOME_ROTATE(t, speed, dur) \
    { (t), STEP_DOME_ROTATE, FX_NONE, "", \
      { (dur), 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, (int8_t)(speed), 0 } }
#define SEQ_LOOP(t, body, period, dur) \
    { (t), STEP_LOOP, FX_NONE, "", \
      { (dur), (period), (body), 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 } }
#define SEQ_RAND(t, set, mode, _unused, mv, jit, distinct) \
    { (t), STEP_RANDOM, FX_PANEL, "", \
      { 0, 0, 0, (uint8_t)(set), (uint16_t)(mode), 0, (mv), (jit), (distinct), 0, 0, 0, 0 } }
// A Body Step. `part` is a Droid Parts Catalog id ("doorFL"), `shape` a
// SeqBodyShape, `howFar` a percentage of that Part's own throw (0 for the whole
// throw), `flutter` how long a flutter goes on (0 on every other shape).
// FX_NONE is the decision, not an omission: the engine undoes nothing a body
// step did, so there is no effect class for terminal cleanup to act on
// (ADR 0049).
#define SEQ_BODY(t, part, shape, howFar, flutter) \
    { (t), STEP_BODY, FX_NONE, part, \
      { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, \
        (uint8_t)(shape), (uint8_t)(howFar), (uint16_t)(flutter) } }
#define SEQ_TERM(t)           { (t), STEP_END, FX_NONE, "", {} }
#define SEQ_CLEAR_LATCHES(t)  { (t), STEP_CLEAR_LATCHES, FX_NONE, "", {} }
#define SEQ_AUDIO_STOP(t)     { (t), STEP_AUDIO_STOP, FX_NONE, "", {} }

// -----------------------------------------------------------------------------
// Toggle groups (ADR 0004 decision 8)  --  body-authoritative latched panel state.
// -----------------------------------------------------------------------------
enum SeqToggleGroup : uint8_t {
    TOGGLE_NONE  = 0,
    TOGGLE_PIES  = 1,
    TOGGLE_LOW   = 2,
    TOGGLE_ALL   = 3,
    // User latches for non-shadowing Learned toggle sequences. RESERVED: the
    // engine's branch-pick/latch switches are
    // not wired for these yet (SeqToggleState has no user fields), so Protocol
    // Check rejects them on save. The values exist so the JSON format and the
    // runtime index can already represent them.
    TOGGLE_USER1 = 4,
    TOGGLE_USER2 = 5,
    TOGGLE_USER3 = 6,
    TOGGLE_USER4 = 7,
};

// -----------------------------------------------------------------------------
// SequenceEntry  --  one body-owned sequence in the catalog. For toggle entries,
// `steps` is the open branch and `closeSteps` the close branch; the engine
// picks the branch from the latched group state at start.
// -----------------------------------------------------------------------------
struct SequenceEntry {
    const char*    name;        // "DM:VADER" etc.  --  exact match, case-sensitive
    const SeqStep* steps;
    uint8_t        stepCount;
    uint32_t       suppressMs;  // suppression window duration
    SeqToggleGroup toggleGroup; // TOGGLE_NONE for non-toggle sequences
    const SeqStep* closeSteps;  // toggle close branch (nullptr otherwise)
    uint8_t        closeStepCount;
    const char*    purpose;     // optional operator-facing description (nullptr if none)
};

// -----------------------------------------------------------------------------
// Actions emitted by the engine for the dispatcher task to map onto queues.
// -----------------------------------------------------------------------------
enum SeqActionKind : uint8_t {
    SEQ_ACT_NONE           = 0,
    SEQ_ACT_DOME_CMD       = 1,  // payload -> domeQueueTx()
    SEQ_ACT_AUDIO_DOLLAR   = 2,  // payload -> audioQueueDollar()
    SEQ_ACT_AUDIO_CATEGORY = 3,  // audioCategory/audioFallbackSlot -> audioQueuePlayCategory()
    SEQ_ACT_AUDIO_STOP     = 4,  // audioQueueTrackStop() (ADR 0010)
    SEQ_ACT_DOME_ROTATE    = 5,  // domeSpeedPct/domeDurationMs -> domeCmdQueue
    SEQ_ACT_BODY_MOVE      = 6,  // payload (Part id) + bodyShape/bodyHowFar/
                                 // bodyFlutterMs -> the body servo path. The
                                 // Part is resolved to an Output by the
                                 // Sequence Coordinator at dispatch, every
                                 // time, because wiring the arm must start the
                                 // step working with nothing re-authored
                                 // (include/droid_part_availability.h).
};

struct SeqAction {
    SeqActionKind kind;
    char          payload[64];
    uint8_t       audioCategory;
    uint8_t       audioFallbackSlot;
    int8_t        domeSpeedPct;
    uint32_t      domeDurationMs;
    // BODY_MOVE. Already resolved through seqBodyShape()/seqBodyHowFar(), so a
    // consumer reads a shape and a percentage rather than the two defaults.
    uint8_t       bodyShape;
    uint8_t       bodyHowFar;
    uint16_t      bodyFlutterMs;
};

// Latched per-group panel state. Owned by the engine; the dispatcher task
// resets it on estop-clear and dome-reconnect resync via seqEngineClearLatches().
struct SeqToggleState {
    bool piesOpen;
    bool ringOpen;
};

// Injected RNG (esp_random on target, deterministic stub in native tests).
typedef uint32_t (*SeqRandFn)();

// -----------------------------------------------------------------------------
// Engine runtime state. Treat as opaque outside sequence_engine.cpp and tests.
// -----------------------------------------------------------------------------
struct SeqEngineState {
    const SequenceEntry* entry;       // nullptr => idle
    const SeqStep*       steps;       // active branch
    uint8_t              stepCount;
    bool                 openBranch;  // running a toggle open branch
    uint8_t              cursor;
    uint32_t             startMs;
    uint8_t              activeFx;

    // Net-open RING panel tracking for load-shaped terminal cleanup (issue #2).
    // A group close (:CL15/:CL00) drives every ring servo simultaneously, which
    // browns out the dome from a loaded ring (2026-06-17 hardware repro). So the
    // engine tracks exactly which ring panels this run left logically OPEN and
    // closes only those, one at a time, at a safe cadence. Bit i corresponds to
    // kRingPanels[i] in sequence_engine.cpp. :OP/:CL set/clear bits; :OF does NOT
    // mark a panel open (uncertain state -> authored cleanup); pies are never
    // auto-closed by engine cleanup.
    uint16_t             ringOpenMask;

    // STEP_LOOP runtime
    bool     inLoop;
    uint8_t  loopHeader;       // index of the STEP_LOOP step
    uint32_t iterStartRel;     // current iteration start, relative to loop start

    // STEP_RANDOM runtime
    uint8_t  heldTarget;       // last picked target (SLOTSET_HOLD reuse)
    uint16_t pickedMask;       // targets already picked this run (pickDistinct)

    // Peeked-but-uncommitted action cache (stable across queue-full retries)
    bool      pendingComputed;
    SeqAction pending;
    uint32_t  pendingFireAt;   // absolute ms

    // Terminal auto-reset drain. finalDueRel[i] is finalQ[i]'s fire offset (ms)
    // relative to finishStartMs, so staggered individual ring closes drain at a
    // safe cadence while instant resets use 0. finishStartMs is set lazily on the
    // first finishing peek (seqEngineAbort carries no nowMs). The queue is sized
    // for the worst case: a few effect resets plus one individual close per ring
    // panel (7) with margin.
    bool      finishing;
    bool      finishAbnormal;
    bool      finishStartSet;
    uint32_t  finishStartMs;
    SeqAction finalQ[16];
    uint16_t  finalDueRel[16];
    uint8_t   finalCount;
    uint8_t   finalCursor;

    SeqToggleState latches;

    // True after a committed non-zero STEP_DOME_ROTATE. Terminal and abnormal
    // cleanup emit a neutral rotation action through the same body-owned path.
    bool      domeRotateActive;

    // True once this run dispatched any DV:<name> dome visual preset. Terminal
    // cleanup then explicitly closes the DV lifecycle with DV:RESET_VISUALS so
    // the dome's visual_preset telemetry settles to RESET_VISUALS (machine-
    // verifiable teardown) and the named rendering is deterministically reset.
    // Raw @0T1/@0P1/*ST00 do not clear the dome's DV preset field (issue #9 #2).
    bool      dvPresetActive;
};

// Zero the engine to idle with all latches closed.
void seqEngineInit(SeqEngineState& st);

// True while a sequence is running or draining terminal resets.
bool seqEngineActive(const SeqEngineState& st);

// Name of the active sequence, or nullptr when idle.
const char* seqEngineName(const SeqEngineState& st);

// Force all toggle latches to closed (estop-clear / dome-reconnect resync).
void seqEngineClearLatches(SeqEngineState& st);

// Start (or restart) a sequence. Caller must abort+drain any active sequence
// first if preempt cleanup is desired. Picks the toggle branch from latches.
void seqEngineStart(SeqEngineState& st, const SequenceEntry* entry, uint32_t nowMs);

// Switch to abnormal termination: subsequent peeks drain the auto-reset
// actions for everything in activeFx, then the engine goes idle. No-op if idle.
void seqEngineAbort(SeqEngineState& st);

// Return the next due action at nowMs without consuming it. Returns false when
// idle or nothing is due yet.
bool seqEnginePeek(SeqEngineState& st, uint32_t nowMs, SeqRandFn rnd, SeqAction& out);

// Consume the previously peeked action after successful dispatch.
void seqEngineCommit(SeqEngineState& st);

// Physical ring panel count, and the individual close command (":CLnn") for ring
// panel index i (0..count-1). Lets callers outside the engine (the dispatcher's
// estop-clear / dome-reconnect resync) stage a ring-only close one panel at a
// time instead of a brownout-prone group :CL15. `buf` needs >= 6 bytes; returns
// false for an out-of-range index or too-small buffer. Single source of truth
// for the ring panel set lives in sequence_engine.cpp.
uint8_t seqEngineRingPanelCount(void);
bool    seqEngineRingCloseCmd(uint8_t i, char* buf, uint8_t bufLen);

// Ring panel NUMBER (e.g. 1, 2, 13) for ring index i (0..count-1), or -1 if out
// of range. Single source of the ring panel set for callers that need to map a
// ringOpenMask/touched-mask bit back to a panel number (e.g. run-evidence).
int seqEngineRingPanelNumber(uint8_t i);

// True while the engine is draining terminal/abort auto-reset actions. Lets the
// dispatcher classify a peeked action as cleanup (vs normal choreography) for
// run-evidence capture. False when idle or running normal steps.
bool seqEngineFinishing(const SeqEngineState& st);
