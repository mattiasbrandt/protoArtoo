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
// Execution model — peek/commit:
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
    STEP_RANDOM         = 4,  // emit ":SM<slot>,<pulse>,<moveMs>" with random
                              // slot/pulse resolved at fire time
    STEP_AUDIO_CATEGORY = 5,  // random track from a config-backed sound category
};

// -----------------------------------------------------------------------------
// Effect class bitmask — persistent dome/body state a step activates. The
// engine ORs fired steps' classes into activeFx and emits the matching reset
// commands on terminal transitions (ADR 0004 decision 7), only for what was
// actually activated.
// -----------------------------------------------------------------------------
enum SeqEffectClass : uint8_t {
    FX_NONE      = 0,
    FX_LOGIC_PSI = 1 << 0,  // @0T* / @0P*  — reset with @0T1 + @0P1
    FX_PANEL     = 1 << 1,  // panel opens   — reset with :CL00 (close + release)
    FX_HOLO      = 1 << 2,  // holo effects  — reset with *ST00
    FX_AUDIO     = 1 << 3,  // long audio    — stop on ABNORMAL termination only
};

// -----------------------------------------------------------------------------
// Random slot sets (dome panel slot map from issue #2 spec).
//   RING = P1,P2,P3,P4,P7,P11,P13   = slots 0..6
//   PIE  = PP1,PP2,PP3,PP4,PP5,PP6  = slots 8,9,12,10,7,11
//   ALL  = ring + pie
//   HOLD = reuse the slot picked by the previous STEP_RANDOM (SCREAM flutter)
// -----------------------------------------------------------------------------
enum SeqSlotSet : uint8_t {
    SLOTSET_RING = 0,
    SLOTSET_PIE  = 1,
    SLOTSET_ALL  = 2,
    SLOTSET_HOLD = 3,
};

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
    uint16_t pulseMin;          // RANDOM: inclusive pulse range
    uint16_t pulseMax;
    uint16_t moveMs;            // RANDOM: servo travel time
    uint16_t jitterMs;          // RANDOM: random 0..jitterMs added to fire time
    uint8_t  pickDistinct;      // RANDOM: avoid slots already picked this run
    uint8_t  audioCategory;     // AUDIO_CATEGORY: AudioPlaybackCategory value
    uint8_t  audioFallbackSlot; // AUDIO_CATEGORY: AudioPlaybackSlot fallback
};

// -----------------------------------------------------------------------------
// SeqStep — POD step, statically allocated, serializable-ready.
//
// tMs:  Milliseconds from sequence start (or from iteration start for steps
//       inside a STEP_LOOP body) when this step fires.
// payload: Dome command (STEP_DOME_CMD) or audio $-command (STEP_AUDIO).
//          64 bytes — matches DomeTxCmd.buf.
// -----------------------------------------------------------------------------
struct SeqStep {
    uint32_t      tMs;
    SeqStepType   type;
    uint8_t       effectClass;
    char          payload[64];
    SeqStepParams params;
};

// -----------------------------------------------------------------------------
// Catalog authoring macros — keep the positional SeqStepParams ordering in one
// place. The firmware toolchain cannot rely on C++20 designated initializers.
// -----------------------------------------------------------------------------
#define SEQ_DOME(t, fx, cmd)  { (t), STEP_DOME_CMD, (uint8_t)(fx), cmd, {} }
#define SEQ_AUDIO(t, cmd)     { (t), STEP_AUDIO, FX_NONE, cmd, {} }
#define SEQ_AUDIO_FX(t, fx, cmd) { (t), STEP_AUDIO, (uint8_t)(fx), cmd, {} }
#define SEQ_AUDIO_CAT(t, cat, fb) \
    { (t), STEP_AUDIO_CATEGORY, FX_AUDIO, "", \
      { 0, 0, 0, 0, 0, 0, 0, 0, 0, (uint8_t)(cat), (uint8_t)(fb) } }
#define SEQ_LOOP(t, body, period, dur) \
    { (t), STEP_LOOP, FX_NONE, "", \
      { (dur), (period), (body), 0, 0, 0, 0, 0, 0, 0, 0 } }
#define SEQ_RAND(t, set, pmin, pmax, mv, jit, distinct) \
    { (t), STEP_RANDOM, FX_PANEL, "", \
      { 0, 0, 0, (uint8_t)(set), (pmin), (pmax), (mv), (jit), (distinct), 0, 0 } }
#define SEQ_TERM(t)           { (t), STEP_END, FX_NONE, "", {} }

// -----------------------------------------------------------------------------
// Toggle groups (ADR 0004 decision 8) — body-authoritative latched panel state.
// -----------------------------------------------------------------------------
enum SeqToggleGroup : uint8_t {
    TOGGLE_NONE  = 0,
    TOGGLE_PIES  = 1,
    TOGGLE_LOW   = 2,
    TOGGLE_ALL   = 3,
    // User latches for non-shadowing Learned toggle sequences (issue #2 slice 3,
    // grill decision 4). Branch-pick + latch execution are wired with the
    // runtime store (slice 3c); the values exist now so Protocol Check can
    // validate them and the engine's switch defaults treat them as open-branch.
    TOGGLE_USER1 = 4,
    TOGGLE_USER2 = 5,
    TOGGLE_USER3 = 6,
    TOGGLE_USER4 = 7,
};

// -----------------------------------------------------------------------------
// SequenceEntry — one body-owned sequence in the catalog. For toggle entries,
// `steps` is the open branch and `closeSteps` the close branch; the engine
// picks the branch from the latched group state at start.
// -----------------------------------------------------------------------------
struct SequenceEntry {
    const char*    name;        // "DM:VADER" etc. — exact match, case-sensitive
    const SeqStep* steps;
    uint8_t        stepCount;
    uint32_t       suppressMs;  // suppression window duration
    SeqToggleGroup toggleGroup; // TOGGLE_NONE for non-toggle sequences
    const SeqStep* closeSteps;  // toggle close branch (nullptr otherwise)
    uint8_t        closeStepCount;
};

// -----------------------------------------------------------------------------
// Actions emitted by the engine for the dispatcher task to map onto queues.
// -----------------------------------------------------------------------------
enum SeqActionKind : uint8_t {
    SEQ_ACT_NONE           = 0,
    SEQ_ACT_DOME_CMD       = 1,  // payload -> domeQueueTx()
    SEQ_ACT_AUDIO_DOLLAR   = 2,  // payload -> audioQueueDollar()
    SEQ_ACT_AUDIO_CATEGORY = 3,  // audioCategory/audioFallbackSlot -> audioQueuePlayCategory()
    SEQ_ACT_AUDIO_STOP     = 4,  // audioQueueStop()
};

struct SeqAction {
    SeqActionKind kind;
    char          payload[64];
    uint8_t       audioCategory;
    uint8_t       audioFallbackSlot;
};

// Latched per-group panel state. Owned by the engine; the dispatcher task
// resets it on estop-clear and dome-reconnect resync via seqEngineClearLatches().
struct SeqToggleState {
    bool piesOpen;
    bool lowOpen;
    bool allOpen;
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

    // STEP_LOOP runtime
    bool     inLoop;
    uint8_t  loopHeader;       // index of the STEP_LOOP step
    uint32_t iterStartRel;     // current iteration start, relative to loop start

    // STEP_RANDOM runtime
    uint8_t  heldSlot;         // last picked slot (SLOTSET_HOLD reuse)
    uint16_t pickedMask;       // slots already picked this run (pickDistinct)

    // Peeked-but-uncommitted action cache (stable across queue-full retries)
    bool      pendingComputed;
    SeqAction pending;
    uint32_t  pendingFireAt;   // absolute ms

    // Terminal auto-reset drain
    bool      finishing;
    bool      finishAbnormal;
    SeqAction finalQ[5];
    uint8_t   finalCount;
    uint8_t   finalCursor;

    SeqToggleState latches;
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
