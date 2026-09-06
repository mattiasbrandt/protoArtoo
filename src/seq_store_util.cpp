// =============================================================================
// src/seq_store_util.cpp
//
// Pure decision logic for the Learned Sequence store (ADR 0006). See
// header. No Arduino/FreeRTOS/LittleFS dependencies  --  natively testable.
// =============================================================================

#include "seq_store_util.h"

#include <stdio.h>
#include <string.h>

static ProtocolCheckResult uok() {
    ProtocolCheckResult r = { true, "", "" };
    return r;
}
static ProtocolCheckResult ufail(const char* field, const char* msg) {
    ProtocolCheckResult r = { false, "", "" };
    strncpy(r.field, field, sizeof(r.field) - 1);
    strncpy(r.message, msg, sizeof(r.message) - 1);
    return r;
}

bool seqStoreNameToFile(const char* name, char* out, size_t cap) {
    if (name == nullptr || out == nullptr || cap == 0) return false;
    if (strncmp(name, "DM:", 3) != 0) return false;
    int n = snprintf(out, cap, "%s.json", name);
    if (n <= 0 || (size_t)n >= cap) return false;
    for (size_t i = 0; out[i] != '\0'; ++i) {
        if (out[i] == ':') out[i] = '_';
    }
    return true;
}

ProtocolCheckResult seqStoreCapacityCheck(bool isNew, uint8_t count,
                                          size_t fileLen, size_t freeBytes) {
    if (isNew && count >= SEQ_STORE_MAX) {
        return ufail("name", "store full (16 sequences max)");
    }
    if (fileLen > SEQ_FILE_MAX_BYTES) {
        // The size in the message comes from the same macro as the constant it
        // reports (see the header), so a per-chip cap cannot tell an operator
        // one number while enforcing another.
        return ufail("json", SEQ_FILE_TOO_LARGE_MESSAGE);
    }
    if (freeBytes < SEQ_FS_FREE_FLOOR + fileLen) {
        return ufail("json", "insufficient filesystem space");
    }
    return uok();
}
