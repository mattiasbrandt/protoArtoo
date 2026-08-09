// =============================================================================
// include/api_helpers.h
//
// Pure parsing helpers for web API parameter validation.
// No Arduino, no FreeRTOS, no hardware dependencies - testable in native env.
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
// params: s - null-terminated string, modified in place (must not be null)
// thread-safe: yes (pure function, no globals)
// -----------------------------------------------------------------------------
void trimAsciiWhitespace(char* s);

// -----------------------------------------------------------------------------
// parseDriveValue()
// Parse a null-terminated decimal integer string into an int16_t.
// Accepts negative values. Rejects empty strings, non-numeric input, and
// strings with trailing non-numeric characters.
// params: raw - null-terminated input string (must not be null)
//         out - receives parsed value on success
// returns: true on success, false on any parse error
// thread-safe: yes (pure function, no globals)
// -----------------------------------------------------------------------------
bool parseDriveValue(const char* raw, int16_t* out);

// -----------------------------------------------------------------------------
// parseUint32Value()
// Parse a null-terminated decimal unsigned integer string into a uint32_t.
// Rejects empty strings, negative values, non-numeric input, and strings with
// trailing non-numeric characters.
// params: raw - null-terminated input string (must not be null)
//         out - receives parsed value on success
// returns: true on success, false on any parse error
// thread-safe: yes (pure function, no globals)
// -----------------------------------------------------------------------------
bool parseUint32Value(const char* raw, uint32_t* out);

// -----------------------------------------------------------------------------
// parseBoolValue()
// Parse a null-terminated string into a bool.
// Accepts: "true", "1" -> true; "false", "0" -> false.
// All other values return false (parse failure).
// params: raw - null-terminated input string (must not be null)
//         out - receives parsed value on success
// returns: true on success, false on unrecognised input
// thread-safe: yes (pure function, no globals)
// -----------------------------------------------------------------------------
bool parseBoolValue(const char* raw, bool* out);

// Validate and copy an operator-facing droid name into the persisted form.
// Names may contain lowercase a-z, 0-9, and '-'. Uppercase, whitespace, and
// other punctuation are rejected.
// Returns false if the result is empty or does not fit in out.
bool normalizeDroidName(const char* raw, char* out, size_t outSize);
