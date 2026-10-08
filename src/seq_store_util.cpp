// =============================================================================
// src/seq_store_util.cpp
//
// Pure decision logic for the Learned Sequence store (ADR 0006). See
// header. No Arduino/FreeRTOS/LittleFS dependencies  --  natively testable.
// =============================================================================

#include "seq_store_util.h"

#include <stdio.h>
#include <stdlib.h>
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
    // The board's cap, not the index capacity: an over-cap droid (see the
    // header) holds more than this, and `count >= cap` refuses a new name for
    // as long as that lasts. An existing name is an overwrite and still saves.
    if (isNew && count >= SEQ_STORE_CAP) {
        return ufail("name", SEQ_STORE_FULL_MESSAGE);
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

uint32_t seqStoreRunLengthMs(const SeqStep* steps, uint8_t count) {
    if (steps == nullptr || count == 0) return 0;
    const SeqStep& last = steps[count - 1];
    return (last.type == STEP_END) ? last.tMs : 0;
}

bool seqStoreCutPurpose(const char* purpose, char* out, size_t cap) {
    if (out == nullptr || cap == 0) return false;
    out[0] = '\0';
    if (purpose == nullptr) return false;
    const size_t len = strlen(purpose);
    size_t keep = (len < cap) ? len : cap - 1;
    if (keep < len) {
        // A UTF-8 continuation byte is 10xxxxxx. One at the cut means the
        // character it belongs to started before it and does not fit whole.
        while (keep > 0 && ((uint8_t)purpose[keep] & 0xC0) == 0x80) --keep;
    }
    memcpy(out, purpose, keep);
    out[keep] = '\0';
    return keep < len;
}

// -----------------------------------------------------------------------------
// Splicing a phrase in (ADR 0046, #438)
// -----------------------------------------------------------------------------

// Whether a step sits inside a loop body in `steps` (it is then timed from the
// loop's pass, and keeps its time when spliced).
static bool inLoopBodyAt(const SeqStep* steps, uint8_t count, uint8_t at) {
    for (uint8_t i = 0; i < count && i < at; ++i) {
        if (steps[i].type == STEP_LOOP && at <= (uint16_t)i + steps[i].params.bodyCount) return true;
    }
    return false;
}

// seqStoreSplicePhrase(): see the header. Out of line and allocation-only, so
// its frame is not stacked under the parser on the Coordinator's measured chain.
__attribute__((noinline)) bool seqStoreSplicePhrase(SeqStep** buf, uint8_t* count, uint8_t at,
                                                    const SeqStep* child, uint8_t childCount) {
    const uint8_t childKept =
        (child != nullptr && childCount > 0 && child[childCount - 1].type == STEP_END) ? (uint8_t)(childCount - 1)
                                                                                     : childCount;
    const uint16_t total = (uint16_t)(*count - 1 + (child != nullptr ? childKept : 0));
    if (total > PC_MAX_STEPS || total == 0) return false;
    SeqStep* merged = (SeqStep*)malloc(sizeof(SeqStep) * total);
    SeqStep* sorted = (SeqStep*)malloc(sizeof(SeqStep) * total);
    if (merged == nullptr || sorted == nullptr) {
        free(merged);
        free(sorted);
        return false;
    }
    const uint32_t atMs = (*buf)[at].tMs;
    uint16_t n = 0;
    for (uint8_t i = 0; i < *count; ++i) {
        if (i != at) {
            merged[n++] = (*buf)[i];
            continue;
        }
        for (uint8_t k = 0; child != nullptr && k < childKept; ++k) {
            merged[n] = child[k];
            if (!inLoopBodyAt(child, childKept, k)) merged[n].tMs += atMs;
            ++n;
        }
    }
    // Stable selection of the earliest unplaced unit, O(n^2) over at most 96.
    uint32_t placed[3] = {0, 0, 0};
    uint16_t out = 0;
    while (out < total) {
        int16_t best = -1;
        for (uint16_t i = 0; i < total; ++i) {
            if (placed[i / 32] & (1u << (i % 32))) continue;
            if (inLoopBodyAt(merged, (uint8_t)total, (uint8_t)i)) continue;
            if (best < 0 || merged[i].tMs < merged[best].tMs) best = (int16_t)i;
        }
        if (best < 0) break;  // defensive: every unit placed
        const uint16_t span = (merged[best].type == STEP_LOOP) ? (uint16_t)(1 + merged[best].params.bodyCount) : 1u;
        for (uint16_t k = 0; k < span && best + k < total; ++k) {
            sorted[out++] = merged[best + k];
            placed[(best + k) / 32] |= (1u << ((best + k) % 32));
        }
    }
    free(merged);
    // The engine ends the run at the end step and never runs a step after it,
    // so a phrase that runs past the parent's end is cut there, as written.
    for (uint16_t i = 0; i < out; ++i) {
        if (sorted[i].type == STEP_END) {
            out = (uint16_t)(i + 1);
            break;
        }
    }
    free(*buf);
    *buf = sorted;
    *count = (uint8_t)out;
    return true;
}

