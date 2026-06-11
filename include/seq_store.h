// =============================================================================
// include/seq_store.h
//
// Learned Sequence runtime store — LittleFS I/O layer (issue #2 slice 3c,
// ADR 0006). FIRMWARE-ONLY (LittleFS + FreeRTOS mutex); the pure name index and
// routing seam live in seq_store_index.{h,cpp} / sequence_catalog.cpp so the
// lookup precedence stays native-testable.
//
// Files live under /seq/ on LittleFS, one JSON-v1 file per sequence named
// DM_<BODY>.json (':' -> '_'). The embedded `name` field is authoritative.
//
// Concurrency: a mutex guards every index mutation together with its file
// operation, so the in-memory index and the filesystem never diverge under
// concurrent web-handler / dispatcher access.
//
// Copy semantics (ADR 0004 decision 3): seqStoreLoad() parses into the store's
// static staging buffers and the dispatcher runs from them; a later save/delete
// rewrites files and the index but never the buffers of a running sequence.
// Save validation uses a transient heap buffer, never the run buffers.
// =============================================================================
#pragma once

#include <stddef.h>
#include <stdint.h>

#include "protocol_check.h"   // ProtocolCheckResult
#include "sequence_engine.h"  // SequenceEntry

class Print;  // Arduino print target (seqStoreStreamFile); full def in the .cpp

// Capacity guards (issue #2 grill decision 5).
static const size_t SEQ_FILE_MAX_BYTES = 12 * 1024;   // per-file cap
static const size_t SEQ_FS_FREE_FLOOR  = 24 * 1024;   // LittleFS free-space floor

// Mount LittleFS (idempotent), ensure /seq exists, and index every valid
// Learned Sequence file. Invalid files are skipped with a warning. Call once at
// boot BEFORE the dispatcher task starts (the boot scan reuses the run buffers).
void seqStoreInit();

// Load a Learned Sequence by name into the store's static staging buffers and
// build `out` (pointers into those buffers, valid until the next load). Parses +
// runs Protocol Check (stamps effectClass). Only the dispatcher calls this, one
// sequence at a time. Returns ok on success.
ProtocolCheckResult seqStoreLoad(const char* name, SequenceEntry& out);

// Validate + persist a Learned Sequence from JSON text. Runs Protocol Check
// (transient heap staging), enforces capacity (16-file cap + free-space floor +
// per-file size), writes temp-file + rename, and reindexes. Returns a
// field-level error on rejection (nothing written), ok on success.
ProtocolCheckResult seqStoreSave(const char* json, size_t len);

// Delete a Learned Sequence and its index entry (Memory Wipe). Returns true if
// a file/entry was removed.
bool seqStoreDelete(const char* name);

// Stream a Learned Sequence file's raw JSON to `out` (GET /api/seq?name=).
// Returns false if the name is not indexed or the file is missing.
bool seqStoreStreamFile(const char* name, Print& out);
