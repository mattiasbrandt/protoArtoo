/**
 * Test: embedded-cli Patch 2's embeddedCliResetInput() primitive
 * (lib/embedded-cli/VENDORED.md "Patch 2: Safe Overflow")
 *
 * SCOPE, corrected 2026-09-03 (#262). This file covers what Patch 2 actually
 * shipped: the public reset primitive. Each test below calls
 * embeddedCliResetInput() from the test body, because that is where the call
 * came from - the "listener counts bytes and resets on overflow" half Patch 2
 * described was never written, so nothing in production drove this path for an
 * overflow, and the file's original header ("the listener detects overflow and
 * rejects the line") described an intention rather than the tree.
 *
 * The product behaviour those words promised - an over-length line refused
 * whole, answered with `line-too-long`, never executed as the prefix that fit -
 * is Patch 7's, and lives in test/test_native/test_cli_line_too_long/ (the
 * library half) and test/test_native/test_console_line_overflow/ (the record
 * the serial adapter emits in response). What remains here is still worth
 * keeping: embeddedCliResetInput() is a shipped public function with a live
 * production caller in include/console_host_attach.h, and these three cases
 * pin its contract.
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
 * Test 2: a reset between the overlong input and Enter yields no command
 *
 * Scenario:
 * 1. Command buffer size is 256
 * 2. Feed 260 characters (4 more than the buffer can hold)
 * 3. Call embeddedCliResetInput() - standing in for the overflow listener
 *    Patch 2 described and no production code ever became
 * 4. Press Enter
 * 5. Verify the buffer is empty and no command is executed
 *
 * What this pins is the primitive: after a reset, Enter submits nothing. It is
 * NOT evidence that the product refuses an over-length line, because nothing
 * in production makes this call for an overflow - Patch 7 refuses the line
 * inside the library instead, and test_cli_line_too_long.cpp covers that.
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

    // Feed 260 characters; the library stops storing at 254 and drops the rest
    for (int i = 0; i < 260; i++) {
        embeddedCliReceiveChar(cli, 'a');
        embeddedCliProcess(cli);
    }

    // Stand in for the listener Patch 2 described: clear the partial command.
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

    // Feed 260 characters (overflow) without processing
    for (int i = 0; i < 260; i++) {
        embeddedCliReceiveChar(cli, 'x');
    }

    // Process to detect overflow
    embeddedCliProcess(cli);

    // Reset the buffer
    embeddedCliResetInput(cli);

    // Reset state before testing recovery
    commandCount = 0;
    lastCommand[0] = '\0';

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

/**
 * Test 4 (#270): the reset leaves an EMPTY command buffer, not just a zero
 * count.
 *
 * embeddedCliResetInput() zeroed cmdSize and left the old line's bytes in
 * cmdBuffer. Every other mutator keeps strlen(cmdBuffer) == cmdSize, and
 * every insertion position the editor computes is
 * `strlen(cmdBuffer) - cursorPos` (onCharInput), so the next real keystroke
 * landed at the stale strlen() offset - past a fragment the operator cannot
 * see - and the line they then submitted was not the line they typed.
 *
 * Tests 2 and 3 above could not see this: both reach the reset through an rx
 * FIFO overflow, and embeddedCliProcess()'s own overflow discard already
 * clears cmdBuffer[0]. The live production caller does not - a host
 * (re)attach calls embeddedCliResetInput() directly on an ordinary,
 * mid-edited line (include/console_host_attach.h, #260), which is the P4
 * attach path.
 *
 * The cursor is moved left first, so this also pins the second half: with an
 * emptied buffer a non-zero cursorPos makes that subtraction underflow.
 */
void test_reset_clears_the_command_buffer_and_the_cursor(void) {
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

    // An ordinary half-typed line, with the cursor moved two characters left
    // (VT100 "\x1B[D" twice) - the state a cable pull can leave behind.
    const char *typed = "abcdef";
    for (const char *p = typed; *p; p++) {
        embeddedCliReceiveChar(cli, *p);
    }
    for (int i = 0; i < 2; i++) {
        embeddedCliReceiveChar(cli, 0x1B);
        embeddedCliReceiveChar(cli, '[');
        embeddedCliReceiveChar(cli, 'D');
    }
    embeddedCliProcess(cli);
    TEST_ASSERT_EQUAL_STRING_MESSAGE("abcdef", embeddedCliGetCmdBuffer(cli),
                                     "fixture did not leave the typed line in the buffer");

    embeddedCliResetInput(cli);

    TEST_ASSERT_EQUAL_STRING_MESSAGE(
        "", embeddedCliGetCmdBuffer(cli),
        "the reset left the old line's bytes in cmdBuffer; every insertion "
        "position the editor computes is strlen(cmdBuffer) - cursorPos");

    // The operator's next command must be exactly what they type.
    commandCount = 0;
    lastCommand[0] = '\0';
    const char *next = "hi";
    for (const char *p = next; *p; p++) {
        embeddedCliReceiveChar(cli, *p);
    }
    embeddedCliReceiveChar(cli, '\r');
    embeddedCliProcess(cli);

    TEST_ASSERT_EQUAL_INT_MESSAGE(1, commandCount, "the next line did not execute");
    TEST_ASSERT_EQUAL_STRING_MESSAGE("hi", lastCommand,
                                     "the first command after a reset was not what was typed");

    embeddedCliFree(cli);
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_safe_overflow_valid_length_accepted);
    RUN_TEST(test_safe_overflow_exceeds_buffer_rejected);
    RUN_TEST(test_safe_overflow_recovery);
    RUN_TEST(test_reset_clears_the_command_buffer_and_the_cursor);
    return UNITY_END();
}
