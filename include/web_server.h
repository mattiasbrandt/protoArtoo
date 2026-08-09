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

// Network Recovery Mode local entry gesture (ADR 0015). See
// include/wifi_recovery_gesture.h for the pure decision rule. The count is
// persisted under NVS_NAMESPACE so it survives the reboot(s) the gesture
// itself requires; it is cleared once uptime confirms the boot was not part
// of a rapid power-cycle sequence.
extern const char* kWifiRecoveryCycleKey;
extern const uint32_t WIFI_RECOVERY_GESTURE_STABLE_MS;

// HTTP server bootstrap: start PsychicHttp, mDNS, and OTA. Called from
// handleWiFiEvent() in web_network_bootstrap.cpp when WiFi is ready.
// Must be called from the WiFi event callback path, never directly from setup().
void startHttpServerOnce();

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
// Bounded request-lifecycle trace (issue #54 evidence). See
// web_request_psychic.cpp for field semantics and the
// single-writer/bounded-overwrite contract.
#define PA_REQUEST_TRACE_MAX 32
struct RequestLifecycleEntry {
    char requestPath[28];    // request URL path, truncated (long-lived routes excluded,
                              // see web_request_psychic.cpp)
    uint32_t startMs;        // middleware admission time -- the earliest point our own
                              // code sees this request; a large gap between this and the
                              // browser's own request-start timestamp means the delay is
                              // below us (TCP accept/backlog), not in our handler (issue #67)
    uint32_t handlerDoneMs;  // 0 until next() returns
};
// Copies up to maxEntries oldest-first trace entries into out; returns the
// number copied.
size_t copyRequestLifecycleTrace(RequestLifecycleEntry* out, size_t maxEntries);
#endif
