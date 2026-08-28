// =============================================================================
// include/console_cli_line.h
//
// Reconstructs a meta-command's full command line from embedded-cli's split
// name/args pair (ADR 0034, #219 R2).
//
// embedded-cli's default onCommand callback hands the command name and its
// argument remainder as two separate strings (CliCommand::name / ::args -
// lib/embedded-cli/include/embedded_cli.h). consoleExecuteCommand()'s own
// parsing for the Console's two meta-commands ("help <op>", "operations
// type=<t>") expects ONE combined "name args" string instead - see the
// prefix checks at the top of consoleExecuteCommand() in console_module.cpp.
// This is the seam between those two shapes.
//
// Pure string logic only (no I/O, no allocation, no FreeRTOS/Arduino
// dependency), so it is unit-testable on the host, unlike the adapter that
// calls it (src/tasks/console_task.cpp is Arduino-only - see its
// onCliCommand() for how this is wired in, and for the reconstruction
// scope fence below).
//
// SCOPE FENCE: reconstruction applies ONLY to the two existing meta-commands
// ("help", "operations"). Registry-entry operations that take arguments
// (drive.action.move speed=200 steer=0, system.config.log-level
// value=debug) are #221's and #226's contract to design - they may need
// quote-aware tokenization (docs/console-protocol.md section 1.2) this
// project does not implement yet. Do not widen this function's meta-command
// set to cover them; that decision belongs to those tickets.
// =============================================================================

#pragma once

#include <stddef.h>

// Reconstruct a meta-command's full command line, or return commandName
// unchanged if reconstruction does not apply.
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
// either commandName itself (no reconstruction needed - not a meta-command,
// or a meta-command with no arguments) or outBuf (reconstructed
// "commandName args", truncated to fit outBufSize if necessary, same
// truncate-not-overflow behavior as snprintf).
const char* consoleBuildCommandLine(const char* commandName, const char* args, char* outBuf,
                                     size_t outBufSize);
