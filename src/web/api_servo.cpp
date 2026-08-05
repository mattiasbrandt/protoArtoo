// =============================================================================
// src/web/api_servo.cpp
//
// Servo control API endpoint
//   POST /api/servo  — Control arm servos (open/close/position/stop)
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
#include "ledc_pwm.h"
#include "logging.h"
#include "robot_state.h"

extern QueueHandle_t servoCmdQueue;

static const char* TAG = "SERVO_API";

namespace {

// Map arm name string to arm ID.
//
// int16_t, not int8_t: the broadcast id is 255, which an int8_t return
// truncates to -1 -- the invalid-arm sentinel. That is how "both" came to be
// rejected as an invalid arm on an endpoint whose own error message offers it,
// even though ServoTask has always accepted 255 (robot_state.h). A wider
// return keeps the sentinel and the id distinguishable.
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
        req.send(400, "application/json",
                 "{\"ok\":false,\"error\":\"Missing arm or action parameter\"}");
        return;
    }

    int16_t armId = parseArmId(arm);
    if (armId < 0) {
        req.send(400, "application/json",
                 "{\"ok\":false,\"error\":\"Invalid arm. Use: arm1, arm2, aux1, aux2, aux3, "
                 "or both\"}");
        return;
    }

    ServoCommand cmd;
    cmd.armId = (uint8_t)armId;
    cmd.source = SRC_WEB_API;
    cmd.timestampMs = millis();

    // Parse action
    if (!parseAction(action, cmd.type, cmd.positionUs)) {
        req.send(400, "application/json",
                 "{\"ok\":false,\"error\":\"Invalid action. Use: open, close, stop, or position\"}");
        return;
    }

    // Handle position action with positionUs parameter
    if (cmd.type == SERVO_CMD_POSITION && strcmp(action, "position") == 0) {
        char positionRaw[16] = {};
        if (!req.param("positionUs", positionRaw, sizeof(positionRaw))) {
            req.send(400, "application/json",
                     "{\"ok\":false,\"error\":\"Missing positionUs parameter for position "
                     "action\"}");
            return;
        }
        // Unparseable input lands on the same range error a numerically
        // out-of-range value gets, which is what this endpoint has always
        // answered -- the vendor's toInt() read garbage as 0.
        uint32_t parsed = 0;
        if (!parseUint32Value(positionRaw, &parsed) || parsed < SERVO_PULSE_MIN_US ||
            parsed > SERVO_PULSE_MAX_US) {
            char errBuf[96];
            snprintf(errBuf, sizeof(errBuf),
                     "{\"ok\":false,\"error\":\"positionUs must be between %u and %u\"}",
                     (unsigned)SERVO_PULSE_MIN_US, (unsigned)SERVO_PULSE_MAX_US);
            req.send(400, "application/json", errBuf);
            return;
        }
        cmd.positionUs = (uint16_t)parsed;
    }

    // Send command to servo task queue (non-blocking)
    if (xQueueSend(servoCmdQueue, &cmd, 0) != pdTRUE) {
        req.send(503, "application/json", "{\"ok\":false,\"error\":\"Servo command queue full\"}");
        return;
    }

    PA_LOG_INFO(TAG, "[WEB] Servo command queued: arm=%s, action=%s", arm, action);
    req.send(200, "application/json", "{\"ok\":true}");
}
