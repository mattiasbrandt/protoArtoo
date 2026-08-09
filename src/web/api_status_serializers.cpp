// =============================================================================
// src/web/api_status_serializers.cpp
//
// Pure JSON serialization helpers for WiFi and health status.
// No Arduino, no FreeRTOS, no hardware dependencies - testable in native env.
// =============================================================================

#include "api_status.h"

#include <cstdio>
#include <cstring>

#include "config.h"

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
    snprintf(buf, bufSize,
             "{"
             "\"debug\":{\"label\":\"S0\",\"name\":\"ESP debug\",\"active\":true,\"note\":\"USB "
             "debug serial\"},"
             "\"hoverboard\":{\"label\":\"S1\",\"name\":\"Hoverboard\",\"active\":true,"
             "\"hardwareRequired\":true,\"note\":\"Firmware path active; full behavior needs Artoo "
             "PCB + hoverboard chain\"},"
             "\"sound\":{\"label\":\"S2\",\"name\":\"Sound\",\"active\":false,\"hardwareRequired\":"
             "true,\"note\":\"Requires S2 wiring and a supported sound module\"},"
             "\"dome\":{\"label\":\"S3\",\"name\":\"protoR2link\",\"active\":%s,\"heartbeatRx\":%"
             "lu,\"heartbeatTx\":%lu,\"hardwareRequired\":"
             "true,\"note\":\"Body-dome serial transport over S3 (GPIO 33/34)\"}"
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
