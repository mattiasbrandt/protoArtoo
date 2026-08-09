// =============================================================================
// src/seq_store_index.cpp
//
// Pure in-memory Learned Sequence name index (ADR 0006).
// See header. No locking here  --  the firmware I/O layer (seq_store.cpp) holds a
// mutex across index mutation + the matching file operation so the index and
// filesystem never diverge under concurrent web/dispatcher access.
// =============================================================================

#include "seq_store_index.h"

#include <string.h>

static SeqIndexEntry s_entries[SEQ_STORE_MAX];
static uint8_t       s_count = 0;

void seqStoreIndexClear() {
    s_count = 0;
}

static int findSlot(const char* name) {
    if (name == nullptr || name[0] == '\0') return -1;
    for (uint8_t i = 0; i < s_count; ++i) {
        if (strcmp(s_entries[i].name, name) == 0) return (int)i;
    }
    return -1;
}

bool seqStoreIndexAdd(const SeqIndexEntry& e) {
    if (e.name[0] == '\0') return false;
    int slot = findSlot(e.name);
    if (slot >= 0) {
        s_entries[slot] = e;  // update in place
        return true;
    }
    if (s_count >= SEQ_STORE_MAX) return false;
    s_entries[s_count++] = e;
    return true;
}

bool seqStoreIndexRemove(const char* name) {
    int slot = findSlot(name);
    if (slot < 0) return false;
    // Compact: move the last entry into the freed slot.
    s_entries[slot] = s_entries[s_count - 1];
    --s_count;
    return true;
}

const SeqIndexEntry* seqStoreIndexFind(const char* name) {
    int slot = findSlot(name);
    return (slot >= 0) ? &s_entries[slot] : nullptr;
}

uint8_t seqStoreIndexCount() {
    return s_count;
}

const SeqIndexEntry* seqStoreIndexAt(uint8_t i) {
    return (i < s_count) ? &s_entries[i] : nullptr;
}
