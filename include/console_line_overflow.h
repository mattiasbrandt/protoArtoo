// =============================================================================
// include/console_line_overflow.h
//
// The serial adapter's answer to an input line that lost bytes before it
// could be stored (ADR 0034, #262).
//
// WHAT THIS CLOSES. The browser adapter refuses an over-length command
// outright, with a single `type=result status=err outcome=invalid
// reason=line-too-long` record (src/web/api_console.cpp). The serial adapter
// used to accept it, silently drop whatever did not fit the fixed command
// buffer, and execute the prefix - so the two adapters disagreed about a
// failure, and docs/console-protocol.md s.1.3 ("a line longer than the input
// buffer is discarded whole and answered with `invalid reason=line-too-long`;
// a truncated command never executes") was true on one of them only. #206's
// acceptance matrix states the same guarantee: "All failures are explicit; no
// truncated command executes."
//
// The discard itself is the library's, not this file's:
// lib/embedded-cli/VENDORED.md Patch 7 refuses the line inside embedded-cli,
// where cmdSize and cmdMaxSize live, and calls EmbeddedCli::onLineTooLong.
// This file is only the other half - turning that event into the SAME record
// the browser adapter emits for the same failure, so the wire answer is
// identical whichever adapter the operator is on.
//
// WHAT THIS DOES NOT CLOSE, on purpose: the two adapters still refuse at
// DIFFERENT lengths. The browser's limit is 255 bytes (api_console.cpp's
// `char command[256]`); serial's is 62 (embedded-cli's default cmdBufferSize
// 64, less the two bytes tokenisation reserves - src/tasks/console_task.cpp
// takes embeddedCliDefaultConfig() unmodified). Raising the serial buffer to
// match would change embeddedCliRequiredSize() against the task's fixed
// 2 KB cliBuffer, and task/static buffer sizing on this project is a MEASURED
// outcome on real boards (#206 "Not yet specified"; #217 and #228 own the
// measurement), not something to pick from a host. So the length divergence
// is recorded rather than guessed at, and what changes here is that exceeding
// either limit now produces the same explicit answer instead of one adapter
// silently running a command the operator did not type.
//
// HEADER-ONLY DELIBERATELY, for the reason include/console_host_attach.h and
// include/console_completion.h give: platformio.ini's [env:native]
// `build_src_filter` is an explicit allowlist of src/*.cpp translation units
// and is fenced on this ticket, so a new src/console/*.cpp could not be
// pulled into the native test binary. An `inline` function in a header needs
// no filter entry. No Arduino and no FreeRTOS here - the record is emitted
// through the caller's own sink, so this runs on the host exactly as it runs
// on the device.
// =============================================================================

#pragma once

#include "console_module.h"  // ConsoleRecordSink, the status/outcome/reason
                             // enums, and consoleGetNextRequestId()

// Emit the Console Record that answers a line the input layer could not keep
// whole. Byte-for-byte the record src/web/api_console.cpp emits for an
// over-length browser command: one `result`, no `begin`/`end` pair, because
// nothing was executed and there is no operation to bracket.
//
// The request ID is allocated HERE rather than passed in. A refused line
// never reached consoleExecuteCommand(), so no ID was ever assigned to it,
// and an answer with no ID would be the one record on the wire the operator
// (and any script reading them) could not attribute. The counter is the same
// global one both adapters draw from, so the refusal takes its place in the
// same monotonic sequence as every other request.
inline void consoleEmitLineTooLong(const ConsoleRecordSink* sink) {
    if (sink == nullptr || sink->onRecordResult == nullptr) {
        return;
    }
    sink->onRecordResult(consoleGetNextRequestId(), CONSOLE_STATUS_ERR,
                         CONSOLE_OUTCOME_INVALID, CONSOLE_REASON_LINE_TOO_LONG);
}
