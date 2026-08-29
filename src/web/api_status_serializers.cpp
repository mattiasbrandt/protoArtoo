// =============================================================================
// src/web/api_status_serializers.cpp
//
// JSON serialization helpers for WiFi, health and serial-link status, plus
// the state-capture ("Zone Snapshot", ADR 0034) functions behind them.
//
// The format*Json() functions are pure - no globals, no Arduino, no FreeRTOS -
// and unchanged in shape: they still take plain scalars and own the JSON
// layout. The capture*Snapshot() functions below them DO read RobotState (via
// robotStateMux) and the config cache; they exist so src/web/api_status.cpp's
// REST handlers and src/console/console_module.cpp's Console executors read
// state through the exact same function instead of two copies that can drift
// apart. This file is in the native test build filter (platformio.ini), so
// both the format and the capture halves are host-testable.
// =============================================================================

#include "api_status.h"

#include <cstdio>
#include <cstring>

#ifdef ARDUINO
#include <Arduino.h>
#include <esp_heap_caps.h>
#endif

#include "config.h"
#include "config_cache.h"
#include "dome_link.h"
#include "robot_state.h"
#include "web_network_manager.h"
#include "web_server.h"

WiFiConnectivityFields deriveWiFiConnectivityFields(bool apEnabled, bool staConnected,
                                                    unsigned int apStationCount, long staRssi) {
    WiFiConnectivityFields fields{};
    fields.wifiConnected = apEnabled || staConnected;
    fields.wifiClientConnected = apEnabled && apStationCount > 0U;
    fields.wifiRssi = staConnected ? staRssi : 0;
    return fields;
}

void formatWifiJson(char* buf, size_t bufSize, const char* apSsid, const char* apIp,
                    bool staEnabled, bool staConnected, const char* staIp, const char* staSsid,
                    long wifiRssi, bool networkRecovery) {
    snprintf(buf, bufSize,
             "{\"apSsid\":\"%s\",\"apIp\":\"%s\",\"staEnabled\":%s,\"staConnected\":%s,\"staIp\":"
             "\"%s\",\"staSsid\":\"%s\",\"wifiRssi\":%ld,\"networkRecovery\":%s}",
             apSsid, apIp, staEnabled ? "true" : "false", staConnected ? "true" : "false", staIp,
             staSsid, wifiRssi, networkRecovery ? "true" : "false");
}

const char* wifiStatusApSsid(const char* activeApSsid) {
    if (activeApSsid != nullptr && activeApSsid[0] != '\0') {
        return activeApSsid;
    }
    return WIFI_AP_SSID;
}

void formatSerialJson(char* buf, size_t bufSize, bool domeLinkActive, unsigned long domeHbRx,
                      unsigned long bodyHbTx) {
    // The dome port's label/name/note literals are shared with
    // captureDomeSerialLinkSnapshot's caller in console_module.cpp via the
    // DOME_SERIAL_LINK_* constants (api_status.h) instead of being hand-typed
    // twice - dome.status.serial-link's Console executor cites the same
    // literals rather than a second copy that could drift from this one.
    snprintf(buf, bufSize,
             "{"
             "\"debug\":{\"label\":\"S0\",\"name\":\"ESP debug\",\"active\":true,\"note\":\"USB "
             "debug serial\"},"
             "\"hoverboard\":{\"label\":\"S1\",\"name\":\"Hoverboard\",\"active\":true,"
             "\"hardwareRequired\":true,\"note\":\"Firmware path active; full behavior needs Artoo "
             "PCB + hoverboard chain\"},"
             "\"sound\":{\"label\":\"S2\",\"name\":\"Sound\",\"active\":false,\"hardwareRequired\":"
             "true,\"note\":\"Requires S2 wiring and a supported sound module\"},"
             "\"dome\":{\"label\":\"" DOME_SERIAL_LINK_LABEL "\",\"name\":\"" DOME_SERIAL_LINK_NAME
             "\",\"active\":%s,\"heartbeatRx\":%"
             "lu,\"heartbeatTx\":%lu,\"hardwareRequired\":"
             "true,\"note\":\"" DOME_SERIAL_LINK_NOTE "\"}"
             "}",
             domeLinkActive ? "true" : "false", domeHbRx, bodyHbTx);
}

void formatHealthJson(char* buf, size_t bufSize, bool estop, bool sbusSignalLost,
                      bool sbusHwFailsafe, bool webControlEnabled, bool wifiConnected,
                      bool wifiClientConnected, bool fsReady, unsigned long heapFree,
                      unsigned long heapMin, unsigned long heapLargestBlock, long wifiRssi) {
    snprintf(buf, bufSize,
             "{\"estop\":%s,\"sbusSignalLost\":%s,\"sbusHwFailsafe\":%s,\"webControlEnabled\":%s,"
             "\"wifiConnected\":%s,\"wifiClientConnected\":%s,\"littleFsReady\":%s,"
             "\"heapFree\":%lu,\"heapMin\":%lu,\"heapLargestBlock\":%lu,\"wifiRssi\":%ld}",
             estop ? "true" : "false", sbusSignalLost ? "true" : "false",
             sbusHwFailsafe ? "true" : "false", webControlEnabled ? "true" : "false",
             wifiConnected ? "true" : "false", wifiClientConnected ? "true" : "false",
             fsReady ? "true" : "false", heapFree, heapMin, heapLargestBlock, wifiRssi);
}

// =============================================================================
// State-capture ("Zone Snapshot", ADR 0034) functions
// =============================================================================

void captureHealthSnapshot(HealthSnapshot* out) {
    if (out == nullptr) {
        return;
    }
    *out = HealthSnapshot{};

    FailsafeDiagnostics diag = {};
    taskENTER_CRITICAL(&robotStateMux);
    copyFailsafeDiagnosticsLocked(&diag);
    out->webControlEnabled = robotState.webControlEnabled;
    taskEXIT_CRITICAL(&robotStateMux);
    out->estop = diag.estop;
    out->sbusSignalLost = diag.sbusSignalLost;
    out->sbusHwFailsafe = diag.sbusHwFailsafe;

    WifiConnectivityStatus connectivity = networkManagerQueryConnectivity();
    out->wifiConnected = connectivity.wifiConnected;
    out->wifiClientConnected = connectivity.wifiClientConnected;
    out->wifiRssi = connectivity.wifiRssi;

    out->littleFsReady = webLittleFsMounted();

#ifdef ARDUINO
    out->heapFree = ESP.getFreeHeap();
    out->heapMin = ESP.getMinFreeHeap();
    out->heapLargestBlock = (unsigned long)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
#else
    // Native tests: no standard-C equivalent of the ESP heap APIs. Matches
    // the stub values consoleExecuteSystemStatusHealth used before this
    // snapshot existed, so the native tests built against it stay stable.
    out->heapFree = 262144;
    out->heapMin = 262144;
    out->heapLargestBlock = 262144;
#endif
}

void captureWifiStatusSnapshot(WifiStatusSnapshot* out) {
    if (out == nullptr) {
        return;
    }
    *out = WifiStatusSnapshot{};

    WifiConnectivityStatus connectivity = networkManagerQueryConnectivity();
    WifiConfig activeWifi = {};
    configCacheReadActiveWifi(&activeWifi);

    snprintf(out->apSsid, sizeof(out->apSsid), "%s", wifiStatusApSsid(activeWifi.ap_ssid));
    snprintf(out->apIp, sizeof(out->apIp), "%s", connectivity.apIp);
    out->staEnabled = connectivity.staEnabled;
    out->staConnected = connectivity.staConnected;
    snprintf(out->staIp, sizeof(out->staIp), "%s", connectivity.staIp);
    snprintf(out->staSsid, sizeof(out->staSsid), "%s", connectivity.staSsid);
    out->wifiRssi = connectivity.wifiRssi;
    out->networkRecovery = configCacheReadActiveWifiRecovery();
}

void captureDomeStatusSnapshot(DomeStatusSnapshot* out) {
    if (out == nullptr) {
        return;
    }
    *out = DomeStatusSnapshot{};

    ConfigSnapshot cfg = {};
    configCacheRead(&cfg);
    taskENTER_CRITICAL(&robotStateMux);
    out->domeTargetSpeed = robotState.domeTargetSpeed;
    taskEXIT_CRITICAL(&robotStateMux);
    out->domeEnabled = cfg.system.enable_dome_esc;
}

void captureDomeSerialLinkSnapshot(DomeSerialLinkSnapshot* out) {
    if (out == nullptr) {
        return;
    }
    *out = DomeSerialLinkSnapshot{};

    out->active = domeConnected();
    taskENTER_CRITICAL(&robotStateMux);
    out->heartbeatRx = (unsigned long)robotState.domeHbRx;
    out->heartbeatTx = (unsigned long)robotState.bodyHbTx;
    taskEXIT_CRITICAL(&robotStateMux);
}
