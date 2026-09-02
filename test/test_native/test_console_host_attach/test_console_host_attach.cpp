/**
 * Test: consoleResetInputForAttach (#260) - USB CDC attach resets the line
 * and reprints the invitation.
 *
 * Exercises include/console_host_attach.h against the REAL vendored
 * lib/embedded-cli source (not a mock of it), the same way
 * test_cli_safe_overflow.cpp and test_console_completion.cpp already do.
 *
 * Case 1 is the "prove it can fail" control: it calls ONLY
 * embeddedCliResetInput() - the naive fix a maintainer might reach for
 * instead of console_host_attach.h - and shows it corrupts the next typed
 * command instead of cleanly starting it. Cases 2-4 exercise the actual fix.
 */

#include <unity.h>
#include <string.h>
#include <stdio.h>

#include "console_host_attach.h"

// Test fixture: command tracking + captured output
static char lastCommand[512];
static char lastArgs[512];
static int commandCount = 0;
static char outputCapture[2048];
static size_t outputCaptureLen = 0;

static void onCommand(EmbeddedCli *cli, CliCommand *cmd) {
    (void)cli;
    snprintf(lastCommand, sizeof(lastCommand), "%s", cmd->name);
    snprintf(lastArgs, sizeof(lastArgs), "%s", cmd->args != nullptr ? cmd->args : "");
    commandCount++;
}

// Captures every character embedded-cli writes back (echo, invitation,
// line breaks) so tests can assert on what a re-attaching operator would
// actually see, not just internal state.
static void writeChar(EmbeddedCli *cli, char c) {
    (void)cli;
    if (outputCaptureLen + 1 < sizeof(outputCapture)) {
        outputCapture[outputCaptureLen++] = c;
        outputCapture[outputCaptureLen] = '\0';
    }
}

static EmbeddedCli *newTestCli(CLI_UINT *cliBuffer, size_t cliBufferBytes) {
    EmbeddedCliConfig config = {
        .invitation = "> ",
        .rxBufferSize = 256,
        .cmdBufferSize = 256,
        .historyBufferSize = 256,
        .maxBindingCount = 10,
        .cliBuffer = cliBuffer,
        .cliBufferSize = (uint16_t)cliBufferBytes,
        .enableAutoComplete = false,
    };
    EmbeddedCli *cli = embeddedCliNew(&config);
    TEST_ASSERT_NOT_NULL(cli);
    cli->onCommand = onCommand;
    cli->writeChar = writeChar;
    return cli;
}

static void feedString(EmbeddedCli *cli, const char *s) {
    for (const char *p = s; *p; p++) {
        embeddedCliReceiveChar(cli, *p);
    }
}

void setUp(void) {
    commandCount = 0;
    lastCommand[0] = '\0';
    lastArgs[0] = '\0';
    outputCapture[0] = '\0';
    outputCaptureLen = 0;
}

void tearDown(void) {
}

/**
 * Case 1 (control, expected to demonstrate the defect): embeddedCliResetInput()
 * alone, with no follow-up, leaves the command buffer's actual bytes and
 * cursor position stale. The very next real keystrokes are inserted at
 * strlen(cmdBuffer) - cursorPos (embedded_cli.c onCharInput), not at the
 * front of an empty line, so the resulting command is neither the stale
 * fragment nor the newly typed text - a silently corrupted first command,
 * exactly fault #3 in #260's issue body. If a future change collapses
 * console_host_attach.h back down to a bare embeddedCliResetInput() call,
 * this assertion is what should catch it.
 */
void test_reset_input_alone_corrupts_next_command(void) {
    static CLI_UINT cliBuffer[4096 / sizeof(CLI_UINT)];
    EmbeddedCli *cli = newTestCli(cliBuffer, sizeof(cliBuffer));

    // Operator was mid-line, typed "driv", then the host detached before
    // Enter.
    feedString(cli, "driv");
    embeddedCliProcess(cli);

    // The naive "fix": clear cmdSize only.
    embeddedCliResetInput(cli);

    // Operator reattaches and types a short, unrelated command.
    feedString(cli, "go");
    embeddedCliReceiveChar(cli, '\r');
    embeddedCliProcess(cli);

    TEST_ASSERT_EQUAL_INT(1, commandCount);
    // The defect: the submitted command is not the clean "go" the operator
    // typed. (What it actually is depends on embedded-cli's internal
    // cmdSize/parseCommand interaction with the stale buffer - the point of
    // this test is that it is wrong, not what specific garbage results.)
    TEST_ASSERT_NOT_EQUAL(0, strcmp(lastCommand, "go"));

    embeddedCliFree(cli);
}

/**
 * Case 2: consoleResetInputForAttach() leaves the command buffer genuinely
 * empty - the direct AC ("command buffer is reset on attach, so no fragment
 * from a previous session is inherited") - and the queued synthetic Enter
 * does not itself submit anything (no stray command execution from the
 * reset).
 */
void test_reset_for_attach_clears_buffer_without_executing(void) {
    static CLI_UINT cliBuffer[4096 / sizeof(CLI_UINT)];
    EmbeddedCli *cli = newTestCli(cliBuffer, sizeof(cliBuffer));

    feedString(cli, "driv");
    embeddedCliProcess(cli);
    TEST_ASSERT_EQUAL_INT(4, (int)strlen(embeddedCliGetCmdBuffer(cli)));

    consoleResetInputForAttach(cli);
    embeddedCliProcess(cli);  // drains the queued synthetic Enter

    TEST_ASSERT_EQUAL_INT(0, commandCount);
    TEST_ASSERT_EQUAL_STRING("", embeddedCliGetCmdBuffer(cli));

    embeddedCliFree(cli);
}

/**
 * Case 3: after the reset, the operator's real next command is submitted
 * exactly as typed - the corrupted-first-command fault from Case 1 does not
 * reproduce when console_host_attach.h's full reset is used.
 */
void test_reset_for_attach_next_command_is_clean(void) {
    static CLI_UINT cliBuffer[4096 / sizeof(CLI_UINT)];
    EmbeddedCli *cli = newTestCli(cliBuffer, sizeof(cliBuffer));

    feedString(cli, "driv");
    embeddedCliProcess(cli);

    consoleResetInputForAttach(cli);
    embeddedCliProcess(cli);

    feedString(cli, "go");
    embeddedCliReceiveChar(cli, '\r');
    embeddedCliProcess(cli);

    TEST_ASSERT_EQUAL_INT(1, commandCount);
    TEST_ASSERT_EQUAL_STRING("go", lastCommand);

    embeddedCliFree(cli);
}

/**
 * Case 4: the reset also clears a non-zero cursor position (an operator
 * mid-line-edit, not just mid-append, when the host detached) - the other
 * half of the "no fragment inherited" AC that Case 1's always-cursorPos-0
 * scenario does not exercise. Left-arrow (ESC [ D) moves the cursor into
 * the middle of "driv" before the reset.
 */
void test_reset_for_attach_clears_nonzero_cursor(void) {
    static CLI_UINT cliBuffer[4096 / sizeof(CLI_UINT)];
    EmbeddedCli *cli = newTestCli(cliBuffer, sizeof(cliBuffer));

    feedString(cli, "driv");
    embeddedCliProcess(cli);
    // Left-arrow twice: ESC [ D moves the cursor left one position each time
    // (embedded_cli.c onEscapedInput), landing cursorPos at 2 (mid-buffer).
    feedString(cli, "\x1b[D\x1b[D");
    embeddedCliProcess(cli);

    consoleResetInputForAttach(cli);
    embeddedCliProcess(cli);

    TEST_ASSERT_EQUAL_STRING("", embeddedCliGetCmdBuffer(cli));

    feedString(cli, "hi");
    embeddedCliReceiveChar(cli, '\r');
    embeddedCliProcess(cli);

    TEST_ASSERT_EQUAL_INT(1, commandCount);
    TEST_ASSERT_EQUAL_STRING("hi", lastCommand);

    embeddedCliFree(cli);
}

/**
 * Case 5: attaching with nothing ever typed (the ordinary boot / first-
 * attach case, and every subsequent attach where the operator did not leave
 * a fragment) is a safe no-op that still reprints the invitation and still
 * lets the next real command through cleanly.
 */
void test_reset_for_attach_on_empty_buffer_is_safe(void) {
    static CLI_UINT cliBuffer[4096 / sizeof(CLI_UINT)];
    EmbeddedCli *cli = newTestCli(cliBuffer, sizeof(cliBuffer));

    consoleResetInputForAttach(cli);
    embeddedCliProcess(cli);

    TEST_ASSERT_EQUAL_INT(0, commandCount);
    TEST_ASSERT_EQUAL_STRING("", embeddedCliGetCmdBuffer(cli));

    feedString(cli, "ok");
    embeddedCliReceiveChar(cli, '\r');
    embeddedCliProcess(cli);

    TEST_ASSERT_EQUAL_INT(1, commandCount);
    TEST_ASSERT_EQUAL_STRING("ok", lastCommand);

    embeddedCliFree(cli);
}

/**
 * Case 6: the queued synthetic Enter re-echoes the "> " invitation (AC:
 * "gets the invitation reprinted, without rebooting the board") - captured
 * via the writeChar seam the same way the real Serial transport receives it
 * in console_task.cpp.
 */
void test_reset_for_attach_reprints_invitation(void) {
    static CLI_UINT cliBuffer[4096 / sizeof(CLI_UINT)];
    EmbeddedCli *cli = newTestCli(cliBuffer, sizeof(cliBuffer));

    feedString(cli, "driv");
    embeddedCliProcess(cli);
    outputCaptureLen = 0;
    outputCapture[0] = '\0';

    consoleResetInputForAttach(cli);
    embeddedCliProcess(cli);

    TEST_ASSERT_NOT_NULL(strstr(outputCapture, "> "));

    embeddedCliFree(cli);
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_reset_input_alone_corrupts_next_command);
    RUN_TEST(test_reset_for_attach_clears_buffer_without_executing);
    RUN_TEST(test_reset_for_attach_next_command_is_clean);
    RUN_TEST(test_reset_for_attach_clears_nonzero_cursor);
    RUN_TEST(test_reset_for_attach_on_empty_buffer_is_safe);
    RUN_TEST(test_reset_for_attach_reprints_invitation);
    return UNITY_END();
}
