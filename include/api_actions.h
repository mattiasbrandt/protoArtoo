#pragma once

#include <ESPAsyncWebServer.h>

#include "rc_mapping.h"

inline bool robotActionIsWebTestable(RobotActionId target) {
    return target != SYSTEM_ACTION_ESTOP && !robotActionIsAnalog(target);
}

enum ActionTestGuardResult : uint8_t {
    ACTION_TEST_ALLOWED = 0,
    ACTION_TEST_SAFETY_CRITICAL_BLOCKED,
    ACTION_TEST_WEB_CONTROL_DISABLED,
    ACTION_TEST_ANALOG_ACTION_NOT_TESTABLE,
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
        return ACTION_TEST_ANALOG_ACTION_NOT_TESTABLE;
    }
    return ACTION_TEST_ALLOWED;
}

void registerActionsRoutes(AsyncWebServer& server);