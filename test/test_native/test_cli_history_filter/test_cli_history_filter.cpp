/**
 * Test: embedded-cli History Filter callback patch (#227)
 *
 * Verifies EmbeddedCli::shouldStoreHistory: an optional predicate consulted
 * BEFORE a submitted line reaches the history ring, so a line the project
 * refuses can never be recalled with Up-arrow - see
 * lib/embedded-cli/VENDORED.md Patch 6 and the field doc in embedded_cli.h.
 *
 * This is a library-mechanism test (a plain substring predicate standing in
 * for a project rule), not a project-rule test:
 * include/console_write_exclusion.h and
 * test/test_native/test_console_write_exclusion/ own the question of WHICH
 * lines the Console refuses.
 *
 * History is read the way the operator reads it - by pressing Up and looking
 * at the command buffer - because that is the whole exposure being closed;
 * the ring's internals are static to embedded_cli.c and are not the contract.
 */

#include <unity.h>
#include <string.h>
#include <stdio.h>

extern "C" {
#include "embedded_cli.h"
}

static char lastCommand[256];
static int commandCount = 0;

// Every line the filter was asked about, in order, exactly as it received it.
static char filterSaw[8][256];
static int filterCalls = 0;

static void writeChar(EmbeddedCli *cli, char c) {
    (void)cli;
    (void)c;
}

static void onCommand(EmbeddedCli *cli, CliCommand *cmd) {
    (void)cli;
    snprintf(lastCommand, sizeof(lastCommand), "%s", cmd->name);
    ++commandCount;
}

// Stand-in for a project rule: refuse any line mentioning a secret key.
static bool shouldStoreHistory(EmbeddedCli *cli, const char *line) {
    (void)cli;
    if (filterCalls < (int)(sizeof(filterSaw) / sizeof(filterSaw[0]))) {
        snprintf(filterSaw[filterCalls], sizeof(filterSaw[0]), "%s", line);
    }
    ++filterCalls;
    return strstr(line, "sta-password") == NULL;
}

void setUp(void) {
    commandCount = 0;
    lastCommand[0] = '\0';
    filterCalls = 0;
    memset(filterSaw, 0, sizeof(filterSaw));
}

void tearDown(void) {}

static EmbeddedCli *makeCli(CLI_UINT *buffer, size_t bufferBytes, bool withFilter) {
    EmbeddedCliConfig config = {
        .invitation = "> ",
        .rxBufferSize = 256,
        .cmdBufferSize = 128,
        .historyBufferSize = 256,
        .maxBindingCount = 4,
        .cliBuffer = buffer,
        .cliBufferSize = (uint16_t)bufferBytes,
        .enableAutoComplete = false,
    };
    EmbeddedCli *cli = embeddedCliNew(&config);
    TEST_ASSERT_NOT_NULL(cli);
    cli->writeChar = writeChar;
    cli->onCommand = onCommand;
    if (withFilter) {
        cli->shouldStoreHistory = shouldStoreHistory;
    }
    return cli;
}

static void submit(EmbeddedCli *cli, const char *line) {
    for (const char *p = line; *p; ++p) {
        embeddedCliReceiveChar(cli, *p);
    }
    embeddedCliReceiveChar(cli, '\r');
    embeddedCliProcess(cli);
}

// Up-arrow is ESC [ A - the same three bytes a terminal sends.
static void pressUp(EmbeddedCli *cli) {
    embeddedCliReceiveChar(cli, 0x1B);
    embeddedCliReceiveChar(cli, '[');
    embeddedCliReceiveChar(cli, 'A');
    embeddedCliProcess(cli);
}

// -----------------------------------------------------------------------
// Test 1: no filter set - upstream behaviour, every line is stored. This is
// what makes the patch optional rather than a behaviour change for any other
// consumer of the library.
// -----------------------------------------------------------------------
void test_without_a_filter_every_line_is_stored(void) {
    static CLI_UINT buf[4096 / sizeof(CLI_UINT)];
    EmbeddedCli *cli = makeCli(buf, sizeof(buf), false);

    submit(cli, "first.command");
    submit(cli, "second.command");

    pressUp(cli);
    TEST_ASSERT_EQUAL_STRING("second.command", embeddedCliGetCmdBuffer(cli));
    pressUp(cli);
    TEST_ASSERT_EQUAL_STRING("first.command", embeddedCliGetCmdBuffer(cli));
    TEST_ASSERT_EQUAL_INT(0, filterCalls);

    embeddedCliFree(cli);
}

// -----------------------------------------------------------------------
// Test 2: a refused line never enters the ring. THE test this patch exists
// for: after submitting a refused line, Up must produce the last ACCEPTED
// line, and a further Up must not produce the refused one either.
// -----------------------------------------------------------------------
void test_refused_line_never_enters_history(void) {
    static CLI_UINT buf[4096 / sizeof(CLI_UINT)];
    EmbeddedCli *cli = makeCli(buf, sizeof(buf), true);

    submit(cli, "wifi.config.settings mode=client");
    submit(cli, "wifi.config.settings sta-password=hunter2");

    pressUp(cli);
    TEST_ASSERT_EQUAL_STRING("wifi.config.settings mode=client", embeddedCliGetCmdBuffer(cli));
    TEST_ASSERT_NULL(strstr(embeddedCliGetCmdBuffer(cli), "hunter2"));

    // Nothing older to reach: the refused line is not sitting behind the
    // accepted one either.
    pressUp(cli);
    TEST_ASSERT_EQUAL_STRING("wifi.config.settings mode=client", embeddedCliGetCmdBuffer(cli));

    embeddedCliFree(cli);
}

// -----------------------------------------------------------------------
// Test 3: the refused line still EXECUTES. The filter decides what history
// keeps, never what runs - the executor is what answers
// secret-not-settable, and it has to be reached to do so.
// -----------------------------------------------------------------------
void test_refused_line_still_executes(void) {
    static CLI_UINT buf[4096 / sizeof(CLI_UINT)];
    EmbeddedCli *cli = makeCli(buf, sizeof(buf), true);

    submit(cli, "wifi.config.settings sta-password=hunter2");

    TEST_ASSERT_EQUAL_INT(1, commandCount);
    TEST_ASSERT_EQUAL_STRING("wifi.config.settings", lastCommand);

    embeddedCliFree(cli);
}

// -----------------------------------------------------------------------
// Test 4: the filter sees the whole line as typed, before the buffer is
// split into name and args - a predicate that only ever saw the first token
// could not tell an assignment from a plain read of the same operation.
// -----------------------------------------------------------------------
void test_filter_receives_the_whole_line_as_typed(void) {
    static CLI_UINT buf[4096 / sizeof(CLI_UINT)];
    EmbeddedCli *cli = makeCli(buf, sizeof(buf), true);

    submit(cli, "wifi.config.settings mode=client sta-ssid=lab");

    TEST_ASSERT_EQUAL_INT(1, filterCalls);
    TEST_ASSERT_EQUAL_STRING("wifi.config.settings mode=client sta-ssid=lab", filterSaw[0]);

    embeddedCliFree(cli);
}

// -----------------------------------------------------------------------
// Test 5: the ring keeps working after a refusal - the next accepted line is
// stored normally, and an empty line still consults nothing (parseCommand
// returns before the history step for a blank line, unchanged upstream
// behaviour).
// -----------------------------------------------------------------------
void test_ring_survives_a_refusal(void) {
    static CLI_UINT buf[4096 / sizeof(CLI_UINT)];
    EmbeddedCli *cli = makeCli(buf, sizeof(buf), true);

    submit(cli, "wifi.config.settings sta-password=hunter2");
    submit(cli, "   ");
    submit(cli, "system.status.health");

    TEST_ASSERT_EQUAL_INT(2, filterCalls);

    pressUp(cli);
    TEST_ASSERT_EQUAL_STRING("system.status.health", embeddedCliGetCmdBuffer(cli));
    pressUp(cli);
    TEST_ASSERT_EQUAL_STRING("system.status.health", embeddedCliGetCmdBuffer(cli));

    embeddedCliFree(cli);
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_without_a_filter_every_line_is_stored);
    RUN_TEST(test_refused_line_never_enters_history);
    RUN_TEST(test_refused_line_still_executes);
    RUN_TEST(test_filter_receives_the_whole_line_as_typed);
    RUN_TEST(test_ring_survives_a_refusal);
    return UNITY_END();
}
