// =============================================================================
// src/seq_dangling_bindings.cpp
//
// Dangling RC trigger scan for Memory Wipe. See header for the rules.
// =============================================================================

#include "seq_dangling_bindings.h"

#include <string.h>

size_t seqDanglingBindings(const char* name, bool catalogShadows,
                           const RcTriggerBinding* slots, size_t slotCount,
                           SeqDanglingBinding* out, size_t cap) {
    if (name == nullptr || slots == nullptr || out == nullptr) {
        return 0;
    }
    if (catalogShadows) {
        return 0;
    }

    size_t count = 0;
    for (size_t i = 0; i < slotCount && count < cap; ++i) {
        const RcTriggerBinding& b = slots[i];
        if (b.target != DOME_ACTION_SEQ || strcmp(b.marcduinoPayload, name) != 0) {
            continue;
        }
        out[count].source = b.source;
        out[count].channel = b.channel;
        ++count;
    }
    return count;
}
