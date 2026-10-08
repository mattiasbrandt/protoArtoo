// =============================================================================
// include/seq_store_index.h
//
// In-memory Learned Sequence name index (ADR 0006). PURE:
// a fixed static table of sequence metadata, no FreeRTOS/LittleFS/JSON. The
// firmware I/O layer (seq_store.cpp) populates it at boot/CRUD by scanning
// /data/seq/; the pure routing seam (sequenceLookup) and listings query it, so
// runtime-first lookup precedence stays native-testable.
//
// The index holds only metadata needed to route, list, and gate without
// touching the filesystem. Step bodies are parsed on demand at sequence start.
// =============================================================================
#pragma once

#include <stdint.h>

#include "sequence_engine.h"  // SeqToggleGroup

// Index CAPACITY: how many Learned Sequences the droid can hold in memory, list
// and play. Ten on every board. It is deliberately not the number that refuses
// a save -- that is the board's CAP, SEQ_STORE_CAP in seq_store_util.h, which
// is five on the artoo-esp32 (ADR 0065, amended 2026-09-25). The two are kept
// apart because a firmware-only update can leave an artoo-esp32 holding more
// than its cap, and every one it holds must still load at boot, list and play;
// only a new save is refused. Collapsing them back into one constant would
// drop the sixth-and-later sequences out of the boot scan on such a droid.
// History: issue #2 grill decision 5 set 16; the operator lowered it to 10 on
// 2026-09-13 (#382).
static const uint8_t SEQ_INDEX_CAPACITY = 10;

// How much of a sequence's purpose the index keeps for its list row: the first
// 40 bytes of meta.purpose (operator, 2026-10-02, #441). The whole purpose
// stays in the file, where the editor reads it. Bytes, not characters: a
// purpose is UTF-8, and the cut backs off to a whole character.
static const uint8_t SEQ_INDEX_PURPOSE_BYTES = 40;

// What a list row says without opening the file: `steps`, `lengthMs`,
// `purpose` and `purposeCut` are worked out from the file when it is saved,
// and by the boot scan for what is already stored (seq_store.cpp
// fillIndexEntry()). They cost 48 B an entry, 480 B for the table; the flag
// rides in what would otherwise be padding.
struct SeqIndexEntry {
    char           name[24];      // "DM:MYSEQ"
    SeqToggleGroup toggleGroup;   // for retrain coherence + list badges
    uint32_t       suppressMs;
    char           source[8];     // the file's meta.source: "user" | "guild" |
                                  // "factory" (one tuned from a Factory one)
    bool           modified;      // the file's meta.modified; nothing on the
                                  // controller sets it
    char           file[40];      // basename under /data/seq/
    bool           valid;         // false if file fails Protocol Check at boot
    char           id[17];        // stable id a phrase refers to it by (ADR 0046);
                                  // "" for one saved before phrases existed
    uint8_t        steps;         // main branch step count, its end step included
    uint32_t       lengthMs;      // how long a run is: the end step's time, with
                                  // beats resolved (seqStoreRunLengthMs())
    char           purpose[SEQ_INDEX_PURPOSE_BYTES + 1];  // start of meta.purpose
    bool           purposeCut;    // the file's purpose runs on past `purpose`
    bool           mayOpenBody;   // a step of either branch can open a body Part
                                  // (seqStepsMayOpenBodyPart()); rides in padding
};

// Empty the index.
void seqStoreIndexClear();

// Insert or replace by name. Returns false only when the table is full AND the
// name is not already present (an existing name updates in place).
bool seqStoreIndexAdd(const SeqIndexEntry& e);

// Remove by name. Returns true if an entry was removed.
bool seqStoreIndexRemove(const char* name);

// Find by exact name (case-sensitive). nullptr when absent.
const SeqIndexEntry* seqStoreIndexFind(const char* name);

// Find by a phrase's stable reference: a sequence name ("DM:...") finds by
// name, anything else by the stable id. nullptr when absent.
const SeqIndexEntry* seqStoreIndexFindRef(const char* ref);

// Iteration for listings / RC enumeration.
uint8_t seqStoreIndexCount();
const SeqIndexEntry* seqStoreIndexAt(uint8_t i);
