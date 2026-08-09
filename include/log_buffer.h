// =============================================================================
// include/log_buffer.h
//
// Pure ring-buffer helpers for the in-memory log store.
// No Arduino, no FreeRTOS, no hardware dependencies  --  testable in native env.
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
// LOG_LINE_MAX: max chars per stored line (including null terminator space).
// 128 chars covers all normal log lines; longer lines are truncated at source.
static constexpr size_t LOG_LINE_MAX = 128;

// LOG_BUFFER_LINES: in-memory ring depth, scaled to PA_LOG_LEVEL.
//
//   PA_LOG_LEVEL_ERROR (1): 16 lines  --  faults only; minimal memory use
//   PA_LOG_LEVEL_INFO  (2): 20 lines  --  normal operator use; default production
//   PA_LOG_LEVEL_DEBUG (3): 48 lines  --  verbose; more history needed for diagnosis
//
// Deeper debug sessions that need more history should raise the log level.
// The dashboard console always shows all buffered lines; there is no separate
// UI control for ring depth  --  log level is the single knob.
#ifndef PA_LOG_LEVEL
#  define PA_LOG_LEVEL 2
#endif

#if PA_LOG_LEVEL >= 3
static constexpr size_t LOG_BUFFER_LINES = 48;
#elif PA_LOG_LEVEL >= 2
static constexpr size_t LOG_BUFFER_LINES = 20;
#else
static constexpr size_t LOG_BUFFER_LINES = 16;
#endif

// -----------------------------------------------------------------------------
// LogBuffer  --  plain-old-data ring buffer, zero-initialise before use.
// -----------------------------------------------------------------------------
struct LogBuffer {
    char lines[LOG_BUFFER_LINES][LOG_LINE_MAX];
    size_t count;           // number of valid entries (0..LOG_BUFFER_LINES)
    size_t head;            // index where the NEXT write will go (wraps)
    uint32_t totalWritten;  // monotonically increasing count of all appends ever
};

// -----------------------------------------------------------------------------
// logBufferAppend()
// Append a null-terminated line to the ring buffer.
// If the buffer is full the oldest entry is overwritten.
// The line is truncated to LOG_LINE_MAX-1 characters.
// params: buf   --  ring buffer to write into (must not be null)
//         line  --  null-terminated string to append (must not be null)
// thread-safe: NO  --  caller must hold any required lock
// -----------------------------------------------------------------------------
void logBufferAppend(LogBuffer* buf, const char* line);

// -----------------------------------------------------------------------------
// logBufferCopy()
// Copy all buffered lines (oldest first) into a caller-supplied flat buffer,
// each line separated by '\n'.  The output is always null-terminated.
// If the output buffer is too small the copy stops and the result is truncated.
// params: buf         --  ring buffer to read from (must not be null)
//         out         --  destination character buffer (must not be null)
//         outSize     --  size of out in bytes (must be > 0)
// returns: number of bytes written (excluding the null terminator)
// thread-safe: NO  --  caller must hold any required lock
// -----------------------------------------------------------------------------
size_t logBufferCopy(const LogBuffer* buf, char* out, size_t outSize);
