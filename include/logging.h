// =============================================================================
// include/logging.h
//
// Logging macros for protoArtoo  --  shared between tasks, drivers, and web layer.
// Centralizes log formatting to ensure consistent output across the system.
//
// Log level is checked at runtime against the config cache log level, which is
// NVS-backed and adjustable from the Setup page without a reboot. The
// compile-time PA_LOG_LEVEL build flag sets the boot default and the maximum
// ring buffer depth (see log_buffer.h), but does not gate output at compile
// time  --  all levels are always compiled in.
// =============================================================================
#pragma once

#include <Arduino.h>
#include <stdint.h>

#include "config.h"
#include "log_buffer.h"

static constexpr size_t PA_LOG_SERIAL_LINE_MAX = 256;

void paLogInit();

// Record one log line.
//
// It is written ONCE, to the Log Ring, under the ring's own critical section.
// Whether it also reaches the serial wire depends on who owns that wire
// (ADR 0039): before the Console task binds the serial adapter this writes the
// line straight out, because there is no task to drain it yet; afterwards the
// Console task owns the wire and its drain is the only thing that puts a log
// line on it. The decision is read inside the same critical section the append
// happens in, so a line arriving at the instant ownership changes is written
// exactly once - never twice, never not at all.
void paLogLine(const char* line);

// Hand the serial wire to the Console task (ADR 0039). Called once, from
// consoleSerialBindCli(), the moment the Console task has an embedded-cli
// instance to render redraws with. Places the drain cursor at the ring's
// current head so nothing already on the wire is drained a second time.
void paLogWireBindToConsole();

// Read the next log line the Console task owes the wire, advancing the drain
// cursor. `evicted` reports how many lines the ring overwrote before this one
// (0 when the drain kept up) so the drain can mark the loss.
// Returns false when the drain has caught up with the writers.
// See logBufferDrainNext() (log_buffer.h) for the cursor semantics; this wraps
// it in the ring's critical section.
bool paLogDrainNextLine(char* out, size_t outSize, uint32_t* evicted);

uint8_t configCurrentLogLevel();

// Inline helper  --  reads the live log level under the config cache lock.
// Used by every log macro to get the current runtime level without a full
inline uint8_t paCurrentLogLevel() {
    return configCurrentLogLevel();
}

#define _PA_LOG_FORMAT(level, tag, fmt, ...)                                      \
    do {                                                                          \
        char _pa_log_buf[PA_LOG_SERIAL_LINE_MAX];                                 \
        /* Serial lines are bounded here; the retained SSE ring truncates to LOG_LINE_MAX. */ \
        _Pragma("GCC diagnostic push")                                            \
        _Pragma("GCC diagnostic ignored \"-Wformat-truncation\"")                 \
        snprintf(_pa_log_buf, sizeof(_pa_log_buf), "[%lu][" level "][%s] " fmt,               \
                 (unsigned long)millis(), tag, ##__VA_ARGS__);                                \
        _Pragma("GCC diagnostic pop")                                             \
        paLogLine(_pa_log_buf);                                                   \
    } while (0)

// Tag-based logging macros with runtime level check
#define PA_LOG_ERROR(tag, fmt, ...)                                         \
    do {                                                                    \
        if (paCurrentLogLevel() >= PA_LOG_LEVEL_ERROR) {                    \
            _PA_LOG_FORMAT("E", tag, fmt, ##__VA_ARGS__);                   \
        }                                                                   \
    } while (0)

#define PA_LOG_WARN(tag, fmt, ...)                                          \
    do {                                                                    \
        if (paCurrentLogLevel() >= PA_LOG_LEVEL_WARN) {                     \
            _PA_LOG_FORMAT("W", tag, fmt, ##__VA_ARGS__);                   \
        }                                                                   \
    } while (0)

#define PA_LOG_INFO(tag, fmt, ...)                                          \
    do {                                                                    \
        if (paCurrentLogLevel() >= PA_LOG_LEVEL_INFO) {                     \
            _PA_LOG_FORMAT("I", tag, fmt, ##__VA_ARGS__);                   \
        }                                                                   \
    } while (0)

#define PA_LOG_DEBUG(tag, fmt, ...)                                         \
    do {                                                                    \
        if (paCurrentLogLevel() >= PA_LOG_LEVEL_DEBUG) {                    \
            _PA_LOG_FORMAT("D", tag, fmt, ##__VA_ARGS__);                   \
        }                                                                   \
    } while (0)
