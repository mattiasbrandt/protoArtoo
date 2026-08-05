// =============================================================================
// src/web/api_actions.cpp
//
// POST /api/actions/test — dispatch one action through RC trigger path for testing.
//
// GET /api/actions lives in api_actions_json.cpp: it is ported to the
// WebRequest seam (ADR 0021) and bound by the seam route table, and keeping it
// out of this file keeps it clear of the RC-dispatch and FreeRTOS dependencies
// the test route needs.
// =============================================================================

#include "../../include/api_actions.h"

#include <ArduinoJson.h>
#include <ESPAsyncWebServer.h>

#include "../../include/action_registry.h"
#include "../../include/rc_input.h"
#include "../../include/robot_state.h"

namespace {

bool webControlEnabledForActionTest() {
    taskENTER_CRITICAL(&robotStateMux);
    const bool enabled = robotState.webControlEnabled;
    taskEXIT_CRITICAL(&robotStateMux);
    return enabled;
}

const ActionEntry* findActionEntry(RobotActionId id) {
    for (size_t i = 0; i < ACTION_REGISTRY_SIZE; ++i) {
        if (ACTION_REGISTRY[i].id == id) {
            return &ACTION_REGISTRY[i];
        }
    }
    return nullptr;
}

const char* actionDomainForId(RobotActionId id) {
    const ActionEntry* entry = findActionEntry(id);
    return entry != nullptr ? entry->domain : "unknown";
}

}  // namespace

void registerActionsRoutes(AsyncWebServer& server) {
    // GET /api/actions is registered by the seam route table, not here.

    server.on("/api/actions/test", HTTP_POST, [](AsyncWebServerRequest* req) {

        String token;
        const AsyncWebParameter* tokenParam = req->getParam("token", true);
        if (tokenParam != nullptr) {
            token = tokenParam->value();
        } else if (req->hasParam("plain", true)) {
            JsonDocument bodyDoc;
            const String rawBody = req->getParam("plain", true)->value();
            if (deserializeJson(bodyDoc, rawBody.c_str())) {
                req->send(400, "application/json",
                          "{\"ok\":false,\"error\":\"invalid json body\"}");
                return;
            }
            JsonVariantConst tokenVar = bodyDoc["token"];
            if (!tokenVar.is<const char*>()) {
                req->send(400, "application/json",
                          "{\"ok\":false,\"error\":\"invalid_action_token\"}");
                return;
            }
            token = tokenVar.as<const char*>();
        }

        token.trim();
        if (token.length() == 0) {
            req->send(400, "application/json",
                      "{\"ok\":false,\"error\":\"invalid_action_token\"}");
            return;
        }

        RobotActionId target = ROBOT_ACTION_NONE;
        if (!parseRobotActionId(token.c_str(), &target)) {
            req->send(400, "application/json",
                      "{\"ok\":false,\"error\":\"invalid_action_token\"}");
            return;
        }

        switch (evaluateActionTestGuard(target, webControlEnabledForActionTest())) {
            case ACTION_TEST_SAFETY_CRITICAL_BLOCKED:
                req->send(403, "application/json",
                          "{\"ok\":false,\"error\":\"safety_critical_blocked\"}");
                return;
            case ACTION_TEST_WEB_CONTROL_DISABLED:
                req->send(423, "application/json",
                          "{\"ok\":false,\"error\":\"web_control_disabled\"}");
                return;
            case ACTION_TEST_ACTION_NOT_TESTABLE:
                req->send(422, "application/json",
                          "{\"ok\":false,\"error\":\"action_not_testable\"}");
                return;
            case ACTION_TEST_ALLOWED:
            default:
                break;
        }

        dispatchRcTriggerActionTest(target, "", true);

        JsonDocument doc;
        doc["ok"] = true;
        doc["token"] = token;
        doc["domain"] = actionDomainForId(target);
        auto* stream = req->beginResponseStream("application/json");
        if (stream == nullptr) {
            req->send(500, "application/json",
                      "{\"ok\":false,\"error\":\"response stream alloc failed\"}");
            return;
        }
        serializeJson(doc, *stream);
        req->send(stream);
    });
}
