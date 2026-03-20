// =============================================================================
// include/web_server.h
//
// =============================================================================
#pragma once

#include <Arduino.h>

#include <cstddef>
#include <cstdint>

#include "config.h"
#include "log_buffer.h"
#include "logging.h"

class AsyncWebServer;

bool buildStatusJson(char* buffer, size_t bufferSize);
size_t copyRecentLogs(char* buffer, size_t bufferSize);
uint32_t copyNewLogLinesSince(uint32_t lastSent, char out[][LOG_LINE_MAX], size_t maxLines,
                              size_t* linesCopied);
size_t getLogBufferCount();
bool copyLogLineAt(size_t idx, char* out, size_t outSize);
bool webLittleFsMounted();
void requestSystemRestart(uint32_t delayMs);
void webServerInit();
