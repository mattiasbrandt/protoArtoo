// =============================================================================
// src/console/console_cli_line.cpp
//
// See include/console_cli_line.h for the contract and the scope fence.
// =============================================================================

#include "console_cli_line.h"

#include <string.h>
#include <stdio.h>

const char* consoleBuildCommandLine(const char* commandName, const char* args, char* outBuf,
                                     size_t outBufSize) {
    if (commandName == nullptr) {
        return nullptr;
    }

    // Only these two meta-commands reconstruct args into the command line -
    // see the scope fence in console_cli_line.h.
    bool isMetaCommand =
        (strcmp(commandName, "help") == 0) || (strcmp(commandName, "operations") == 0);

    if (isMetaCommand && args != nullptr && args[0] != '\0' && outBuf != nullptr &&
        outBufSize > 0) {
        snprintf(outBuf, outBufSize, "%s %s", commandName, args);
        return outBuf;
    }

    // No reconstruction: not a meta-command, or a meta-command with no
    // arguments (both "help" and "operations" have valid no-argument forms).
    return commandName;
}
