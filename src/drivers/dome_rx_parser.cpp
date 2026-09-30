// =============================================================================
// src/drivers/dome_rx_parser.cpp
//
// Marcduino command parser implementation.
//
// Architecture decision:
// - Body-side Marcduino handling is intentionally implemented in-repo (plain
//   parsing), not via Reeltwo/Marcduino runtime libraries.
// - Reason: body firmware only needs a bounded subset with deterministic routing
//   to local queues and safety gates (estop, feature toggles, non-blocking sends).
// - Scope: this parser handles the protoArtoo body-owned subset and explicitly
//   ignores/delegates unsupported prefixes by topology design.
//
// Reference sources:
// - docs/goal.md (body-vs-dome ownership and routing)
// - docs/commands.md (command surface inventory)
//
// Testing strategy:
// - Pure mapping/conversion helpers are split into marcduino_helpers.h and
//   covered by native tests under test/test_native/test_marcduino_helpers/.
// =============================================================================

#include "dome_rx_parser.h"

#include <string.h>

#include "audio_task.h"
#include "commanded_modes.h"
#include "ledc_pwm.h"
#include "logging.h"
#include "marcduino_helpers.h"
#include "queue_drop_tracker.h"
#include "robot_state.h"
#include "sequence_dispatcher.h"  // :SE30..:SE36 start as Factory Sequences
#include "web_server.h"

static const char* TAG = "MARCDUINO";

// -----------------------------------------------------------------------------
// handlePanelCommand()
// :OPnn / :CLnn / :OFnn / :MVnnvvvv on a body Output. The number is read by
// marcduino_panel_command_arm_id(), the same reading the ownership resolver
// takes (include/marcduino_ownership.h), so a line routed here is one this
// handler can place.
//
// :OF is a flutter, a Move Shape the body models (ADR 0049), and it ends
// open. Until the flutter oscillation is performed it resolves to that open
// end - the one part of the shape the body can already do.
// -----------------------------------------------------------------------------
MarcduinoBodyOutcome handlePanelCommand(const char* cmd) {
    const uint8_t armId = marcduino_panel_command_arm_id(cmd);
    if (armId == 254) {
        return MarcduinoBodyOutcome::NotHandled;
    }

    ServoCommand servoCmd = {};
    servoCmd.source = SRC_INTERNAL;
    servoCmd.timestampMs = millis();
    servoCmd.armId = armId;

    if (strncmp(cmd, ":OP", 3) == 0 || strncmp(cmd, ":OF", 3) == 0) {
        servoCmd.type = SERVO_CMD_OPEN;
    } else if (strncmp(cmd, ":CL", 3) == 0) {
        servoCmd.type = SERVO_CMD_CLOSE;
    } else {
        // :MV - marcduino_panel_command_arm_id() admits no fourth head.
        servoCmd.type = SERVO_CMD_POSITION;
        const char* value_str = cmd + 5;
        if (value_str[0] == '\0')
            return MarcduinoBodyOutcome::NotHandled;
        servoCmd.positionUs = marcduino_mv_value_to_pulse_us(atoi(value_str));
    }

    taskENTER_CRITICAL(&robotStateMux);
    bool estop = robotState.estop;
    taskEXIT_CRITICAL(&robotStateMux);

    if (estop) {
        PA_LOG_WARN(TAG, "[SERVO] panel command rejected - estop active");
        return MarcduinoBodyOutcome::BlockedByEstop;
    }

    if (xQueueSend(servoCmdQueue, &servoCmd, 0) != pdTRUE) {
        logQueueDrop(QUEUE_SERVO_CMD, "servo panel command");
        return MarcduinoBodyOutcome::QueueFull;
    }
    PA_LOG_INFO(TAG, "[SERVO] panel command: %s", cmd);
    return MarcduinoBodyOutcome::Applied;
}

// -----------------------------------------------------------------------------
// handleSequenceCommand()
// Parse Marcduino sequence commands for body-owned behavior.
//
// Direct body sequence IDs: :SE30-:SE36
// Full-droid sequence IDs:  :SE01-:SE09, :SE15, :SE16 (the body half, from
//                           marcduino_full_droid_body_actions(); the dome half
//                           is the router's to forward, never this handler's)
//
// A body sequence is a Factory Sequence built from Body Steps (ADR 0049), so it
// starts through the Sequence Coordinator under its DM:SE<nn> name rather than
// as a command to ServoTask.
//
// A full-droid half that could do nothing at all - no audio in it, and its body
// routine held by estop - answers BlockedByEstop; one that played its audio
// with the routine held answers Applied, as the RC droid_seq_* tokens do.
// -----------------------------------------------------------------------------
MarcduinoBodyOutcome handleSequenceCommand(const char* cmd) {
    if (cmd[0] != ':' || cmd[1] != 'S' || cmd[2] != 'E') {
        return MarcduinoBodyOutcome::NotHandled;
    }

    const int seqId = atoi(cmd + 3);
    FullDroidBodyAction bodyAction{nullptr, -1};

    if (marcduino_sequence_id_valid(seqId)) {
        bodyAction.bodySeqId = seqId;
    } else {
        bodyAction = marcduino_full_droid_body_actions(seqId);
        if (bodyAction.audioDollarCmd == nullptr && bodyAction.bodySeqId < 0) {
            return MarcduinoBodyOutcome::NotHandled;
        }
    }

    bool acted = false;
    bool queueFull = false;
    bool heldByEstop = false;
    if (bodyAction.audioDollarCmd != nullptr) {
        if (audioQueueDollar(bodyAction.audioDollarCmd, SRC_INTERNAL)) {
            acted = true;
        } else {
            PA_LOG_WARN(TAG, "[AUDIO] queue full, dropped: %s", bodyAction.audioDollarCmd);
            queueFull = true;
        }
    }

    int queuedSeqId = -1;
    if (bodyAction.bodySeqId >= 30) {
        if (!marcduino_sequence_id_valid(bodyAction.bodySeqId)) {
            return MarcduinoBodyOutcome::NotHandled;
        }

        taskENTER_CRITICAL(&robotStateMux);
        bool estop = robotState.estop;
        taskEXIT_CRITICAL(&robotStateMux);

        if (estop) {
            PA_LOG_WARN(TAG, "[SEQ] body routine rejected - estop active");
            heldByEstop = true;
        } else if (!sequenceStart(sequenceBodyRoutineName(bodyAction.bodySeqId), SRC_INTERNAL)) {
            PA_LOG_WARN(TAG, "[SEQ] body routine :SE%02d not started - sequence queue full",
                        bodyAction.bodySeqId);
            queueFull = true;
        } else {
            acted = true;
            queuedSeqId = bodyAction.bodySeqId;
        }
    }

    PA_LOG_INFO(TAG, "[MARCDUINO] SE%02d -> audio=%s seq=%d", seqId,
                bodyAction.audioDollarCmd != nullptr ? bodyAction.audioDollarCmd : "none",
                queuedSeqId);
    if (queueFull) {
        return MarcduinoBodyOutcome::QueueFull;
    }
    if (!acted && heldByEstop) {
        return MarcduinoBodyOutcome::BlockedByEstop;
    }
    return MarcduinoBodyOutcome::Applied;
}

// -----------------------------------------------------------------------------
// executeMarcduinoBodyCommand()
// The body's half of a builder's line, once the router has decided the body
// owns it. ':' and '#' only - '$' and the raw families never reach it.
// -----------------------------------------------------------------------------
MarcduinoBodyOutcome executeMarcduinoBodyCommand(const char* line) {
    if (!line || line[0] == '\0')
        return MarcduinoBodyOutcome::NotHandled;

    if (line[0] == ':') {
        if (line[1] == 'S' && line[2] == 'E') {
            return handleSequenceCommand(line);
        }
        return handlePanelCommand(line);
    }

    if (line[0] == '#') {
        if (!marcduino_is_body_hash_command(line)) {
            return MarcduinoBodyOutcome::NotHandled;
        }
        if (strcmp(line, "#PAHB") == 0) {
            PA_LOG_DEBUG(TAG, "[HB] body heartbeat echo ignored");
            return MarcduinoBodyOutcome::Applied;
        }
        const bool syncSleep = strcmp(line, "#APSL") == 0;
        if (commandedSetSleep(syncSleep, SRC_INTERNAL)) {
            requestStatusBroadcastNow();
            PA_LOG_INFO(TAG, "[SYSTEM] sleep sync from dome: %s", syncSleep ? "sleep" : "wake");
        }
        return MarcduinoBodyOutcome::Applied;
    }

    return MarcduinoBodyOutcome::NotHandled;
}

// -----------------------------------------------------------------------------
// parseMarcduinoCommand()
// Main entry point  --  parse and dispatch Marcduino command.
// -----------------------------------------------------------------------------
bool parseMarcduinoCommand(const char* line) {
    if (!line || line[0] == '\0')
        return false;

    switch (line[0]) {
        case ':':
        case '#': {
            // Panel, sequence or '#' line. Held by estop reads as not handled,
            // as it always has here; a queue that refused still recognised it.
            const MarcduinoBodyOutcome outcome = executeMarcduinoBodyCommand(line);
            if (outcome == MarcduinoBodyOutcome::NotHandled && line[0] == '#') {
                PA_LOG_DEBUG(TAG, "[CONFIG] unhandled body command: %s", line);
            }
            return outcome == MarcduinoBodyOutcome::Applied ||
                   outcome == MarcduinoBodyOutcome::QueueFull;
        }

        case '$':
            // Route to AudioTask queue (non-blocking). The queue send will fail
            // gracefully if the queue is full  --  queueOverflowCount is incremented
            // by audioQueueDollar() in that case.
            if (!audioQueueDollar(line, SRC_INTERNAL)) {
                PA_LOG_WARN(TAG, "[AUDIO] queue full, dropped: %s", line);
            } else {
                PA_LOG_DEBUG(TAG, "[AUDIO] queued: %s", line);
            }
            return true;

        case '@':
            PA_LOG_DEBUG(TAG, "Display/logics command deferred to dome link: %s", line);
            return false;

        case '*':
            PA_LOG_DEBUG(TAG, "Holo projector command deferred to dome link: %s", line);
            return false;

        case '%':
            PA_LOG_DEBUG(TAG, "Slave-out command deferred to dome link: %s", line);
            return false;

        case '&':
            PA_LOG_DEBUG(TAG, "Marcduino I2C command not applicable to body controller: %s", line);
            return false;

        case '!':
            PA_LOG_DEBUG(TAG, "Marcduino custom extension not applicable to body controller: %s",
                         line);
            return false;

        default:
            PA_LOG_DEBUG(TAG, "Unknown Marcduino prefix ignored: %s", line);
            return false;
    }
}
