/**
 * Test: embedded-cli Catalog Completion Callback patch (#238)
 *
 * Verifies EmbeddedCli::getCompletionCandidate: an external Tab completion
 * source that replaces bindings-based completion for a cli instance and
 * completes the CURRENT TOKEN (substring after the last space) instead of
 * the whole line - see lib/embedded-cli/VENDORED.md Patch 5 and the field
 * doc on EmbeddedCli::getCompletionCandidate in embedded_cli.h.
 *
 * This is a library-mechanism test (a plain fixed candidate list), not a
 * project-catalog test - include/console_completion.h and
 * test/test_native/test_console_completion/ cover the project's own
 * operation-name / argument-key mode selection against the real catalog.
 */

#include <unity.h>
#include <string.h>
#include <stdio.h>

extern "C" {
#include "embedded_cli.h"
}

// -----------------------------------------------------------------------
// Fixture: a fixed candidate list standing in for a project catalog.
// "speed=" / "steer=" model argument-key candidates (trailing "="); the
// rest model operation-name candidates.
// -----------------------------------------------------------------------
static const char* const kCandidates[] = {
    "alpha", "alphabet", "beta", "speed=", "steer=",
};
static const size_t kCandidateCount = sizeof(kCandidates) / sizeof(kCandidates[0]);

static const char* testCandidateSource(EmbeddedCli* cli, uint16_t index) {
    (void)cli;
    if (index >= kCandidateCount) return NULL;
    return kCandidates[index];
}

// Output capture: records everything writeChar receives, so listing output
// can be inspected.
static char outputBuf[2048];
static size_t outputLen = 0;

static void writeChar(EmbeddedCli* cli, char c) {
    (void)cli;
    if (outputLen + 1 < sizeof(outputBuf)) {
        outputBuf[outputLen++] = c;
        outputBuf[outputLen] = '\0';
    }
}

static char lastCommand[256];
static char lastArgs[256];
static int commandCount = 0;

static void onCommand(EmbeddedCli* cli, CliCommand* cmd) {
    (void)cli;
    snprintf(lastCommand, sizeof(lastCommand), "%s", cmd->name);
    snprintf(lastArgs, sizeof(lastArgs), "%s", cmd->args ? cmd->args : "");
    commandCount++;
}

void setUp(void) {
    outputLen = 0;
    outputBuf[0] = '\0';
    commandCount = 0;
    lastCommand[0] = '\0';
    lastArgs[0] = '\0';
}

void tearDown(void) {}

// Shared helper: create a cli instance wired to the fixture, with an
// optional binding also registered (to test the replace-not-merge rule).
static EmbeddedCli* makeCli(CLI_UINT* buffer, size_t bufferBytes, uint16_t cmdBufferSize,
                             bool withCompetingBinding) {
    EmbeddedCliConfig config = {
        .invitation = "> ",
        .rxBufferSize = 256,
        .cmdBufferSize = cmdBufferSize,
        .historyBufferSize = 256,
        .maxBindingCount = 4,
        .cliBuffer = buffer,
        .cliBufferSize = (uint16_t)bufferBytes,
        .enableAutoComplete = false,
    };
    EmbeddedCli* cli = embeddedCliNew(&config);
    TEST_ASSERT_NOT_NULL(cli);
    cli->writeChar = writeChar;
    cli->onCommand = onCommand;
    cli->getCompletionCandidate = testCandidateSource;

    if (withCompetingBinding) {
        CliCommandBinding b = {
            "alpha_bound", "competing binding", false, NULL, NULL,
        };
        embeddedCliAddBinding(cli, b);
    }
    return cli;
}

static void typeString(EmbeddedCli* cli, const char* s) {
    for (const char* p = s; *p; ++p) {
        embeddedCliReceiveChar(cli, *p);
    }
    embeddedCliProcess(cli);
}

static void pressTab(EmbeddedCli* cli) {
    embeddedCliReceiveChar(cli, '\t');
    embeddedCliProcess(cli);
}

static void pressEnter(EmbeddedCli* cli) {
    embeddedCliReceiveChar(cli, '\r');
    embeddedCliProcess(cli);
}

// -----------------------------------------------------------------------
// Test 1: unique match completes fully and appends a trailing space
// (operation-name-shaped candidate, does not end in "=").
// -----------------------------------------------------------------------
void test_unique_match_completes_with_trailing_space(void) {
    static CLI_UINT buf[4096 / sizeof(CLI_UINT)];
    EmbeddedCli* cli = makeCli(buf, sizeof(buf), 64, false);

    typeString(cli, "bet");
    pressTab(cli);

    TEST_ASSERT_EQUAL_STRING("beta ", embeddedCliGetCmdBuffer(cli));

    embeddedCliFree(cli);
}

// -----------------------------------------------------------------------
// Test 2: unique match ending in "=" completes WITHOUT a trailing space
// (docs/console-protocol.md s.1.2: key=value, no space around "=").
// -----------------------------------------------------------------------
void test_arg_key_candidate_completes_without_trailing_space(void) {
    static CLI_UINT buf[4096 / sizeof(CLI_UINT)];
    EmbeddedCli* cli = makeCli(buf, sizeof(buf), 64, false);

    typeString(cli, "sp");
    pressTab(cli);

    TEST_ASSERT_EQUAL_STRING("speed=", embeddedCliGetCmdBuffer(cli));

    embeddedCliFree(cli);
}

// -----------------------------------------------------------------------
// Test 3: ambiguous prefix extends to the longest common prefix on the
// first Tab, then lists candidates and restores the line unchanged on a
// second Tab (docs/console-protocol.md s.8).
// -----------------------------------------------------------------------
void test_ambiguous_prefix_extends_then_lists(void) {
    static CLI_UINT buf[4096 / sizeof(CLI_UINT)];
    EmbeddedCli* cli = makeCli(buf, sizeof(buf), 64, false);

    typeString(cli, "al");
    pressTab(cli);
    // "alpha" and "alphabet" share "alpha" - extends, no listing yet: the
    // full candidate name never appears in what was written to the
    // terminal, only the newly-completed characters ("pha") are echoed.
    TEST_ASSERT_EQUAL_STRING("alpha", embeddedCliGetCmdBuffer(cli));
    TEST_ASSERT_NULL(strstr(outputBuf, "alphabet"));

    outputLen = 0;
    outputBuf[0] = '\0';
    pressTab(cli);
    // No further extension is possible: lists both candidates and restores
    // the typed line unchanged.
    TEST_ASSERT_EQUAL_STRING("alpha", embeddedCliGetCmdBuffer(cli));
    TEST_ASSERT_NOT_NULL(strstr(outputBuf, "alpha"));
    TEST_ASSERT_NOT_NULL(strstr(outputBuf, "alphabet"));

    embeddedCliFree(cli);
}

// -----------------------------------------------------------------------
// Test 4: completion operates on the CURRENT TOKEN only - an already-typed
// earlier token (an operation name) is preserved untouched.
// -----------------------------------------------------------------------
void test_completion_preserves_earlier_tokens(void) {
    static CLI_UINT buf[4096 / sizeof(CLI_UINT)];
    EmbeddedCli* cli = makeCli(buf, sizeof(buf), 64, false);

    typeString(cli, "opname sp");
    pressTab(cli);

    TEST_ASSERT_EQUAL_STRING("opname speed=", embeddedCliGetCmdBuffer(cli));

    embeddedCliFree(cli);
}

// -----------------------------------------------------------------------
// Test 5: an external source REPLACES binding-based completion; a bound
// command that would otherwise match the prefix is never offered.
// -----------------------------------------------------------------------
void test_external_source_replaces_bindings_not_merges(void) {
    static CLI_UINT buf[4096 / sizeof(CLI_UINT)];
    EmbeddedCli* cli = makeCli(buf, sizeof(buf), 64, true);  // registers "alpha_bound"

    typeString(cli, "al");
    pressTab(cli);
    // If bindings were merged in, "alpha", "alphabet" and "alpha_bound"
    // would share only "alpha" as a common prefix (unchanged) OR the
    // presence of a 3rd candidate would change the outcome; either way the
    // buffer must reflect only the external source's two candidates.
    TEST_ASSERT_EQUAL_STRING("alpha", embeddedCliGetCmdBuffer(cli));

    outputLen = 0;
    outputBuf[0] = '\0';
    pressTab(cli);
    TEST_ASSERT_NOT_NULL(strstr(outputBuf, "alphabet"));
    TEST_ASSERT_NULL(strstr(outputBuf, "alpha_bound"));

    embeddedCliFree(cli);
}

// -----------------------------------------------------------------------
// Test 6: a candidate that would overflow the fixed command buffer is
// refused rather than corrupting it (mirrors the Safe Overflow patch's
// philosophy for this new write path).
// -----------------------------------------------------------------------
void test_overflowing_candidate_is_refused(void) {
    static CLI_UINT buf[4096 / sizeof(CLI_UINT)];
    // cmdBufferSize is deliberately tiny: "speed=" (6 chars) cannot fit
    // after "sp" (2 chars already typed) within an 6-byte buffer (2 header
    // bytes reserved per embedded_cli.c's onCharInput bound).
    EmbeddedCli* cli = makeCli(buf, sizeof(buf), 6, false);

    typeString(cli, "sp");
    char before[16];
    snprintf(before, sizeof(before), "%s", embeddedCliGetCmdBuffer(cli));

    pressTab(cli);

    TEST_ASSERT_EQUAL_STRING(before, embeddedCliGetCmdBuffer(cli));

    embeddedCliFree(cli);
}

// -----------------------------------------------------------------------
// Test 7 (regression, #213 x #238 interaction): pressing Enter never
// autocompletes, even with an external completion source wired in and even
// after a prior Tab press already extended (but did not fully complete)
// the line. This is what proves #238's completion mechanism did not
// reintroduce the defect #213's Safe Enter patch fixed.
// -----------------------------------------------------------------------
void test_enter_never_autocompletes_with_external_source(void) {
    static CLI_UINT buf[4096 / sizeof(CLI_UINT)];
    EmbeddedCli* cli = makeCli(buf, sizeof(buf), 64, false);

    // Ambiguous, unsubmitted prefix + Enter: the literal typed text runs,
    // not any candidate it could have expanded to.
    typeString(cli, "al");
    pressEnter(cli);
    TEST_ASSERT_EQUAL_INT(1, commandCount);
    TEST_ASSERT_EQUAL_STRING("al", lastCommand);

    embeddedCliFree(cli);
}

void test_enter_after_tab_extension_executes_exactly_the_buffer(void) {
    static CLI_UINT buf[4096 / sizeof(CLI_UINT)];
    EmbeddedCli* cli = makeCli(buf, sizeof(buf), 64, false);

    typeString(cli, "al");
    pressTab(cli);  // extends to "alpha" (still ambiguous vs "alphabet")
    TEST_ASSERT_EQUAL_STRING("alpha", embeddedCliGetCmdBuffer(cli));

    pressEnter(cli);
    // Enter runs exactly what is now in the buffer ("alpha"), not a further
    // silent expansion to "alphabet" or anything else.
    TEST_ASSERT_EQUAL_INT(1, commandCount);
    TEST_ASSERT_EQUAL_STRING("alpha", lastCommand);

    embeddedCliFree(cli);
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_unique_match_completes_with_trailing_space);
    RUN_TEST(test_arg_key_candidate_completes_without_trailing_space);
    RUN_TEST(test_ambiguous_prefix_extends_then_lists);
    RUN_TEST(test_completion_preserves_earlier_tokens);
    RUN_TEST(test_external_source_replaces_bindings_not_merges);
    RUN_TEST(test_overflowing_candidate_is_refused);
    RUN_TEST(test_enter_never_autocompletes_with_external_source);
    RUN_TEST(test_enter_after_tab_extension_executes_exactly_the_buffer);
    return UNITY_END();
}
