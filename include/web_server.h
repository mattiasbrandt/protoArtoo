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
void requestStatusBroadcastNow();
size_t copyRecentLogs(char* buffer, size_t bufferSize);
uint32_t copyNewLogLinesSince(uint32_t lastSent, char out[][LOG_LINE_MAX], size_t maxLines,
                              size_t* linesCopied);
size_t getLogBufferCount();
bool copyLogLineAt(size_t idx, char* out, size_t outSize);
bool webLittleFsMounted();
bool webOtaActive();
void requestSystemRestart(uint32_t delayMs);
void webServerInit();
// Returns true when at least one client is connected to the SSE event stream.
bool webServerHasSSEClients();

#if PA_HEAP_PROFILE
// Bounded request-lifecycle trace (issue #54 evidence). See web_server.cpp
// for field semantics and the single-writer/bounded-overwrite contract.
#define PA_REQUEST_TRACE_MAX 32
struct RequestLifecycleEntry {
    char requestClass[12];   // "diag", "api", or "static" (SSE excluded, see web_server.cpp)
    uint32_t startMs;        // middleware admission time
    uint32_t handlerDoneMs;  // 0 until next() returns
    uint32_t disconnectMs;   // 0 until onDisconnect fires
};
// Copies up to maxEntries oldest-first trace entries into out; returns the
// number copied.
size_t copyRequestLifecycleTrace(RequestLifecycleEntry* out, size_t maxEntries);
#endif
