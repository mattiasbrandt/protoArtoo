// =============================================================================
// include/sequence_dispatcher.h
//
// Body-side DM:* sequence coordinator — RTOS-facing API (ADR 0004, issue #2).
//
// Architecture:
//   sequenceStart() is the single choke point for all DM:* commands. It routes
//   to one of three paths:
//     CATALOG  — body-owned sequence; cursor runs in SequenceDispatcherTask.
//     ALIAS    — direct dome forward (:SE## / $NNN); domeQueueTx() called inline.
//     FALLBACK — unknown DM:* name; domeQueueTx() called inline (dome runs it).
//
//   The task runs a 10 ms tick on Core 0. It feeds the pure cursor engine
//   (sequence_engine.h), manages the suppression window
//   (robotState.domeSeqActive), and handles estop / preempt transitions.
//
// Step model, catalog types, and engine API live in sequence_engine.h.
// Catalog and alias tables live in src/tasks/sequence_catalog.cpp.
// =============================================================================
#pragma once

#include <stdint.h>

#include "robot_state.h"      // CommandSource, QueueHandle_t
#include "sequence_engine.h"  // SeqStep, SequenceEntry, engine API

// -----------------------------------------------------------------------------
// SequenceLookupResult — returned by sequenceLookup() for test-observable routing.
// -----------------------------------------------------------------------------
enum SequenceLookupKind : uint8_t {
    SEQ_CATALOG  = 0,  // in body catalog (Factory Sequence)
    SEQ_ALIAS    = 1,  // known alias -> aliasTarget forwarded to dome
    SEQ_FALLBACK = 2,  // unknown; forward name as-is to dome
    SEQ_RUNTIME  = 3,  // in the runtime store (Learned Sequence); runs in the
                       // dispatcher like SEQ_CATALOG, loaded on demand from FS
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

// Catalog lookup — returns the body-owned entry or nullptr.
const SequenceEntry* sequenceCatalogFind(const char* name);
