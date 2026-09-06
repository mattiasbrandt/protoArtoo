// =============================================================================
// include/console_cli_line.h
//
// Reconstructs a full command line from embedded-cli's split name/args pair
// (ADR 0036; #219 R2 landed this narrowed to "help"/"operations" only, #221
// widens it to every command).
//
// embedded-cli's default onCommand callback hands the command name and its
// argument remainder as two separate strings (CliCommand::name / ::args -
// lib/embedded-cli/include/embedded_cli.h). consoleExecuteCommand() expects
// ONE combined "name args" string on ConsoleRequest::operationName (matching
// what the web adapter has always handed it, src/web/api_console.cpp), then
// splits it right back into a bare operation name plus a raw argument
// remainder as its own first step (consoleSplitCommandLine(),
// include/console_args.h) before tokenizing the remainder. This function is
// the serial-side half of getting both adapters to that same combined
// shape; the actual argument parsing/validation lives in console_args.h and
// console_module.cpp, not here.
//
// Pure string logic only (no I/O, no allocation, no FreeRTOS/Arduino
// dependency), so it is unit-testable on the host, unlike the adapter that
// calls it (src/tasks/console_task.cpp is Arduino-only - see its
// onCliCommand() for how this is wired in).
//
// #221 resolved the previous SCOPE FENCE here by widening: this now
// reconstructs "name args" for every command, not just the two meta-
// commands. Kept as a real (non-header-only) file rather than folded into
// console_args.h: [env:native]'s build_src_filter (platformio.ini) already
// allowlists console_cli_line.cpp and is fenced on this ticket (no raise
// available to add a new translation unit), so widening this existing,
// already-buildable file is the lower-risk resolution - deleting it would
// leave a dangling build_src_filter entry with nothing to compile.
// =============================================================================

#pragma once

#include <stddef.h>

// Reconstruct a command's full command line, or return commandName
// unchanged if it had no arguments (nothing to reconstruct).
//
// commandName - embedded-cli's parsed command name (CliCommand::name). Must
//               not be NULL.
// args        - embedded-cli's argument remainder (CliCommand::args), or
//               NULL/empty if the command had no arguments.
// outBuf      - caller-owned scratch buffer, used only when reconstruction
//               is needed. Must outlive the returned pointer's use.
// outBufSize  - size of outBuf in bytes.
//
// Returns a pointer to the command line to hand to consoleExecuteCommand():
// either commandName itself (args was NULL/empty - nothing to append) or
// outBuf (reconstructed "commandName args", truncated to fit outBufSize if
// necessary, same truncate-not-overflow behavior as snprintf).
const char* consoleBuildCommandLine(const char* commandName, const char* args, char* outBuf,
                                     size_t outBufSize);
