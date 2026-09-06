// =============================================================================
// include/console_record.h
//
// Console Record formatting and utilities.
// Records are formatted as key=value lines with printable-ASCII envelope.
// =============================================================================

#pragma once

#include <stddef.h>
#include <stdint.h>
#include "console_module.h"

// Buffer size for a single formatted record line, its CR LF excluded.
//
// The longest line this grammar produces is a `field` record carrying prose:
//
//   "< id=" 5 + id <= 10 + " type=field name=" 17 + name + " value=" 7 + value
//
// The longest values are a help `description` and a `params` list, both
// clamped to 255 bytes in src/console/console_module.cpp, and the longest
// field name in use is `requires_web_control` at 20 - so 314 bytes worst case.
// At 256 a `description` past roughly 213 bytes could not be formatted at all,
// and the serial adapter threw those records away while the browser adapter
// returned the same text in full (#282). 384 carries the worst case with 70
// bytes to spare.
//
// A closing record (`id=NNNNNNNNN type=end status=ok outcome=unavailable
// reason=not-in-this-build dropped=NN`) is ~100 bytes and was never near
// either number.
//
// This is not the only cap a record line meets: the framed emitter that puts
// it on the wire has its own (CONSOLE_SERIAL_FRAME_LINE_MAX,
// include/console_serial_output.h), which is static_asserted against this one
// so raising this alone cannot silently start clipping records.
#define CONSOLE_RECORD_LINE_MAX 384

// Format a single key=value pair for a record
// Returns number of bytes written (not including NUL terminator)
size_t consoleFormatPair(char* buffer, size_t bufferSize, const char* key, const char* value);

// -----------------------------------------------------------------------------
// Record line formatting (the serial wire grammar, docs/console-protocol.md 3.1)
//
// One home for all five record lines, because the rule that decides whether a
// line is sendable is easy to get wrong in a way nothing sees. snprintf()
// returns the length it WOULD have written, so a caller that writes
// `if (len < sizeof(buf)) emit(buf)` drops an over-long record with no line, no
// marker and no count -- which is what lost every serial `help` description
// past ~213 bytes while the browser adapter returned it (#282).
//
// Every one of these returns the byte count written (terminator excluded) or
// 0 when the record does not fit, exactly like consoleFormatPair above. A
// caller must treat 0 as a DROPPED RECORD - counted on the request's closing
// line as `dropped=<n>` - and never as an empty line.
//
// They live here rather than in the serial adapter so the wire rule is
// provable on the host: src/tasks/console_task.cpp is not native-compiled.
// -----------------------------------------------------------------------------

// `< id=<n> type=begin operation=<operationType>`
size_t consoleFormatBeginRecord(char* buffer, size_t bufferSize, uint32_t requestId,
                                const char* operationType);

// `< id=<n> type=field name=<name> value=<value>`
size_t consoleFormatFieldRecord(char* buffer, size_t bufferSize, uint32_t requestId,
                                const char* name, const char* value);

// `< id=<n> type=item value=<value>`
size_t consoleFormatItemRecord(char* buffer, size_t bufferSize, uint32_t requestId,
                               const char* value);

// `< id=<n> type=result status=<s> outcome=<o>[ reason=<r>][ dropped=<n>]` and
// its `type=end` twin. droppedSuffix is the already-formatted ` dropped=<n>`
// (or an empty string): the count belongs to the adapter that did the dropping,
// so it is passed in rather than reached for from here
// (consoleSerialFormatDroppedSuffix, include/console_serial_output.h).
size_t consoleFormatResultRecord(char* buffer, size_t bufferSize, uint32_t requestId,
                                 ConsoleStatus status, ConsoleOutcome outcome,
                                 ConsoleReason reason, const char* droppedSuffix);
size_t consoleFormatEndRecord(char* buffer, size_t bufferSize, uint32_t requestId,
                              ConsoleStatus status, ConsoleOutcome outcome,
                              ConsoleReason reason, const char* droppedSuffix);

// Quote a value if it contains spaces, equals, or quotes
// Used for human-text fields (e.g., WiFi SSID)
// Returns pointer to the value (either the original or a quoted copy)
const char* consoleQuoteValue(const char* value, char* tempBuffer, size_t tempBufferSize);

// Get the string representation of an outcome
const char* consoleOutcomeString(ConsoleOutcome outcome);

// Get the string representation of a reason
const char* consoleReasonString(ConsoleReason reason);

// Whether a record should carry a reason= field at all.
//
// Both adapters call this, so the rule cannot drift between them: the field is
// present exactly when there is a reason, and absent otherwise. Deciding this
// per adapter is what let the serial path special-case one reason value and
// drop it from a genuine availability answer.
bool consoleReasonIsPresent(ConsoleReason reason);

// Get the string representation of a status
const char* consoleStatusString(ConsoleStatus status);
