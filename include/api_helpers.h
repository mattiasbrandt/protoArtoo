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
