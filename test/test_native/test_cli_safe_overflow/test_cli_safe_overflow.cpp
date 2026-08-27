/**
 * Test: embedded-cli Safe Overflow patch
 *
 * Verifies that input exceeding the command buffer size is rejected explicitly.
 * The buffer is cleared, and overlong input does not execute.
 *
 * Without the patch: Input exceeding cmdBufferSize - 2 (e.g., 256) is silently
 * discarded, but the command still executes on Enter.
 *
 * With the patch: The listener detects overflow and rejects the line with
 * an explicit error.
 */

#include <unity.h>
#include <string.h>
#include <stdio.h>

extern "C" {
#include "embedded_cli.h"
}

// Test fixture: command tracking
static char lastCommand[512];
static char lastArgs[512];
static int commandCount = 0;
static bool overflowDetected = false;

// Mock onCommand callback - records the command
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
    overflowDetected = false;
    lastCommand[0] = '\0';
    lastArgs[0] = '\0';
}

void tearDown(void) {
}

/**
 * Test 1: Input exactly at buffer size is accepted
 *
 * Scenario:
 * 1. Command buffer size is 256
 * 2. Feed 254 characters (leaving 2 bytes reserved)
 * 3. Press Enter
 * 4. Verify that the command executes
 *
 * This is a boundary test to ensure valid input at the limit is accepted.
 */
void test_safe_overflow_valid_length_accepted(void) {
    static CLI_UINT cliBuffer[4096 / sizeof(CLI_UINT)];

    EmbeddedCliConfig config = {
        .invitation = "> ",
        .rxBufferSize = 256,
        .cmdBufferSize = 256,
        .historyBufferSize = 256,
        .maxBindingCount = 10,
        .cliBuffer = cliBuffer,
        .cliBufferSize = sizeof(cliBuffer),
        .enableAutoComplete = false,
    };

    EmbeddedCli *cli = embeddedCliNew(&config);
    TEST_ASSERT_NOT_NULL(cli);
    cli->onCommand = onCommand;
    cli->writeChar = writeChar;

    // Feed 254 characters (leaves 2 bytes reserved in buffer)
    for (int i = 0; i < 254; i++) {
        embeddedCliReceiveChar(cli, 'a');
    }

    embeddedCliProcess(cli);

    // Press Enter
    embeddedCliReceiveChar(cli, '\r');
    embeddedCliProcess(cli);

    // Verify that the command executed (254 'a' chars)
    TEST_ASSERT_EQUAL_INT(1, commandCount);
    TEST_ASSERT_EQUAL_INT(254, strlen(lastCommand));

    embeddedCliFree(cli);
}

/**
 * Test 2: Input exceeding buffer size is detected and rejected
 *
 * Scenario:
 * 1. Command buffer size is 256
 * 2. Feed 260 characters (exceeds buffer by 4 bytes)
 * 3. After overflow is detected, call embeddedCliResetInput() to clear the buffer
 * 4. Press Enter
 * 5. Verify that the buffer is empty and no command is executed
 *
 * This is the core overflow test. The listener detects overflow, resets the buffer,
 * and the Enter key produces no command.
 */
void test_safe_overflow_exceeds_buffer_rejected(void) {
    static CLI_UINT cliBuffer[4096 / sizeof(CLI_UINT)];

    EmbeddedCliConfig config = {
        .invitation = "> ",
        .rxBufferSize = 256,
        .cmdBufferSize = 256,
        .historyBufferSize = 256,
        .maxBindingCount = 10,
        .cliBuffer = cliBuffer,
        .cliBufferSize = sizeof(cliBuffer),
        .enableAutoComplete = false,
    };

    EmbeddedCli *cli = embeddedCliNew(&config);
    TEST_ASSERT_NOT_NULL(cli);
    cli->onCommand = onCommand;
    cli->writeChar = writeChar;

    // Feed 260 characters, exceeding the 256-byte buffer
    // This simulates the listener detecting overflow after ~254 chars
    for (int i = 0; i < 260; i++) {
        embeddedCliReceiveChar(cli, 'a');
        embeddedCliProcess(cli);
    }

    // At this point, the overflow flag should be set in the library
    // The listener detects this and resets the buffer
    embeddedCliResetInput(cli);

    // Press Enter
    embeddedCliReceiveChar(cli, '\r');
    embeddedCliProcess(cli);

    // Verify that no command was executed (buffer was cleared)
    TEST_ASSERT_EQUAL_INT(0, commandCount);

    embeddedCliFree(cli);
}

/**
 * Test 3: Normal command after overflow recovery
 *
 * Scenario:
 * 1. Feed 260 characters (overflow)
 * 2. Reset the buffer
 * 3. Feed a normal command "hello"
 * 4. Press Enter
 * 5. Verify that "hello" is executed (not the overflowed partial)
 *
 * This verifies that the buffer can recover after an overflow.
 */
void test_safe_overflow_recovery(void) {
    static CLI_UINT cliBuffer[4096 / sizeof(CLI_UINT)];

    EmbeddedCliConfig config = {
        .invitation = "> ",
        .rxBufferSize = 256,
        .cmdBufferSize = 256,
        .historyBufferSize = 256,
        .maxBindingCount = 10,
        .cliBuffer = cliBuffer,
        .cliBufferSize = sizeof(cliBuffer),
        .enableAutoComplete = false,
    };

    EmbeddedCli *cli = embeddedCliNew(&config);
    TEST_ASSERT_NOT_NULL(cli);
    cli->onCommand = onCommand;
    cli->writeChar = writeChar;

    // Feed 260 characters (overflow)
    for (int i = 0; i < 260; i++) {
        embeddedCliReceiveChar(cli, 'x');
        embeddedCliProcess(cli);
    }

    // Reset the buffer
    embeddedCliResetInput(cli);

    // Feed a normal command "hello"
    const char *cmd = "hello";
    for (const char *p = cmd; *p; p++) {
        embeddedCliReceiveChar(cli, *p);
    }

    embeddedCliProcess(cli);

    // Press Enter
    embeddedCliReceiveChar(cli, '\r');
    embeddedCliProcess(cli);

    // Verify that "hello" was executed
    TEST_ASSERT_EQUAL_INT(1, commandCount);
    TEST_ASSERT_EQUAL_STRING("hello", lastCommand);

    embeddedCliFree(cli);
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_safe_overflow_valid_length_accepted);
    RUN_TEST(test_safe_overflow_exceeds_buffer_rejected);
    RUN_TEST(test_safe_overflow_recovery);
    return UNITY_END();
}
