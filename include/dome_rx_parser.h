// =============================================================================
// include/dome_rx_parser.h
//
// Marcduino command parser for dome->body serial communication.
// Parses :OP/:CL/:MV commands and body sequences (:SE30-:SE36).
// Compatibility alias: :SE01 maps to body :SE30 arm choreography.
//
// Design note:
// - This is a body-side custom parser by intent (no Reeltwo parser dependency
//   on the body firmware).
// - It implements the protoArtoo subset and routing contract documented in
//   docs/goal.md and docs/marcduino_commands.md.
// =============================================================================
#pragma once

#include <Arduino.h>

// Parse a Marcduino command line and execute.
// Returns true if command was recognized and processed.
bool parseMarcduinoCommand(const char* line);

// Command handlers  --  exposed for testing
bool handlePanelCommand(const char* cmd);     // :OP, :CL, :MV
bool handleSequenceCommand(const char* cmd);  // :SE30-:SE36 (+ :SE01 alias)
