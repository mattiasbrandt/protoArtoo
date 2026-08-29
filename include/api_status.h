// =============================================================================
// include/api_status.h
//
// Status and telemetry API endpoints, all ported to the project-owned
// WebRequest seam (ADR 0021) and bound by the seam route table.
// Also declares WiFi, health, and serial status state-capture and
// serialization helpers, shared with the Console module (ADR 0034).
// =============================================================================
#pragma once

#include <stddef.h>
#include <stdint.h>

#include "config_store.h"  // WIFI_SSID_MAX_LEN, for the copy-out snapshot below
#include "web_request.h"

// WiFi connectivity status fields derived from WiFi modes and station counts.
struct WiFiConnectivityFields {
    bool wifiConnected;
    bool wifiClientConnected;
    long wifiRssi;
};

// =============================================================================
// State-capture snapshots (ADR 0034)
//
// Each capture*Snapshot() function is the "Zone Snapshot capture" ADR 0034
// names: the read step behind a status query, factored out of the hand-written
// build*Json() gather blocks in src/web/api_status.cpp and src/web/web_server.cpp
// so the REST handler and the Console module (src/console/console_module.cpp)
// read RobotState/config exactly once, through one function, instead of two
// copies that can drift apart. The format*Json() functions below are NOT
// rewritten - they still take plain scalar arguments and still own the JSON
// shape; only the read step is shared (ADR 0034: "the proven JSON builders
// are not rewritten").
// =============================================================================

// GET /api/health's fields, verbatim (formatHealthJson's JSON keys).
struct HealthSnapshot {
    bool estop;
    bool sbusSignalLost;
    bool sbusHwFailsafe;
    bool webControlEnabled;
    bool wifiConnected;
    bool wifiClientConnected;
    bool littleFsReady;
    unsigned long heapFree;
    unsigned long heapMin;
    unsigned long heapLargestBlock;
    long wifiRssi;
};

// Capture the health snapshot: estop/SBUS diagnostics under robotStateMux,
// WiFi connectivity through the network manager seam, heap through the
// Arduino/esp_heap_caps APIs (stubbed on native builds).
// thread-safe: yes (owns its own short critical section)
void captureHealthSnapshot(HealthSnapshot* out);

// GET /api/wifi's fields, verbatim (formatWifiJson's JSON keys).
struct WifiStatusSnapshot {
    char apSsid[WIFI_SSID_MAX_LEN + 1];
    char apIp[16];    // dotted-quad + NUL, matches WifiConnectivityStatus::apIp
    bool staEnabled;
    bool staConnected;
    char staIp[16];   // dotted-quad + NUL, matches WifiConnectivityStatus::staIp
    char staSsid[WIFI_SSID_MAX_LEN + 1];
    long wifiRssi;
    bool networkRecovery;
};

// Capture the WiFi status snapshot the same way buildWifiJson() (api_status.cpp)
// does: active WiFi config from the config cache, connectivity through the
// network manager seam.
// thread-safe: yes (no RobotState access; config cache is its own mutex)
void captureWifiStatusSnapshot(WifiStatusSnapshot* out);

// The two dynamic fields of dome.status.current, verbatim JSON keys from
// buildStatusJson() (src/web/web_server.cpp): "domeTargetSpeed" and
// "domeEnabled". buildStatusJson's other ~60 fields belong to aggregate-field
// registry rows (is_query: false, #212), not independently console-queryable;
// this snapshot exists only for the two fields the registry promotes to a
// real query (docs/action-registry.yaml: dome.status.current).
struct DomeStatusSnapshot {
    float domeTargetSpeed;
    bool domeEnabled;
};

// Capture the dome slice of the /api/status snapshot. Used by both
// buildStatusJson() (replacing its own inline reads of the same two fields)
// and the Console module, so the two can never disagree about what
// "domeTargetSpeed"/"domeEnabled" mean.
// thread-safe: yes (owns its own short critical section, independent of any
// caller's already-open one - see the call site comment in web_server.cpp for
// why a second short critical section is preferred over nesting)
void captureDomeStatusSnapshot(DomeStatusSnapshot* out);

// The dynamic fields of the "dome" port object inside formatSerialJson()'s
// GET /api/serial response (below): "active", "heartbeatRx", "heartbeatTx".
// The other keys in that sub-object (label, name, hardwareRequired, note) are
// compile-time string literals, not state, and are emitted by the Console
// executor directly from the named constants formatSerialJson() uses (below)
// rather than through this struct.
struct DomeSerialLinkSnapshot {
    bool active;
    unsigned long heartbeatRx;
    unsigned long heartbeatTx;
};

// Capture the dome serial link snapshot the same way buildSerialJson()
// (api_status.cpp) does: domeConnected() plus the heartbeat counters under
// robotStateMux.
// thread-safe: yes (owns its own short critical section)
void captureDomeSerialLinkSnapshot(DomeSerialLinkSnapshot* out);

// The dome port's compile-time metadata, factored out of formatSerialJson()'s
// format string so the Console executor for dome.status.serial-link can cite
// the identical literals instead of a second hand-typed copy. Not part of
// dome.status.serial-link's registry fields (they are not state).
#define DOME_SERIAL_LINK_LABEL "S3"
#define DOME_SERIAL_LINK_NAME "protoR2link"
#define DOME_SERIAL_LINK_NOTE "Body-dome serial transport over S3 (GPIO 33/34)"

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

// Endpoint handlers
void handleWifiGet(WebRequest& req);

// GET /api/status. Ported ahead of the rest of its route group because the
// admission counters it carries are what the load harness and the migration
// scorecard read; without it the guard's evidence is unobservable.
void handleStatusGet(WebRequest& req);

void handleHealthGet(WebRequest& req);
void handleSerialGet(WebRequest& req);
