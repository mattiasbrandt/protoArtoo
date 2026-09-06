// =============================================================================
// include/console_direct_action_types.h
//
// Shared shape for the Controller Console's "direct action" dispatch (#226
// criterion 4, #222, #257): a plain synchronous C function taking validated
// Console arguments, resolved by canonical operation name before
// ACTION_REGISTRY[]/RobotActionId lookup in consoleExecuteCommand()'s
// CONSOLE_OP_ACTION case (src/console/console_module.cpp) - never the queued
// RC dispatch core the ACTION_REGISTRY[]-resolved executors use.
//
// Type-only header (no functions, no data) so it carries zero linkage/ABI
// footprint of its own - every per-domain direct-action header under this
// directory (console_direct_action_system.h, _drive.h, _sound.h, _aux_rc.h,
// _servo.h) and console_module.cpp itself include this one for the row shape,
// none of them for an implementation.
// =============================================================================
#pragma once

#include <stdint.h>

#include "console_module.h"  // ConsoleCommandSource, ConsoleRecordSink
#include "console_args.h"    // ConsoleArgs

typedef void (*ConsoleDirectActionExecutorFn)(uint32_t requestId, const char* operationName,
                                              const ConsoleArgs& args, ConsoleCommandSource source,
                                              const ConsoleRecordSink* sink);

struct ConsoleDirectActionExecutorEntry {
    const char* operationName;
    ConsoleDirectActionExecutorFn executor;
};
