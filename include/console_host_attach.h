// =============================================================================
// include/console_host_attach.h
//
// Full input-buffer reset for a (re)attaching host on the serial adapter's
// embedded-cli instance (ADR 0034, #260).
//
// Called from src/tasks/console_task.cpp when the task's own host-presence
// tracking (Serial's operator bool()) observes a debounced false->true
// transition - a host attaching or re-attaching to the transport (native USB
// CDC on the FireBeetle 2; inert on artoo-esp32, see console_task.cpp for
// why). Never touches lib/embedded-cli/** (fenced, #260's pinned coordinator
// comment; the vendored patch set is already at its five-patch ceiling) -
// this is exactly the "caller-side work" the ticket asks for, built only
// from the library's existing public API.
//
// HEADER-ONLY DELIBERATELY, same reason as console_completion.h:
// platformio.ini's native build_src_filter is fenced for this ticket, so a
// new src/**.cpp translation unit has no way to be pulled into the native
// test env. An `inline` function here needs no build_src_filter entry - the
// native test and console_task.cpp both just #include this header, and both
// link the real lib/embedded-cli object the same way console_completion.h's
// native test already does.
//
// Why embeddedCliResetInput() alone is not enough (this is the whole reason
// this file exists instead of a one-line call at the site):
//
// embeddedCliResetInput() is the library's own public reset primitive
// (lib/embedded-cli's Patch 2, VENDORED.md), documented as clearing "the
// partial command without discarding OTHER STATE". Reading the
// implementation (embedded_cli.c): it zeroes impl->cmdSize and clears
// CLI_FLAG_OVERFLOW - nothing else. It deliberately does NOT touch
// impl->cmdBuffer's contents, impl->cursorPos, impl->inputLineLength or
// impl->history.current. Every OTHER mutator in the library maintains the
// invariant strlen(cmdBuffer) == cmdSize (onCharInput/backspace shift the
// trailing NUL along with the shifted bytes); embeddedCliResetInput() is the
// one function that breaks it on purpose, because its designed caller
// (an RX-overflow listener) already knows the very next bytes fed in start a
// brand new line at cursorPos 0, so the stale bytes past the new cmdSize
// never get read again by that caller's usage pattern.
//
// A host re-attach is a different caller shape: the next bytes are typed
// interactively, at whatever cursorPos was last left (possibly non-zero, if
// the operator was mid-line-edit when the cable was pulled), and every
// insertion position the library computes is
// `strlen(cmdBuffer) - cursorPos` (embedded_cli.c's onCharInput) - so a
// resetInput()-only "reset" leaves the NEXT real keystroke inserted at the
// stale strlen() offset, past whatever fragment survives, not at the front
// of an empty line. That is exactly the "silently corrupted first command"
// fault #260 exists to close, and test/test_native/test_console_host_attach
// demonstrates it directly (a scenario that goes red if this file's queued
// synthetic Enter is removed and only embeddedCliResetInput() is called).
//
// The fix uses the library's OWN full-reset code path instead of trying to
// hand-roll one: onControlInput's Enter branch (embedded_cli.c) is the only
// place cmdBuffer[0], cursorPos, inputLineLength and history.current are all
// cleared together - the same code every ordinary Enter keypress already
// runs, already exercised by the library's own tests. Zeroing cmdSize first
// (embeddedCliResetInput) guarantees the queued synthetic Enter's
// `if (impl->cmdSize > 0) parseCommand();` guard is false, so the reset can
// never submit a stale or corrupted command - only clear state.
//
// Known, deliberately out-of-scope residual gaps (both require a detach that
// lands mid-multi-byte-input, not just mid-line - see #260's status comment
// for why neither is worth a caller-side workaround):
//  - If the host disconnected between the ESC and the terminating byte of a
//    VT100 arrow/function-key escape sequence, embedded-cli's
//    CLI_FLAG_ESCAPE_MODE stays set (no public getter/clearer exists) and
//    the queued synthetic Enter is consumed by onEscapedInput() instead of
//    onControlInput() - the reset silently no-ops that one poll. USB CDC
//    delivers a terminal's 3-6 byte escape burst as one read; splitting a
//    detach exactly inside it is not reachable from any host-side action a
//    terminal emulator takes.
//  - If the operator's last byte before detaching was itself a real,
//    already-processed Enter (cmdBuffer/cursorPos already clean), the
//    duplicate CR/LF filter in onControlInput can absorb the synthetic
//    Enter, so the "> " invitation does not re-echo on that poll - purely
//    cosmetic (the banner still prints, and the input state was already
//    correct), and self-heals on the operator's first keystroke.
// =============================================================================

#pragma once

extern "C" {
#include "embedded_cli.h"
}

// Reset embedded-cli's input state for a (re)attaching host: no fragment
// from before the attach survives, and no stale content can execute as a
// command. Queues the reset; the caller's own embeddedCliProcess() drains it
// (embeddedCliReceiveChar is FIFO and documented single-caller-only, so this
// must run before the caller reads any real bytes from the transport in the
// same poll - console_task.cpp orders it that way).
inline void consoleResetInputForAttach(EmbeddedCli *cli) {
    // Clear the library's "how much has been typed" counter and any pending
    // overflow flag first, so the synthetic Enter below can never see
    // cmdSize > 0 and submit whatever was typed before the host attached.
    embeddedCliResetInput(cli);

    // Queue a synthetic Enter. embedded-cli's own Enter handling is the only
    // public code path that also clears cmdBuffer[0], cursorPos,
    // inputLineLength and the history cursor - the full reset this ticket
    // needs, reusing already-proven library code rather than reaching past
    // the public API into implementation details this project does not own.
    embeddedCliReceiveChar(cli, '\r');
}
