// =============================================================================
// include/logging.h
//
// Logging macros for protoArtoo — shared between tasks, drivers, and web layer.
// Centralizes log formatting to ensure consistent output across the system.
// =============================================================================
#pragma once

#include <Arduino.h>

#include "config.h"

void paLogLine(const char* tag, const char* message);

#define _PA_LOG_FORMAT(level, tag, fmt, ...)                            \
    do {                                                                \
        char _pa_log_buf[192];                                          \
        snprintf(_pa_log_buf, sizeof(_pa_log_buf), fmt, ##__VA_ARGS__); \
        paLogLine(tag, _pa_log_buf);                                    \
    } while (0)

// Tag-based logging macros with consistent formatting
#define PA_LOG_ERROR(tag, fmt, ...)                                 \
    do {                                                            \
        if (PA_LOG_LEVEL >= PA_LOG_LEVEL_ERROR) {                   \
            Serial.printf("[E][%s] " fmt "\n", tag, ##__VA_ARGS__); \
            _PA_LOG_FORMAT("E", tag, fmt, ##__VA_ARGS__);           \
        }                                                           \
    } while (0)

#define PA_LOG_WARN(tag, fmt, ...)                                  \
    do {                                                            \
        if (PA_LOG_LEVEL >= PA_LOG_LEVEL_INFO) {                    \
            Serial.printf("[W][%s] " fmt "\n", tag, ##__VA_ARGS__); \
            _PA_LOG_FORMAT("W", tag, fmt, ##__VA_ARGS__);           \
        }                                                           \
    } while (0)

#define PA_LOG_INFO(tag, fmt, ...)                                  \
    do {                                                            \
        if (PA_LOG_LEVEL >= PA_LOG_LEVEL_INFO) {                    \
            Serial.printf("[I][%s] " fmt "\n", tag, ##__VA_ARGS__); \
            _PA_LOG_FORMAT("I", tag, fmt, ##__VA_ARGS__);           \
        }                                                           \
    } while (0)

#define PA_LOG_DEBUG(tag, fmt, ...)                                 \
    do {                                                            \
        if (PA_LOG_LEVEL >= PA_LOG_LEVEL_DEBUG) {                   \
            Serial.printf("[D][%s] " fmt "\n", tag, ##__VA_ARGS__); \
            _PA_LOG_FORMAT("D", tag, fmt, ##__VA_ARGS__);           \
        }                                                           \
    } while (0)
