// =============================================================================
// src/log_buffer.cpp
//
// Pure ring-buffer implementation for the in-memory log store.
// No Arduino, no FreeRTOS, no hardware dependencies.
// =============================================================================

#include "log_buffer.h"

#include <stdio.h>
#include <string.h>

void logBufferAppend(LogBuffer* buf, const char* line) {
    strncpy(buf->lines[buf->head], line, LOG_LINE_MAX - 1);
    buf->lines[buf->head][LOG_LINE_MAX - 1] = '\0';
    buf->head = (buf->head + 1) % LOG_BUFFER_LINES;
    if (buf->count < LOG_BUFFER_LINES) {
        buf->count++;
    }
}

size_t logBufferCopy(const LogBuffer* buf, char* out, size_t outSize) {
    if (outSize == 0) {
        return 0;
    }

    size_t start = (buf->head + LOG_BUFFER_LINES - buf->count) % LOG_BUFFER_LINES;
    size_t used = 0;

    for (size_t i = 0; i < buf->count; ++i) {
        const char* line = buf->lines[(start + i) % LOG_BUFFER_LINES];
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
