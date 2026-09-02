// =============================================================================
// src/web/api_servo.cpp
//
// Servo control API endpoint
//   POST /api/servo  - Control arm servos (open/close/position/stop)
//
// Written against the project-owned WebRequest seam (ADR 0021) and bound by the
// seam route table. The command goes onto servoCmdQueue with a zero wait, so
// this never blocks on ServoTask.
// =============================================================================

#include "api_servo.h"

#include <Arduino.h>
#include <stdio.h>
#include <string.h>

#include "api_helpers.h"
#include "api_json_response.h"
#include "ledc_pwm.h"
#include "logging.h"
#include "robot_state.h"

extern QueueHandle_t servoCmdQueue;

static const char* TAG = "SERVO_API";

// See include/api_servo.h for the full contract - exported so the Controller
// Console's servo.action.* executors (include/console_direct_action_servo.h)
// reuse the same target<->id mapping.
int16_t parseArmId(const char* arm) {
    if (strcmp(arm, "arm1") == 0 || strcmp(arm, "ARM1") == 0)
        return 0;
    if (strcmp(arm, "arm2") == 0 || strcmp(arm, "ARM2") == 0)
        return 1;
    if (strcmp(arm, "aux1") == 0 || strcmp(arm, "AUX1") == 0)
        return 2;
    if (strcmp(arm, "aux2") == 0 || strcmp(arm, "AUX2") == 0)
        return 3;
    if (strcmp(arm, "aux3") == 0 || strcmp(arm, "AUX3") == 0)
        return 4;
    if (strcmp(arm, "both") == 0 || strcmp(arm, "BOTH") == 0)
        return 255;
    return -1;  // Invalid
}

// See include/api_servo.h for the full contract. `cmd` is zero-initialised
// here (the pre-port handler left an uninitialised local's `sequenceId`
// field, dead for every type but SERVO_CMD_SEQUENCE - src/tasks/
// servo_task.cpp never reads it for OPEN/CLOSE/POSITION - so this closes
// that latent UB without changing anything ServoTask observes).
ServoSubmitOutcome servoSubmitCommand(uint8_t armId, ServoCommandType type, uint16_t positionUs,
                                       CommandSource source) {
    ServoSubmitOutcome outcome;
    ServoCommand cmd = {};
    cmd.armId = armId;
    cmd.type = type;
    cmd.positionUs = positionUs;
    cmd.source = source;
    cmd.timestampMs = millis();
    outcome.ok = (xQueueSend(servoCmdQueue, &cmd, 0) == pdTRUE);
    return outcome;
}

namespace {

// Map action string to command type and get position
bool parseAction(const char* action, ServoCommandType& type, uint16_t& positionUs) {
    if (strcmp(action, "open") == 0) {
        type = SERVO_CMD_OPEN;
        return true;
    }
    if (strcmp(action, "close") == 0) {
        type = SERVO_CMD_CLOSE;
        return true;
    }
    if (strcmp(action, "stop") == 0) {
        type = SERVO_CMD_POSITION;
        positionUs = SERVO_PULSE_NEUTRAL_US;  // Stop at neutral
        return true;
    }
    if (strcmp(action, "position") == 0) {
        type = SERVO_CMD_POSITION;
        return true;
    }
    return false;
}

}  // namespace

void handleServoPost(WebRequest& req) {
    // Both buffers are wider than the longest value either name accepts, so an
    // over-long input arrives at the parsers as an over-long string and is
    // rejected, rather than being truncated into a valid one (web_request.h).
    char arm[16] = {};
    char action[16] = {};
    if (!req.param("arm", arm, sizeof(arm)) || !req.param("action", action, sizeof(action))) {
        webSendJsonError(req, 400, "Missing arm or action parameter");
        return;
    }

    int16_t armId = parseArmId(arm);
    if (armId < 0) {
        webSendJsonError(req, 400, "Invalid arm. Use: arm1, arm2, aux1, aux2, aux3, or both");
        return;
    }

    ServoCommandType type;
    uint16_t positionUs = 0;
    if (!parseAction(action, type, positionUs)) {
        webSendJsonError(req, 400, "Invalid action. Use: open, close, stop, or position");
        return;
    }

    // Handle position action with positionUs parameter
    if (type == SERVO_CMD_POSITION && strcmp(action, "position") == 0) {
        char positionRaw[16] = {};
        if (!req.param("positionUs", positionRaw, sizeof(positionRaw))) {
            webSendJsonError(req, 400, "Missing positionUs parameter for position action");
            return;
        }
        // Unparseable input lands on the same range error a numerically
        // out-of-range value gets, which is what this endpoint has always
        // answered -- the vendor's toInt() read garbage as 0.
        uint32_t parsed = 0;
        if (!parseUint32Value(positionRaw, &parsed) || parsed < SERVO_PULSE_MIN_US ||
            parsed > SERVO_PULSE_MAX_US) {
            char errMsg[64];
            snprintf(errMsg, sizeof(errMsg),
                     "positionUs must be between %u and %u",
                     (unsigned)SERVO_PULSE_MIN_US, (unsigned)SERVO_PULSE_MAX_US);
            webSendJsonError(req, 400, errMsg);
            return;
        }
        positionUs = (uint16_t)parsed;
    }

    // Commit Step (ADR 0034 criterion 1, include/api_servo.h): the same
    // servoCmdQueue submission both this handler and the Console's
    // servo.action.* executors now make.
    ServoSubmitOutcome outcome = servoSubmitCommand((uint8_t)armId, type, positionUs, SRC_WEB_API);
    if (!outcome.ok) {
        webSendJsonError(req, 503, "Servo command queue full");
        return;
    }

    PA_LOG_INFO(TAG, "[WEB] Servo command queued: arm=%s, action=%s", arm, action);
    req.send(200, "application/json", "{\"ok\":true}");
}
