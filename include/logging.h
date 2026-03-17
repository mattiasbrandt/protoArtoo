// =============================================================================
// include/logging.h
//
// Logging macros for protoArtoo — shared between tasks, drivers, and web layer.
// Centralizes log formatting to ensure consistent output across the system.
//
// Log level is checked at runtime against robotState.cfg_logLevel, which is
// NVS-backed and adjustable from the Setup page without a reboot. The
// compile-time PA_LOG_LEVEL build flag sets the boot default and the maximum
// ring buffer depth (see log_buffer.h), but does not gate output at compile
// time — all levels are always compiled in.
// =============================================================================
#pragma once

#include <Arduino.h>

#include "config.h"
#include "robot_state.h"

// robotState and robotStateMux are declared extern in robot_state.h (included above).
// No need to re-declare them here.

void paLogLine(const char* tag, const char* message);

// Inline helper — reads cfg_logLevel under a brief critical section.
// Used by every log macro to get the current runtime level without a full
// portMUX lock on every message (reading a single uint8_t is atomic on ESP32).
inline uint8_t paCurrentLogLevel() {
    return robotState.cfg_logLevel;
}

#define _PA_LOG_FORMAT(tag, fmt, ...)                                   \
    do {                                                                \
        char _pa_log_buf[192];                                          \
        snprintf(_pa_log_buf, sizeof(_pa_log_buf), fmt, ##__VA_ARGS__); \
        paLogLine(tag, _pa_log_buf);                                    \
    } while (0)

// Tag-based logging macros with runtime level check
#define PA_LOG_ERROR(tag, fmt, ...)                                         \
    do {                                                                    \
        if (paCurrentLogLevel() >= PA_LOG_LEVEL_ERROR) {                    \
            Serial.printf("[E][%s] " fmt "\n", tag, ##__VA_ARGS__);         \
            _PA_LOG_FORMAT(tag, fmt, ##__VA_ARGS__);                        \
        }                                                                   \
    } while (0)

#define PA_LOG_WARN(tag, fmt, ...)                                          \
    do {                                                                    \
        if (paCurrentLogLevel() >= PA_LOG_LEVEL_INFO) {                     \
            Serial.printf("[W][%s] " fmt "\n", tag, ##__VA_ARGS__);         \
            _PA_LOG_FORMAT(tag, fmt, ##__VA_ARGS__);                        \
        }                                                                   \
    } while (0)

#define PA_LOG_INFO(tag, fmt, ...)                                          \
    do {                                                                    \
        if (paCurrentLogLevel() >= PA_LOG_LEVEL_INFO) {                     \
            Serial.printf("[I][%s] " fmt "\n", tag, ##__VA_ARGS__);         \
            _PA_LOG_FORMAT(tag, fmt, ##__VA_ARGS__);                        \
        }                                                                   \
    } while (0)

#define PA_LOG_DEBUG(tag, fmt, ...)                                         \
    do {                                                                    \
        if (paCurrentLogLevel() >= PA_LOG_LEVEL_DEBUG) {                    \
            Serial.printf("[D][%s] " fmt "\n", tag, ##__VA_ARGS__);         \
            _PA_LOG_FORMAT(tag, fmt, ##__VA_ARGS__);                        \
        }                                                                   \
    } while (0)
