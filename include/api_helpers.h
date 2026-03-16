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

// -----------------------------------------------------------------------------
// ManualCommand — recognised command tokens for POST /api/manual-command.
// Returned by resolveManualCommand() so callers can dispatch without string
// comparison.
// -----------------------------------------------------------------------------
enum ManualCommand : uint8_t {
    MC_UNKNOWN = 0,
    MC_ESTOP,
    MC_CLEAR_ESTOP,
    MC_ENABLE_WEB_CONTROL,
    MC_DISABLE_WEB_CONTROL,
    MC_REBOOT,
    MC_STATIONARY_MODE,
    MC_DRIVING_MODE,
};

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

// -----------------------------------------------------------------------------
// resolveManualCommand()
// Map a null-terminated command string to a ManualCommand enum value.
// Input is trimmed and lowercased before matching (caller must pass already
// normalised input — lowercase, no leading/trailing whitespace).
// params: command — null-terminated, already-normalised command string
// returns: ManualCommand enum value; MC_UNKNOWN if not recognised
// thread-safe: yes (pure function, no globals)
// -----------------------------------------------------------------------------
ManualCommand resolveManualCommand(const char* command);

// -----------------------------------------------------------------------------
// formatConfigJson()
// Write a JSON config object into a caller-supplied buffer.
// Pure function — no globals, no Arduino, no FreeRTOS.
// params: buf               — output buffer (must not be null)
//         bufSize           — size of buf in bytes
//         speedLimitMax     — current speed limit cap
//         webDriveTimeoutMs — current web drive timeout in ms
//         ch8ModeLock       — current CH8 mode-lock setting
// thread-safe: yes (pure function, no globals)
// -----------------------------------------------------------------------------
void formatConfigJson(char* buf, size_t bufSize, int16_t speedLimitMax, uint32_t webDriveTimeoutMs,
                      bool ch8ModeLock);

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
// thread-safe: yes (pure function, no globals)
// -----------------------------------------------------------------------------
void formatWifiJson(char* buf, size_t bufSize, const char* apSsid, const char* apIp,
                    bool staEnabled, bool staConnected, const char* staIp, long wifiRssi);

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
// formatHealthJson()
// Write a JSON health/diagnostics object into a caller-supplied buffer.
// Pure function — no globals, no Arduino, no FreeRTOS.
// params: buf               — output buffer (must not be null)
//         bufSize           — size of buf in bytes
//         estop             — current estop state
//         sbusSignalLost    — true if SBUS signal is lost
//         sbusHwFailsafe    — true if SBUS hardware failsafe is active
//         webControlEnabled — true if web drive control is enabled
//         wifiConnected     — true if STA WiFi is connected
//         wifiClientConnected — true if a WiFi client is connected (same as wifiConnected)
//         fsReady           — true if LittleFS is mounted
//         heapFree          — current free heap in bytes
//         heapMin           — minimum free heap since boot in bytes
//         wifiRssi          — WiFi RSSI in dBm (0 if not connected)
// thread-safe: yes (pure function, no globals)
// -----------------------------------------------------------------------------
void formatHealthJson(char* buf, size_t bufSize, bool estop, bool sbusSignalLost,
                      bool sbusHwFailsafe, bool webControlEnabled, bool wifiConnected,
                      bool wifiClientConnected, bool fsReady, unsigned long heapFree,
                      unsigned long heapMin, long wifiRssi);
