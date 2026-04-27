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
#include "ledc_pwm.h"
#include "logging.h"
#include "marcduino_helpers.h"
#include "robot_state.h"
#include "web_server.h"

static const char* TAG = "MARCDUINO";

// -----------------------------------------------------------------------------
// handlePanelCommand()
// -----------------------------------------------------------------------------
bool handlePanelCommand(const char* cmd) {
    if (cmd[0] != ':' || (cmd[1] != 'O' && cmd[1] != 'C' && cmd[1] != 'M')) {
        return false;
    }

    if (strlen(cmd) < 5) {
        return false;
    }

    ServoCommand servoCmd = {};
    servoCmd.source = SRC_INTERNAL;
    servoCmd.timestampMs = millis();

    if (strncmp(cmd, ":OP", 3) == 0) {
        servoCmd.type = SERVO_CMD_OPEN;
        int panel = atoi(cmd + 3);
        servoCmd.armId = marcduino_panel_to_arm_id(panel);
        if (servoCmd.armId == 254)
            return false;
    } else if (strncmp(cmd, ":CL", 3) == 0) {
        servoCmd.type = SERVO_CMD_CLOSE;
        int panel = atoi(cmd + 3);
        servoCmd.armId = marcduino_panel_to_arm_id(panel);
        if (servoCmd.armId == 254)
            return false;
    } else if (strncmp(cmd, ":MV", 3) == 0) {
        servoCmd.type = SERVO_CMD_POSITION;
        int panel = ((cmd[3] - '0') * 10) + (cmd[4] - '0');
        const char* value_str = cmd + 5;
        if (value_str[0] == '\0')
            return false;
        int value = atoi(value_str);

        servoCmd.armId = marcduino_panel_to_arm_id_mv(panel);
        if (servoCmd.armId == 254)
            return false;

        servoCmd.positionUs = marcduino_mv_value_to_pulse_us(value);
    } else {
        return false;
    }

    taskENTER_CRITICAL(&robotStateMux);
    bool estop = robotState.estop;
    taskEXIT_CRITICAL(&robotStateMux);

    if (estop) {
        PA_LOG_WARN(TAG, "[SERVO] panel command rejected — estop active");
        return false;
    }

    if (xQueueSend(servoCmdQueue, &servoCmd, 0) != pdTRUE) {
        taskENTER_CRITICAL(&robotStateMux);
        robotState.queueOverflowCount++;
        taskEXIT_CRITICAL(&robotStateMux);
    }
    PA_LOG_INFO(TAG, "[SERVO] panel command: %s", cmd);
    return true;
}

// -----------------------------------------------------------------------------
// handleSequenceCommand()
// Parse Marcduino sequence commands for body-owned behavior.
//
// Direct body sequence IDs: :SE30-:SE36
// Full-droid sequence IDs:  :SE01-:SE09, :SE15, :SE16 (decomposed locally)
// -----------------------------------------------------------------------------
bool handleSequenceCommand(const char* cmd) {
    if (cmd[0] != ':' || cmd[1] != 'S' || cmd[2] != 'E') {
        return false;
    }

    const int seqId = atoi(cmd + 3);
    int mappedSeqId = -1;
    FullDroidBodyAction bodyAction{nullptr, -1};

    if (marcduino_sequence_id_valid(seqId)) {
        mappedSeqId = seqId;
    } else {
        bodyAction = marcduino_full_droid_body_actions(seqId);
        if (bodyAction.audioDollarCmd == nullptr && bodyAction.bodySeqId < 0) {
            return false;
        }
        mappedSeqId = bodyAction.bodySeqId;
    }

    bool handled = false;
    if (bodyAction.audioDollarCmd != nullptr) {
        handled = true;
        if (!audioQueueDollar(bodyAction.audioDollarCmd, SRC_INTERNAL)) {
            PA_LOG_WARN(TAG, "[AUDIO] queue full, dropped: %s", bodyAction.audioDollarCmd);
        }
    }

    int queuedSeqId = -1;
    if (mappedSeqId >= 30) {
        if (!marcduino_sequence_id_valid(mappedSeqId)) {
            return false;
        }

        taskENTER_CRITICAL(&robotStateMux);
        bool estop = robotState.estop;
        taskEXIT_CRITICAL(&robotStateMux);

        if (estop) {
            PA_LOG_WARN(TAG, "[SERVO] sequence command rejected - estop active");
        } else {
            ServoCommand servoCmd = {};
            servoCmd.type = SERVO_CMD_SEQUENCE;
            servoCmd.sequenceId = (uint8_t)mappedSeqId;
            servoCmd.source = SRC_INTERNAL;
            servoCmd.timestampMs = millis();

            if (xQueueSend(servoCmdQueue, &servoCmd, 0) != pdTRUE) {
                taskENTER_CRITICAL(&robotStateMux);
                robotState.queueOverflowCount++;
                taskEXIT_CRITICAL(&robotStateMux);
            }
            handled = true;
            queuedSeqId = mappedSeqId;
        }
    }

    if (!handled) {
        return false;
    }

    PA_LOG_INFO(TAG, "[MARCDUINO] SE%02d -> audio=%s seq=%d", seqId,
                bodyAction.audioDollarCmd != nullptr ? bodyAction.audioDollarCmd : "none",
                queuedSeqId);
    return true;
}

// -----------------------------------------------------------------------------
// parseMarcduinoCommand()
// Main entry point — parse and dispatch Marcduino command.
// -----------------------------------------------------------------------------
bool parseMarcduinoCommand(const char* line) {
    if (!line || line[0] == '\0')
        return false;

    switch (line[0]) {
        case ':':
            // Panel or sequence command
            if (line[1] == 'S' && line[2] == 'E') {
                return handleSequenceCommand(line);
            } else {
                return handlePanelCommand(line);
            }

        case '$':
            // Route to AudioTask queue (non-blocking). The queue send will fail
            // gracefully if the queue is full — queueOverflowCount is incremented
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

        case '#': {
            bool syncSleep = false;
            bool syncWake = false;
            if (strcmp(line, "#APSL") == 0) {
                syncSleep = true;
            } else if (strcmp(line, "#APWU") == 0) {
                syncWake = true;
            }

            if (syncSleep || syncWake) {
                bool changed = false;
                const bool sleepMode = syncSleep;
                const uint32_t nowMs = millis();
                taskENTER_CRITICAL(&robotStateMux);
                if (robotState.sleepMode != sleepMode) {
                    robotState.sleepMode = sleepMode;
                    robotState.sleepSinceMs = sleepMode ? nowMs : 0U;
                    changed = true;
                }
                taskEXIT_CRITICAL(&robotStateMux);

                if (changed) {
                    requestStatusBroadcastNow();
                    PA_LOG_INFO(TAG, "[SYSTEM] sleep sync from dome: %s",
                                sleepMode ? "sleep" : "wake");
                }
                return true;
            }

            PA_LOG_INFO(TAG,
                        "[CONFIG] body-local setup command reserved for future handling; "
                        "no ConfigTask in Phase 3: %s",
                        line);
            return true;
        }

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
