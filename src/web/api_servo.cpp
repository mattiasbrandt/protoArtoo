// =============================================================================
// src/web/api_servo.cpp
//
// Servo control API endpoint
//   POST /api/servo  — Control arm servos (open/close/position/stop)
// =============================================================================

#include <Arduino.h>
#include <ESPAsyncWebServer.h>

#include "ledc_pwm.h"
#include "logging.h"
#include "robot_state.h"

extern QueueHandle_t servoCmdQueue;

static const char* TAG = "SERVO_API";

// Map arm name string to arm ID
static int8_t parseArmId(const String& arm) {
    if (arm == "arm1" || arm == "ARM1")
        return 0;
    if (arm == "arm2" || arm == "ARM2")
        return 1;
    if (arm == "aux1" || arm == "AUX1")
        return 2;
    if (arm == "aux2" || arm == "AUX2")
        return 3;
    if (arm == "aux3" || arm == "AUX3")
        return 4;
    if (arm == "both" || arm == "BOTH")
        return 255;
    return -1;  // Invalid
}

// Map action string to command type and get position
static bool parseAction(const String& action, ServoCommandType& type, uint16_t& positionUs) {
    if (action == "open") {
        type = SERVO_CMD_OPEN;
        return true;
    }
    if (action == "close") {
        type = SERVO_CMD_CLOSE;
        return true;
    }
    if (action == "stop") {
        type = SERVO_CMD_POSITION;
        positionUs = SERVO_PULSE_NEUTRAL_US;  // Stop at neutral
        return true;
    }
    if (action == "position") {
        type = SERVO_CMD_POSITION;
        return true;
    }
    return false;
}

void registerServoRoutes(AsyncWebServer& server) {
    server.on("/api/servo", HTTP_POST, [](AsyncWebServerRequest* req) {
        // Validate required parameters
        const AsyncWebParameter* armParam = req->getParam("arm", true);
        const AsyncWebParameter* actionParam = req->getParam("action", true);
        if (armParam == nullptr || actionParam == nullptr) {
            req->send(400, "application/json",
                      "{\"ok\":false,\"error\":\"Missing arm or action parameter\"}");
            return;
        }

        String arm = armParam->value();
        String action = actionParam->value();

        int8_t armId = parseArmId(arm);
        if (armId < 0) {
            req->send(400, "application/json",
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
            req->send(
                400, "application/json",
                "{\"ok\":false,\"error\":\"Invalid action. Use: open, close, stop, or position\"}");
            return;
        }

        // Handle position action with positionUs parameter
        if (cmd.type == SERVO_CMD_POSITION && action == "position") {
            const AsyncWebParameter* positionParam = req->getParam("positionUs", true);
            if (positionParam == nullptr) {
                req->send(400, "application/json",
                          "{\"ok\":false,\"error\":\"Missing positionUs parameter for position "
                          "action\"}");
                return;
            }
            cmd.positionUs = (uint16_t)positionParam->value().toInt();
            // Validate range
            if (cmd.positionUs < SERVO_PULSE_MIN_US || cmd.positionUs > SERVO_PULSE_MAX_US) {
                char errBuf[96];
                snprintf(errBuf, sizeof(errBuf),
                         "{\"ok\":false,\"error\":\"positionUs must be between %u and %u\"}",
                         (unsigned)SERVO_PULSE_MIN_US, (unsigned)SERVO_PULSE_MAX_US);
                req->send(400, "application/json", errBuf);
                return;
            }
        }

        // Send command to servo task queue (non-blocking)
        if (xQueueSend(servoCmdQueue, &cmd, 0) != pdTRUE) {
            req->send(503, "application/json",
                      "{\"ok\":false,\"error\":\"Servo command queue full\"}");
            return;
        }

        PA_LOG_INFO(TAG, "[WEB] Servo command queued: arm=%s, action=%s", arm.c_str(),
                    action.c_str());
        req->send(200, "application/json", "{\"ok\":true}");
    });
}
