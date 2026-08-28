// =============================================================================
// include/console_record.h
//
// Console Record formatting and utilities.
// Records are formatted as key=value lines with printable-ASCII envelope.
// =============================================================================

#pragma once

#include <stddef.h>
#include "console_module.h"

// Buffer size for a single formatted record line
// Includes: id=NNNNNNNNN type=end status=ok outcome=unavailable reason=not-in-this-build
#define CONSOLE_RECORD_LINE_MAX 256

// Format a single key=value pair for a record
// Returns number of bytes written (not including NUL terminator)
size_t consoleFormatPair(char* buffer, size_t bufferSize, const char* key, const char* value);

// Quote a value if it contains spaces, equals, or quotes
// Used for human-text fields (e.g., WiFi SSID)
// Returns pointer to the value (either the original or a quoted copy)
const char* consoleQuoteValue(const char* value, char* tempBuffer, size_t tempBufferSize);

// Get the string representation of an outcome
const char* consoleOutcomeString(ConsoleOutcome outcome);

// Get the string representation of a reason
const char* consoleReasonString(ConsoleReason reason);

// Get the string representation of a status
const char* consoleStatusString(ConsoleStatus status);
