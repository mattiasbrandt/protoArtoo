// =============================================================================
// sequence_run_evidence.h — body-side machine-readable "last sequence run"
// record (issue #2 task #6).
//
// Purpose: so an agent can verify what the body actually did on the last
// body-owned DM:* run from one API response, instead of the operator visually
// diffing every detail (the operator-burden-reduction requirement). It answers:
// what did the body think it just ran, what did it send, what cleanup did it
// emit, and did it know anything went wrong?
//
// Capture is allocation-free and bounded, and happens on the dispatcher task.
// The record derives net-open / touched ring masks and effect scopes from the
// commands it records (the actual wire stream), not from engine internals, so it
// reflects what was sent. JSON serialization happens only in the API handler
// from a snapshot copy (seqEvidenceSnapshot).
// =============================================================================
#pragma once

#include <stdint.h>

#include "sequence_engine.h"  // SeqAction

// Bounded buffers. Per the operator's review: prefer a deeper TX ring (64) since
// tail-only evidence can miss the important bit (ROCKMARCH P1). Entries are
// capped at the dome/audio command size (SeqAction.payload is 64).
#define SEQ_EVID_CMD_LEN      64
#define SEQ_EVID_TX_CAP       64   // recent TX ring depth
#define SEQ_EVID_CLEANUP_CAP  16   // terminal/abort cleanup commands (separate)
#define SEQ_EVID_NAME_LEN     32
#define SEQ_EVID_REASON_LEN   24

// How the run ended.
enum SeqRunOutcome : uint8_t {
    SEQ_RUN_NONE      = 0,  // no run recorded yet
    SEQ_RUN_RUNNING   = 1,  // started, not yet ended
    SEQ_RUN_COMPLETED = 2,  // ran to its terminal end
    SEQ_RUN_ABORTED   = 3,  // generic abort
    SEQ_RUN_PREEMPTED = 4,  // superseded by a new sequence
    SEQ_RUN_ESTOP     = 5,  // aborted by estop
    SEQ_RUN_RECONNECT = 6,  // aborted by dome reconnect resync
};

const char* seqRunOutcomeName(SeqRunOutcome o);

// Effect-scope bits inferred from the recorded command stream (coarse mirror of
// Protocol Check's classifyDome). Lets agents diff against the parity table.
enum SeqEvidScope : uint8_t {
    SEQ_EVID_FX_PANEL     = 1 << 0,
    SEQ_EVID_FX_LOGIC_PSI = 1 << 1,
    SEQ_EVID_FX_HOLO      = 1 << 2,
    SEQ_EVID_FX_AUDIO     = 1 << 3,
    SEQ_EVID_FX_DOME_SEQ  = 1 << 4,  // :SE## legacy dome sequence
};

// The record. POD; safe to memcpy for a snapshot.
struct SeqRunEvidence {
    SeqRunOutcome outcome;
    bool          valid;                 // a run has been recorded
    char          name[SEQ_EVID_NAME_LEN];
    uint8_t       source;                // CommandSource of the trigger
    char          reason[SEQ_EVID_REASON_LEN];  // abort reason, when applicable
    uint32_t      startMs;
    uint32_t      endMs;                 // 0 while still running

    uint8_t       fxScopes;              // SeqEvidScope bits seen on the wire
    uint16_t      netOpenRingMask;       // ring panels left logically OPEN
    uint16_t      touchedRingMask;       // ring panels actuated at all (:OP/:CL/:OF)

    // General TX stream (bounded ring of the most recent commands).
    char     tx[SEQ_EVID_TX_CAP][SEQ_EVID_CMD_LEN];
    uint8_t  txHead;                     // next write index (ring)
    uint16_t txTotalCount;               // total commands emitted this run
    uint16_t txOverflowCount;            // commands dropped from the ring (total>cap)

    // Cleanup commands captured SEPARATELY from the general stream.
    char     cleanup[SEQ_EVID_CLEANUP_CAP][SEQ_EVID_CMD_LEN];
    uint8_t  cleanupCount;               // entries stored (<= cap)
    uint8_t  cleanupTotalCount;          // total cleanup commands emitted
    bool     cleanupTruncated;           // total > cap

    // "Did it know anything went wrong" — counter deltas over the run window.
    uint32_t domeQueueDropDelta;         // robotState.queueOverflowCount delta
    uint32_t dispatchRetryCount;         // queue-full retries during this run
};

// Lifecycle (called from the dispatcher task). All allocation-free.
//   seqEvidenceBegin    — start a new record (captures the drop baseline).
//   seqEvidenceRecordTx — append one emitted command; cleanup=true routes it to
//                         the cleanup buffer too and infers no extra scope.
//   seqEvidenceNoteRetry— a dispatch hit a full queue and will retry.
//   seqEvidenceEnd      — finalize outcome/reason/endMs and the drop delta.
void seqEvidenceBegin(const char* name, uint8_t source, uint32_t startMs,
                      uint32_t domeQueueDropBaseline);
void seqEvidenceRecordTx(const SeqAction& act, bool cleanup);
void seqEvidenceNoteRetry(void);
void seqEvidenceEnd(SeqRunOutcome outcome, const char* reason, uint32_t endMs,
                    uint32_t domeQueueDropNow);

// Copy the current record under lock for the API handler. Returns false (and an
// out.valid==false record) when no run has been recorded yet.
bool seqEvidenceSnapshot(SeqRunEvidence& out);
