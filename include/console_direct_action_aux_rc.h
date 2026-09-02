// =============================================================================
// include/console_direct_action_aux_rc.h
//
// Controller Console direct-action executors - aux LED and RC domains,
// combined into one file the way the row counts warrant (2 aux rows + 2 rc
// rows): rc.action.toggle-debug (#226 criterion 4's fifth Commanded Mode),
// rc.action.test-bindable (the by-token guard+dispatch tester, #221
// remainder), aux.action.led-color and aux.action.led-effect (#221
// remainder). Split out of src/console/console_module.cpp by #257 so these
// rows can be extended without colliding with the other domains' files.
//
// HEADER-ONLY DELIBERATELY - see include/console_direct_action_system.h's
// header comment for the full reasoning (native's fenced build_src_filter
// allowlist has no room for a new src/console/*.cpp here) and for why
// `static`, not `inline`, is the right linkage given this header has exactly
// one includer.
//
// NOT a standalone compilation unit: #include'd from src/console/
// console_module.cpp only, at the point these executors used to live, so
// their bodies can see that file's own `static` consoleEmitArgFailure(),
// consoleCommandSourceFor(), consoleAnswerActionGuardResult() and
// consoleMapDispatchOutcome() by ordinary same-translation-unit visibility.
// Not included, and must not be included, from anywhere else.
// =============================================================================
#pragma once

#include <stdlib.h>

#include "console_direct_action_types.h"  // ConsoleDirectActionExecutorFn/Entry
#include "console_module.h"               // ConsoleCommandSource, ConsoleRecordSink
#include "console_args.h"                 // ConsoleArgs, consoleArgsFind(), schema validation
#include "console_catalog.h"              // ConsoleCatalogEntry, consoleCatalogFindByName()
#include "robot_state.h"                  // robotState, robotStateMux, AuxLedEffect
#include "commanded_modes.h"              // commandedSetRcDebug()
#include "api_helpers.h"                  // parseBoolValue()
#include "rc_action_types.h"              // RobotActionId, ROBOT_ACTION_NONE, parseRobotActionId()
#include "api_actions.h"                  // ActionTestGuardResult, evaluateActionTestGuard()
#include "rc_input.h"                     // dispatchRcTriggerActionTest(), RcDispatchOutcome
#include "aux_led.h"                      // parseAuxLedEffect(), auxLedQueueSetColor/SetEffect()

// rc.action.toggle-debug: enabled=true|false, the same JSON field
// POST /api/rc/debug reads (src/web/api_rc.cpp).
static void consoleExecuteDirectRcDebug(uint32_t requestId, const char* operationName,
                                        const ConsoleArgs& args, ConsoleCommandSource source,
                                        const ConsoleRecordSink* sink) {
    const char* badKey = nullptr;
    for (size_t i = 0; i < args.count; ++i) {
        if (strcmp(args.items[i].key, "enabled") != 0) {
            badKey = args.items[i].key;
            break;
        }
    }
    if (badKey != nullptr) {
        consoleEmitArgFailure(requestId, operationName, badKey, CONSOLE_REASON_UNKNOWN_ARGUMENT, sink);
        return;
    }
    const char* raw = consoleArgsFind(args, "enabled");
    if (raw == nullptr) {
        consoleEmitArgFailure(requestId, operationName, "enabled", CONSOLE_REASON_MISSING_ARGUMENT,
                              sink);
        return;
    }
    bool enabled = false;
    if (!parseBoolValue(raw, &enabled)) {
        consoleEmitArgFailure(requestId, operationName, "enabled", CONSOLE_REASON_OUT_OF_RANGE, sink);
        return;
    }

    commandedSetRcDebug(enabled, consoleCommandSourceFor(source));

    if (sink->onRecordResult) {
        sink->onRecordResult(requestId, CONSOLE_STATUS_OK, CONSOLE_OUTCOME_APPLIED,
                            CONSOLE_REASON_NONE);
    }
}

// rc.action.test-bindable: token=<rc-token>, e.g. "arm1_toggle" - dispatches
// ANY RC-bindable action by its RC token (parseRobotActionId()'s grammar,
// include/rc_action_types.h - NOT the canonical console name
// consoleFindRobotActionId() resolves elsewhere, src/console/
// console_module.cpp) through the SAME guard and dispatch core POST
// /api/actions/test uses (handleActionsTestPost(), src/web/api_actions.cpp) -
// reused verbatim, not duplicated. Unlike consoleExecuteAction() (src/console/
// console_module.cpp), this never carves out the two Marcduino payload-needing
// targets: REST's /api/actions/test never accepted a payload either ("" is
// always what it dispatches), so "no widening" means this generic by-token
// tester does not gain one just because the Console's OWN typed dispatch of
// dome.action.marcduino-sequence/-command elsewhere does.
static void consoleExecuteDirectTestBindable(uint32_t requestId, const char* operationName,
                                             const ConsoleArgs& args, ConsoleCommandSource source,
                                             const ConsoleRecordSink* sink) {
    const char* badKey = nullptr;
    for (size_t i = 0; i < args.count; ++i) {
        if (strcmp(args.items[i].key, "token") != 0) {
            badKey = args.items[i].key;
            break;
        }
    }
    if (badKey != nullptr) {
        consoleEmitArgFailure(requestId, operationName, badKey, CONSOLE_REASON_UNKNOWN_ARGUMENT, sink);
        return;
    }
    const char* token = consoleArgsFind(args, "token");
    if (token == nullptr || token[0] == '\0') {
        consoleEmitArgFailure(requestId, operationName, "token", CONSOLE_REASON_MISSING_ARGUMENT, sink);
        return;
    }

    RobotActionId target = ROBOT_ACTION_NONE;
    if (!parseRobotActionId(token, &target)) {
        consoleEmitArgFailure(requestId, operationName, "token", CONSOLE_REASON_OUT_OF_RANGE, sink);
        return;
    }

    bool webControlEnabled = false;
    taskENTER_CRITICAL(&robotStateMux);
    webControlEnabled = robotState.webControlEnabled;
    taskEXIT_CRITICAL(&robotStateMux);

    ActionTestGuardResult guard = evaluateActionTestGuard(target, webControlEnabled);
    if (guard != ACTION_TEST_ALLOWED) {
        consoleAnswerActionGuardResult(requestId, guard, target, sink);
        return;
    }

    RcDispatchOutcome dispatchOutcome =
        dispatchRcTriggerActionTest(target, "", true, consoleCommandSourceFor(source));
    ConsoleReason reason = CONSOLE_REASON_NONE;
    ConsoleOutcome outcome = consoleMapDispatchOutcome(dispatchOutcome, &reason);
    ConsoleStatus status = (outcome == CONSOLE_OUTCOME_QUEUED) ? CONSOLE_STATUS_OK : CONSOLE_STATUS_ERR;
    if (sink->onRecordResult) {
        sink->onRecordResult(requestId, status, outcome, reason);
    }
}

// aux.action.led-color: r=/g=/b=<0..255> - the same auxLedQueueSetColor()
// call handleAuxLedColorPost() makes (src/web/api_aux_led.cpp), reused
// verbatim (a single complete queue call, no persist - no extraction
// needed). The two refusal reasons handleAuxLedColorPost()'s own
// sendAuxLedQueueRefusal() distinguishes - strip unavailable vs. queue full -
// are reproduced the same way: robotState.auxLed.available/pin read
// directly (isAuxLedAvailable() is private to api_aux_led.cpp's anonymous
// namespace), matching the drive.action.move precedent (include/
// console_direct_action_drive.h) of reading RobotState directly rather than
// exporting a REST-private helper. "Unavailable" (pin==0, no strip
// configured) maps to CONSOLE_REASON_COMPONENT_DISABLED - "the owning
// Component Toggle is off" (docs/console-protocol.md s.6), the exact fit for
// an AUX LED pin unset via aux.config.led-pin.
static bool consoleAuxLedAvailable() {
    bool available = false;
    uint8_t pin = 0;
    taskENTER_CRITICAL(&robotStateMux);
    available = robotState.auxLed.available;
    pin = robotState.auxLed.pin;
    taskEXIT_CRITICAL(&robotStateMux);
    return available && pin != 0;
}

static void consoleAnswerAuxLedRefusal(uint32_t requestId, const ConsoleRecordSink* sink) {
    if (!consoleAuxLedAvailable()) {
        if (sink->onRecordResult) {
            sink->onRecordResult(requestId, CONSOLE_STATUS_ERR, CONSOLE_OUTCOME_UNAVAILABLE,
                                CONSOLE_REASON_COMPONENT_DISABLED);
        }
        return;
    }
    if (sink->onRecordResult) {
        sink->onRecordResult(requestId, CONSOLE_STATUS_ERR, CONSOLE_OUTCOME_QUEUE_FULL,
                            CONSOLE_REASON_QUEUE_FULL);
    }
}

static void consoleExecuteAuxLedColor(uint32_t requestId, const char* operationName,
                                      const ConsoleArgs& args, ConsoleCommandSource source,
                                      const ConsoleRecordSink* sink) {
    const ConsoleCatalogEntry* entry = consoleCatalogFindByName(operationName);
    char badKey[40] = {};
    ConsoleArgSchemaStatus schemaStatus = consoleValidateArgsAgainstSchema(
        entry != nullptr ? entry->params : nullptr, args, badKey, sizeof(badKey));
    if (schemaStatus != CONSOLE_ARG_SCHEMA_OK) {
        ConsoleReason reason = (schemaStatus == CONSOLE_ARG_SCHEMA_UNKNOWN_KEY)
                                   ? CONSOLE_REASON_UNKNOWN_ARGUMENT
                               : (schemaStatus == CONSOLE_ARG_SCHEMA_MISSING_REQUIRED)
                                   ? CONSOLE_REASON_MISSING_ARGUMENT
                                   : CONSOLE_REASON_OUT_OF_RANGE;
        consoleEmitArgFailure(requestId, operationName, badKey, reason, sink);
        return;
    }

    long rgb[3] = {0, 0, 0};
    const char* keys[3] = {"r", "g", "b"};
    for (int i = 0; i < 3; ++i) {
        char* end = nullptr;
        rgb[i] = strtol(consoleArgsFind(args, keys[i]), &end, 10);
        if (*end != '\0' || rgb[i] < 0 || rgb[i] > 255) {
            consoleEmitArgFailure(requestId, operationName, keys[i], CONSOLE_REASON_OUT_OF_RANGE, sink);
            return;
        }
    }

    if (!auxLedQueueSetColor((uint8_t)rgb[0], (uint8_t)rgb[1], (uint8_t)rgb[2],
                             consoleCommandSourceFor(source))) {
        consoleAnswerAuxLedRefusal(requestId, sink);
        return;
    }

    if (sink->onRecordResult) {
        sink->onRecordResult(requestId, CONSOLE_STATUS_OK, CONSOLE_OUTCOME_QUEUED, CONSOLE_REASON_NONE);
    }
}

// aux.action.led-effect: effect=<off|solid|blink|pulse> - the same
// auxLedQueueSetEffect() call handleAuxLedEffectPost() makes (src/web/
// api_aux_led.cpp). Bypasses consoleValidateArgsAgainstSchema()'s enum check
// for this one param: tools/generate_console_catalog.py's YAML loader reads
// the registry's bare `off` value as the Python boolean False (YAML 1.1's
// implicit off/on/yes/no typing), so the generated
// g_enum_aux_action_led_effect_effect[] array literally contains the string
// "False" where "off" belongs (src/console/console_catalog.cpp) - a
// registry-generator defect, reported on the ticket rather than papered over
// by accepting the wrong literal here or by regenerating the catalog (that
// regeneration reshuffles every subsequent help_offset in the same
// generated file, out of this slice). parseAuxLedEffect() (include/
// aux_led.h) is the SAME validator handleAuxLedEffectPost() calls, reused
// verbatim instead - the "hardcode the one schema this operation needs"
// precedent the two Marcduino targets already set (consoleExecuteAction(),
// src/console/console_module.cpp).
static void consoleExecuteAuxLedEffect(uint32_t requestId, const char* operationName,
                                       const ConsoleArgs& args, ConsoleCommandSource source,
                                       const ConsoleRecordSink* sink) {
    const char* badKey = nullptr;
    for (size_t i = 0; i < args.count; ++i) {
        if (strcmp(args.items[i].key, "effect") != 0) {
            badKey = args.items[i].key;
            break;
        }
    }
    if (badKey != nullptr) {
        consoleEmitArgFailure(requestId, operationName, badKey, CONSOLE_REASON_UNKNOWN_ARGUMENT, sink);
        return;
    }
    const char* raw = consoleArgsFind(args, "effect");
    if (raw == nullptr) {
        consoleEmitArgFailure(requestId, operationName, "effect", CONSOLE_REASON_MISSING_ARGUMENT, sink);
        return;
    }
    AuxLedEffect effect = AUX_LED_EFFECT_OFF;
    if (!parseAuxLedEffect(raw, &effect)) {
        consoleEmitArgFailure(requestId, operationName, "effect", CONSOLE_REASON_OUT_OF_RANGE, sink);
        return;
    }

    if (!auxLedQueueSetEffect(effect, consoleCommandSourceFor(source))) {
        consoleAnswerAuxLedRefusal(requestId, sink);
        return;
    }

    if (sink->onRecordResult) {
        sink->onRecordResult(requestId, CONSOLE_STATUS_OK, CONSOLE_OUTCOME_QUEUED, CONSOLE_REASON_NONE);
    }
}

static const ConsoleDirectActionExecutorEntry g_auxRcDirectActionExecutors[] = {
    {"rc.action.toggle-debug", consoleExecuteDirectRcDebug},
    {"rc.action.test-bindable", consoleExecuteDirectTestBindable},
    {"aux.action.led-color", consoleExecuteAuxLedColor},
    {"aux.action.led-effect", consoleExecuteAuxLedEffect},
};
static const size_t kAuxRcDirectActionExecutorCount =
    sizeof(g_auxRcDirectActionExecutors) / sizeof(g_auxRcDirectActionExecutors[0]);
