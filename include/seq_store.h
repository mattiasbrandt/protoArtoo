// =============================================================================
// include/seq_store.h
//
// Learned Sequence runtime store — LittleFS I/O layer (ADR 0006).
// FIRMWARE-ONLY (LittleFS + FreeRTOS mutex); the pure name index and
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
// Copy semantics (ADR 0004 decision 3): loading is two-phase. seqStorePrepare()
// parses + validates into a transient heap staging pair (never the run
// buffers), so a failed load cannot disturb a running sequence; after the
// dispatcher drains the previous run, seqStoreCommit() copies the staged
// sequence into heap run buffers RIGHT-SIZED to its step counts, which the
// engine executes from, and seqStoreReleaseRun() frees them at run end. A later
// save/delete rewrites files and the index but never the run buffers, and a
// sequence whose file is deleted mid-run finishes from its RAM copy.
// Save validation likewise uses a transient heap buffer. (Run buffers were a
// fixed 2 x 96-step static block (~17 KB) before issue #8; making them dynamic
// + right-sized reclaims that RAM whenever no Learned Sequence is running.)
//
// Staging is right-sized too: every parse (boot scan, prepare, save) allocates
// its main and close branches as two separate blocks sized to the payload's own
// step counts, never the 96+96-step worst case in one piece. The former single
// 18432-byte request was larger than the largest contiguous 8-bit block the
// controller could offer, so every save failed regardless of payload size.
// =============================================================================
#pragma once

#include <stddef.h>
#include <stdint.h>

#include "protocol_check.h"   // ProtocolCheckResult
#include "seq_store_util.h"    // SEQ_FILE_MAX_BYTES, SEQ_FS_FREE_FLOOR, helpers
#include "sequence_engine.h"  // SequenceEntry


// Mount LittleFS (idempotent), ensure /seq exists, and index every valid
// Learned Sequence file. Invalid files are skipped with a warning. Call once at
// boot BEFORE the dispatcher task starts (the boot scan uses a transient heap
// buffer freed before the dispatcher runs).
void seqStoreInit();

// Phase 1 of a load: parse a Learned Sequence by name into a transient heap
// staging pair and run Protocol Check (stamps effectClass). The run buffers
// and any running sequence are untouched. Only the dispatcher task calls
// this, one load at a time; a successful prepare stays staged until
// seqStoreCommit(). Calling prepare again discards a previous staging.
ProtocolCheckResult seqStorePrepare(const char* name);

// Phase 2: copy the staged sequence into heap run buffers RIGHT-SIZED to its
// step counts and build `out` (pointers into those buffers, valid until the
// next commit or seqStoreReleaseRun()). Call only after a successful
// seqStorePrepare(), once the previous run has been drained. Frees the staging.
// Returns false if nothing is staged OR a run-buffer allocation fails (the run
// is refused gracefully rather than crashing on a tight heap).
bool seqStoreCommit(SequenceEntry& out);

// Free the heap run buffers after a Learned Sequence run has fully drained.
// The dispatcher calls this at sequence end/abort so an idle body (or a
// Factory-only run — Factory steps live in flash) holds zero run-buffer RAM.
// No-op / safe to call when nothing is allocated. Dispatcher task only.
void seqStoreReleaseRun();

// Validate + persist a Learned Sequence from JSON text. Runs Protocol Check
// (transient heap staging), enforces capacity (16-file cap + free-space floor +
// per-file size), writes temp-file + rename, and reindexes. Returns a
// field-level error on rejection (nothing written), ok on success.
ProtocolCheckResult seqStoreSave(const char* json, size_t len);

// Delete a Learned Sequence and its index entry (Memory Wipe). Returns true
// only if the file is actually gone (removed, or index-only entry cleaned
// up); a failed file removal keeps the index entry so the store and the
// filesystem cannot diverge.
bool seqStoreDelete(const char* name);

// Copy up to `capacity` bytes of a Learned Sequence file's raw JSON, starting
// at byte `offset`, into `out` (GET /api/seq?name=). Returns the number of
// bytes copied; 0 means end of file, an unindexed name, or a read failure --
// all three end the response body, which is the only distinction the caller
// can act on anyway.
//
// Offset-addressed rather than streamed into an Arduino Print: this is what
// WebRequest::sendChunked() drives (ADR 0021), so no backend and no handler
// ever holds more than one chunk of a file that runs to SEQ_FILE_MAX_BYTES.
// Each call opens and seeks, which costs a handful of LittleFS opens across a
// whole file and, unlike a handle cached between calls, leaves nothing open if
// the client disconnects mid-body.
size_t seqStoreReadFileSlice(const char* name, size_t offset, uint8_t* out, size_t capacity);
