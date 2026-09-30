// =============================================================================
// include/dome_rx_parser.h
//
// Marcduino command parser: the body's handler for the lines it owns.
// Runs :OP/:CL/:OF/:MV on the body Outputs, the body routines :SE30-:SE36,
// the body half of the full-droid sequences :SE01-:SE09/:SE15/:SE16, and the
// '#' lines marcduino_is_body_hash_command() names.
//
// Two callers, and the difference matters:
// - parseMarcduinoCommand() is the dome RX handler (src/tasks/dome_link.cpp)
//   and the RC audio trigger. It forwards nothing, ever: a line the dome sent
//   must not be sent back to it.
// - executeMarcduinoBodyCommand() is what a builder's line reaches once
//   include/marcduino_router.h has decided the body owns it (ADR 0055).
//
// Design note:
// - This is a body-side custom parser by intent (no Reeltwo parser dependency
//   on the body firmware).
// - What it answers is published in docs/marcduino_commands.md.
// =============================================================================
#pragma once

#include <Arduino.h>

// What the body's handler did with one line it was given.
enum class MarcduinoBodyOutcome : uint8_t {
    NotHandled,      // not a line the body runs (or one too malformed to run)
    Applied,         // queued or done
    BlockedByEstop,  // refused: estop is latched
    QueueFull,       // the owning queue refused it
};

// Parse a Marcduino command line and execute.
// Returns true if command was recognized and processed.
bool parseMarcduinoCommand(const char* line);

// Run one ':' or '#' line the body owns, and say what happened. Forwards
// nothing: the routing decision is the caller's (include/marcduino_router.h).
// A Mood line is not run here either; the router applies it.
MarcduinoBodyOutcome executeMarcduinoBodyCommand(const char* line);

// Command handlers  --  exposed for testing
MarcduinoBodyOutcome handlePanelCommand(const char* cmd);     // :OP, :CL, :OF, :MV
MarcduinoBodyOutcome handleSequenceCommand(const char* cmd);  // :SE30-:SE36, and the body
                                                              // half of :SE01-09/15/16
