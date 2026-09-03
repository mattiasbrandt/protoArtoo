// =============================================================================
// src/web/api_status.cpp
//
// Status and telemetry API endpoints, all on the WebRequest seam (ADR 0021).
//   GET /api/status  - JSON status snapshot
//   GET /api/health  - health telemetry
//   GET /api/wifi    - WiFi status
//   GET /api/serial  - serial port status
// =============================================================================

#include "api_status.h"

#include "config.h"
#include "dome_task.h"
#include "log_buffer.h"
#include "logging.h"
#include "web_request.h"
#include "web_server.h"

// The gather step for each of these three lives in captureWifiStatusSnapshot/
// captureHealthSnapshot/captureDomeSerialLinkSnapshot (api_status_serializers.cpp)
// rather than here, so the Console module's status executors
// (src/console/console_module.cpp) read the exact same state through the
// exact same function instead of a second, driftable copy (ADR 0034).
static void buildWifiJson(char* buffer, size_t bufferSize) {
    WifiStatusSnapshot snap = {};
    captureWifiStatusSnapshot(&snap);
    formatWifiJson(buffer, bufferSize, snap.apSsid, snap.apIp, snap.staEnabled, snap.staConnected,
                   snap.staIp, snap.staSsid, snap.wifiRssi, snap.networkRecovery);
}

static void buildSerialJson(char* buffer, size_t bufferSize) {
    DomeSerialLinkSnapshot snap = {};
    captureDomeSerialLinkSnapshot(&snap);
    formatSerialJson(buffer, bufferSize, snap.active, snap.heartbeatRx, snap.heartbeatTx);
}

static void buildHealthJson(char* buffer, size_t bufferSize) {
    HealthSnapshot snap = {};
    captureHealthSnapshot(&snap);
    formatHealthJson(buffer, bufferSize, snap.estop, snap.sbusSignalLost, snap.sbusHwFailsafe,
                     snap.webControlEnabled, snap.wifiConnected, snap.wifiClientConnected,
                     snap.littleFsReady, snap.heapFree, snap.heapMin, snap.heapLargestBlock,
                     snap.wifiRssi, snap.uptimeMs, snap.resetReason);
}

// GET /api/wifi - active connection diagnostics, read by the WiFi page
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

// GET /api/serial - per-port status for the setup page's serial panel. The
// payload is a fixed set of literal port descriptions with three values
// substituted, so it is bounded by the format string rather than by device
// state; 768 bytes covers it with headroom and fits a stack frame.
void handleSerialGet(WebRequest& req) {
    char body[768];
    buildSerialJson(body, sizeof(body));
    req.send(200, "application/json", body);
}

// GET /api/health - the small telemetry payload the shell polls, and (#225)
// the survival set the Console's system.status.health answers below the HTTP
// admission floor. Most fields are a bool or a fixed-width number, but
// resetReason (#225) is a variable-length string - resetReasonName()'s
// longest literal is "DEEPSLEEP" (9 chars) - so this is no longer the fixed
// upper bound the prior comment claimed. Worst case (every numeric field
// maxed, "DEEPSLEEP") is 303 bytes; 384 keeps headroom above that.
void handleHealthGet(WebRequest& req) {
    char body[384];
    buildHealthJson(body, sizeof(body));
    req.send(200, "application/json", body);
}
