// =============================================================================
// include/api_status.h
//
// Status and telemetry API endpoints, all ported to the project-owned
// WebRequest seam (ADR 0021) and bound by the seam route table.
// Also declares WiFi, health, and serial status serialization helpers.
// =============================================================================
#pragma once

#include <stddef.h>
#include <stdint.h>

#include "web_request.h"
#include "wifi_module_update_support.h"

// WiFi connectivity status fields derived from WiFi modes and station counts.
struct WiFiConnectivityFields {
    bool wifiConnected;
    bool wifiClientConnected;
    long wifiRssi;
};

// Compute canonical WiFi status booleans used in JSON status/health payloads.
// Pure function - no globals, no Arduino, no FreeRTOS.
// params: apEnabled       - true when AP mode is active (AP or AP+STA)
//         staConnected    - true when STA is connected (`WL_CONNECTED`)
//         apStationCount  - number of stations currently attached to soft AP
//         staRssi         - RSSI to upstream AP in dBm (valid when staConnected)
// returns: derived wifiConnected / wifiClientConnected flags + wifiRssi
// thread-safe: yes (pure function, no globals)
WiFiConnectivityFields deriveWiFiConnectivityFields(bool apEnabled, bool staConnected,
                                                    unsigned int apStationCount, long staRssi);

// Write a JSON WiFi status object into a caller-supplied buffer.
// Pure function - no globals, no Arduino, no FreeRTOS.
// params: buf          - output buffer (must not be null)
//         bufSize      - size of buf in bytes
//         apSsid       - AP SSID string (must not be null)
//         apIp         - AP IP address string (must not be null)
//         staEnabled   - true if STA mode is active
//         staConnected - true if STA is connected to upstream AP
//         staIp        - STA IP address string (empty string if not connected)
//         wifiRssi     - WiFi signal strength in dBm (0 if not connected)
//         networkRecovery - true if Network Recovery Mode (ADR 0015) is the
//                           posture actually active this boot
// thread-safe: yes (pure function, no globals)
void formatWifiJson(char* buf, size_t bufSize, const char* apSsid, const char* apIp,
                    bool staEnabled, bool staConnected, const char* staIp, const char* staSsid,
                    long wifiRssi, bool networkRecovery);

// Select the AP SSID that diagnostics should report. Active saved Standalone AP
// settings own the operator-facing AP name; fallback protects startup/default
// paths that have not captured an active AP SSID yet.
const char* wifiStatusApSsid(const char* activeApSsid);

// Write a JSON serial-port status object into a caller-supplied buffer.
// Pure function - no globals, no Arduino, no FreeRTOS.
// params: buf            - output buffer (must not be null)
//         bufSize        - size of buf in bytes
//         domeLinkActive - true if dome heartbeat link is active
//         domeHbRx       - dome heartbeat receive counter
//         bodyHbTx       - body heartbeat transmit counter
// thread-safe: yes (pure function, no globals)
void formatSerialJson(char* buf, size_t bufSize, bool domeLinkActive, unsigned long domeHbRx,
                      unsigned long bodyHbTx);

// Write a JSON health/diagnostics object into a caller-supplied buffer.
// Pure function - no globals, no Arduino, no FreeRTOS.
// params: buf               - output buffer (must not be null)
//         bufSize           - size of buf in bytes
//         estop             - current estop state
//         sbusSignalLost    - true if SBUS signal is lost
//         sbusHwFailsafe    - true if SBUS hardware failsafe is active
//         webControlEnabled - true if web drive control is enabled
//         wifiConnected     - true if control-surface WiFi is available (AP active or STA
//         connected) wifiClientConnected - true if at least one station is attached to soft AP
//         fsReady           - true if LittleFS is mounted
//         heapFree          - current free heap in bytes
//         heapMin           - minimum free heap since boot in bytes
//         heapLargestBlock  - largest contiguous free heap block in bytes
//         wifiRssi          - STA RSSI in dBm (0 when STA disconnected)
// thread-safe: yes (pure function, no globals)
void formatHealthJson(char* buf, size_t bufSize, bool estop, bool sbusSignalLost,
                      bool sbusHwFailsafe, bool webControlEnabled, bool wifiConnected,
                      bool wifiClientConnected, bool fsReady, unsigned long heapFree,
                      unsigned long heapMin, unsigned long heapLargestBlock, long wifiRssi);

// Write a JSON WiFi Module object for GET /api/status.
// Pure function - no globals, no Arduino, no FreeRTOS.
// `version` is omitted when result.versionPresent is false (never null, never
// synthesized as "0.0.0" for an unread version). hostVersion is always
// present. Typical object is ~50-75 bytes; worst-case uint32 versions fit
// in 160 bytes. The /api/status body is a 3072-byte static.
void formatWifiModuleJson(char* buf, size_t bufSize,
                          const WifiModuleUpdateSupportResult& result, uint32_t hostMajor,
                          uint32_t hostMinor, uint32_t hostPatch);

// Endpoint handlers
void handleWifiGet(WebRequest& req);

// GET /api/status. Ported ahead of the rest of its route group because the
// admission counters it carries are what the load harness and the migration
// scorecard read; without it the guard's evidence is unobservable.
void handleStatusGet(WebRequest& req);

void handleHealthGet(WebRequest& req);
void handleSerialGet(WebRequest& req);
