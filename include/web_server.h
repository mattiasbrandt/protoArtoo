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
// of a rapid power-cycle sequence (see include/wifi_recovery_gesture.h for
// kWifiRecoveryCycleKey).
extern const uint32_t WIFI_RECOVERY_GESTURE_STABLE_MS;

// HTTP server bootstrap: start PsychicHttp, mDNS, and OTA. Called from
// handleWiFiEvent() in web_network_bootstrap.cpp when WiFi is ready.
// Must be called from the WiFi event callback path, never directly from setup().
void startHttpServerOnce();

bool buildStatusJson(char* buffer, size_t bufferSize);
void requestStatusBroadcastNow();
size_t copyRecentLogs(char* buffer, size_t bufferSize);
// Boot-allocated /api/logs response buffer (ring capacity * LOG_LINE_MAX + 1).
// Returns nullptr with *size = 0 until paLogRingApplyBootDepth() has run or if
// its allocation failed. Sized and synchronised for exactly one caller
// (handleLogsGet(), src/web/api_logs.cpp) - do not add a second caller from
// another task; see getLogBufferCount()/copyLogLineAt() below instead (#239).
char* recentLogsBodyBuffer(size_t* size);
uint32_t copyNewLogLinesSince(uint32_t lastSent, char out[][LOG_LINE_MAX], size_t maxLines,
                              size_t* linesCopied);
// getLogBufferCount()/copyLogLineAt(): read the log ring directly, one line
// at a time, each call independently locked (logMux, src/main.cpp) and
// released before returning - no shared buffer, no lock held across a
// multi-line read. This is what the Console's system.status.logs query is
// built on (src/console/console_module.cpp, #239) instead of borrowing
// recentLogsBodyBuffer()/copyRecentLogs() above.
size_t getLogBufferCount();
bool copyLogLineAt(size_t idx, char* out, size_t outSize);
bool webLittleFsMounted();
bool webOtaActive();
void requestSystemRestart(uint32_t delayMs);
void webServerInit();
// Returns true when at least one client is connected to the SSE event stream.
bool webServerHasSSEClients();
