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
    // Answered in full before the end record was written: queries (help,
    // status) complete synchronously, so "queued" would describe work that
    // never entered a queue. Distinct from APPLIED, which means state changed.
    CONSOLE_OUTCOME_COMPLETED = 8,
} ConsoleOutcome;

// NONE is 0 so a zero-initialised record carries "no reason" rather than
// borrowing a real availability reason. A success path must pass NONE: the
// adapters decide whether to render the field by testing against it, so any
// real value passed as filler would be printed as though it applied.
typedef enum {
    CONSOLE_REASON_NONE = 0,
    CONSOLE_REASON_NOT_IN_THIS_BUILD = 1,
    CONSOLE_REASON_NOT_ON_THIS_BOARD = 2,
    CONSOLE_REASON_COMPONENT_DISABLED = 3,
    CONSOLE_REASON_BLOCKED_BY_STATE = 4,
    CONSOLE_REASON_TEMPORARILY_UNAVAILABLE = 5,
    CONSOLE_REASON_LINE_TOO_LONG = 6,
    CONSOLE_REASON_SECRET_NOT_SETTABLE = 7,
    CONSOLE_REASON_UNKNOWN_OPERATION = 8,
    CONSOLE_REASON_UNKNOWN_ARGUMENT = 9,
    CONSOLE_REASON_MISSING_ARGUMENT = 10,
    CONSOLE_REASON_OUT_OF_RANGE = 11,
    CONSOLE_REASON_NOT_EXECUTABLE = 12,
    CONSOLE_REASON_EXECUTOR_NOT_READY = 13,
    CONSOLE_REASON_QUEUE_FULL = 14,
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
    // name and value are not NULL, but valid only during the callback's duration.
    // The module reuses a single buffer for successive field emissions, so adapters
    // that retain values must copy them into owned storage.
    void (*onRecordField)(uint32_t requestId, const char* name, const char* value);

    // Called for list items (T2+ scope)
    void (*onRecordItem)(uint32_t requestId, const char* value);

    // Called for a single-record response (error, action completion, config write).
    // Per docs/console-protocol.md, guard paths (unknown operation, unavailable,
    // non-status operation type) emit a single type=result record, not begin+end.
    void (*onRecordResult)(uint32_t requestId, ConsoleStatus status,
                          ConsoleOutcome outcome, ConsoleReason reason);

    // Called at the end of a multi-record response (after all fields/items)
    void (*onRecordEnd)(uint32_t requestId, ConsoleStatus status, ConsoleOutcome outcome,
                       ConsoleReason reason);
} ConsoleRecordSink;

// =============================================================================
// Help Text Reader (Dependency Injection)
// =============================================================================
// The Console module reads help text on demand from a reader that the caller
// provides. This allows LittleFS-backed reads on Arduino and memory-backed reads
// in native tests. A NULL reader means "help unavailable" (graceful degradation).
// =============================================================================

typedef struct {
    // Seek to offset in the help file. Returns true on success.
    // ctx is the pointer provided in the reader struct.
    bool (*seek)(void* ctx, uint32_t offset);

    // Read bytes from current position into out_buffer.
    // Returns the number of bytes read (0 on failure or EOF).
    // ctx is the pointer provided in the reader struct.
    size_t (*read)(void* ctx, char* out_buffer, size_t len);

    // Caller-owned context (e.g., a File handle on Arduino, a buffer pointer in tests)
    void* ctx;
} ConsoleHelpReader;

// Set the help reader for the Console module.
// Pass NULL to disable help text (graceful degradation if LittleFS is unavailable).
// Called from setup() after LittleFS is ready (see ADR 0034).
void consoleModuleSetHelpReader(const ConsoleHelpReader* reader);

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
