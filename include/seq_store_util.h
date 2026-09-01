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
#include "seq_store_index.h"   // SEQ_STORE_MAX

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
//   of the ~114 KB internal free heap measured on the P4 (heap_health.h and
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

// Stringified from the same macro as the constant so the operator-visible size
// in the rejection message cannot drift from the size actually enforced.
#define PA_SEQ_STR_INNER(x) #x
#define PA_SEQ_STR(x) PA_SEQ_STR_INNER(x)
#define SEQ_FILE_TOO_LARGE_MESSAGE \
    "file too large (" PA_SEQ_STR(PA_SEQ_FILE_MAX_KB) " KB max)"

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
