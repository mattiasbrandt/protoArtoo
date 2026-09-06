// =============================================================================
// src/console/console_cli_line.cpp
//
// See include/console_cli_line.h for the contract (widened by #221 to
// reconstruct every command, not just "help"/"operations").
// =============================================================================

#include "console_cli_line.h"

#include <string.h>
#include <stdio.h>

const char* consoleBuildCommandLine(const char* commandName, const char* args, char* outBuf,
                                     size_t outBufSize) {
    if (commandName == nullptr) {
        return nullptr;
    }

    // #221 widened this: every command reconstructs, not just "help" and
    // "operations" (see include/console_cli_line.h for why this file stays
    // alive rather than being deleted). consoleExecuteCommand() splits the
    // combined line right back into a bare name + raw argument remainder as
    // its own first step (consoleSplitCommandLine(), include/console_args.h),
    // so this only needs to get both adapters to the same "name args" shape
    // - it does no argument-aware parsing of its own.
    if (args != nullptr && args[0] != '\0' && outBuf != nullptr && outBufSize > 0) {
        snprintf(outBuf, outBufSize, "%s %s", commandName, args);
        return outBuf;
    }

    // No reconstruction needed: no arguments were typed.
    return commandName;
}
