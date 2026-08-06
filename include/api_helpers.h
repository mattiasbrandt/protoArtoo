// =============================================================================
// include/api_helpers.h
//
// Pure parsing helpers for web API parameter validation.
// No Arduino, no FreeRTOS, no hardware dependencies — testable in native env.
//
// All functions take null-terminated C strings and write results via out-params.
// Returns true on success, false on parse failure or out-of-range input.
// =============================================================================
#pragma once

#include <stddef.h>
#include <stdint.h>

#include "drive_speed_preset.h"
// -----------------------------------------------------------------------------
// trimAsciiWhitespace()
// Strip leading and trailing spaces, tabs, CR and LF from a null-terminated
// string, in place. Handlers that used to receive an Arduino String and call
// String::trim() need this once they take copied-out C strings across the
// WebRequest seam (ADR 0021), and more than one of them does -- a name or
// token that only differs by a stray space has to keep reaching validation as
// the same value it did before.
// params: s — null-terminated string, modified in place (must not be null)
// thread-safe: yes (pure function, no globals)
// -----------------------------------------------------------------------------
void trimAsciiWhitespace(char* s);

// -----------------------------------------------------------------------------
// parseDriveValue()
// Parse a null-terminated decimal integer string into an int16_t.
// Accepts negative values. Rejects empty strings, non-numeric input, and
// strings with trailing non-numeric characters.
// params: raw — null-terminated input string (must not be null)
//         out — receives parsed value on success
// returns: true on success, false on any parse error
// thread-safe: yes (pure function, no globals)
// -----------------------------------------------------------------------------
bool parseDriveValue(const char* raw, int16_t* out);

// -----------------------------------------------------------------------------
// parseUint32Value()
// Parse a null-terminated decimal unsigned integer string into a uint32_t.
// Rejects empty strings, negative values, non-numeric input, and strings with
// trailing non-numeric characters.
// params: raw — null-terminated input string (must not be null)
//         out — receives parsed value on success
// returns: true on success, false on any parse error
// thread-safe: yes (pure function, no globals)
// -----------------------------------------------------------------------------
bool parseUint32Value(const char* raw, uint32_t* out);

// -----------------------------------------------------------------------------
// parseBoolValue()
// Parse a null-terminated string into a bool.
// Accepts: "true", "1" → true; "false", "0" → false.
// All other values return false (parse failure).
// params: raw — null-terminated input string (must not be null)
//         out — receives parsed value on success
// returns: true on success, false on unrecognised input
// thread-safe: yes (pure function, no globals)
// -----------------------------------------------------------------------------
bool parseBoolValue(const char* raw, bool* out);

// Validate and copy an operator-facing droid name into the persisted form.
// Names may contain lowercase a-z, 0-9, and '-'. Uppercase, whitespace, and
// other punctuation are rejected.
// Returns false if the result is empty or does not fit in out.
bool normalizeDroidName(const char* raw, char* out, size_t outSize);

// -----------------------------------------------------------------------------
// formatConfigJson()
// Write a JSON config object into a caller-supplied buffer.
// Pure function — no globals, no Arduino, no FreeRTOS.
// params: buf               — output buffer (must not be null)
//         bufSize           — size of buf in bytes
//         speedLimitMax     — current speed limit cap
//         webDriveTimeoutMs — current web drive timeout in ms
// thread-safe: yes (pure function, no globals)
// -----------------------------------------------------------------------------
void formatConfigJson(char* buf, size_t bufSize, int16_t speedLimitMax, uint32_t webDriveTimeoutMs);

// -----------------------------------------------------------------------------
// formatWifiJson()
// Write a JSON WiFi status object into a caller-supplied buffer.
// Pure function — no globals, no Arduino, no FreeRTOS.
// params: buf          — output buffer (must not be null)
//         bufSize      — size of buf in bytes
//         apSsid       — AP SSID string (must not be null)
//         apIp         — AP IP address string (must not be null)
//         staEnabled   — true if STA mode is active
//         staConnected — true if STA is connected to upstream AP
//         staIp        — STA IP address string (empty string if not connected)
//         wifiRssi     — WiFi signal strength in dBm (0 if not connected)
//         networkRecovery — true if Network Recovery Mode (ADR 0015) is the
//                           posture actually active this boot
// thread-safe: yes (pure function, no globals)
// -----------------------------------------------------------------------------
void formatWifiJson(char* buf, size_t bufSize, const char* apSsid, const char* apIp,
                    bool staEnabled, bool staConnected, const char* staIp, const char* staSsid,
                    long wifiRssi, bool networkRecovery);

// Select the AP SSID that diagnostics should report. Active saved Standalone AP
// settings own the operator-facing AP name; fallback protects startup/default
// paths that have not captured an active AP SSID yet.
const char* wifiStatusApSsid(const char* activeApSsid);

// -----------------------------------------------------------------------------
// formatSerialJson()
// Write a JSON serial-port status object into a caller-supplied buffer.
// Pure function — no globals, no Arduino, no FreeRTOS.
// params: buf            — output buffer (must not be null)
//         bufSize        — size of buf in bytes
//         domeLinkActive — true if dome heartbeat link is active
//         domeHbRx       — dome heartbeat receive counter
//         bodyHbTx       — body heartbeat transmit counter
// thread-safe: yes (pure function, no globals)
// -----------------------------------------------------------------------------
void formatSerialJson(char* buf, size_t bufSize, bool domeLinkActive, unsigned long domeHbRx,
                      unsigned long bodyHbTx);

// -----------------------------------------------------------------------------
// WiFiConnectivityFields
// Derived connectivity fields shared by /api/health and /api/status.
// `wifiConnected` answers "is WiFi service reachable from operators" while
// `wifiClientConnected` answers "is an external client currently attached to AP".
// -----------------------------------------------------------------------------
struct WiFiConnectivityFields {
    bool wifiConnected;
    bool wifiClientConnected;
    long wifiRssi;
};

// -----------------------------------------------------------------------------
// deriveWiFiConnectivityFields()
// Compute canonical WiFi status booleans used in JSON status/health payloads.
// Pure function — no globals, no Arduino, no FreeRTOS.
// params: apEnabled       — true when AP mode is active (AP or AP+STA)
//         staConnected    — true when STA is connected (`WL_CONNECTED`)
//         apStationCount  — number of stations currently attached to soft AP
//         staRssi         — RSSI to upstream AP in dBm (valid when staConnected)
// returns: derived wifiConnected / wifiClientConnected flags + wifiRssi
// thread-safe: yes (pure function, no globals)
// -----------------------------------------------------------------------------
WiFiConnectivityFields deriveWiFiConnectivityFields(bool apEnabled, bool staConnected,
                                                    unsigned int apStationCount, long staRssi);

// -----------------------------------------------------------------------------
// formatHealthJson()
// Write a JSON health/diagnostics object into a caller-supplied buffer.
// Pure function — no globals, no Arduino, no FreeRTOS.
// params: buf               — output buffer (must not be null)
//         bufSize           — size of buf in bytes
//         estop             — current estop state
//         sbusSignalLost    — true if SBUS signal is lost
//         sbusHwFailsafe    — true if SBUS hardware failsafe is active
//         webControlEnabled — true if web drive control is enabled
//         wifiConnected     — true if control-surface WiFi is available (AP active or STA
//         connected) wifiClientConnected — true if at least one station is attached to soft AP
//         fsReady           — true if LittleFS is mounted
//         heapFree          — current free heap in bytes
//         heapMin           — minimum free heap since boot in bytes
//         heapLargestBlock  — largest contiguous free heap block in bytes
//         wifiRssi          — STA RSSI in dBm (0 when STA disconnected)
// thread-safe: yes (pure function, no globals)
// -----------------------------------------------------------------------------
void formatHealthJson(char* buf, size_t bufSize, bool estop, bool sbusSignalLost,
                      bool sbusHwFailsafe, bool webControlEnabled, bool wifiConnected,
                      bool wifiClientConnected, bool fsReady, unsigned long heapFree,
                      unsigned long heapMin, unsigned long heapLargestBlock, long wifiRssi);

// -----------------------------------------------------------------------------
// formatAudioStatusJson()
// Write a JSON audio-module status object into a caller-supplied buffer.
// Pure function — no globals, no Arduino, no FreeRTOS.
// params: buf          — output buffer (must not be null)
//         bufSize      — size of buf in bytes (256 bytes sufficient with RX diagnostics)
//         driverName   — driver name string e.g. "DY-SV5W" (must not be null)
//         capabilities — AudioDriver::AUDIO_CAP_* bitmask; controls which fields are meaningful
//         linkOk       — true if module responded to at least one UART query
//         active       — true if firmware sent a play command recently (audioActive)
//         playState    — 0=stop 1=playing 2=paused 0xFF=unknown
//         device       — 0=USB 1=SD/TF 2=FLASH 0xFF=none/unknown
//         totalTracks  — total tracks reported by module (0 if unknown)
//         currentTrack — currently selected track (0 if unknown)
//         rxStatus     — compact RX diagnostic string (must not be null)
//         rxDetail     — operator-readable RX diagnostic (must not be null)
// thread-safe: yes (pure function, no globals)
// -----------------------------------------------------------------------------
void formatAudioStatusJson(char* buf, size_t bufSize, const char* driverName,
                           uint8_t capabilities, bool linkOk, bool active,
                           uint8_t playState, uint8_t device, uint16_t totalTracks,
                           uint16_t currentTrack, const char* rxStatus,
                           const char* rxDetail);

// Format JSON response for sleep/wake control endpoints.
// Output: {"ok":true,"sleepMode":<bool>,"changed":<bool>}
// Returns false if the payload does not fit in buf.
bool formatSleepControlJson(char* buf, size_t bufSize, bool sleepMode, bool changed);

// Format JSON response for identity endpoints.
// Output: {"droidName":"...","mdnsUseName":<bool>}
// Returns false if the payload does not fit in buf.
bool formatIdentityJson(char* buf, size_t bufSize, const char* droidName, bool mdnsUseName);

// Format JSON response for speed preset endpoint.
// Output: {"ok":true,"preset":"slow|normal|turbo","speedLimitMax":<0..600>}
// Returns false if the payload does not fit in buf or preset is invalid.
bool formatSpeedPresetResponseJson(char* buf, size_t bufSize, SpeedPresetId preset,
                                   int16_t speedLimitMax);

// Format JSON response for AUX LED endpoints.
// Output: {"ok":true,"auxLed":{"pin":<u8>,"r":<u8>,"g":<u8>,"b":<u8>,"effect":"..."}}
// Returns false if the payload does not fit in buf.
bool formatAuxLedStateJson(char* buf, size_t bufSize, uint8_t pin, uint8_t r, uint8_t g,
                           uint8_t b, const char* effect);
