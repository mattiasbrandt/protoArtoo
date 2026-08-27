/**
 * Test: embedded-cli UTF-8 Ingestion patch
 *
 * Verifies that high-bit bytes (>= 0x80) are accepted into the command buffer.
 * UTF-8 validation is delegated to the Console dispatcher.
 *
 * Without the patch: Bytes >= 0x80 are rejected by isDisplayableChar(),
 * silently discarding UTF-8 sequences. SSIDs and other values containing
 * non-ASCII characters are corrupted.
 *
 * With the patch: High-bit bytes pass through unchanged, preserving UTF-8 input.
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

// Mock onCommand callback - records the command exactly as typed
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
 * Test 1: UTF-8 sequence passes through unchanged
 *
 * Scenario:
 * 1. Type a command with an argument containing UTF-8
 * 2. Example: "test arg=café" where "café" contains a UTF-8 'é' (0xC3 0xA9)
 * 3. Press Enter
 * 4. Verify that the UTF-8 bytes were preserved in the command
 *
 * This is the core test for UTF-8 ingestion.
 */
void test_utf8_ingestion_high_bit_bytes_preserved(void) {
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

    // Type "test" followed by UTF-8 bytes for "café"
    // "é" in UTF-8 is 0xC3 0xA9
    embeddedCliReceiveChar(cli, 't');
    embeddedCliReceiveChar(cli, 'e');
    embeddedCliReceiveChar(cli, 's');
    embeddedCliReceiveChar(cli, 't');
    embeddedCliReceiveChar(cli, ' ');

    // Feed UTF-8 "é" (0xC3 0xA9)
    embeddedCliReceiveChar(cli, (char)0xC3);
    embeddedCliReceiveChar(cli, (char)0xA9);

    embeddedCliProcess(cli);

    // Press Enter
    embeddedCliReceiveChar(cli, '\r');
    embeddedCliProcess(cli);

    // Verify that the command contains the UTF-8 bytes
    TEST_ASSERT_EQUAL_INT(1, commandCount);
    TEST_ASSERT_EQUAL_INT(7, strlen(lastCommand));  // "test" (4) + space (1) + UTF-8 (2)

    // Check that the high-bit bytes are present
    unsigned char *bytes = (unsigned char *)lastCommand;
    TEST_ASSERT_EQUAL_INT(0xC3, bytes[5]);
    TEST_ASSERT_EQUAL_INT(0xA9, bytes[6]);

    embeddedCliFree(cli);
}

/**
 * Test 2: UTF-8 SSID argument preserved
 *
 * Scenario:
 * 1. Type "wifi.config.settings sta-ssid=<UTF-8 SSID>"
 * 2. Example: sta-ssid="Кафе" (Cyrillic for "Cafe")
 * 3. Press Enter
 * 4. Verify that the UTF-8 SSID bytes are in the command args
 *
 * This simulates the real use case: setting a WiFi SSID with non-ASCII characters.
 */
void test_utf8_ingestion_ssid_preserved(void) {
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

    // Type the command
    const char *cmd_prefix = "wifi.config.settings sta-ssid=";
    for (const char *p = cmd_prefix; *p; p++) {
        embeddedCliReceiveChar(cli, *p);
    }

    // Feed UTF-8 Cyrillic "Ка" (K=0xD0 0x9A, a=0xD0 0xB0)
    embeddedCliReceiveChar(cli, (char)0xD0);
    embeddedCliReceiveChar(cli, (char)0x9A);
    embeddedCliReceiveChar(cli, (char)0xD0);
    embeddedCliReceiveChar(cli, (char)0xB0);

    embeddedCliProcess(cli);

    // Press Enter
    embeddedCliReceiveChar(cli, '\r');
    embeddedCliProcess(cli);

    // Verify that the command was executed
    TEST_ASSERT_EQUAL_INT(1, commandCount);

    // Verify the command name
    TEST_ASSERT_EQUAL_STRING("wifi.config.settings", lastCommand);

    // Verify that the args contain the UTF-8 bytes
    unsigned char *bytes = (unsigned char *)lastArgs;
    // args should be "sta-ssid=" followed by UTF-8 bytes
    size_t expected_len = strlen("sta-ssid=") + 4;  // 4 UTF-8 bytes
    TEST_ASSERT_EQUAL_INT(expected_len, strlen(lastArgs));

    embeddedCliFree(cli);
}

/**
 * Test 3: ASCII-only command still works (regression)
 *
 * Scenario:
 * 1. Type a normal ASCII command
 * 2. Press Enter
 * 3. Verify that it executes normally
 *
 * This is a regression check to ensure the patch doesn't break ASCII.
 */
void test_utf8_ingestion_ascii_still_works(void) {
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

    // Type an ASCII command
    const char *cmd = "help system.status";
    for (const char *p = cmd; *p; p++) {
        embeddedCliReceiveChar(cli, *p);
    }

    embeddedCliProcess(cli);

    // Press Enter
    embeddedCliReceiveChar(cli, '\r');
    embeddedCliProcess(cli);

    // Verify that the command executed
    TEST_ASSERT_EQUAL_INT(1, commandCount);
    TEST_ASSERT_EQUAL_STRING("help", lastCommand);
    TEST_ASSERT_EQUAL_STRING("system.status", lastArgs);

    embeddedCliFree(cli);
}
