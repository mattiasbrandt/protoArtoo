// =============================================================================
// include/seq_store_util.h
//
// Pure decision logic for the Learned Sequence store (ADR 0006).
// Extracted from seq_store.cpp so the capacity policy and the name->file
// mapping are native-testable against the real production code, without an
// (unfaithful) LittleFS emulation. seq_store.cpp calls these around its I/O.
// =============================================================================
#pragma once

#include <stddef.h>
#include <stdint.h>

#include "config.h"            // PA_CHIP_TARGET_* (chip-target selection)
#include "protocol_check.h"    // ProtocolCheckResult, PC_MAX_STEPS, PC_CMD_MAX
#include "seq_store_index.h"   // SEQ_INDEX_CAPACITY

// Capacity guards (issue #2 grill decision 5), sized per chip target.
//
// PER-FILE CAP. What binds this is the HEAP, not the filesystem: a save holds
// the raw body (the route buffers up to this cap, web_seam_routes.cpp), the
// ArduinoJson document parsed from it, and the SeqStep staging pair, all three
// at once (seq_store.cpp seqStoreSave). Measured on a 32-bit host against
// ArduinoJson 7.4.3 with the format's own generator:
//
//                                     text      JsonDocument   staging    peak
//   96+96 steps at PC_CMD_MAX      18843 B         11811 B    18432 B   49086 B
//
//   ESP32 (artoo-esp32): 12 KB, unchanged. Steady-state free heap measured on
//   that board is 42692 B (see config.h's task-stack block), so the 49 KB peak
//   above does not fit and the cap sits below the model's own ceiling -- an
//   artoo-esp32 sequence at 96+96 steps simply cannot be saved. That is the
//   scarcity, and it is why the number is 12 KB rather than anything derived
//   from the format.
//
//   ESP32-P4: 24 KB, derived from the model instead. 18843 B is the largest
//   JSON the format can produce (PC_MAX_STEPS = 96 per branch, both branches,
//   every step a dome command at PC_CMD_MAX = 63); 24 KB rounds that up and
//   leaves 5733 B for the `meta` block, whose origin/license/notes/purpose
//   fields are free text no validator bounds. The 49 KB transient peak is 43%
//   of the ~114 KB internal free heap measured on the P4 (heap_reading.h and
//   tasks/safety.cpp record that figure from #245), and an over-large document
//   still fails gracefully: deserializeJson returns NoMemory and seqStoreSave
//   answers a field-level error with nothing written.
//
// FREE-SPACE FLOOR. Two cap-sized allowances, which is a rule rather than a
// literal because the mechanism sets it: seqStoreSave writes `.tmp.json` while
// the outgoing copy of the same sequence is still on disk and only removes it
// just before the rename, so one full-size file coexists with the incoming one
// that seqStoreCapacityCheck already reserved; the second allowance covers
// LittleFS allocating in whole erase blocks and keeping metadata block pairs,
// so a file costs more on disk than its byte length. ADR 0006 records the 24 KB
// value without a derivation; this rule reproduces it exactly, so artoo-esp32
// is unchanged, and it moves with the cap on the P4 (49152 B, 0.47% of that
// board's 9.88 MB LittleFS partition against 3.75% of artoo-esp32's 640 KB).
//
// `#if defined` rather than `#if`: PA_CHIP_TARGET_* are presence macros defined
// only for the selected chip, not 0/1 Board Capability Gates -- see config.h's
// "Chip target mapping".
#if defined(PA_CHIP_TARGET_ESP32P4)
  #define PA_SEQ_FILE_MAX_KB 24
#elif defined(PA_CHIP_TARGET_ESP32)
  #define PA_SEQ_FILE_MAX_KB 12
#else
  #error "the Learned Sequence per-file cap has no value for this chip target"
#endif

// STORE CAP: how many Learned Sequences this board lets a builder save. It is a
// Board Variant fact, the first thing a builder can do that depends on which
// board they bought (ADR 0065, amended 2026-09-25), so it is selected on
// PA_BOARD rather than on the chip target. The droid reports it in GET
// /api/identity (learned_sequence_cap) and the Sequences page reads it from
// there, so no page carries a copy of its own.
//
//   artoo-esp32: 5. Its 160-block filesystem partition holds both the web image
//   and the saved sequences, and tools/build_budgets.json derives the image's
//   budget from this number: four full-size sequences, plus the free space the
//   fifth save demands (SEQ_FS_FREE_FLOOR + the file). Changing it means
//   redoing that arithmetic.
//
//   firebeetle2: 10. Its partition is 9.88 MB; the web image never competes.
//
// A save is refused only when it is NEW and the store already holds at least
// the cap. `>=` rather than `==` is load-bearing: a firmware-only update can
// boot an artoo-esp32 holding more than five (the index capacity above the cap
// is what keeps them), and such a droid must refuse new saves until the
// builder has deleted down below the cap, while overwriting one it already
// holds still saves.
#if PA_BOARD == PA_BOARD_ARTOO_ESP32
  #define PA_SEQ_STORE_CAP 5
#elif PA_BOARD == PA_BOARD_FIREBEETLE2
  #define PA_SEQ_STORE_CAP 10
#else
  #error "the Learned Sequence store cap has no value for this board"
#endif

static const uint8_t SEQ_STORE_CAP = PA_SEQ_STORE_CAP;
static_assert(PA_SEQ_STORE_CAP <= SEQ_INDEX_CAPACITY,
              "a board's Learned Sequence cap cannot exceed what the index holds:"
              " a save the cap accepts would then be dropped by seqStoreIndexAdd");

// Stringified from the same macro as the constant so the operator-visible size
// in the rejection message cannot drift from the size actually enforced.
#define PA_SEQ_STR_INNER(x) #x
#define PA_SEQ_STR(x) PA_SEQ_STR_INNER(x)
#define SEQ_FILE_TOO_LARGE_MESSAGE \
    "file too large (" PA_SEQ_STR(PA_SEQ_FILE_MAX_KB) " KB max)"
#define SEQ_STORE_FULL_MESSAGE \
    "store full (" PA_SEQ_STR(PA_SEQ_STORE_CAP) " sequences max)"

static const size_t SEQ_FILE_MAX_BYTES = PA_SEQ_FILE_MAX_KB * 1024;  // per-file cap
static const size_t SEQ_FS_FREE_FLOOR  = 2 * SEQ_FILE_MAX_BYTES;  // LittleFS free-space floor

// Map a sequence name to its on-disk basename: "DM:MYSEQ" -> "DM_MYSEQ.json"
// (':' -> '_'). Returns false for a null/implausible name or buffer overflow.
// Output excludes the directory; callers prefix the store directory.
bool seqStoreNameToFile(const char* name, char* out, size_t cap);

// Capacity decision for a save. `isNew` is true when the name is not already in
// the index (a new file consumes a slot); `count` is the current index size;
// `fileLen` is the incoming JSON length; `freeBytes` is the current LittleFS
// free space. Returns ok when the save may proceed, else a field-level error.
ProtocolCheckResult seqStoreCapacityCheck(bool isNew, uint8_t count,
                                          size_t fileLen, size_t freeBytes);

// How long a run of a branch is: its end step's time. Read after the parse, so
// a beat is already the millisecond it resolves to (seq_json.cpp), and from the
// same step protocolCheck() takes the end time from - one reading of a length,
// not two. 0 for a branch that does not end in an end step: a stored file the
// boot scan indexes as invalid, the only place one reaches here.
uint32_t seqStoreRunLengthMs(const SeqStep* steps, uint8_t count);

// The start of a purpose, for a list row: as much of `purpose` as fits `cap`
// (terminator included), ending on a whole UTF-8 character, so a cut never
// leaves half of one for the JSON writer to send. Returns true when there was
// more than was kept. A null purpose is an empty one.
bool seqStoreCutPurpose(const char* purpose, char* out, size_t cap);

// -----------------------------------------------------------------------------
// seqStoreSplicePhrase()
// A sequence inside a sequence, spliced when it is loaded to run (ADR 0046):
// replaces (*buf)[at] - a phrase step - by `child`'s steps without their end
// step, each timed from the phrase step (a step inside one of the phrase's own
// loop bodies keeps its pass-relative time), then puts the branch's top-level
// units back in time order, stably, a loop header travelling with its body.
// Anything sorted after the branch's end step is cut: the engine never runs
// it. A null child removes the phrase step - its sequence is gone. On success
// *buf is a new heap block (the old one freed) and *count its length. Returns
// false, leaving the branch untouched, when the result would pass PC_MAX_STEPS
// or the heap refuses the two working blocks.
// -----------------------------------------------------------------------------
bool seqStoreSplicePhrase(SeqStep** buf, uint8_t* count, uint8_t at, const SeqStep* child,
                          uint8_t childCount);

// -----------------------------------------------------------------------------
// seqStoreSplicePhrases()
// Every phrase a branch holds, spliced in one level per pass, down to
// PC_NEST_DEPTH_MAX: a phrase still there on the pass past it is nested too
// deep, and is left out. `load(ref, deep, &child, &childCount)` finds the
// phrase a step names - a null child leaves it out - and `release()` frees what
// load() took, once the splice is done with it. Returns false when a splice
// would not fit (seqStoreSplicePhrase()), leaving the branch as the splices
// before it made it.
//
// A template, so seqStorePrepare()'s loader inlines into its own frame rather
// than stacking one more on the Sequence Coordinator's measured chain
// (ADR 0040), and a native test drives the real loop with a fake loader.
// -----------------------------------------------------------------------------
// Marks a phrase step a pass still owes; no SeqEffectClass has this value.
static const uint8_t SEQ_PHRASE_OWED = 0x80;
static_assert(FX_AUDIO_BOUNDED < SEQ_PHRASE_OWED, "a phrase mark must not read as an effect class");

template <typename Load, typename Release>
inline bool seqStoreSplicePhrases(SeqStep** buf, uint8_t* count, Load&& load, Release&& release) {
    for (uint8_t pass = 0; pass <= PC_NEST_DEPTH_MAX && *buf != nullptr; ++pass) {
        // A pass splices exactly the phrases the branch held when it began:
        // the ones a splice brings in are the next pass's, one level down,
        // even where they sort ahead of one this pass still owes. Re-sorting
        // moves steps, so the owed ones are marked and found afresh by mark.
        // A phrase step's effectClass is otherwise FX_NONE and never read.
        uint8_t todo = 0;
        for (uint8_t k = 0; k < *count; ++k) {
            if ((*buf)[k].type == STEP_SEQUENCE) {
                (*buf)[k].effectClass = SEQ_PHRASE_OWED;
                ++todo;
            }
        }
        for (; todo > 0; --todo) {
            uint8_t at = 0;
            while (at < *count && !((*buf)[at].type == STEP_SEQUENCE &&
                                    (*buf)[at].effectClass == SEQ_PHRASE_OWED)) {
                ++at;
            }
            if (at >= *count) break;
            const SeqStep* child = nullptr;
            uint8_t childCount = 0;
            load((const char*)(*buf)[at].payload, pass == PC_NEST_DEPTH_MAX, &child, &childCount);
            const bool ok = seqStoreSplicePhrase(buf, count, at, child, childCount);
            release();
            if (!ok) return false;
        }
    }
    return true;
}
