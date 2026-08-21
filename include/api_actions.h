// =============================================================================
// include/api_actions.h
//
// Action registry API endpoints. Both GET /api/actions and
// POST /api/actions/test are ported to the project-owned WebRequest seam
// (ADR 0021) and bound by the seam route table.
// =============================================================================
#pragma once

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

void handleActionsGet(WebRequest& req);
void handleActionsTestPost(WebRequest& req);