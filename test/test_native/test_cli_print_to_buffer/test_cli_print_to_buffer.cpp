// =============================================================================
// test/test_native/test_cli_print_to_buffer/test_cli_print_to_buffer.cpp
//
// lib/embedded-cli VENDORED.md Patch 8 (single-write redraw), #268.
//
// embeddedCliPrintToBuffer() exists so a transport can deliver a whole
// mid-entry redraw in ONE write instead of ~70 single-character ones. The
// property that matters is therefore not "it produces something reasonable"
// but "it produces EXACTLY what the per-character path produces" - the moment
// the two renders differ, the project has two redraw contracts again, which
// is the defect #268 is fixing rather than one to reintroduce here.
// =============================================================================

#include <unity.h>

#include <string.h>

extern "C" {
#include "embedded_cli.h"
}

// ----------------------------------------------------------------------------
// Per-character capture, standing in for a transport's writeChar
// ----------------------------------------------------------------------------

static char g_capture[1024];
static size_t g_captureLen;

static void captureWriteChar(EmbeddedCli* cli, char c) {
    (void)cli;
    if (g_captureLen < sizeof(g_capture)) {
        g_capture[g_captureLen++] = c;
    }
}

// Two independent instances, driven identically, so one can be rendered
// through writeChar and the other into a buffer and the results compared.
static CLI_UINT g_cliBufferA[512];
static CLI_UINT g_cliBufferB[512];

static EmbeddedCli* newCli(CLI_UINT* storage, size_t storageSize) {
    EmbeddedCliConfig* config = embeddedCliDefaultConfig();
    config->cliBuffer = storage;
    config->cliBufferSize = (uint16_t)storageSize;
    // Match the Console task's own configuration (src/tasks/console_task.cpp):
    // live autocompletion is off there, so a render here ends at the buffered
    // command rather than at a cursor-save/restore pair no production redraw
    // carries.
    config->enableAutoComplete = false;
    EmbeddedCli* cli = embeddedCliNew(config);
    TEST_ASSERT_NOT_NULL_MESSAGE(cli, "embeddedCliNew returned NULL with a static buffer");
    cli->writeChar = captureWriteChar;
    embeddedCliProcess(cli);  // emits the initial invitation
    return cli;
}

static void typeChars(EmbeddedCli* cli, const char* text) {
    for (const char* p = text; *p != '\0'; ++p) {
        embeddedCliReceiveChar(cli, *p);
    }
    embeddedCliProcess(cli);
}

// The rendered redraw is byte-for-byte the per-character render, with a
// partial command buffered - the state #268's flood happens in.
void test_buffer_render_matches_writechar_render_midentry(void) {
    EmbeddedCli* a = newCli(g_cliBufferA, sizeof(g_cliBufferA));
    EmbeddedCli* b = newCli(g_cliBufferB, sizeof(g_cliBufferB));

    typeChars(a, "system.stat");
    typeChars(b, "system.stat");

    const char* line = "[65485][I][WebServer] slowest response phase now 144 ms (0)";

    g_captureLen = 0;
    embeddedCliPrint(a, line);
    const size_t perCharLen = g_captureLen;
    TEST_ASSERT_TRUE_MESSAGE(perCharLen > 0, "the per-character render wrote nothing");

    char rendered[512];
    const size_t renderedLen = embeddedCliPrintToBuffer(b, line, rendered, sizeof(rendered));

    TEST_ASSERT_EQUAL_INT_MESSAGE((int)perCharLen, (int)renderedLen,
                                  "the buffered render is a different length than the "
                                  "per-character render of the same state");
    TEST_ASSERT_EQUAL_MEMORY_MESSAGE(g_capture, rendered, perCharLen,
                                     "the buffered render differs byte-for-byte from the "
                                     "per-character render: two redraw contracts, not one");
}

// Nothing may leak onto the transport while rendering into a buffer: a single
// character escaping writeChar would land outside the one write the caller is
// about to make, which is exactly the interleaving this patch removes.
void test_buffer_render_writes_nothing_through_writechar(void) {
    EmbeddedCli* cli = newCli(g_cliBufferA, sizeof(g_cliBufferA));
    typeChars(cli, "sys");

    g_captureLen = 0;
    char rendered[512];
    const size_t renderedLen = embeddedCliPrintToBuffer(cli, "[INFO] heartbeat", rendered,
                                                        sizeof(rendered));

    TEST_ASSERT_TRUE_MESSAGE(renderedLen > 0, "the render produced nothing");
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, (int)g_captureLen,
                                  "characters reached writeChar during a buffered render");
}

// The redraw carries the operator's buffered command, not a fragment of the
// line just printed (#268's transcript: the prompt came back followed by the
// log line's own tail).
void test_buffer_render_ends_with_prompt_and_buffered_command(void) {
    EmbeddedCli* cli = newCli(g_cliBufferA, sizeof(g_cliBufferA));
    typeChars(cli, "sys");

    char rendered[512];
    const size_t renderedLen = embeddedCliPrintToBuffer(cli, "[INFO] heartbeat", rendered,
                                                        sizeof(rendered));
    TEST_ASSERT_TRUE(renderedLen >= 5);
    TEST_ASSERT_EQUAL_MEMORY_MESSAGE("> sys", rendered + renderedLen - 5, 5,
                                     "the redraw must end with the invitation and the "
                                     "buffered command");
}

// A render that does not fit reports 0, writes nothing the caller can mistake
// for a whole redraw, and leaves the line editor exactly as it was - the next
// render must not erase a line this one never drew.
void test_buffer_render_that_does_not_fit_reports_zero_and_leaves_state_intact(void) {
    EmbeddedCli* a = newCli(g_cliBufferA, sizeof(g_cliBufferA));
    EmbeddedCli* b = newCli(g_cliBufferB, sizeof(g_cliBufferB));
    typeChars(a, "system.stat");
    typeChars(b, "system.stat");

    const char* line = "[INFO] a line that cannot possibly fit the buffer below";

    char tiny[8];
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, (int)embeddedCliPrintToBuffer(b, line, tiny, sizeof(tiny)),
                                  "a render that overflows its buffer must report 0");

    // Both instances have now seen the same input; if the refused render had
    // advanced b's editor state, the next successful render would differ.
    g_captureLen = 0;
    embeddedCliPrint(a, line);
    const size_t perCharLen = g_captureLen;

    char rendered[512];
    const size_t renderedLen = embeddedCliPrintToBuffer(b, line, rendered, sizeof(rendered));

    TEST_ASSERT_EQUAL_INT_MESSAGE((int)perCharLen, (int)renderedLen,
                                  "the refused render left the line editor in a different "
                                  "state than an untouched instance");
    TEST_ASSERT_EQUAL_MEMORY_MESSAGE(g_capture, rendered, perCharLen,
                                     "the refused render changed what the next render draws");
}

// A zero-size or NULL destination is refused rather than written to.
void test_buffer_render_rejects_unusable_destination(void) {
    EmbeddedCli* cli = newCli(g_cliBufferA, sizeof(g_cliBufferA));
    char rendered[512];

    TEST_ASSERT_EQUAL_INT(0, (int)embeddedCliPrintToBuffer(cli, "[INFO] x", nullptr,
                                                           sizeof(rendered)));
    TEST_ASSERT_EQUAL_INT(0, (int)embeddedCliPrintToBuffer(cli, "[INFO] x", rendered, 0));
    TEST_ASSERT_EQUAL_INT(0, (int)embeddedCliPrintToBuffer(cli, nullptr, rendered,
                                                           sizeof(rendered)));
}

// The library's re-entry guard (outBuffer already set) is deliberately NOT
// covered here: the only way back into the library mid-render is
// cli->writeChar, and the test above proves that never runs during a buffered
// render, so the guard is unreachable by construction from this side. A test
// for it could only pass, never fail, which proves nothing. It stays in the
// library as a guard against a future candidate-source callback reaching back
// in - see the comment at its site.

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_buffer_render_matches_writechar_render_midentry);
    RUN_TEST(test_buffer_render_writes_nothing_through_writechar);
    RUN_TEST(test_buffer_render_ends_with_prompt_and_buffered_command);
    RUN_TEST(test_buffer_render_that_does_not_fit_reports_zero_and_leaves_state_intact);
    RUN_TEST(test_buffer_render_rejects_unusable_destination);
    return UNITY_END();
}
