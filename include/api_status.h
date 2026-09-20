// =============================================================================
// include/api_status.h
//
// Status and telemetry API endpoints, all ported to the project-owned
// WebRequest seam (ADR 0021) and bound by the seam route table.
// Also declares WiFi, health, and serial status state-capture and
// serialization helpers, shared with the Console module (ADR 0036).
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
// State-capture snapshots (ADR 0036)
//
// Each capture*Snapshot() function is the "Zone Snapshot capture" ADR 0036
// names: the read step behind a status query, factored out of the hand-written
// build*Json() gather blocks in src/web/api_status.cpp and src/web/web_server.cpp
// so the REST handler and the Console module (src/console/console_module.cpp)
// read RobotState/config exactly once, through one function, instead of two
// copies that can drift apart. The format*Json() functions below are NOT
// rewritten - they still take plain scalar arguments and still own the JSON
// shape; only the read step is shared (ADR 0036: "the proven JSON builders
// are not rewritten").
// =============================================================================

// GET /api/health's fields, verbatim (formatHealthJson's JSON keys).
//
// uptimeMs/resetReason (#225): read by the Survival Path - the serial
// Console, the one operator surface that answers when HTTP admission refuses
// everything (CONTEXT.md). They are ALSO served here over /api/health, which
// is an ordinary endpoint behind the ordinary admission floor and is shed
// before /api/status: webPathIsDiagnostic() (src/web/web_admission.cpp) names
// the paths that get the lower Diagnostic Floor, and /api/health is not one of
// them. Do not read "survival" as a property of this endpoint.
// Added additively, same key spellings /api/status already uses
// (src/web/web_server.cpp: "uptimeMs", "resetReason") so a serial transcript
// and the REST API name the same thing the same way (docs/console-protocol.md
// s.3.5). resetReason is a `const char*` to resetReasonName()'s static
// string literal (include/reset_reason.h) - never copied into a buffer, so
// this field costs no allocation and no extra storage.
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
    unsigned long uptimeMs;
    const char* resetReason;
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

// Where one Output has been told to be (#362): the width ServoTask put on its
// pin, where the move in progress ends, and whether there is a pulse on it at
// all. Both widths are COMMANDED -- nothing on this droid reads a servo back --
// so every surface labels them that way. GET /api/servo/outputs and the
// Console's servo.api.get-outputs both read a position through this one
// function, so the two cannot disagree about where an Output stands.
struct ServoOutputCommandedSnapshot {
    bool pulsing;         // false: no pulse on the pin, and the two widths are 0
    uint16_t nowUs;       // the width on the pin, part way through a move too
    uint16_t targetUs;    // where the move in progress ends; nowUs when none is
    uint8_t nudgesDone;   // Find by Moving nudges that have ended here since boot
                          // (ServoCommandedPosition::nudgesDone, #363); handed on
                          // whether or not the output is pulsing -- a count is a
                          // count, and 0 is the honest one for an output never nudged
    bool held;            // the calibration dial has this Output (#364, ADR 0064)
    ServoLimpReason limp; // why there is no pulse; read only while !pulsing
};

// Capture one Output's commanded position from ServoTask's mirror
// (RobotState::servoCommanded). An Output Address ServoTask does not drive --
// an expander's row, or an address that is not a servo -- answers pulsing
// false.
// thread-safe: yes (owns its own short critical section)
void captureServoOutputCommanded(ServoOutputDriver driver, uint8_t channel,
                                 ServoOutputCommandedSnapshot* out);

// The dynamic fields of the "dome" port object inside formatSerialJson()'s
// GET /api/serial response (below): "active", "heartbeatRx", "heartbeatTx".
// The other keys in that sub-object (label, name, hardwareRequired, note) are
// not state: name and note are the constants below, and label is read from the
// running board. The Console executor for dome.status.serial-link emits the
// three dynamic fields and none of the rest.
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

// The dome port's compile-time metadata, named here rather than buried in
// formatSerialJson()'s format string. Not part of dome.status.serial-link's
// registry fields (they are not state), and its Console executor emits only
// the dynamic ones.
//
// THE LABEL IS GONE ON PURPOSE (#348). It was "S3", which is the Artoo PCB's
// silkscreen and not a thing a FireBeetle 2 has; formatSerialJson() now reads
// what the running board prints from include/component_labels.inc. The note
// named that board's GPIO 33/34 as fact for the same reason and no longer
// does - where the signal is routed is the Board Lane, on GET /api/identity.
#define DOME_SERIAL_LINK_NAME "protoR2link"
#define DOME_SERIAL_LINK_NOTE "Body-dome serial transport"

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
//
// The three labels are the BOARD COMPONENT LABELS the running board prints
// beside those connectors, and they are parameters rather than literals
// because they differ per board: "S1"/"S2"/"S3" is the Artoo PCB's silkscreen
// and a FireBeetle 2 prints GPIO numbers there (ADR 0033, #339, #348). The
// caller reads them through boardComponentLabel() (include/board_outputs.h),
// which is the one place a board's printed legend lives; passing them in is
// what lets a host test ask for one board's legend from an image built for
// another. A board that declares none passes "" - never another board's.
// params: buf            - output buffer (must not be null)
//         bufSize        - size of buf in bytes
//         driveLabel     - what this board prints at the drive connector
//         soundLabel     - what this board prints at the sound connector
//         domeLabel      - what this board prints at the dome-link connector
//         domeLinkActive - true if dome heartbeat link is active
//         domeHbRx       - dome heartbeat receive counter
//         bodyHbTx       - body heartbeat transmit counter
// thread-safe: yes (pure function, no globals)
void formatSerialJson(char* buf, size_t bufSize, const char* driveLabel, const char* soundLabel,
                      const char* domeLabel, bool domeLinkActive, unsigned long domeHbRx,
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
//         uptimeMs          - milliseconds since boot (#225, same key /api/status uses)
//         resetReason       - resetReasonName()'s static string for the last reset (#225)
// thread-safe: yes (pure function, no globals)
void formatHealthJson(char* buf, size_t bufSize, bool estop, bool sbusSignalLost,
                      bool sbusHwFailsafe, bool webControlEnabled, bool wifiConnected,
                      bool wifiClientConnected, bool fsReady, unsigned long heapFree,
                      unsigned long heapMin, unsigned long heapLargestBlock, long wifiRssi,
                      unsigned long uptimeMs, const char* resetReason);

// Endpoint handlers
void handleWifiGet(WebRequest& req);

// GET /api/status. Ported ahead of the rest of its route group because the
// admission counters it carries are what the load harness and the migration
// scorecard read; without it the guard's evidence is unobservable.
void handleStatusGet(WebRequest& req);

void handleHealthGet(WebRequest& req);
void handleSerialGet(WebRequest& req);
