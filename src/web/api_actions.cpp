// =============================================================================
// src/web/api_actions.cpp
//
// GET /api/actions — JSON array of all RC-bindable robot actions.
// POST /api/actions/test — dispatch one action through RC trigger path for testing.
// =============================================================================

#include "../../include/api_actions.h"

#include <ArduinoJson.h>
#include <ESPAsyncWebServer.h>

#include "../../include/action_registry.h"
#include "../../include/logging.h"
#include "../../include/rc_input.h"
#include "../../include/robot_state.h"

static const char* TAG = "Actions";

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
    server.on("/api/actions", HTTP_GET, [](AsyncWebServerRequest* req) {
        JsonDocument doc;
        JsonArray arr = doc.to<JsonArray>();

        for (size_t i = 0; i < ACTION_REGISTRY_SIZE; ++i) {
            const ActionEntry& e = ACTION_REGISTRY[i];
            JsonObject obj = arr.add<JsonObject>();
            obj["id"] = static_cast<int>(e.id);
            obj["name"] = e.name;
            obj["display_name"] = e.display_name;
            obj["domain"] = e.domain;
            obj["description"] = e.description;
            obj["safety_critical"] = e.safety_critical;
            obj["testable"] = robotActionIsWebTestable(e.id);
            obj["token"] = robotActionIdToString(e.id);
        }

        auto* stream = req->beginResponseStream("application/json");
        if (stream == nullptr) {
            req->send(500, "application/json",
                      "{\"ok\":false,\"error\":\"response stream alloc failed\"}");
            return;
        }
        serializeJson(doc, *stream);
        req->send(stream);
        PA_LOG_DEBUG(TAG, "GET /api/actions (%zu entries)", ACTION_REGISTRY_SIZE);
    });

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
            case ACTION_TEST_ANALOG_ACTION_NOT_TESTABLE:
                req->send(422, "application/json",
                          "{\"ok\":false,\"error\":\"analog_action_not_testable\"}");
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