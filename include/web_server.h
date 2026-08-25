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

// ESP-IDF reset reason enum — native test builds need this for
// evaluateNetworkRecoveryGesture() to compile. Device builds include this
// from esp_system.h via Arduino.h.
#ifdef PA_NATIVE_TEST_STUBS
enum esp_reset_reason_t {
    ESP_RST_UNKNOWN   = 0,
    ESP_RST_POWERON   = 1,
    ESP_RST_EXT       = 2,
    ESP_RST_SW        = 3,
    ESP_RST_PANIC     = 4,
    ESP_RST_INT_WDT   = 5,
    ESP_RST_TASK_WDT  = 6,
    ESP_RST_WDT       = 7,
    ESP_RST_DEEPSLEEP = 8,
    ESP_RST_BROWNOUT  = 9,
    ESP_RST_SDIO      = 10,
};

esp_reset_reason_t esp_reset_reason();
#endif

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
// its allocation failed.
char* recentLogsBodyBuffer(size_t* size);
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
