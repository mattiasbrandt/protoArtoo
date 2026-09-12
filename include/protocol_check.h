// =============================================================================
// include/protocol_check.h
//
// Protocol Check  --  the safety validator every Learned Sequence passes on save
// (ADR 0006). Pure module: no Arduino, FreeRTOS, filesystem,
// or JSON dependencies, so the full accept/reject matrix is natively testable.
//
// Protocol Check operates on the parsed staging representation (the same
// SeqStep / SeqStepParams model the sequence engine executes), NOT on raw JSON.
// JSON parsing (seq_json.cpp) feeds it; the runtime store (seq_store.cpp) calls
// it before committing a file. This keeps validation independent of the wire
// format and lets the engine stay the single interpreter.
//
// Two guarantees it enforces that the format cannot express a bypass for:
//   - Estop, suppression, and auto-reset remain engine-level invariants.
//   - Every step that activates persistent dome/body state is stamped with the
//     matching SeqEffectClass, so the engine's auto-reset fires correctly.
//     Inference is deliberately conservative: the worst case is an idempotent
//     over-reset, never a missed cleanup.
// =============================================================================
#pragma once

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "sequence_engine.h"  // SeqStep, SeqStepParams, SeqToggleGroup, SeqEffectClass

// -----------------------------------------------------------------------------
// Result  --  field-level error reporting for the editor/API. ok == true means the
// draft passed and (for branch checks) effectClass has been stamped on each step.
// -----------------------------------------------------------------------------
struct ProtocolCheckResult {
    bool ok;
    char field[24];    // e.g. "name", "suppressMs", "steps[3].cmd"
    char message[96];  // human-readable reason
};

// -----------------------------------------------------------------------------
// Result constructors  --  shared by every module that produces a
// ProtocolCheckResult (validator, JSON codec, runtime store) so the error
// shape and truncation rules stay identical everywhere.
// -----------------------------------------------------------------------------
inline ProtocolCheckResult pcOk() {
    ProtocolCheckResult r = { true, "", "" };
    return r;
}

inline ProtocolCheckResult pcFail(const char* field, const char* message) {
    ProtocolCheckResult r = { false, "", "" };
    strncpy(r.field, field, sizeof(r.field) - 1);
    strncpy(r.message, message, sizeof(r.message) - 1);
    return r;
}

// pcFail() with an indexed field path: "<label>[<idx>].<suffix>".
inline ProtocolCheckResult pcFailAt(const char* label, uint8_t idx,
                                    const char* suffix, const char* message) {
    char field[24];
    snprintf(field, sizeof(field), "%s[%u].%s", label, (unsigned)idx, suffix);
    return pcFail(field, message);
}

// -----------------------------------------------------------------------------
// Bounds (single source of truth; mirrored in docs/sequence-authoring.md).
// -----------------------------------------------------------------------------
static const uint8_t  PC_MAX_STEPS        = 96;
static const uint32_t PC_SUPPRESS_MIN_MS  = 1000;
static const uint32_t PC_SUPPRESS_MAX_MS  = 120000;
static const uint16_t PC_SM_SLOT_MAX      = 12;
static const uint16_t PC_SM_PULSE_MIN     = 800;
static const uint16_t PC_SM_PULSE_MAX     = 2200;
static const uint16_t PC_SM_MOVE_MIN      = 50;
static const uint16_t PC_SM_MOVE_MAX      = 5000;
static const uint16_t PC_LOOP_PERIOD_MIN  = 100;
static const uint16_t PC_LOOP_PERIOD_MAX  = 60000;
static const uint32_t PC_LOOP_DUR_MAX     = 120000;
static const uint16_t PC_RAND_JITTER_MAX  = 2000;
static const uint8_t  PC_NAME_BODY_MAX    = 18;  // chars after "DM:"
static const uint8_t  PC_CMD_MAX          = 63;  // payload[64] minus NUL

// A Body Step's flutter duration, bounded by the model's own two ends rather
// than by a measurement of the body (ADR 0049). Both are derived, not chosen:
//   MIN  the shortest servo move this model already accepts (PC_SM_MOVE_MIN),
//        so a flutter too short to contain one move cannot be authored.
//   MAX  the longest repeat this model already accepts (PC_LOOP_PERIOD_MAX),
//        and a flutter is a repeat.
// These are FORM bounds and nothing more. Whether this droid's Output can
// actually complete a move inside the duration depends on that Output's own
// Motion Profile, which is the Rehearsal's question and never blocks a save
// (ADR 0044). In particular neither number is a Cadence Floor: the Floor is
// UNMEASURED on the body, and the ~450 ms figure is the DOME's, measured on
// dome hardware -- it is not restated here as a body figure.
static const uint16_t PC_BODY_FLUTTER_MS_MIN = PC_SM_MOVE_MIN;
static const uint16_t PC_BODY_FLUTTER_MS_MAX = PC_LOOP_PERIOD_MAX;

// -----------------------------------------------------------------------------
// Staging draft  --  the in-memory form a Learned Sequence takes between JSON parse
// and engine execution. `steps`/`closeSteps` point at caller-owned buffers
// (the runtime staging buffer, or test fixtures); the draft itself is small.
// -----------------------------------------------------------------------------
struct SeqDraft {
    char           name[24];
    uint32_t       suppressMs;
    SeqToggleGroup toggleGroup;
    SeqStep*       steps;           // main / open branch
    uint8_t        stepCount;
    SeqStep*       closeSteps;      // toggle close branch; nullptr if none
    uint8_t        closeStepCount;
};

// True if `g` is a recognised SeqToggleGroup value (incl. the user latches).
// Note: protocolCheckMeta() additionally REJECTS the user latches for now  -- 
// the engine's branch-pick/latch execution is not wired for them yet.
bool protocolCheckToggleGroupValid(SeqToggleGroup g);

// Validate sequence-level metadata and retrain (shadowing) rules. `endTimeMs`
// is the main branch's terminal STEP_END time, used for the suppress>=end rule.
ProtocolCheckResult protocolCheckMeta(const char* name, uint32_t suppressMs,
                                      SeqToggleGroup toggleGroup,
                                      uint32_t endTimeMs);

// Validate one branch and STAMP effectClass on every step. `label` prefixes
// field paths in errors ("steps" or "closeSteps").
ProtocolCheckResult protocolCheckBranch(const char* label, SeqStep* steps,
                                        uint8_t count);

// Convenience: full check of a draft (meta + main branch + close branch when
// present). Stamps effectClass on both branches. Returns the first failure.
ProtocolCheckResult protocolCheck(SeqDraft& draft);
