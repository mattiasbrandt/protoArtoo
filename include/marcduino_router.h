// =============================================================================
// include/marcduino_router.h
//
// Where a builder's ':' or '#' Marcduino line is routed (ADR 0055, #449): the
// body runs the lines it owns (include/marcduino_ownership.h) and forwards the
// rest to the Dome Controller verbatim.
//
// Called where a builder's command enters - POST /api/manual-command and the
// Console's dome.action.send-command (executeManualCommand(),
// src/web/api_drive.cpp), and an RC or Console dome.action.marcduino-command
// binding (rcDispatchSingleAction(), src/rc_dispatcher_helpers.cpp) - and
// never from parseMarcduinoCommand(), which is also the dome RX handler: a
// line the dome sent is never forwarded back to it.
//
// HEADER-ONLY DELIBERATELY: src/rc_dispatcher_helpers.cpp is not in
// [env:native]'s build_src_filter and api_drive.cpp is, so this is the one
// place both entry points can share the routing and a native test still runs
// the real thing rather than a stub standing in for it.
// =============================================================================
#pragma once

#include <stdint.h>
#include <string.h>  // strlen

#include "dome_link.h"            // domeConnected(), domeQueueTx()
#include "dome_rx_parser.h"       // executeMarcduinoBodyCommand()
#include "logging.h"
#include "marcduino_ownership.h"  // marcduinoCommandOwner(), MarcduinoRouteOutcome
#include "mood.h"                 // moodIdFromSeCommand()
#include "servo_task.h"           // servoTaskDrivesOutput()

// The router's one log line, kept out of line on purpose. Every PA_LOG_* puts
// a line buffer in the frame of the function it is written in, and
// routeMarcduinoLine() sits on RCInputTask's measured chain directly under the
// body handler: logging inline walked that chain at 5904 B on the P4, past
// the stack rule's step for its 7168 B stack; out of line it walks 5632 B
// (#449, tools/check_task_stack_chains.py). The buffer then lives only while a
// line is logged, beside the handler rather than under it.
//
// Rate-limited: one line per outcome per kMarcduinoRouteLogIntervalMs. The RC
// path reaches this from RCInputTask, and a binding held against a dome that
// is not connected would otherwise write a warning on every press it repeats
// (#449). Two static words and millis(), no heap, no lock: two tasks racing on
// them can at worst let one extra line through, which is a log line and
// nothing more.
constexpr uint32_t kMarcduinoRouteLogIntervalMs = 5000;

__attribute__((noinline)) inline void marcduinoLogRoute(MarcduinoRouteOutcome outcome,
                                                        const char* line) {
    static uint32_t lastLoggedMs[16] = {};
    static uint16_t loggedOnce = 0;  // one bit per outcome: logged at least once
    const uint8_t slot = (uint8_t)outcome & 0x0F;
    const uint32_t nowMs = millis();
    if ((loggedOnce & (1u << slot)) != 0 &&
        (uint32_t)(nowMs - lastLoggedMs[slot]) < kMarcduinoRouteLogIntervalMs) {
        return;
    }
    loggedOnce |= (uint16_t)(1u << slot);
    lastLoggedMs[slot] = nowMs;

    switch (outcome) {
        case MarcduinoRouteOutcome::Forwarded:
            PA_LOG_DEBUG("MARCDUINO", "forwarded to dome: %s", line);
            break;
        case MarcduinoRouteOutcome::DomeLinkDown:
            PA_LOG_WARN("MARCDUINO", "not forwarded, dome not connected: %s", line);
            break;
        case MarcduinoRouteOutcome::DomeQueueFull:
            PA_LOG_WARN("MARCDUINO", "not forwarded, dome TX queue full: %s", line);
            break;
        case MarcduinoRouteOutcome::OutputUndriven:
            PA_LOG_WARN("MARCDUINO", "%s refused - nothing drives that Output", line);
            break;
        case MarcduinoRouteOutcome::NotRun:
            PA_LOG_WARN("MARCDUINO", "%s not run", line);
            break;
        case MarcduinoRouteOutcome::LineTooLong:
            PA_LOG_WARN("MARCDUINO", "refused, longer than %u characters: %.24s...",
                        (unsigned)DOME_TX_LINE_MAX, line);
            break;
        case MarcduinoRouteOutcome::Applied:
        case MarcduinoRouteOutcome::BlockedByEstop:
        case MarcduinoRouteOutcome::QueueFull:
            // The body handler logged these itself.
            break;
    }
}

// -----------------------------------------------------------------------------
// marcduinoForwardToDome()
// The one forward: POST /api/dome/cmd's shape (handleDomeCmdPost(),
// src/web/api_drive.cpp) - verbatim, and a line that could not be queued is not
// answered success. domeQueueTx() alone only knows the queue; with protoR2link
// down the line would be queued for a link that sends nothing, so the link is
// asked first, the way the RC droid_seq_* forward already does
// (src/rc_dispatcher_helpers.cpp).
// -----------------------------------------------------------------------------
// A line the dome TX buffer holds whole. Asked before anything runs, so a
// line too long to forward is never half acted on.
inline bool marcduinoLineFitsDomeTx(const char* line) {
    return strlen(line) <= DOME_TX_LINE_MAX;
}

inline MarcduinoRouteOutcome marcduinoForwardToDome(const char* line) {
    MarcduinoRouteOutcome outcome = MarcduinoRouteOutcome::Forwarded;
    if (!marcduinoLineFitsDomeTx(line)) {
        outcome = MarcduinoRouteOutcome::LineTooLong;
    } else if (!domeConnected()) {
        outcome = MarcduinoRouteOutcome::DomeLinkDown;
    } else if (!domeQueueTx(line)) {
        outcome = MarcduinoRouteOutcome::DomeQueueFull;
    }
    marcduinoLogRoute(outcome, line);
    return outcome;
}

// Whether a body-owned panel line names an Output ServoTask does not drive;
// SERVO_OUTPUT_BOTH_ARMS is the ARM1+ARM2 broadcast and needs both. ServoTask
// drops a command for an Output it does not drive without a word (#364), so
// asking here is the only way the sender hears it. A line that names no Output
// is not refused here: the body handler reports it.
//
// Out of line for the reason marcduinoLogRoute() is: routeMarcduinoLine() is on
// RCInputTask's measured chain, and inlined there the line's Output Address
// grew its frame by 16 B on the ESP32-P4 walk (#444).
__attribute__((noinline)) inline bool marcduinoPanelOutputUndriven(const char* line) {
    const ServoOutputAddress output = marcduino_panel_command_output(line);
    if (output == SERVO_OUTPUT_NONE) {
        return false;
    }
    if (output == SERVO_OUTPUT_BOTH_ARMS) {
        return !servoTaskDrivesOutput(boardOutputAddress(0)) ||
               !servoTaskDrivesOutput(boardOutputAddress(1));
    }
    return !servoTaskDrivesOutput(output);
}

inline MarcduinoRouteOutcome marcduinoRouteFromBody(MarcduinoBodyOutcome body) {
    switch (body) {
        case MarcduinoBodyOutcome::Applied:
            return MarcduinoRouteOutcome::Applied;
        case MarcduinoBodyOutcome::BlockedByEstop:
            return MarcduinoRouteOutcome::BlockedByEstop;
        case MarcduinoBodyOutcome::QueueFull:
            return MarcduinoRouteOutcome::QueueFull;
        case MarcduinoBodyOutcome::NotHandled:
            break;
    }
    return MarcduinoRouteOutcome::NotRun;
}

// -----------------------------------------------------------------------------
// routeMarcduinoLine()
// Route one ':' or '#' line. A line the body owns and refuses is not
// forwarded: it is the body's, and the dome would answer it as something else
// (:OP01 is body arm 1 here and dome panel 1 there - ADR 0055).
//
// A full-droid sequence runs its body half and is forwarded whatever that half
// did, as the RC droid_seq_* tokens do: estop holds the body routine, never
// the dome's panels and lights. Its answer is the forward's; a body queue that
// refused its half turns a Forwarded into QueueFull.
//
// A Mood is the body's, and this never applies one: it answers NotRun, and a
// door that may apply a Mood does so before calling here (executeManualCommand(),
// src/web/api_drive.cpp). applyMood() writes flash, and the RC binding path
// runs on RCInputTask (Core 1), so a call to it anywhere in this function would
// sit on that task's stack chain whether or not the RC path could take it -
// the P4 walk measured +1920 B on RCInputTask when it did (#449). Moods have
// their own control everywhere a binding reaches (system.action.set-mood).
// -----------------------------------------------------------------------------
inline MarcduinoRouteOutcome routeMarcduinoLine(const char* line) {
    if (!marcduinoLineFitsDomeTx(line)) {
        marcduinoLogRoute(MarcduinoRouteOutcome::LineTooLong, line);
        return MarcduinoRouteOutcome::LineTooLong;
    }
    switch (marcduinoCommandOwner(line)) {
        case MarcduinoOwner::Dome:
            return marcduinoForwardToDome(line);

        case MarcduinoOwner::Body: {
            if (moodIdFromSeCommand(line) != 0) {
                marcduinoLogRoute(MarcduinoRouteOutcome::NotRun, line);
                return MarcduinoRouteOutcome::NotRun;
            }
            // A panel head whose number is not all digits: refused here, before
            // the handler, so no reading of it can reach an Output.
            if (marcduino_is_panel_command(line) && !marcduino_panel_command_well_formed(line)) {
                marcduinoLogRoute(MarcduinoRouteOutcome::NotRun, line);
                return MarcduinoRouteOutcome::NotRun;
            }
            if (marcduinoPanelOutputUndriven(line)) {
                marcduinoLogRoute(MarcduinoRouteOutcome::OutputUndriven, line);
                return MarcduinoRouteOutcome::OutputUndriven;
            }
            return marcduinoRouteFromBody(executeMarcduinoBodyCommand(line));
        }

        case MarcduinoOwner::BodyAndDome: {
            const MarcduinoBodyOutcome body = executeMarcduinoBodyCommand(line);
            const MarcduinoRouteOutcome forward = marcduinoForwardToDome(line);
            if (forward != MarcduinoRouteOutcome::Forwarded) {
                return forward;
            }
            return body == MarcduinoBodyOutcome::QueueFull ? MarcduinoRouteOutcome::QueueFull
                                                           : MarcduinoRouteOutcome::Forwarded;
        }
    }
    return MarcduinoRouteOutcome::NotRun;
}
