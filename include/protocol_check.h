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

#include "seq_tempo.h"        // SeqTempo - the optional tempo block (ADR 0058)
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

// A Gesture's times (ADR 0046), bounded by the model's own ends the same way:
//   step    its pace: a move this model accepts, up to the longest repeat.
//   speed   how long a full throw takes when the Gesture overrides the
//           Output's own (ADR 0052): a servo move this model accepts.
//   repeat  how often it starts again: the loop period's own bounds.
//   extent  how long it keeps repeating: the loop duration's own bound.
// Form only. A pace faster than the Cadence Floor is not refused: the
// Coordinator paces what it generates whatever the authored spread asks, and
// saying the spread will not keep time is the Rehearsal's (ADR 0044).
static const uint16_t PC_GESTURE_STEP_MS_MIN   = PC_SM_MOVE_MIN;
static const uint16_t PC_GESTURE_STEP_MS_MAX   = PC_LOOP_PERIOD_MAX;
static const uint16_t PC_GESTURE_SPEED_MS_MIN  = PC_SM_MOVE_MIN;
static const uint16_t PC_GESTURE_SPEED_MS_MAX  = PC_SM_MOVE_MAX;
static const uint16_t PC_GESTURE_REPEAT_MS_MIN = PC_LOOP_PERIOD_MIN;
static const uint16_t PC_GESTURE_REPEAT_MS_MAX = PC_LOOP_PERIOD_MAX;
static const uint32_t PC_GESTURE_EXTENT_MS_MAX = PC_LOOP_DUR_MAX;

// A sequence inside a sequence (ADR 0046). The depth is the stated bound the
// engine's "stack" has: a phrase may hold phrases three levels down, and no
// further. A sequence may name at most eight phrases directly, which bounds
// the walk that checks them. A Learned Sequence's stable `id` is 1..16
// lowercase letters and digits, minted by the editor and never changed.
static const uint8_t PC_NEST_DEPTH_MAX = 3;
static const uint8_t PC_NEST_REFS_MAX  = 8;
static const uint8_t PC_SEQ_ID_MAX     = 16;
static const uint8_t PC_SEQ_REF_MAX    = 23;  // a name ("DM:" + 18) or an id, plus NUL in 24

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

// Validate a sequence's tempo block (ADR 0058): a BPM inside the range a beat
// period may have, a bar that holds its own downbeat, a confidence on 0..1,
// and a hash only where the analysed route produced one. Form only -- a LOW
// confidence and a hash that no longer matches the track are the Rehearsal's
// warnings and never refuse a save. The wire codec asks this before it
// resolves a single beat against the tempo, because a beat is only a
// millisecond once the tempo it counts in is known to be well formed.
ProtocolCheckResult protocolCheckTempo(const SeqTempo& tempo);

// Whether `ref` is a well-formed stable reference: a sequence name
// (DM:[A-Z0-9_]{1,18}) or a Learned Sequence id ([0-9a-z]{1,16}).
bool protocolCheckSeqRefValid(const char* ref);
// Whether `name` is a well-formed sequence name, DM:[A-Z0-9_]{1,18}, and
// nothing else: the form a stored Sequence name is checked by (the Stand Down
// Sequence Setting, src/config_settings.cpp) without asking whether the
// Sequence exists today.
bool protocolCheckSeqNameValid(const char* name);
bool protocolCheckSeqIdValid(const char* id);

// What the store knows about one referenced sequence, for the nesting walk:
// whether it exists, whether it is a toggle (which cannot be nested: it has
// two branches and a latch, and a phrase is one run), how many steps its main
// branch has, and the phrases it names in turn.
struct SeqNestInfo {
    bool    found;
    bool    toggle;
    uint8_t stepCount;
    uint8_t refCount;
    char    refs[PC_NEST_REFS_MAX][PC_SEQ_REF_MAX + 1];
};

// Fills `out` for `ref`, or reports it not found. The store supplies it
// (seq_store.cpp); tests supply a table.
typedef void (*SeqNestLookup)(const char* ref, SeqNestInfo* out, void* ctx);

// The rules a sequence holding sequences must keep on save (ADR 0046): every
// phrase exists on this droid and is not a toggle, nothing reaches back to the
// sequence being saved or to a phrase already on its own path (a cycle), no
// path is deeper than PC_NEST_DEPTH_MAX, and the whole run, spliced, fits in
// PC_MAX_STEPS. The steps and the close half are both held to them, each
// counted by itself, and a refusal names its half in its field. `selfId` and
// `selfName` are the sequence being saved; either
// may be empty. The walk holds its state on the heap, not on the caller's
// stack; an allocation failure refuses the save rather than skipping the
// check.
ProtocolCheckResult protocolCheckNesting(const SeqDraft& draft, const char* selfId,
                                         const char* selfName, SeqNestLookup lookup, void* ctx);

// Convenience: full check of a draft (meta + main branch + close branch when
// present). Stamps effectClass on both branches. Returns the first failure.
ProtocolCheckResult protocolCheck(SeqDraft& draft);
