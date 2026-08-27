/**
 * Test: embedded-cli Safe Enter patch
 *
 * Verifies that pressing Enter on a unique prefix does NOT auto-expand to a
 * bound command. The typed text is executed as-is.
 *
 * Without the patch: Typing "so" + Enter on a system where "sound_rand_general"
 * is bound would auto-expand and execute "sound_rand_general".
 *
 * With the patch: Typing "so" + Enter executes the literal "so" command,
 * which the dispatcher can reject as unknown.
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

// Mock onCommand callback - records the command name exactly as typed
static void onCommand(EmbeddedCli *cli, CliCommand *cmd) {
    snprintf(lastCommand, sizeof(lastCommand), "%s", cmd->name);
    snprintf(lastArgs, sizeof(lastArgs), "%s", cmd->args);
    commandCount++;
}

// Mock writeChar - ignores output
static void writeChar(EmbeddedCli *cli, char c) {
    (void)cli;
    (void)c;
}

// Test setup
void setUp(void) {
    commandCount = 0;
    lastCommand[0] = '\0';
    lastArgs[0] = '\0';
}

void tearDown(void) {
}

/**
 * Test 1: Unique prefix + Enter executes the typed text, not the expansion
 *
 * Scenario:
 * 1. Bind "sound_rand_general" with help text
 * 2. Type "so" (unique prefix of the binding)
 * 3. Press Enter
 * 4. Verify that the command executed is "so", not "sound_rand_general"
 *
 * Without the patch, "sound_rand_general" would be executed.
 */
void test_safe_enter_unique_prefix_not_auto_expanded(void) {
    // Configure CLI with one binding
    CliCommandBinding bindings[] = {
        {
            .name = "sound_rand_general",
            .help = "Play random general sound",
            .tokenizeArgs = false,
            .binding = NULL,  // NULL binding: command goes to onCommand for dispatch
        },
    };

    EmbeddedCliConfig config = {
        .invitation = "> ",
        .rxBufferSize = 256,
        .cmdBufferSize = 256,
        .historyBufferSize = 256,
        .maxBindingCount = 10,
        .enableAutoComplete = false,  // Live autocomplete OFF - but Enter still calls onAutocompleteRequest without patch
    };

    // Allocate static CLI buffer
    CLI_UINT cliBuffer[BYTES_TO_CLI_UINTS(256)];
    config.cliBuffer = cliBuffer;

    // Create CLI instance
    EmbeddedCli *cli = embeddedCliNew(&config);
    cli->onCommand = onCommand;
    cli->writeChar = writeChar;
    cli->appContext = (void *)1;  // Dummy context

    // Add binding
    embeddedCliAddBinding(cli, &bindings[0]);

    // Type "so" (unique prefix for "sound_rand_general")
    embeddedCliReceiveChar(cli, 's');
    embeddedCliReceiveChar(cli, 'o');

    // Process to ensure command buffer has "so"
    embeddedCliProcess(cli);

    // Press Enter - this triggers onAutocompleteRequest in the original code
    embeddedCliReceiveChar(cli, '\r');

    // Process - this should execute the typed command "so", not the auto-expanded "sound_rand_general"
    embeddedCliProcess(cli);

    // Verify that the command executed was "so", not "sound_rand_general"
    TEST_ASSERT_EQUAL_INT(1, commandCount);
    TEST_ASSERT_EQUAL_STRING("so", lastCommand);

    embeddedCliFree(cli);
}

/**
 * Test 2: Exact bound command name + Enter executes correctly (regression check)
 *
 * Scenario:
 * 1. Bind "sound_rand_general"
 * 2. Type the exact name "sound_rand_general"
 * 3. Press Enter
 * 4. Verify that the command executed is "sound_rand_general"
 *
 * This is a regression check to ensure the patch doesn't break normal execution.
 */
void test_safe_enter_exact_match_executes(void) {
    CliCommandBinding bindings[] = {
        {
            .name = "sound_rand_general",
            .help = "Play random general sound",
            .tokenizeArgs = false,
            .binding = NULL,
        },
    };

    EmbeddedCliConfig config = {
        .invitation = "> ",
        .rxBufferSize = 256,
        .cmdBufferSize = 256,
        .historyBufferSize = 256,
        .maxBindingCount = 10,
        .enableAutoComplete = false,
    };

    CLI_UINT cliBuffer[BYTES_TO_CLI_UINTS(256)];
    config.cliBuffer = cliBuffer;

    EmbeddedCli *cli = embeddedCliNew(&config);
    cli->onCommand = onCommand;
    cli->writeChar = writeChar;

    embeddedCliAddBinding(cli, &bindings[0]);

    // Type the exact bound name
    const char *cmd = "sound_rand_general";
    for (const char *p = cmd; *p; p++) {
        embeddedCliReceiveChar(cli, *p);
    }

    embeddedCliProcess(cli);

    // Press Enter
    embeddedCliReceiveChar(cli, '\r');
    embeddedCliProcess(cli);

    // Verify that the command executed is exactly "sound_rand_general"
    TEST_ASSERT_EQUAL_INT(1, commandCount);
    TEST_ASSERT_EQUAL_STRING("sound_rand_general", lastCommand);

    embeddedCliFree(cli);
}

/**
 * Test 3: Unknown command + Enter is passed to onCommand
 *
 * Scenario:
 * 1. Type "unknown_command"
 * 2. Press Enter
 * 3. Verify that onCommand is called with the exact typed text
 *
 * This verifies that unbound commands are dispatched as-is.
 */
void test_safe_enter_unknown_command_dispatched_as_is(void) {
    EmbeddedCliConfig config = {
        .invitation = "> ",
        .rxBufferSize = 256,
        .cmdBufferSize = 256,
        .historyBufferSize = 256,
        .maxBindingCount = 10,
        .enableAutoComplete = false,
    };

    CLI_UINT cliBuffer[BYTES_TO_CLI_UINTS(256)];
    config.cliBuffer = cliBuffer;

    EmbeddedCli *cli = embeddedCliNew(&config);
    cli->onCommand = onCommand;
    cli->writeChar = writeChar;

    // Type an unknown command
    const char *cmd = "unknown_command";
    for (const char *p = cmd; *p; p++) {
        embeddedCliReceiveChar(cli, *p);
    }

    embeddedCliProcess(cli);

    // Press Enter
    embeddedCliReceiveChar(cli, '\r');
    embeddedCliProcess(cli);

    // Verify that onCommand was called with "unknown_command"
    TEST_ASSERT_EQUAL_INT(1, commandCount);
    TEST_ASSERT_EQUAL_STRING("unknown_command", lastCommand);

    embeddedCliFree(cli);
}
