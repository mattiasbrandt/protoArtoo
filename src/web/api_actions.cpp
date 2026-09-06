// =============================================================================
// src/web/api_actions.cpp
//
// POST /api/actions/test - dispatch one action through RC trigger path for
// testing, ported to the WebRequest seam (ADR 0021).
//
// GET /api/actions lives in api_actions_json.cpp: keeping it out of this file
// keeps it clear of the RC-dispatch and FreeRTOS dependencies the test route
// needs, so the host tests can build and drive it directly.
// =============================================================================

#include "../../include/api_actions.h"

#include <ArduinoJson.h>
#include <stdio.h>
#include <string.h>

#include "../../include/action_registry.h"
#include "../../include/api_helpers.h"  // trimAsciiWhitespace
#include "../../include/api_json_response.h"
#include "../../include/rc_input.h"
#include "../../include/robot_state.h"

namespace {

static const char* TAG = "Actions";

// An action token is a registry identifier, far shorter than this; the buffer
// is oversized so an over-long token still reaches parseRobotActionId() as an
// unparseable string rather than a truncation that happens to match.
constexpr size_t kTokenBufSize = 64;
constexpr size_t kResponseMaxBytes = 512;

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

void sendJsonError(WebRequest& req, int code, const char* message) {
    JsonDocument doc;
    doc["ok"] = false;
    doc["error"] = message;
    webSendJsonDocument(req, doc, kResponseMaxBytes, TAG, code);
}

}  // namespace

void handleActionsTestPost(WebRequest& req) {
    // The token arrives either as a form field (data/app.js and data/rc.js both
    // use postForm) or inside a JSON body. Both device backends parse a form
    // body into parameters and leave only an unparsed body for body(), so
    // "parameter first, then JSON" resolves the two without either backend
    // having to special-case a content type.
    char token[kTokenBufSize] = {};
    if (!req.param("token", token, sizeof(token)) || token[0] == '\0') {
        const char* body = req.body();
        if (body != nullptr) {
            JsonDocument bodyDoc;
            if (deserializeJson(bodyDoc, body)) {
                sendJsonError(req, 400, "invalid json body");
                return;
            }
            JsonVariantConst tokenVar = bodyDoc["token"];
            if (!tokenVar.is<const char*>()) {
                sendJsonError(req, 400, "invalid_action_token");
                return;
            }
            snprintf(token, sizeof(token), "%s", tokenVar.as<const char*>());
        }
    }

    trimAsciiWhitespace(token);
    if (token[0] == '\0') {
        sendJsonError(req, 400, "invalid_action_token");
        return;
    }

    RobotActionId target = ROBOT_ACTION_NONE;
    if (!parseRobotActionId(token, &target)) {
        sendJsonError(req, 400, "invalid_action_token");
        return;
    }

    switch (evaluateActionTestGuard(target, webControlEnabledForActionTest())) {
        case ACTION_TEST_SAFETY_CRITICAL_BLOCKED:
            sendJsonError(req, 403, "safety_critical_blocked");
            return;
        case ACTION_TEST_WEB_CONTROL_DISABLED:
            sendJsonError(req, 423, "web_control_disabled");
            return;
        case ACTION_TEST_ACTION_NOT_TESTABLE:
            sendJsonError(req, 422, "action_not_testable");
            return;
        case ACTION_TEST_ALLOWED:
        default:
            break;
    }

    // SRC_WEB_API: this route is the REST test button (data/rc.js), distinct
    // from the Controller Console's own action executor (SRC_WEB_CONSOLE /
    // SRC_SERIAL_CONSOLE, console_module.cpp) even though both ultimately
    // call this same dispatch core (#220).
    RcDispatchOutcome outcome = dispatchRcTriggerActionTest(target, "", true, SRC_WEB_API);

    // Reports what actually happened instead of always "ok":true - the whole
    // point of #220: the guard above already refused anything it can refuse
    // before dispatch, so everything reaching here is a real outcome, not a
    // guard failure. HTTP 200 in every case: the request itself was valid
    // and processed; "outcome"/"ok" carry the truthful result, matching the
    // Console's own separation of transport status from outcome
    // (docs/console-protocol.md s.3.3).
    const char* outcomeStr = "queued";
    bool ok = true;
    switch (outcome) {
        case RcDispatchOutcome::kQueued:
            outcomeStr = "queued";
            ok = true;
            break;
        case RcDispatchOutcome::kQueueFull:
            outcomeStr = "queue-full";
            ok = false;
            break;
        case RcDispatchOutcome::kBlockedByState:
            outcomeStr = "unavailable";
            ok = false;
            break;
    }

    JsonDocument doc;
    doc["ok"] = ok;
    doc["outcome"] = outcomeStr;
    doc["token"] = token;
    doc["domain"] = actionDomainForId(target);
    webSendJsonDocument(req, doc, kResponseMaxBytes, TAG);
}
