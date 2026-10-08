// =============================================================================
// include/api_console.h
//
// POST /api/console - browser adapter for the Controller Console (ADR 0036).
// Receives command lines from the Live Logs command box, executes them through
// the Console module, and returns Console Records as JSON.
//
// The bounded path's record sink and the request's other buffers are declared
// here rather than in src/web/api_console.cpp only so the web request scratch
// (include/web_request_scratch.h) can size itself by them.
// =============================================================================
#pragma once

#include <stddef.h>
#include <stdint.h>

#include "log_buffer.h"  // LOG_LINE_MAX - the per-item arena reserve #239 needs
#include "web_request.h"

// Maximum number of records + value storage for a single BOUNDED response.
// `operations` never uses this path (see fillOperationsResponse below) -
// this cap sizes the small, fixed responses every other command produces.
#define CONSOLE_RESPONSE_RECORDS_MAX 32
#define CONSOLE_RECORD_VALUE_ARENA 2048

// Worst-case bytes one item's value can consume in the arena. The longest
// item value any query emits today is a log line (system.status.logs, #239),
// up to LOG_LINE_MAX-1 bytes. Reserving this much before accepting an item
// guarantees arenaStoreString() never has to silently truncate a value on
// this path - an item this file refuses is visible on the wire
// (webSink.itemsTruncated -> "truncated":true); one arenaStoreString() quietly
// shortened would not be. Revisit if a future item-emitting query's values
// can be longer than a log line.
#define CONSOLE_ITEM_VALUE_RESERVE_BYTES LOG_LINE_MAX

struct ConsoleRecord {
    const char* type;
    uint32_t requestId;
    const char* status;
    const char* outcome;
    const char* reason;
    const char* operation;
    const char* name;
    const char* value;
};

// Arena holds copied values; all record pointers reference this storage
struct ConsoleWebSink {
    ConsoleRecord records[CONSOLE_RESPONSE_RECORDS_MAX];
    size_t recordCount;
    char valueArena[CONSOLE_RECORD_VALUE_ARENA];
    size_t arenaUsed;
    // Set the moment a sink callback cannot record a record because the
    // array is full - including the `end`/`result` record that would have
    // closed the group. handleConsolePost() checks this before building any
    // response, so a command that outgrows this path answers with an
    // explicit failure instead of a JSON body that looks complete but is
    // missing its close (#240). system.status.logs (#239) never reaches this:
    // its item count is bounded up front by itemsToSkip below, precisely so
    // it degrades via itemsTruncated instead.
    bool overflowed;
    // #239: system.status.logs-style graceful item truncation.
    // itemsToSkip is set once, before consoleExecuteCommand() runs
    // (handleConsolePost()'s system.status.logs branch), to the number of
    // OLDEST items to discard so the KEPT items are the newest ones - a
    // discarded item never touches records[]/valueArena, so skipping costs
    // nothing, unlike storing-then-evicting an already-kept item would.
    // itemsTruncated is set the moment any item is skipped this way, or an
    // item is refused for arena headroom (webOnRecordItem_impl) - reported to
    // the client on the JSON response envelope ("truncated":true), never as
    // a new Console Record field; the wire protocol itself is unchanged.
    size_t itemsToSkip;
    bool itemsTruncated;
};

// #266: this struct alone was ~3.1 KB on the real 32-bit target, and living
// on handleConsolePost()'s stack (alongside two 256-byte command buffers) was
// enough by itself to overflow the httpd task's 8 KB stack on the very first
// POST, for every command including the smallest possible one - measured via
// `-fstack-usage` (handleConsolePost()'s frame: 3776 B before, 240 B after
// moving it and the buffers to static storage; see handleConsolePost()'s own
// comment). Guards against this struct growing back into stack-sized
// territory unnoticed: a future field addition that trips this budget must
// re-measure the static chain (this struct now lives in the web request
// scratch, include/web_request_scratch.h, so it costs .bss - RAM budget in
// tools/build_budgets.json - not stack, but a large ADDITIONAL request-scoped
// struct like this one, declared as a stack local in api_console.cpp or a new
// adapter, would reopen exactly this defect) rather than silently raising the
// number here.
//
// PA_NATIVE_TEST_STUBS-gated out: the native test build compiles this same
// struct for the HOST'S pointer width, not the target's. ConsoleRecord holds
// seven `const char*` fields, so a 64-bit host makes this struct ~1 KB larger
// than the real ESP32/ESP32-P4 build (measured: 4136 B host vs 3092 B
// target) for a reason with zero bearing on either chip's actual stack
// (native tests never run on an httpd task) - asserting the target's number
// against a host-compiled size would either false-fail on every native build
// or have to be loosened past the point of guarding anything.
#if !defined(PA_NATIVE_TEST_STUBS)
static_assert(sizeof(ConsoleWebSink) <= 3200,
              "ConsoleWebSink grew past its #266 stack-safety budget - "
              "re-measure handleConsolePost()'s -fstack-usage frame before "
              "raising this number");
#endif

// The longest command line the browser adapter takes, terminator included.
#define CONSOLE_WEB_COMMAND_MAX 256

// Everything one POST /api/console holds for the length of the request, in the
// web request scratch (include/web_request_scratch.h).
struct ConsoleWebScratch {
    char command[CONSOLE_WEB_COMMAND_MAX];
    // The `operations` routing check's copy of the line: consoleSplitCommandLine()
    // cuts the copy it is given, and `command` must stay whole.
    char routeScratch[CONSOLE_WEB_COMMAND_MAX];
    // The line fillOperationsResponse() replays on every chunk of a streamed
    // `operations` answer.
    char operationsCommand[CONSOLE_WEB_COMMAND_MAX];
    ConsoleWebSink webSink;
    char responseBody[4096];
};

void handleConsolePost(WebRequest& req);
