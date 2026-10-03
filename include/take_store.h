// =============================================================================
// include/take_store.h
//
// The take store - LittleFS I/O for take files (#442, ADR 0061). FIRMWARE-ONLY.
// The figures and the file names are the pure include/take_store_util.h; the
// file's encoding is include/take_capture.h.
//
// A take's file is "/seq/<owner>.<take>.new" from the moment it is kept off a
// performance, and "/seq/<owner>.<take>.take" once its sequence is saved
// naming it (take_store_util.h says why the state lives in the name). The
// sequence store calls in here after a save and a delete, and at boot, so a
// take lives and dies with the sequence that holds it:
//   - a save names the takes the sequence holds: those become kept, and any
//     other take of that sequence - one replaced or removed in the editor -
//     is deleted;
//   - deleting a sequence deletes every take it owns;
//   - a boot deletes what no sequence holds: the ".new" takes of performances
//     never saved, and the takes of a sequence that is gone.
//
// Concurrency: one mutex of its own, never held together with the sequence
// store's (seq_store.cpp calls these after releasing its lock), so the two
// cannot deadlock. Core 0 only: web handlers and the boot path.
// =============================================================================
#pragma once

#include <stddef.h>
#include <stdint.h>

#include "take_capture.h"     // TakeCapture
#include "take_store_util.h"  // TAKE_ID_LEN

// The take ids one sequence names, read from its `takes` array.
struct TakeRefs {
    uint8_t count;
    char ids[TAKE_STORE_CAP][TAKE_ID_LEN + 1];
};

// Boot, after seqStoreInit() has indexed the sequences and reconciled each
// one's takes (takeStoreSequenceSaved()): deletes every ".new" take, and every
// take whose owner is no indexed sequence.
void takeStoreInit();

// How many take files the droid holds, kept and not yet saved together.
uint8_t takeStoreHeld();

// Whether a performance could be kept now, asked when a take is armed so a
// builder does not perform for nothing. nullptr when there is room - free
// slots, or a ".new" take the keep will replace - else the reason, written
// into `out` and returned.
const char* takeStoreRoomRefusal(char* out, size_t cap);

// Write a finished capture as a ".new" take of the sequence whose stable id is
// `owner`, minting its id into `idOut` (TAKE_ID_LEN + 1). On a full store a
// ".new" take is replaced first and its id written to `replacedOut` ("" when
// none was). Returns nullptr on success, else the reason, written into
// `refusal`.
const char* takeStoreWriteNew(const char* owner, const TakeCapture& capture, char* idOut,
                              char* replacedOut, char* refusal, size_t refusalCap);

// After the sequence whose stable id is `owner` has been saved naming `refs`:
// each named take that exists becomes kept, and - when `dropOthers` - every
// other take of `owner` is deleted. A named take that is not on the droid is
// left to arrive (a restore posts the sequence before its takes).
//
// `dropOthers` is false when another sequence carries the same id: a rename
// is a save under a new name that leaves the old file, so two sequences can
// share one id, and a take the other one names is not this save's to delete.
void takeStoreSequenceSaved(const char* owner, const TakeRefs& refs, bool dropOthers);

// After the sequence whose stable id is `owner` has been deleted, and no other
// sequence carries that id: every take it owned is deleted.
void takeStoreSequenceDeleted(const char* owner);

// Whether take `take` of `owner` is on the droid, kept or not.
bool takeStoreHas(const char* owner, const char* take);

// Up to `capacity` bytes of that take's file from `offset` (GET
// /api/take/file, WebRequest::sendChunked()). 0 at the end, or when it is gone.
size_t takeStoreReadSlice(const char* owner, const char* take, size_t offset, uint8_t* out,
                          size_t capacity);

// -----------------------------------------------------------------------------
// A take file arriving in pieces (a backup's restore, POST /api/take/file):
// begun with the file's name, fed each piece as it arrives, finished once.
// It is checked as it comes - header, Part ids, every sample - written to a
// temporary file, and renamed into place as a kept take only when the whole
// of it passed and its owner is a sequence on the droid. A begin that refused
// is still finished: the pieces are dropped, and the finish answers with the
// begin's reason. One at a time: the web server dispatches one request at a
// time.
// -----------------------------------------------------------------------------
const char* takeStoreRestoreBegin(const char* fileName, char* refusal, size_t refusalCap);
void takeStoreRestoreAppend(const uint8_t* data, size_t len);
const char* takeStoreRestoreFinish(char* refusal, size_t refusalCap);
