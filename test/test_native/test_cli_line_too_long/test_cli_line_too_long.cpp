/**
 * Test: embedded-cli Patch 7 - explicit line-too-long
 * (lib/embedded-cli/VENDORED.md "Patch 7: Explicit Line-Too-Long")
 *
 * The defect: upstream's onCharInput() returns silently once the fixed
 * command buffer is full ("ignore input after reaching limit"), and Enter
 * then dispatches whatever fit. An operator who types past the limit gets a
 * DIFFERENT command executed from the one they typed, with no error - which
 * is what made docs/console-protocol.md s.1.3 ("a line longer than the input
 * buffer is discarded whole and answered with `invalid reason=line-too-long`;
 * a truncated command never executes") false on the serial adapter while true
 * on the browser one (src/web/api_console.cpp).
 *
 * Patch 7 closes it in the library, where cmdSize and cmdMaxSize live. A
 * listener-side byte count - the shape lib/embedded-cli/VENDORED.md Patch 2
 * originally sketched - cannot work here, and test_backspace_does_not_clear
 * below is the reason it cannot: the editor changes the stored line without a
 * one-to-one relationship to the bytes received (Backspace, Delete, Home/End,
 * arrow keys and history recall all do), so a listener counting received
 * bytes would refuse lines that fit and accept lines that did not.
 *
 * Every test drives the real vendored library through its public API only.
 * The command buffer is deliberately small (16 bytes, so 14 usable) to keep
 * the fixtures readable; the mechanism is size-independent.
 */

#include <unity.h>
#include <string.h>
#include <stdio.h>

extern "C" {
#include "embedded_cli.h"
}

// A small command buffer keeps the fixtures short. cmdMaxSize 16 leaves 14
// usable bytes: onCharInput accepts while cmdSize + 2 < cmdMaxSize.
static const uint16_t kCmdBufferSize = 16;
static const int kUsableChars = kCmdBufferSize - 2;

static char lastCommand[256];
static int commandCount = 0;
static int lineTooLongCount = 0;

static void onCommand(EmbeddedCli *cli, CliCommand *cmd) {
    (void)cli;
    snprintf(lastCommand, sizeof(lastCommand), "%s", cmd->name != nullptr ? cmd->name : "");
    commandCount++;
}

static void onLineTooLong(EmbeddedCli *cli) {
    (void)cli;
    lineTooLongCount++;
}

static void writeChar(EmbeddedCli *cli, char c) {
    (void)cli;
    (void)c;
}

void setUp(void) {
    commandCount = 0;
    lineTooLongCount = 0;
    lastCommand[0] = '\0';
}

void tearDown(void) {
}

// Feed one character and let the library process it, which is how
// src/tasks/console_task.cpp drives it: never more bytes than the rx FIFO can
// hold before the next embeddedCliProcess().
static void type(EmbeddedCli *cli, char c) {
    embeddedCliReceiveChar(cli, c);
    embeddedCliProcess(cli);
}

static void typeString(EmbeddedCli *cli, const char *s) {
    for (const char *p = s; *p != '\0'; ++p) {
        type(cli, *p);
    }
}

static void typeRepeated(EmbeddedCli *cli, char c, int count) {
    for (int i = 0; i < count; ++i) {
        type(cli, c);
    }
}

// One cli per test: the flag under test is per-instance state, and a shared
// instance would let one test's pending refusal reach the next one.
static EmbeddedCli *newCli(CLI_UINT *buffer, size_t bufferBytes, bool wireCallback) {
    EmbeddedCliConfig config = {
        .invitation = "> ",
        .rxBufferSize = 64,
        .cmdBufferSize = kCmdBufferSize,
        .historyBufferSize = 128,
        .maxBindingCount = 4,
        .cliBuffer = buffer,
        .cliBufferSize = (uint16_t)bufferBytes,
        .enableAutoComplete = false,
    };
    EmbeddedCli *cli = embeddedCliNew(&config);
    TEST_ASSERT_NOT_NULL(cli);
    cli->onCommand = onCommand;
    cli->writeChar = writeChar;
    if (wireCallback) {
        cli->onLineTooLong = onLineTooLong;
    }
    return cli;
}

/**
 * The line that fits still runs. Nothing about the patch narrows the accepted
 * length: the largest line the buffer holds is dispatched normally and no
 * refusal is reported.
 */
void test_line_at_the_limit_still_executes(void) {
    static CLI_UINT buffer[1024 / sizeof(CLI_UINT)];
    EmbeddedCli *cli = newCli(buffer, sizeof(buffer), true);

    typeRepeated(cli, 'a', kUsableChars);
    type(cli, '\r');

    TEST_ASSERT_EQUAL_INT_MESSAGE(1, commandCount, "a line that fits was not dispatched");
    TEST_ASSERT_EQUAL_INT_MESSAGE(kUsableChars, (int)strlen(lastCommand),
                                  "the dispatched line was not the full typed line");
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, lineTooLongCount,
                                  "a line that fits was reported as too long");

    embeddedCliFree(cli);
}

/**
 * The defect itself. One character past the limit and the whole line is
 * refused: onCommand never fires, and the caller is told exactly once.
 *
 * Reverting Patch 7's onCharInput flag turns this red with commandCount 1 and
 * lastCommand holding the 14-character prefix - which is precisely the
 * silently-truncated command the patch exists to stop from executing.
 */
void test_one_char_past_the_limit_refuses_the_whole_line(void) {
    static CLI_UINT buffer[1024 / sizeof(CLI_UINT)];
    EmbeddedCli *cli = newCli(buffer, sizeof(buffer), true);

    typeRepeated(cli, 'b', kUsableChars + 1);
    type(cli, '\r');

    TEST_ASSERT_EQUAL_INT_MESSAGE(0, commandCount,
                                  "a truncated line executed instead of being refused");
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, lineTooLongCount,
                                  "the caller was not told the line was too long");

    embeddedCliFree(cli);
}

/**
 * Many lost bytes are still one refusal, and CRLF is still one line ending -
 * so a paste of an overlong line produces exactly one answer on the wire, not
 * one per dropped byte and not two for the two ending characters.
 */
void test_many_lost_bytes_and_crlf_produce_exactly_one_refusal(void) {
    static CLI_UINT buffer[1024 / sizeof(CLI_UINT)];
    EmbeddedCli *cli = newCli(buffer, sizeof(buffer), true);

    typeRepeated(cli, 'c', kUsableChars + 40);
    type(cli, '\r');
    type(cli, '\n');

    TEST_ASSERT_EQUAL_INT_MESSAGE(0, commandCount, "a truncated line executed");
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, lineTooLongCount,
                                  "the refusal was not reported exactly once per line");

    embeddedCliFree(cli);
}

/**
 * The flag is sticky for the whole line, and Backspace does not lift it.
 *
 * This is a deliberate behaviour, not an oversight. The bytes past the buffer
 * were never stored, so what remains after backspacing is not a shortened
 * version of what the operator typed - it is a different line with a hole in
 * the middle of it. Running that is the same defect in a smaller disguise.
 *
 * It is also the reason Patch 7 lives in the library rather than in a
 * listener that counts received bytes: after this sequence the listener has
 * seen kUsableChars + 4 payload bytes and 3 Backspaces, and no arithmetic on
 * those counts distinguishes this line from one that fits.
 */
void test_backspace_does_not_clear_the_refusal(void) {
    static CLI_UINT buffer[1024 / sizeof(CLI_UINT)];
    EmbeddedCli *cli = newCli(buffer, sizeof(buffer), true);

    typeRepeated(cli, 'd', kUsableChars + 4);
    type(cli, '\b');
    type(cli, '\b');
    type(cli, '\b');
    type(cli, '\r');

    TEST_ASSERT_EQUAL_INT_MESSAGE(0, commandCount,
                                  "a line with a hole in it executed after backspacing");
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, lineTooLongCount, "the refusal was not reported");

    embeddedCliFree(cli);
}

/**
 * The refusal does not leak into the next line. After an overlong line is
 * refused, the very next line executes normally and is not itself refused -
 * the operator can simply retype a shorter command.
 */
void test_next_line_after_a_refusal_executes_normally(void) {
    static CLI_UINT buffer[1024 / sizeof(CLI_UINT)];
    EmbeddedCli *cli = newCli(buffer, sizeof(buffer), true);

    typeRepeated(cli, 'e', kUsableChars + 6);
    type(cli, '\r');
    TEST_ASSERT_EQUAL_INT(1, lineTooLongCount);

    typeString(cli, "help");
    type(cli, '\r');

    TEST_ASSERT_EQUAL_INT_MESSAGE(1, commandCount, "the line after a refusal did not execute");
    TEST_ASSERT_EQUAL_STRING_MESSAGE("help", lastCommand,
                                     "the line after a refusal was not the one typed");
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, lineTooLongCount,
                                  "the pending refusal leaked into the following line");

    embeddedCliFree(cli);
}

/**
 * A refused line is not recallable with Up-arrow. parseCommand() is where
 * historyPut() lives, and the refusal path replaces parseCommand() rather
 * than running before it, so a line nobody may execute is also a line nobody
 * can recall and execute later.
 *
 * History is read the way the operator reads it - press Up, look at what the
 * buffer now holds - because the ring internals are static to embedded_cli.c.
 * The recalled text is observed by submitting it: whatever Up-arrow loaded is
 * what onCommand then receives.
 */
void test_refused_line_is_not_in_history(void) {
    static CLI_UINT buffer[1024 / sizeof(CLI_UINT)];
    EmbeddedCli *cli = newCli(buffer, sizeof(buffer), true);

    // An accepted line first, so history is not simply empty.
    typeString(cli, "ok");
    type(cli, '\r');
    TEST_ASSERT_EQUAL_INT(1, commandCount);

    // Then an overlong one, refused.
    typeRepeated(cli, 'f', kUsableChars + 2);
    type(cli, '\r');
    TEST_ASSERT_EQUAL_INT(1, lineTooLongCount);

    // Up-arrow: the most recent RECALLABLE line must be the accepted one.
    type(cli, '\x1B');
    type(cli, '[');
    type(cli, 'A');
    type(cli, '\r');

    TEST_ASSERT_EQUAL_INT_MESSAGE(2, commandCount, "Up-arrow recalled nothing at all");
    TEST_ASSERT_EQUAL_STRING_MESSAGE("ok", lastCommand,
                                     "the refused line was recallable from history");

    embeddedCliFree(cli);
}

/**
 * The discard is unconditional; the callback is only the notification.
 *
 * With onLineTooLong left NULL - the state of every consumer that has not
 * opted in - an overlong line is still refused rather than executed. This is
 * the one behavioural difference from upstream that Patch 7 applies whether
 * or not a caller asks for it, and it is the half that matters for safety.
 */
void test_line_is_refused_even_with_no_callback_wired(void) {
    static CLI_UINT buffer[1024 / sizeof(CLI_UINT)];
    EmbeddedCli *cli = newCli(buffer, sizeof(buffer), false);

    typeRepeated(cli, 'g', kUsableChars + 3);
    type(cli, '\r');

    TEST_ASSERT_EQUAL_INT_MESSAGE(0, commandCount,
                                  "a truncated line executed when no callback was wired");

    embeddedCliFree(cli);
}

/**
 * The rx FIFO's own overflow is reported too.
 *
 * Feeding more bytes than the rx FIFO holds before calling
 * embeddedCliProcess() makes the library discard the partial command
 * (upstream's CLI_FLAG_OVERFLOW branch) - silently, before this patch. The
 * caller now learns about it on the next Enter instead of watching a line
 * disappear.
 *
 * src/tasks/console_task.cpp keeps this branch unreachable in production by
 * processing each byte as it is read, so this covers the library contract for
 * any caller that does not.
 */
void test_rx_fifo_overflow_is_reported_as_line_too_long(void) {
    static CLI_UINT buffer[1024 / sizeof(CLI_UINT)];
    EmbeddedCli *cli = newCli(buffer, sizeof(buffer), true);

    // rxBufferSize is 64; feed past it without processing in between.
    for (int i = 0; i < 100; ++i) {
        embeddedCliReceiveChar(cli, 'h');
    }
    embeddedCliProcess(cli);

    type(cli, '\r');

    TEST_ASSERT_EQUAL_INT_MESSAGE(0, commandCount, "a line the rx FIFO clipped executed");
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, lineTooLongCount,
                                  "an rx FIFO overflow was not reported to the caller");

    embeddedCliFree(cli);
}

/**
 * embeddedCliResetInput() abandons the pending refusal along with the line.
 *
 * include/console_host_attach.h calls it when a USB CDC host (re)attaches and
 * then queues a synthetic Enter to finish the reset. Without this clear, that
 * synthetic Enter would answer `line-too-long` about a line the newly
 * attached operator never typed and cannot see.
 */
void test_reset_input_clears_a_pending_refusal(void) {
    static CLI_UINT buffer[1024 / sizeof(CLI_UINT)];
    EmbeddedCli *cli = newCli(buffer, sizeof(buffer), true);

    typeRepeated(cli, 'i', kUsableChars + 5);

    embeddedCliResetInput(cli);
    type(cli, '\r');

    TEST_ASSERT_EQUAL_INT_MESSAGE(0, lineTooLongCount,
                                  "a reset line still produced a refusal on the next Enter");
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, commandCount, "the reset line executed");

    embeddedCliFree(cli);
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_line_at_the_limit_still_executes);
    RUN_TEST(test_one_char_past_the_limit_refuses_the_whole_line);
    RUN_TEST(test_many_lost_bytes_and_crlf_produce_exactly_one_refusal);
    RUN_TEST(test_backspace_does_not_clear_the_refusal);
    RUN_TEST(test_next_line_after_a_refusal_executes_normally);
    RUN_TEST(test_refused_line_is_not_in_history);
    RUN_TEST(test_line_is_refused_even_with_no_callback_wired);
    RUN_TEST(test_rx_fifo_overflow_is_reported_as_line_too_long);
    RUN_TEST(test_reset_input_clears_a_pending_refusal);
    return UNITY_END();
}
