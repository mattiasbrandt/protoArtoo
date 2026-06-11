// =============================================================================
// include/seq_store_util.h
//
// Pure decision logic for the Learned Sequence store (issue #2 slice 3, ADR
// 0006). Extracted from seq_store.cpp so the capacity policy and the name->file
// mapping are native-testable against the real production code, without an
// (unfaithful) LittleFS emulation. seq_store.cpp calls these around its I/O.
// =============================================================================
#pragma once

#include <stddef.h>
#include <stdint.h>

#include "protocol_check.h"    // ProtocolCheckResult
#include "seq_store_index.h"   // SEQ_STORE_MAX

// Capacity guards (issue #2 grill decision 5).
static const size_t SEQ_FILE_MAX_BYTES = 12 * 1024;   // per-file cap
static const size_t SEQ_FS_FREE_FLOOR  = 24 * 1024;   // LittleFS free-space floor

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
