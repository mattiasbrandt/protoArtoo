// =============================================================================
// include/web_server.h
//
// =============================================================================
#pragma once

#include <Arduino.h>

#include <cstddef>
#include <cstdint>

#include "config.h"

#define PA_LOG_ERROR(tag, fmt, ...)                                         \
    do {                                                                    \
        if (PA_LOG_LEVEL >= PA_LOG_LEVEL_ERROR) {                           \
            char _pa_log_buf[192];                                          \
            snprintf(_pa_log_buf, sizeof(_pa_log_buf), fmt, ##__VA_ARGS__); \
            paLogLine(tag, _pa_log_buf);                                    \
        }                                                                   \
    } while (0)

#define PA_LOG_INFO(tag, fmt, ...)                                          \
    do {                                                                    \
        if (PA_LOG_LEVEL >= PA_LOG_LEVEL_INFO) {                            \
            char _pa_log_buf[192];                                          \
            snprintf(_pa_log_buf, sizeof(_pa_log_buf), fmt, ##__VA_ARGS__); \
            paLogLine(tag, _pa_log_buf);                                    \
        }                                                                   \
    } while (0)

#define PA_LOG_DEBUG(tag, fmt, ...)                                         \
    do {                                                                    \
        if (PA_LOG_LEVEL >= PA_LOG_LEVEL_DEBUG) {                           \
            char _pa_log_buf[192];                                          \
            snprintf(_pa_log_buf, sizeof(_pa_log_buf), fmt, ##__VA_ARGS__); \
            paLogLine(tag, _pa_log_buf);                                    \
        }                                                                   \
    } while (0)

class AsyncWebServer;

void buildStatusJson(char* buffer, size_t bufferSize);
void paLogLine(const char* tag, const char* message);
size_t copyRecentLogs(char* buffer, size_t bufferSize);
bool webLittleFsMounted();
void requestSystemRestart(uint32_t delayMs);
void webServerInit();
void webRegisterRoutes(AsyncWebServer& server);
