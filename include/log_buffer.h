// =============================================================================
// include/log_buffer.h
//
// Pure ring-buffer helpers for the in-memory log store.
// No Arduino, no FreeRTOS, no hardware dependencies — testable in native env.
//
// LogBuffer holds a fixed-size circular array of fixed-width lines.
// Callers are responsible for any locking needed around these functions.
// =============================================================================
#pragma once

#include <stddef.h>
#include <stdint.h>

// -----------------------------------------------------------------------------
// Compile-time ring-buffer dimensions.
// These match the constants in main.cpp; changing them here changes both.
// -----------------------------------------------------------------------------
static constexpr size_t LOG_LINE_MAX = 160;
static constexpr size_t LOG_BUFFER_LINES = 64;

// -----------------------------------------------------------------------------
// LogBuffer — plain-old-data ring buffer, zero-initialise before use.
// -----------------------------------------------------------------------------
struct LogBuffer {
    char lines[LOG_BUFFER_LINES][LOG_LINE_MAX];
    size_t count;  // number of valid entries (0..LOG_BUFFER_LINES)
    size_t head;   // index where the NEXT write will go (wraps)
};

// -----------------------------------------------------------------------------
// logBufferAppend()
// Append a null-terminated line to the ring buffer.
// If the buffer is full the oldest entry is overwritten.
// The line is truncated to LOG_LINE_MAX-1 characters.
// params: buf  — ring buffer to write into (must not be null)
//         line — null-terminated string to append (must not be null)
// thread-safe: NO — caller must hold any required lock
// -----------------------------------------------------------------------------
void logBufferAppend(LogBuffer* buf, const char* line);

// -----------------------------------------------------------------------------
// logBufferCopy()
// Copy all buffered lines (oldest first) into a caller-supplied flat buffer,
// each line separated by '\n'.  The output is always null-terminated.
// If the output buffer is too small the copy stops and the result is truncated.
// params: buf        — ring buffer to read from (must not be null)
//         out        — destination character buffer (must not be null)
//         outSize    — size of out in bytes (must be > 0)
// returns: number of bytes written (excluding the null terminator)
// thread-safe: NO — caller must hold any required lock
// -----------------------------------------------------------------------------
size_t logBufferCopy(const LogBuffer* buf, char* out, size_t outSize);
