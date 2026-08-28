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

#include "config.h"
#include "log_buffer.h"

// Forward declare SemaphoreHandle_t to avoid including freertos/semphr.h
// (FreeRTOS include causes issues in native test stubs)
typedef void* SemaphoreHandle_t;

static constexpr size_t PA_LOG_SERIAL_LINE_MAX = 256;

void paLogInit();
void paLogLine(const char* line);
void paLogLineRaw(const char* line);
uint8_t configCurrentLogLevel();
SemaphoreHandle_t paGetSerialMutex();  // ADR 0034: serial output coordinator (console)

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
