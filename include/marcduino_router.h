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

#include "dome_link.h"            // domeConnected(), domeQueueTx()
#include "dome_rx_parser.h"       // executeMarcduinoBodyCommand()
#include "logging.h"
#include "marcduino_ownership.h"  // marcduinoCommandOwner(), MarcduinoRouteOutcome
#include "mood.h"                 // moodIdFromSeCommand()
#include "servo_task.h"           // servoTaskDrivesOutput()

// -----------------------------------------------------------------------------
// marcduinoForwardToDome()
// The one forward: POST /api/dome/cmd's shape (handleDomeCmdPost(),
// src/web/api_drive.cpp) - verbatim, and a line that could not be queued is not
// answered success. domeQueueTx() alone only knows the queue; with protoR2link
// down the line would be queued for a link that sends nothing, so the link is
// asked first, the way the RC droid_seq_* forward already does
// (src/rc_dispatcher_helpers.cpp).
// -----------------------------------------------------------------------------
inline MarcduinoRouteOutcome marcduinoForwardToDome(const char* line) {
    if (!domeConnected()) {
        PA_LOG_WARN("MARCDUINO", "not forwarded, dome not connected: %s", line);
        return MarcduinoRouteOutcome::DomeLinkDown;
    }
    if (!domeQueueTx(line)) {
        PA_LOG_WARN("MARCDUINO", "not forwarded, dome TX queue full: %s", line);
        return MarcduinoRouteOutcome::DomeQueueFull;
    }
    PA_LOG_DEBUG("MARCDUINO", "forwarded to dome: %s", line);
    return MarcduinoRouteOutcome::Forwarded;
}

// Whether ServoTask drives the Output(s) a body-owned panel line names; 255 is
// the ARM1+ARM2 broadcast and needs both. ServoTask drops a command for an
// Output it does not drive without a word (#364), so asking here is the only
// way the sender hears it.
inline bool marcduinoPanelOutputDriven(uint8_t armId) {
    if (armId == 255) {
        return servoTaskDrivesOutput(0) && servoTaskDrivesOutput(1);
    }
    return servoTaskDrivesOutput(armId);
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
    switch (marcduinoCommandOwner(line)) {
        case MarcduinoOwner::Dome:
            return marcduinoForwardToDome(line);

        case MarcduinoOwner::Body: {
            if (moodIdFromSeCommand(line) != 0) {
                PA_LOG_WARN("MARCDUINO", "mood %s not applied from this path", line);
                return MarcduinoRouteOutcome::NotRun;
            }
            const uint8_t armId = marcduino_panel_command_arm_id(line);
            if (armId != 254 && !marcduinoPanelOutputDriven(armId)) {
                PA_LOG_WARN("MARCDUINO", "%s refused - nothing drives that Output", line);
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
