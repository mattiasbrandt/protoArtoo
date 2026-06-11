// =============================================================================
// include/seq_store_index.h
//
// In-memory Learned Sequence name index (issue #2 slice 3, ADR 0006). PURE:
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

// Capacity cap (issue #2 grill decision 5: 16 Learned Sequences total).
static const uint8_t SEQ_STORE_MAX = 16;

struct SeqIndexEntry {
    char           name[24];      // "DM:MYSEQ"
    SeqToggleGroup toggleGroup;   // for retrain coherence + list badges
    uint32_t       suppressMs;
    char           source[8];     // "user" | "guild"
    bool           modified;      // a guild file edited in place
    char           file[40];      // basename under /data/seq/
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

// Iteration for listings / RC enumeration.
uint8_t seqStoreIndexCount();
const SeqIndexEntry* seqStoreIndexAt(uint8_t i);
