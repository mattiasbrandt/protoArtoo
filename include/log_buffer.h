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

// Ring depth follows the operator's saved log level. The ring is sized ONCE at
// boot from NVS (see paLogRingApplyBootDepth in main.cpp): changing the level
// on the Setup page changes emission immediately and ring depth at the next
// reboot, so log level remains the single knob and history depth follows the
// chosen verbosity.
//
//   ERROR (1): 16 lines  --  faults only; minimal memory use
//   WARN  (2): 20 lines  --  faults plus safety warnings; default production
//   INFO  (3): 24 lines  --  normal operator use
//   DEBUG (4): 48 lines  --  verbose; more history needed for diagnosis
//
// LOG_RING_BOOTSTRAP_LINES backs the static ring that captures the few boot
// lines emitted before NVS config loads; those lines are carried into the
// boot-sized ring. LOG_RING_MAX_LINES bounds the sized ring and the native
// test storage.
static constexpr size_t LOG_RING_BOOTSTRAP_LINES = 8;
static constexpr size_t LOG_RING_MAX_LINES = 48;

// Ring depth for a runtime log level (1=Error .. 4=Debug); out-of-range low
// values get the minimum depth, high values the maximum.
size_t logRingLinesForLevel(uint8_t level);

// -----------------------------------------------------------------------------
// LogBuffer  --  plain-old-data ring view over caller-owned storage.
// Zero-initialise, then logBufferInit() with storage before first use.
// -----------------------------------------------------------------------------
struct LogBuffer {
    char (*lines)[LOG_LINE_MAX];  // caller-owned storage, `capacity` slots
    size_t capacity;        // number of line slots in `lines`
    size_t count;           // number of valid entries (0..capacity)
    size_t head;            // index where the NEXT write will go (wraps)
    uint32_t totalWritten;  // monotonically increasing count of all appends ever
};

// -----------------------------------------------------------------------------
// logBufferInit()
// Point the ring at caller-owned storage and reset it to empty.
// params: buf       --  ring buffer to initialise (must not be null)
//         storage   --  array of `capacity` line slots (must not be null)
//         capacity  --  number of slots in storage (must be > 0)
// thread-safe: NO  --  caller must hold any required lock
// -----------------------------------------------------------------------------
void logBufferInit(LogBuffer* buf, char (*storage)[LOG_LINE_MAX], size_t capacity);

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
