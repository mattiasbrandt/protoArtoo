// =============================================================================
// src/console/console_record.cpp
//
// Console Record formatting and string conversion utilities.
// Records are newline-delimited key=value pairs with printable-ASCII envelope.
// =============================================================================

#include "console_record.h"

#include <string.h>
#include <stdio.h>

// =============================================================================
// String Conversion Helpers
// =============================================================================

bool consoleReasonIsPresent(ConsoleReason reason) {
    return reason != CONSOLE_REASON_NONE;
}

const char* consoleStatusString(ConsoleStatus status) {
    switch (status) {
        case CONSOLE_STATUS_OK:
            return "ok";
        case CONSOLE_STATUS_ERR:
            return "err";
        default:
            return "unknown";
    }
}

const char* consoleOutcomeString(ConsoleOutcome outcome) {
    switch (outcome) {
        case CONSOLE_OUTCOME_QUEUED:
            return "queued";
        case CONSOLE_OUTCOME_APPLIED:
            return "applied";
        case CONSOLE_OUTCOME_STAGED_UNTIL_REBOOT:
            return "staged-until-reboot";
        case CONSOLE_OUTCOME_UNAVAILABLE:
            return "unavailable";
        case CONSOLE_OUTCOME_BLOCKED:
            return "blocked";
        case CONSOLE_OUTCOME_QUEUE_FULL:
            return "queue-full";
        case CONSOLE_OUTCOME_INVALID:
            return "invalid";
        case CONSOLE_OUTCOME_INTERNAL_ERROR:
            return "internal-error";
        case CONSOLE_OUTCOME_COMPLETED:
            return "completed";
        default:
            return "unknown";
    }
}

const char* consoleReasonString(ConsoleReason reason) {
    switch (reason) {
        case CONSOLE_REASON_NONE:
            // Never rendered: the adapters omit the reason field entirely when
            // the reason is NONE. Named rather than empty so it is legible if
            // it ever surfaces in a log or a test failure.
            return "none";
        case CONSOLE_REASON_NOT_IN_THIS_BUILD:
            return "not-in-this-build";
        case CONSOLE_REASON_NOT_ON_THIS_BOARD:
            return "not-on-this-board";
        case CONSOLE_REASON_COMPONENT_DISABLED:
            return "component-disabled";
        case CONSOLE_REASON_BLOCKED_BY_STATE:
            return "blocked-by-state";
        case CONSOLE_REASON_TEMPORARILY_UNAVAILABLE:
            return "temporarily-unavailable";
        case CONSOLE_REASON_LINE_TOO_LONG:
            return "line-too-long";
        case CONSOLE_REASON_SECRET_NOT_SETTABLE:
            return "secret-not-settable";
        case CONSOLE_REASON_UNKNOWN_OPERATION:
            return "unknown-operation";
        case CONSOLE_REASON_UNKNOWN_ARGUMENT:
            return "unknown-argument";
        case CONSOLE_REASON_MISSING_ARGUMENT:
            return "missing-argument";
        case CONSOLE_REASON_OUT_OF_RANGE:
            return "out-of-range";
        case CONSOLE_REASON_NOT_EXECUTABLE:
            return "not-executable";
        case CONSOLE_REASON_EXECUTOR_NOT_READY:
            return "executor-not-ready";
        case CONSOLE_REASON_QUEUE_FULL:
            return "queue-full";
        case CONSOLE_REASON_MALFORMED_ARGUMENT:
            return "malformed-argument";
        case CONSOLE_REASON_READ_ONLY:
            return "read-only";
        default:
            return "unknown";
    }
}

// =============================================================================
// Record Formatting
// =============================================================================

size_t consoleFormatPair(char* buffer, size_t bufferSize, const char* key, const char* value) {
    if (buffer == nullptr || bufferSize == 0 || key == nullptr || value == nullptr) {
        return 0;
    }

    // Format as "key=value"
    int written = snprintf(buffer, bufferSize, "%s=%s", key, value);
    if (written < 0 || (size_t)written >= bufferSize) {
        return 0;  // Buffer too small
    }
    return (size_t)written;
}

// -----------------------------------------------------------------------------
// Record lines
//
// One helper decides "did it fit" for all five, because that is the rule the
// serial adapter got wrong for every one of them: snprintf() reports the
// length it WOULD have written, so a `>= bufferSize` result means NOTHING
// usable is in the buffer, and a caller that only tests `< bufferSize` before
// emitting throws the record away without a trace (#282).
// -----------------------------------------------------------------------------
static size_t fitted(int written, size_t bufferSize) {
    if (written < 0 || (size_t)written >= bufferSize) {
        return 0;  // did not fit: the caller must count a dropped record
    }
    return (size_t)written;
}

size_t consoleFormatBeginRecord(char* buffer, size_t bufferSize, uint32_t requestId,
                                const char* operationType) {
    if (buffer == nullptr || bufferSize == 0 || operationType == nullptr) {
        return 0;
    }
    return fitted(snprintf(buffer, bufferSize, "< id=%lu type=begin operation=%s",
                           (unsigned long)requestId, operationType),
                  bufferSize);
}

size_t consoleFormatFieldRecord(char* buffer, size_t bufferSize, uint32_t requestId,
                                const char* name, const char* value) {
    if (buffer == nullptr || bufferSize == 0 || name == nullptr || value == nullptr) {
        return 0;
    }
    return fitted(snprintf(buffer, bufferSize, "< id=%lu type=field name=%s value=%s",
                           (unsigned long)requestId, name, value),
                  bufferSize);
}

size_t consoleFormatItemRecord(char* buffer, size_t bufferSize, uint32_t requestId,
                               const char* value) {
    if (buffer == nullptr || bufferSize == 0 || value == nullptr) {
        return 0;
    }
    return fitted(snprintf(buffer, bufferSize, "< id=%lu type=item value=%s",
                           (unsigned long)requestId, value),
                  bufferSize);
}

// The two closing records differ only in their type token, so they share a
// body: `result` answers a single-record request, `end` closes a group.
static size_t formatClosingRecord(char* buffer, size_t bufferSize, const char* recordType,
                                  uint32_t requestId, ConsoleStatus status,
                                  ConsoleOutcome outcome, ConsoleReason reason,
                                  const char* droppedSuffix) {
    if (buffer == nullptr || bufferSize == 0) {
        return 0;
    }

    // Present exactly when there is a reason, on both closing records and both
    // adapters - consoleReasonIsPresent() is the one place that rule lives, so
    // it cannot drift back into a per-record special case (see its header
    // comment for the availability answer that lost its reason to one).
    char reasonStr[64] = {};
    if (consoleReasonIsPresent(reason)) {
        snprintf(reasonStr, sizeof(reasonStr), " reason=%s", consoleReasonString(reason));
    }

    return fitted(snprintf(buffer, bufferSize, "< id=%lu type=%s status=%s outcome=%s%s%s",
                           (unsigned long)requestId, recordType, consoleStatusString(status),
                           consoleOutcomeString(outcome), reasonStr,
                           (droppedSuffix != nullptr) ? droppedSuffix : ""),
                  bufferSize);
}

size_t consoleFormatResultRecord(char* buffer, size_t bufferSize, uint32_t requestId,
                                 ConsoleStatus status, ConsoleOutcome outcome,
                                 ConsoleReason reason, const char* droppedSuffix) {
    return formatClosingRecord(buffer, bufferSize, "result", requestId, status, outcome, reason,
                               droppedSuffix);
}

size_t consoleFormatEndRecord(char* buffer, size_t bufferSize, uint32_t requestId,
                              ConsoleStatus status, ConsoleOutcome outcome, ConsoleReason reason,
                              const char* droppedSuffix) {
    return formatClosingRecord(buffer, bufferSize, "end", requestId, status, outcome, reason,
                               droppedSuffix);
}

const char* consoleQuoteValue(const char* value, char* tempBuffer, size_t tempBufferSize) {
    if (value == nullptr || tempBuffer == nullptr || tempBufferSize == 0) {
        return value;
    }

    // Check if quoting is needed (spaces, equals, quotes, or backslashes in value)
    // Per docs/console-protocol.md:56-57, backslash escaping is T1 protocol
    bool needsQuote = false;
    for (const char* p = value; *p != '\0'; ++p) {
        if (*p == ' ' || *p == '=' || *p == '"' || *p == '\\') {
            needsQuote = true;
            break;
        }
    }

    if (!needsQuote) {
        return value;
    }

    // Build quoted value with escaped quotes and backslashes
    // Format: "value with \"quotes\" and \\backslash"
    size_t outIdx = 0;
    if (outIdx >= tempBufferSize) return value;
    tempBuffer[outIdx++] = '"';

    for (const char* p = value; *p != '\0' && outIdx < tempBufferSize - 2; ++p) {
        if (*p == '"' || *p == '\\') {
            if (outIdx >= tempBufferSize - 2) break;
            tempBuffer[outIdx++] = '\\';
        }
        tempBuffer[outIdx++] = *p;
    }

    if (outIdx < tempBufferSize) {
        tempBuffer[outIdx++] = '"';
    }
    if (outIdx < tempBufferSize) {
        tempBuffer[outIdx] = '\0';
    }

    return tempBuffer;
}
