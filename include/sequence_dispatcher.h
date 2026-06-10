// =============================================================================
// include/sequence_dispatcher.h
//
// Body-side DM:* sequence coordinator (ADR 0004, issue #2).
//
// Architecture:
//   sequenceStart() is the single choke point for all DM:* commands. It routes
//   to one of three paths:
//     CATALOG  — body-owned sequence; cursor runs in SequenceDispatcherTask.
//     ALIAS    — direct dome forward (:SE## / $NNN); domeQueueTx() called inline.
//     FALLBACK — unknown DM:* name; domeQueueTx() called inline (dome runs it).
//
//   The task runs a 10 ms tick on Core 0. It advances the step cursor, manages
//   the suppression window (robotState.domeSeqActive), and emits auto-reset
//   commands on abort/preempt/estop.
//
// Step model: typed POD structs, statically allocated, no per-step heap.
// STEP_LOOP and STEP_RANDOM are designed-in enum values (slice 2+).
// =============================================================================
#pragma once

#include <stdint.h>

#include "robot_state.h"  // CommandSource, QueueHandle_t

// -----------------------------------------------------------------------------
// Step type — STEP_LOOP and STEP_RANDOM reserved for future slices.
// -----------------------------------------------------------------------------
enum SeqStepType : uint8_t {
    STEP_END      = 0,   // terminal sentinel; no payload
    STEP_DOME_CMD = 1,   // payload -> domeQueueTx()
    STEP_AUDIO    = 2,   // payload -> audioQueueDollar()
    STEP_LOOP     = 3,   // reserved (not yet implemented)
    STEP_RANDOM   = 4,   // reserved (not yet implemented)
};

// -----------------------------------------------------------------------------
// Effect class bitmask — tracks which persistent dome effects were activated.
// On abort or end, the coordinator emits the corresponding reset commands for
// any bit that is set so persistent dome state does not leak.
// -----------------------------------------------------------------------------
enum SeqEffectClass : uint8_t {
    FX_NONE      = 0,
    FX_LOGIC_PSI = 1 << 0,  // @0T* / @0P* — reset with @0T1 + @0P1
    FX_PANEL     = 1 << 1,  // :SM* panel open — reset with :CL00
};

// -----------------------------------------------------------------------------
// SeqStep — POD step, statically allocated, serializable-ready.
//
// tMs:         Absolute milliseconds from sequence start when this step fires.
// type:        What to dispatch.
// effectClass: SeqEffectClass bitmask; OR'd into activeFx when the step fires.
// payload:     Dome command string (STEP_DOME_CMD) or audio dollar cmd (STEP_AUDIO).
//              64 bytes — matches DomeTxCmd.buf; covers all current Marcduino lengths.
// -----------------------------------------------------------------------------
struct SeqStep {
    uint32_t    tMs;
    SeqStepType type;
    uint8_t     effectClass;
    char        payload[64];
};

// -----------------------------------------------------------------------------
// SequenceEntry — one body-owned sequence in the catalog.
// -----------------------------------------------------------------------------
struct SequenceEntry {
    const char*      name;        // "DM:VADER" etc. — exact match, case-sensitive
    const SeqStep*   steps;
    uint8_t          stepCount;
    uint32_t         suppressMs;  // suppression window duration
};

// -----------------------------------------------------------------------------
// SequenceLookupResult — returned by sequenceLookup() for test-observable routing.
// -----------------------------------------------------------------------------
enum SequenceLookupKind : uint8_t {
    SEQ_CATALOG  = 0,  // in body catalog
    SEQ_ALIAS    = 1,  // known alias -> aliasTarget forwarded to dome
    SEQ_FALLBACK = 2,  // unknown; forward name as-is to dome
};

struct SequenceLookupResult {
    SequenceLookupKind kind;
    char               aliasTarget[16];  // populated for SEQ_ALIAS
};

// -----------------------------------------------------------------------------
// SequenceRequest — message sent to sequenceQueue to start or preempt.
// -----------------------------------------------------------------------------
struct SequenceRequest {
    char          name[24];
    CommandSource src;
};

// Queue handle — defined in main.cpp.
extern QueueHandle_t sequenceQueue;

// Init — creates sequenceQueue. Call from main() before starting the task.
void sequenceDispatcherInit();

// Task entry point — pin to Core 0, priority 3.
void sequenceDispatcherTask(void* pvParameters);

// Single choke point for all DM:* commands. Thread-safe; may be called from
// any task context. Returns false if a catalog entry could not be enqueued
// (queue full) or if the name is empty.
bool sequenceStart(const char* name, CommandSource src);

// Pure routing classification — no side effects. Safe to call from any context
// including native tests. Returns SEQ_FALLBACK for non-DM:* names.
SequenceLookupResult sequenceLookup(const char* name);
