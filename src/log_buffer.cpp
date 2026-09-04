// =============================================================================
// src/log_buffer.cpp
//
// Pure ring-buffer implementation for the in-memory log store.
// No Arduino, no FreeRTOS, no hardware dependencies.
// =============================================================================

#include "log_buffer.h"

#include <stdio.h>
#include <string.h>

// The rungs are chip-target specific (log_buffer.h). Naming them here rather
// than repeating literals keeps the ladder in one place: the header carries the
// per-chip numbers and the measurement they came from, and this function only
// maps a level onto them.
size_t logRingLinesForLevel(uint8_t level) {
    switch (level) {
        case 2:
            return LOG_RING_LINES_WARN;
        case 3:
            return LOG_RING_LINES_INFO;
        default:
            return level <= 1 ? LOG_RING_LINES_ERROR : LOG_RING_LINES_DEBUG;
    }
}

void logBufferInit(LogBuffer* buf, char (*storage)[LOG_LINE_MAX], size_t capacity) {
    buf->lines = storage;
    buf->capacity = capacity;
    buf->count = 0;
    buf->head = 0;
    buf->totalWritten = 0;
}

void logBufferAppend(LogBuffer* buf, const char* line) {
    if (buf->lines == nullptr || buf->capacity == 0) {
        return;
    }
    strncpy(buf->lines[buf->head], line, LOG_LINE_MAX - 1);
    buf->lines[buf->head][LOG_LINE_MAX - 1] = '\0';
    buf->head = (buf->head + 1) % buf->capacity;
    if (buf->count < buf->capacity) {
        buf->count++;
    }
    buf->totalWritten++;
}

size_t logBufferCopy(const LogBuffer* buf, char* out, size_t outSize) {
    if (outSize == 0) {
        return 0;
    }
    if (buf->lines == nullptr || buf->capacity == 0) {
        out[0] = '\0';
        return 0;
    }

    size_t start = (buf->head + buf->capacity - buf->count) % buf->capacity;
    size_t used = 0;

    for (size_t i = 0; i < buf->count; ++i) {
        const char* line = buf->lines[(start + i) % buf->capacity];
        int written = snprintf(out + used, outSize - used, "%s\n", line);
        if (written < 0 || (size_t)written >= outSize - used) {
            used = outSize - 1;
            break;
        }
        used += (size_t)written;
    }

    out[used] = '\0';
    return used;
}

bool logBufferDrainNext(const LogBuffer* buf, uint32_t* cursor, char* out, size_t outSize,
                        uint32_t* evicted) {
    if (evicted != nullptr) {
        *evicted = 0;
    }
    if (buf == nullptr || cursor == nullptr || out == nullptr || outSize == 0 ||
        buf->lines == nullptr || buf->capacity == 0) {
        return false;
    }

    const uint32_t total = buf->totalWritten;
    const uint32_t count = (uint32_t)buf->count;
    // The sequence number of the oldest line the ring still holds. Everything
    // below it has been overwritten.
    const uint32_t oldest = (total >= count) ? (total - count) : 0;

    if (*cursor > total) {
        // Ahead of the writer, which means the ring was reset under the reader
        // rather than that lines were lost. Rejoin at the newest line instead
        // of reading the whole ring again as if it were all new.
        *cursor = total;
    }
    if (*cursor < oldest) {
        // The writer overtook the reader. Report the gap once, then continue
        // from the oldest line the ring still has: a reader that silently
        // resumed would turn lost history into an unexplained jump.
        if (evicted != nullptr) {
            *evicted = oldest - *cursor;
        }
        *cursor = oldest;
    }
    if (*cursor >= total) {
        return false;
    }

    const size_t start = (buf->head + buf->capacity - buf->count) % buf->capacity;
    const size_t idx = (start + (size_t)(*cursor - oldest)) % buf->capacity;
    strncpy(out, buf->lines[idx], outSize - 1);
    out[outSize - 1] = '\0';
    (*cursor)++;
    return true;
}
