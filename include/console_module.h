// =============================================================================
// include/console_module.h
//
// Controller Console module - transport-independent operation processor.
// ADR 0034: one operation core below HTTP handlers, structured key=value records.
//
// Public interface for both serial and web adapters to execute operations
// and receive Console Records.
// =============================================================================

#pragma once

#include <stddef.h>
#include <stdint.h>

// =============================================================================
// Command Sources (ADR 0034)
// =============================================================================
typedef enum {
    CONSOLE_SOURCE_SERIAL = 0,
    CONSOLE_SOURCE_WEB = 1,
} ConsoleCommandSource;

// =============================================================================
// Operation Types (from registry)
// =============================================================================
typedef enum {
    CONSOLE_OP_ACTION = 0,
    CONSOLE_OP_STATUS = 1,
    CONSOLE_OP_CONFIG = 2,
    CONSOLE_OP_EVENT = 3,
} ConsoleOperationType;

// =============================================================================
// Stable Outcome and Reason Sets (ADR 0034)
// =============================================================================
typedef enum {
    CONSOLE_STATUS_OK = 0,
    CONSOLE_STATUS_ERR = 1,
} ConsoleStatus;

typedef enum {
    CONSOLE_OUTCOME_QUEUED = 0,
    CONSOLE_OUTCOME_APPLIED = 1,
    CONSOLE_OUTCOME_STAGED_UNTIL_REBOOT = 2,
    CONSOLE_OUTCOME_UNAVAILABLE = 3,
    CONSOLE_OUTCOME_BLOCKED = 4,
    CONSOLE_OUTCOME_QUEUE_FULL = 5,
    CONSOLE_OUTCOME_INVALID = 6,
    CONSOLE_OUTCOME_INTERNAL_ERROR = 7,
} ConsoleOutcome;

typedef enum {
    CONSOLE_REASON_NOT_IN_THIS_BUILD = 0,
    CONSOLE_REASON_NOT_ON_THIS_BOARD = 1,
    CONSOLE_REASON_COMPONENT_DISABLED = 2,
    CONSOLE_REASON_BLOCKED_BY_STATE = 3,
    CONSOLE_REASON_TEMPORARILY_UNAVAILABLE = 4,
    CONSOLE_REASON_LINE_TOO_LONG = 5,
    CONSOLE_REASON_SECRET_NOT_SETTABLE = 6,
    CONSOLE_REASON_UNKNOWN_OPERATION = 7,
    CONSOLE_REASON_UNKNOWN_ARGUMENT = 8,
    CONSOLE_REASON_MISSING_ARGUMENT = 9,
    CONSOLE_REASON_OUT_OF_RANGE = 10,
    CONSOLE_REASON_NOT_EXECUTABLE = 11,
    CONSOLE_REASON_EXECUTOR_NOT_READY = 12,
    CONSOLE_REASON_QUEUE_FULL = 13,
} ConsoleReason;

// =============================================================================
// Request Structure (T1 scope: only system.status.health)
// =============================================================================
typedef struct {
    uint32_t requestId;
    ConsoleCommandSource source;
    const char* operationName;  // e.g. "system.status.health"
    // For T1 (tracer), we only support status queries with no arguments.
} ConsoleRequest;

// =============================================================================
// Console Record Sink (adapter receives records from module)
// Called by the Console module to output records. Adapter implements this.
// =============================================================================
typedef struct {
    // Called at the start of a multi-record response (e.g., status query)
    void (*onRecordBegin)(uint32_t requestId, const char* operationType);

    // Called for each field in a status response
    // name and value are both guaranteed to not be NULL.
    void (*onRecordField)(uint32_t requestId, const char* name, const char* value);

    // Called for list items (T2+ scope)
    void (*onRecordItem)(uint32_t requestId, const char* value);

    // Called at the end of any response
    void (*onRecordEnd)(uint32_t requestId, ConsoleStatus status, ConsoleOutcome outcome,
                       ConsoleReason reason);
} ConsoleRecordSink;

// =============================================================================
// Public API
// =============================================================================

// Initialize the Console module (call once from setup() after config loads)
void consoleModuleInit(void);

// Execute a console command and emit records through the sink.
// The module calls sink callbacks for each record.
// Request ID must be assigned by the caller (one global counter across adapters).
void consoleExecuteCommand(const ConsoleRequest* request, const ConsoleRecordSink* sink);

// Get the next request ID (monotonic across both adapters)
uint32_t consoleGetNextRequestId(void);
