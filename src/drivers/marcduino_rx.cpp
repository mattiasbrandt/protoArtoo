// =============================================================================
// src/drivers/marcduino_rx.cpp
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
// Contract sources:
// - tasks/goal.md (body-vs-dome ownership and routing)
// - tasks/marcduino_commands.md (command reference inventory)
//
// Testing strategy:
// - Pure mapping/conversion helpers are split into marcduino_helpers.h and
//   covered by native tests under test/test_native/test_marcduino_helpers/.
// =============================================================================

#include "marcduino_rx.h"

#include <string.h>

#include "ledc_pwm.h"
#include "logging.h"
#include "marcduino_helpers.h"
#include "robot_state.h"

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
// Parse body sequence commands.
// Supported direct IDs: :SE30-:SE36.
// Compatibility alias: :SE01 maps to body :SE30 arm choreography.
// -----------------------------------------------------------------------------
bool handleSequenceCommand(const char* cmd) {
    if (cmd[0] != ':' || cmd[1] != 'S' || cmd[2] != 'E') {
        return false;
    }

    int seqId = atoi(cmd + 3);
    int mappedSeqId = seqId;

    if (!marcduino_sequence_id_valid(seqId)) {
        int alias = marcduino_full_sequence_to_body_sequence(seqId);
        if (alias < 0) {
            return false;
        }
        mappedSeqId = alias;
        PA_LOG_INFO(TAG, "[SERVO] sequence alias: :SE%02d -> :SE%02d", seqId, mappedSeqId);
    }

    if (!marcduino_sequence_id_valid(mappedSeqId)) {
        return false;
    }

    taskENTER_CRITICAL(&robotStateMux);
    bool estop = robotState.estop;
    taskEXIT_CRITICAL(&robotStateMux);

    if (estop) {
        PA_LOG_WARN(TAG, "[SERVO] sequence command rejected — estop active");
        return false;
    }

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
    PA_LOG_INFO(TAG, "[SERVO] sequence command: %s", cmd);
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
            PA_LOG_INFO(TAG, "[AUDIO] body-local sound command accepted (Phase 3 stub only): %s",
                        line);
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

        case '#':
            PA_LOG_INFO(TAG,
                        "[CONFIG] body-local setup command reserved for future handling; "
                        "no ConfigTask in Phase 3: %s",
                        line);
            return true;

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
