/**
 * Test: embedded-cli Project-Help Ownership patch
 *
 * Verifies that the project's "help" command is not shadowed by the library's
 * internal help binding.
 *
 * Without the patch: The library binds "help" internally as the first binding,
 * so a project command literally named "help" would be shadowed. Any dispatch
 * to "help" would hit the library's built-in handler.
 *
 * With the patch: The library's internal help binding is disabled (set count to 0),
 * so "help" is free for the project to use.
 */

#include <unity.h>
#include <string.h>
#include <stdio.h>

extern "C" {
#include "embedded_cli.h"
}

// Test fixture: command tracking
static char lastCommand[256];
static char lastArgs[256];
static int commandCount = 0;
static bool projectHelpCalled = false;

// Mock onCommand callback - records the command and tracks if project help was called
static void onCommand(EmbeddedCli *cli, CliCommand *cmd) {
    snprintf(lastCommand, sizeof(lastCommand), "%s", cmd->name);
    snprintf(lastArgs, sizeof(lastArgs), "%s", cmd->args);
    commandCount++;

    // Check if this is the project's "help" command
    if (strcmp(cmd->name, "help") == 0) {
        projectHelpCalled = true;
    }
}

// Mock writeChar - ignores output
static void writeChar(EmbeddedCli *cli, char c) {
    (void)cli;
    (void)c;
}

// Test setup
void setUp(void) {
    commandCount = 0;
    projectHelpCalled = false;
    lastCommand[0] = '\0';
    lastArgs[0] = '\0';
}

void tearDown(void) {
}

/**
 * Test 1: Project "help" command is not shadowed
 *
 * Scenario:
 * 1. Create a CLI instance with no bindings (or with non-help bindings)
 * 2. Type "help"
 * 3. Press Enter
 * 4. Verify that onCommand is called with "help" command
 *
 * Without the patch, the library's internal help binding would handle this,
 * never calling onCommand with "help". With the patch, onCommand receives "help".
 */
void test_project_help_ownership_not_shadowed(void) {
    // Create CLI with no bindings
    EmbeddedCliConfig config = {
        .rxBufferSize = 256,
        .cmdBufferSize = 256,
        .historyBufferSize = 256,
        .enableAutoComplete = false,
        .invitation = "> ",
    };

    CLI_UINT cliBuffer[BYTES_TO_CLI_UINTS(256)];
    config.cliBuffer = cliBuffer;

    EmbeddedCli *cli = embeddedCliNew(&config);
    cli->onCommand = onCommand;
    cli->writeChar = writeChar;

    // Type "help"
    embeddedCliReceiveChar(cli, 'h');
    embeddedCliReceiveChar(cli, 'e');
    embeddedCliReceiveChar(cli, 'l');
    embeddedCliReceiveChar(cli, 'p');

    embeddedCliProcess(cli);

    // Press Enter
    embeddedCliReceiveChar(cli, '\r');
    embeddedCliProcess(cli);

    // Verify that onCommand was called with "help"
    TEST_ASSERT_EQUAL_INT(1, commandCount);
    TEST_ASSERT_EQUAL_STRING("help", lastCommand);
    TEST_ASSERT_TRUE(projectHelpCalled);

    embeddedCliFree(cli);
}

/**
 * Test 2: Project can bind "help" explicitly
 *
 * Scenario:
 * 1. Create a CLI instance
 * 2. Add a binding for "help" with project-owned help text
 * 3. Type "help topic"
 * 4. Press Enter
 * 5. Verify that the project's "help" binding is called
 *
 * This verifies that the project can fully own the help command.
 */
void test_project_help_ownership_explicit_binding(void) {
    CliCommandBinding helpBinding = {
        .name = "help",
        .help = "Project help command - shows available commands",
        .tokenizeArgs = false,
        .binding = NULL,  // Dispatch to onCommand
    };

    EmbeddedCliConfig config = {
        .rxBufferSize = 256,
        .cmdBufferSize = 256,
        .historyBufferSize = 256,
        .enableAutoComplete = false,
        .invitation = "> ",
    };

    CLI_UINT cliBuffer[BYTES_TO_CLI_UINTS(256)];
    config.cliBuffer = cliBuffer;

    EmbeddedCli *cli = embeddedCliNew(&config);
    cli->onCommand = onCommand;
    cli->writeChar = writeChar;

    // Add the project's help binding
    bool added = embeddedCliAddBinding(cli, helpBinding);
    TEST_ASSERT_TRUE(added);

    // Type "help system"
    const char *cmd = "help system";
    for (const char *p = cmd; *p; p++) {
        embeddedCliReceiveChar(cli, *p);
    }

    embeddedCliProcess(cli);

    // Press Enter
    embeddedCliReceiveChar(cli, '\r');
    embeddedCliProcess(cli);

    // Verify that onCommand was called with "help" and "system" args
    TEST_ASSERT_EQUAL_INT(1, commandCount);
    TEST_ASSERT_EQUAL_STRING("help", lastCommand);
    TEST_ASSERT_EQUAL_STRING("system", lastArgs);
    TEST_ASSERT_TRUE(projectHelpCalled);

    embeddedCliFree(cli);
}

/**
 * Test 3: Other commands still work (regression)
 *
 * Scenario:
 * 1. Create a CLI instance with a non-help binding
 * 2. Type a different command
 * 3. Press Enter
 * 4. Verify that the binding is called
 *
 * This is a regression check to ensure the patch doesn't break normal command dispatch.
 */
void test_project_help_ownership_other_commands_work(void) {
    CliCommandBinding statusBinding = {
        .name = "system.status",
        .help = "Show system status",
        .tokenizeArgs = false,
        .binding = NULL,
    };

    EmbeddedCliConfig config = {
        .rxBufferSize = 256,
        .cmdBufferSize = 256,
        .historyBufferSize = 256,
        .enableAutoComplete = false,
        .invitation = "> ",
    };

    CLI_UINT cliBuffer[BYTES_TO_CLI_UINTS(256)];
    config.cliBuffer = cliBuffer;

    EmbeddedCli *cli = embeddedCliNew(&config);
    cli->onCommand = onCommand;
    cli->writeChar = writeChar;

    // Add a non-help binding
    embeddedCliAddBinding(cli, statusBinding);

    // Type the command
    const char *cmd = "system.status";
    for (const char *p = cmd; *p; p++) {
        embeddedCliReceiveChar(cli, *p);
    }

    embeddedCliProcess(cli);

    // Press Enter
    embeddedCliReceiveChar(cli, '\r');
    embeddedCliProcess(cli);

    // Verify that the command was executed
    TEST_ASSERT_EQUAL_INT(1, commandCount);
    TEST_ASSERT_EQUAL_STRING("system.status", lastCommand);
    TEST_ASSERT_FALSE(projectHelpCalled);

    embeddedCliFree(cli);
}
