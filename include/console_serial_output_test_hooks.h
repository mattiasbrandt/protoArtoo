// =============================================================================
// include/console_serial_output_test_hooks.h
//
// Native tests only: the one-line entry to the drained-log redraw (#466).
//
// Defined in src/console/console_serial_output.cpp. consoleSerialDrainLogs()
// renders every drained line through the same body, so production reaches
// this path only through the drain; nothing on the droid calls it by name.
// It is declared here, not in include/console_serial_output.h, so a test can
// drive one line through the redraw without a Log Ring.
// =============================================================================
#pragma once

// Emit one drained log line, with its mid-entry redraw, in ONE frame.
//
// Renders the whole redraw -- input line cleared, the line, the break, the
// prompt, the buffered command -- into a buffer with embeddedCliPrintToBuffer()
// and writes that buffer as one frame, waiting for transmit room under the
// same bound as a Console Record (ADR 0039 supersedes ADR 0038's "logs stay
// best-effort": the reason logs could not wait was a TWDT-subscribed logger
// blocking on the CDC, and the Console task is not TWDT-subscribed).
//
// If the redraw does not fit CONSOLE_SERIAL_FRAME_MAX the render is refused
// whole rather than truncated, and the line alone is sent through
// consoleSerialEmitFramedLine(). The line is still written whole; only the
// prompt redraw is lost, and the operator's next keystroke or log line draws
// it again.
//
// CONSTRAINT: the Console task only. It renders through the line editor, whose
// state that task also mutates in embeddedCliProcess(); single ownership is
// what makes both safe without a lock, so a second caller re-opens exactly the
// cross-core editor race ADR 0039 removed. It is also not re-entrant --
// embeddedCliPrintToBuffer() refuses a render started from inside a render.
void consoleSerialEmitLine(const char* line);
