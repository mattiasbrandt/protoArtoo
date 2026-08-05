// =============================================================================
// include/api_actions.h
//
// Action registry API endpoints. GET /api/actions is ported to the
// project-owned WebRequest seam (ADR 0021) and bound by the seam route table;
// POST /api/actions/test still registers against the async server until its
// group lands.
// =============================================================================
#pragma once

#include <ESPAsyncWebServer.h>

#include "rc_mapping.h"
#include "web_request.h"

inline bool robotActionIsWebTestable(RobotActionId target) {
    return target != SYSTEM_ACTION_ESTOP && !robotActionIsAnalog(target) &&
           !robotActionNeedsPayload(target);
}

enum ActionTestGuardResult : uint8_t {
    ACTION_TEST_ALLOWED = 0,
    ACTION_TEST_SAFETY_CRITICAL_BLOCKED,
    ACTION_TEST_WEB_CONTROL_DISABLED,
    ACTION_TEST_ACTION_NOT_TESTABLE,
};

inline ActionTestGuardResult evaluateActionTestGuard(RobotActionId target,
                                                     bool webControlEnabled) {
    if (target == SYSTEM_ACTION_ESTOP) {
        return ACTION_TEST_SAFETY_CRITICAL_BLOCKED;
    }
    if (!webControlEnabled) {
        return ACTION_TEST_WEB_CONTROL_DISABLED;
    }
    if (!robotActionIsWebTestable(target)) {
        return ACTION_TEST_ACTION_NOT_TESTABLE;
    }
    return ACTION_TEST_ALLOWED;
}

// Routes not yet ported to the seam: POST /api/actions/test. Called only from
// the async registration block.
void registerActionsRoutes(AsyncWebServer& server);

void handleActionsGet(WebRequest& req);