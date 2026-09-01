// =============================================================================
// sequence_run_evidence.h  --  body-side machine-readable "last sequence run"
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

#include "config.h"           // PA_CHIP_TARGET_* (chip-target selection)
#include "protocol_check.h"   // PC_CMD_MAX, PC_MAX_STEPS -- the model's own ceilings
#include "sequence_engine.h"  // SeqAction

// Bounded buffers, sized per chip target. The record is held as TWO static
// copies -- the live record on the dispatcher task (sequence_run_evidence.cpp)
// and the snapshot GET /api/seq/last-run serializes from (api_seq.cpp) -- so
// every byte here costs 2x static DRAM.
//
// ESP32 (artoo-esp32): the operator-sanctioned minimum, unchanged. Steady-state
// free heap is tight -- see the 2026-06-18 heap-exhaustion fix -- so the ring is
// 32 entries of 48 bytes, 2204 B per copy against ~5.3 KB at 64x64. Both
// dimensions truncate, and that is the price this board pays.
//
// ESP32-P4: sized from the sequence model's own ceilings instead of from a heap
// floor, so a run that does not loop is captured WHOLE. This is the point of the
// record -- machine-verifiable evidence of what the body actually sent -- and on
// a board with the DRAM to hold it there is no reason to hand an agent a
// truncated answer.
//
//   SEQ_EVID_CMD_LEN      64  = PC_CMD_MAX + 1 (protocol_check.h). It is also
//                               the payload width SeqStep and SeqAction already
//                               carry (sequence_engine.h, "matches DomeTxCmd.buf"),
//                               so no command the format can hold is truncated.
//                               48 clipped 16 characters off a full-length
//                               Marcduino text command such as @1M<message>.
//   SEQ_EVID_TX_CAP      112  = PC_MAX_STEPS (96) + the engine's terminal drain
//                               queue (SeqEngineState::finalQ, 16 entries).
//                               One run executes ONE branch, and one of its
//                               steps is the STEP_END sentinel, so a non-looping
//                               run emits at most 95 authored commands followed
//                               by at most 16 cleanup actions: 111 <= 112.
//   SEQ_EVID_CLEANUP_CAP  16  = finalQ depth exactly. Cleanup is only ever
//                               recorded while the engine is finishing, and
//                               everything it serves then comes out of finalQ
//                               (sequence_dispatcher.cpp drainBestEffort and the
//                               seqEngineFinishing() tick path).
//
// A STEP_LOOP body still repeats without a static bound (period >= 100 ms across
// a duration <= 120 s), so truncation stays possible on BOTH chips and stays
// signalled via txOmittedRecentCount / cleanupTruncated -- never silent.
//
// Cost, measured with sizeof() on a 32-bit host: 8284 B per copy on ESP32-P4
// against 2204 B on ESP32, i.e. +12160 B of static DRAM across the two copies.
// The firebeetle2 image sat at 80030 B of static RAM before this change against
// a 100000 B budget (tools/build_budgets.json), and the P4's DRAM limit is
// 327680 B.
//
// `#if defined` rather than `#if`: PA_CHIP_TARGET_* are presence macros defined
// only for the selected chip, not 0/1 Board Capability Gates, so `#if` on the
// undefined one would silently take the wrong branch. See config.h's "Chip
// target mapping". Keying on the chip target rather than on PA_BOARD means a
// second board variant on either chip inherits the right sizes for free.
#if defined(PA_CHIP_TARGET_ESP32P4)
  #define SEQ_EVID_CMD_LEN      64
  #define SEQ_EVID_TX_CAP      112   // recent TX ring depth
  #define SEQ_EVID_CLEANUP_CAP  16   // terminal/abort cleanup commands (separate)
#elif defined(PA_CHIP_TARGET_ESP32)
  #define SEQ_EVID_CMD_LEN      48
  #define SEQ_EVID_TX_CAP       32   // recent TX ring depth
  #define SEQ_EVID_CLEANUP_CAP  12   // terminal/abort cleanup commands (separate)
#else
  #error "sequence run-evidence ring dimensions have no value for this chip target"
#endif

// Both dimensions are chip-target specific, so the invariants the code around
// them relies on are asserted here rather than re-derived at each use site.
static_assert(SEQ_EVID_TX_CAP <= 256,
              "SeqRunEvidence::txHead is a uint8_t indexing modulo SEQ_EVID_TX_CAP");
static_assert(SEQ_EVID_CLEANUP_CAP <= 255,
              "SeqRunEvidence::cleanupCount is a uint8_t");
static_assert(SEQ_EVID_CMD_LEN <= PC_CMD_MAX + 1,
              "a TX entry wider than the model's own command ceiling is dead space");

// Not chip-target specific: both are already wider than the field they capture.
// A recorded name comes from SeqDraft::name / SeqIndexEntry::name (24 bytes) and
// a reason is a short internal string, so neither truncates on either board.
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
    uint16_t txOmittedRecentCount;       // commands omitted from retained recent ring

    // Cleanup commands captured SEPARATELY from the general stream.
    char     cleanup[SEQ_EVID_CLEANUP_CAP][SEQ_EVID_CMD_LEN];
    uint8_t  cleanupCount;               // entries stored (<= cap)
    uint8_t  cleanupTotalCount;          // total cleanup commands emitted
    bool     cleanupTruncated;           // total > cap

    // "Did it know anything went wrong"  --  counter deltas over the run window.
    uint32_t bodyQueueFullDelta;         // robotState.queueOverflowCount delta
    uint32_t dispatchRetryCount;         // downstream queue-full retries this run
};

// Lifecycle (called from the dispatcher task). All allocation-free.
//   seqEvidenceBegin     --  start a new record (captures body queue-full baseline).
//   seqEvidenceRecordTx  --  append one emitted command; cleanup=true routes it to
//                         the cleanup buffer too and infers no extra scope.
//   seqEvidenceNoteRetry --  a dispatch hit a full queue and will retry.
//   seqEvidenceEnd       --  finalize outcome/reason/endMs and queue-full delta.
void seqEvidenceBegin(const char* name, uint8_t source, uint32_t startMs,
                      uint32_t bodyQueueFullBaseline);
void seqEvidenceRecordTx(const SeqAction& act, bool cleanup);
void seqEvidenceNoteRetry(void);
void seqEvidenceEnd(SeqRunOutcome outcome, const char* reason, uint32_t endMs,
                    uint32_t bodyQueueFullNow);

// Copy the current record under lock for the API handler. Returns false (and an
// out.valid==false record) when no run has been recorded yet.
bool seqEvidenceSnapshot(SeqRunEvidence& out);
