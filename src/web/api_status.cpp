// =============================================================================
// src/web/api_status.cpp
//
// Status and telemetry API endpoints, all on the WebRequest seam (ADR 0021).
//   GET /api/status  — JSON status snapshot
//   GET /api/health  — health telemetry
//   GET /api/wifi    — WiFi status
//   GET /api/serial  — serial port status
// =============================================================================

#include "api_status.h"

#include <Arduino.h>
#include <WiFi.h>
#include <esp_heap_caps.h>

#include "config.h"
#include "config_cache.h"
#include "dome_link.h"
#include "dome_task.h"
#include "log_buffer.h"
#include "logging.h"
#include "robot_state.h"
#include "web_request.h"
#include "web_server.h"

static void buildWifiJson(char* buffer, size_t bufferSize) {
    wl_status_t staStatus = WiFi.status();
    bool staConnected = staStatus == WL_CONNECTED;
    bool staEnabled = WiFi.getMode() == WIFI_STA || WiFi.getMode() == WIFI_AP_STA;
    long wifiRssi = staConnected ? WiFi.RSSI() : 0;
    WifiConfig activeWifi = {};
    configCacheReadActiveWifi(&activeWifi);

    formatWifiJson(buffer, bufferSize, wifiStatusApSsid(activeWifi.ap_ssid),
                   WiFi.softAPIP().toString().c_str(), staEnabled, staConnected,
                   staConnected ? WiFi.localIP().toString().c_str() : "",
                   staConnected ? WiFi.SSID().c_str() : "", wifiRssi,
                   configCacheReadActiveWifiRecovery());
}

static void buildSerialJson(char* buffer, size_t bufferSize) {
    bool domeLinkActive = domeConnected();
    unsigned long hbRx;
    unsigned long hbTx;

    taskENTER_CRITICAL(&robotStateMux);
    hbRx = (unsigned long)robotState.domeHbRx;
    hbTx = (unsigned long)robotState.bodyHbTx;
    taskEXIT_CRITICAL(&robotStateMux);

    formatSerialJson(buffer, bufferSize, domeLinkActive, hbRx, hbTx);
}

static void buildHealthJson(char* buffer, size_t bufferSize) {
    FailsafeDiagnostics diag = {};
    bool webControlEnabled;
    bool wifiConnected;
    bool wifiClientConnected;
    bool fsReady;
    unsigned long heapFree;
    unsigned long heapMin;
    unsigned long heapLargestBlock;
    long wifiRssi;

    taskENTER_CRITICAL(&robotStateMux);
    copyFailsafeDiagnosticsLocked(&diag);
    webControlEnabled = robotState.webControlEnabled;
    taskEXIT_CRITICAL(&robotStateMux);

    int wifiMode = WiFi.getMode();
    bool apEnabled = wifiMode == WIFI_AP || wifiMode == WIFI_AP_STA;
    bool staConnected = WiFi.status() == WL_CONNECTED;
    unsigned int apStationCount = apEnabled ? (unsigned int)WiFi.softAPgetStationNum() : 0U;
    WiFiConnectivityFields wifi =
        deriveWiFiConnectivityFields(apEnabled, staConnected, apStationCount, WiFi.RSSI());
    wifiConnected = wifi.wifiConnected;
    wifiClientConnected = wifi.wifiClientConnected;
    wifiRssi = wifi.wifiRssi;

    fsReady = webLittleFsMounted();
    heapFree = ESP.getFreeHeap();
    heapMin = ESP.getMinFreeHeap();
    heapLargestBlock = (unsigned long)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
    formatHealthJson(buffer, bufferSize, diag.estop, diag.sbusSignalLost, diag.sbusHwFailsafe,
                     webControlEnabled, wifiConnected, wifiClientConnected, fsReady, heapFree, heapMin,
                     heapLargestBlock, wifiRssi);
}

// GET /api/wifi — active connection diagnostics, read by the WiFi page
// alongside the settings it saves through POST /api/wifi. Ported with the
// WiFi write route so that page works end to end on one stack.
void handleWifiGet(WebRequest& req) {
    // Worst case: apSsid/staSsid at WIFI_SSID_MAX_LEN (32), apIp/staIp at
    // 15 chars ("255.255.255.255"), plus fixed JSON literal overhead
    // (~130 bytes including the staSsid and networkRecovery fields). 256
    // bytes keeps headroom above the observed worst case.
    char body[256];
    buildWifiJson(body, sizeof(body));
    req.send(200, "application/json", body);
}

void handleStatusGet(WebRequest& req) {
    // Static, not stack: 3 KB on an 8 KB server task left too little headroom
    // for the snprintf float-formatting frames plus nested interrupt frames
    // under network load (stack-watchpoint panic proven by coredump). Both
    // backends dispatch handlers from a single task, so one shared buffer is
    // race-free -- same pattern as api_logs.cpp.
    static char body[3072];
    if (!buildStatusJson(body, sizeof(body))) {
        PA_LOG_WARN("StatusAPI", "status payload overflowed; returning fallback payload");
    }
    req.send(200, "application/json", body);
}

// GET /api/serial — per-port status for the setup page's serial panel. The
// payload is a fixed set of literal port descriptions with three values
// substituted, so it is bounded by the format string rather than by device
// state; 768 bytes covers it with headroom and fits a stack frame.
void handleSerialGet(WebRequest& req) {
    char body[768];
    buildSerialJson(body, sizeof(body));
    req.send(200, "application/json", body);
}

// GET /api/health — the small telemetry payload the shell polls. Every field is
// a bool or a fixed-width number, so 256 bytes is the whole of it.
void handleHealthGet(WebRequest& req) {
    char body[256];
    buildHealthJson(body, sizeof(body));
    req.send(200, "application/json", body);
}
