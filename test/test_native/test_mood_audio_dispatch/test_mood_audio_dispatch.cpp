// =============================================================================
// test/test_native/test_mood_audio_dispatch/test_mood_audio_dispatch.cpp
//
// Native unit tests for moodAudioCommand() — the pure mood-to-audio mapping.
//
// Tests:
//   - Each valid mood ID maps to the correct $ command
//   - Invalid IDs return nullptr
//   - Return values are the exact strings AudioTask understands
// =============================================================================

#include <unity.h>

#include "audio_dollar_parser.h"
#include "mood.h"

void setUp() {}
void tearDown() {}

// -----------------------------------------------------------------------------
// Valid mood IDs
// -----------------------------------------------------------------------------

void test_quiet_returns_stop() {
    const char* cmd = moodAudioCommand(10);
    TEST_ASSERT_NOT_NULL(cmd);
    TEST_ASSERT_EQUAL_STRING("$s", cmd);
}

void test_full_awake_returns_random() {
    const char* cmd = moodAudioCommand(11);
    TEST_ASSERT_NOT_NULL(cmd);
    TEST_ASSERT_EQUAL_STRING("$R", cmd);
}

void test_mid_awake_returns_random() {
    const char* cmd = moodAudioCommand(13);
    TEST_ASSERT_NOT_NULL(cmd);
    TEST_ASSERT_EQUAL_STRING("$R", cmd);
}

void test_awake_plus_returns_random() {
    const char* cmd = moodAudioCommand(14);
    TEST_ASSERT_NOT_NULL(cmd);
    TEST_ASSERT_EQUAL_STRING("$R", cmd);
}

// -----------------------------------------------------------------------------
// Invalid IDs
// -----------------------------------------------------------------------------

void test_zero_returns_null() {
    TEST_ASSERT_NULL(moodAudioCommand(0));
}

void test_twelve_returns_null() {
    // SE12 is not a mood preset
    TEST_ASSERT_NULL(moodAudioCommand(12));
}

void test_nine_returns_null() {
    TEST_ASSERT_NULL(moodAudioCommand(9));
}

void test_fifteen_returns_null() {
    TEST_ASSERT_NULL(moodAudioCommand(15));
}

void test_255_returns_null() {
    TEST_ASSERT_NULL(moodAudioCommand(255));
}

// -----------------------------------------------------------------------------
// Correct $ prefix — AudioTask requires '$' as first character
// -----------------------------------------------------------------------------

void test_quiet_starts_with_dollar() {
    const char* cmd = moodAudioCommand(10);
    TEST_ASSERT_NOT_NULL(cmd);
    TEST_ASSERT_EQUAL('$', cmd[0]);
}

void test_awake_starts_with_dollar() {
    const char* cmd = moodAudioCommand(11);
    TEST_ASSERT_NOT_NULL(cmd);
    TEST_ASSERT_EQUAL('$', cmd[0]);
}

// -----------------------------------------------------------------------------
// Exhaustive check: only 10, 11, 13, 14 are valid
// -----------------------------------------------------------------------------

void test_only_valid_ids_return_non_null() {
    for (int i = 0; i <= 20; i++) {
        const char* cmd = moodAudioCommand((uint8_t)i);
        bool expected_valid = (i == 10 || i == 11 || i == 13 || i == 14);
        if (expected_valid) {
            TEST_ASSERT_NOT_NULL_MESSAGE(cmd, "Expected non-null for valid mood ID");
        } else {
            TEST_ASSERT_NULL_MESSAGE(cmd, "Expected null for invalid mood ID");
        }
    }
}

// -----------------------------------------------------------------------------
// Integration: moodAudioCommand() output is correctly understood by parseAudioDollar()
// This closes the loop between the mood system and the AudioTask dollar parser.
// -----------------------------------------------------------------------------

void test_quiet_mood_cmd_parses_to_stop() {
    const char* cmd = moodAudioCommand(10);
    TEST_ASSERT_NOT_NULL(cmd);
    AudioAction action = parseAudioDollar(cmd);
    TEST_ASSERT_EQUAL(AUDIO_ACTION_STOP, action.type);
}

void test_full_awake_mood_cmd_parses_to_random_on() {
    const char* cmd = moodAudioCommand(11);
    TEST_ASSERT_NOT_NULL(cmd);
    AudioAction action = parseAudioDollar(cmd);
    TEST_ASSERT_EQUAL(AUDIO_ACTION_RANDOM_ON, action.type);
}

void test_mid_awake_mood_cmd_parses_to_random_on() {
    const char* cmd = moodAudioCommand(13);
    TEST_ASSERT_NOT_NULL(cmd);
    AudioAction action = parseAudioDollar(cmd);
    TEST_ASSERT_EQUAL(AUDIO_ACTION_RANDOM_ON, action.type);
}

void test_awake_plus_mood_cmd_parses_to_random_on() {
    const char* cmd = moodAudioCommand(14);
    TEST_ASSERT_NOT_NULL(cmd);
    AudioAction action = parseAudioDollar(cmd);
    TEST_ASSERT_EQUAL(AUDIO_ACTION_RANDOM_ON, action.type);
}

void test_all_valid_moods_produce_parseable_commands() {
    // Every valid mood must yield a $ command that parseAudioDollar handles —
    // AUDIO_ACTION_NONE would mean AudioTask would silently do nothing.
    const uint8_t validMoods[] = {10, 11, 13, 14};
    for (size_t i = 0; i < sizeof(validMoods); i++) {
        const char* cmd = moodAudioCommand(validMoods[i]);
        TEST_ASSERT_NOT_NULL(cmd);
        AudioAction action = parseAudioDollar(cmd);
        TEST_ASSERT_NOT_EQUAL_MESSAGE(AUDIO_ACTION_NONE, action.type,
            "mood audio command must not parse to NONE");
    }
}

// -----------------------------------------------------------------------------

int main(int argc, char** argv) {
    UNITY_BEGIN();

    RUN_TEST(test_quiet_returns_stop);
    RUN_TEST(test_full_awake_returns_random);
    RUN_TEST(test_mid_awake_returns_random);
    RUN_TEST(test_awake_plus_returns_random);

    RUN_TEST(test_zero_returns_null);
    RUN_TEST(test_twelve_returns_null);
    RUN_TEST(test_nine_returns_null);
    RUN_TEST(test_fifteen_returns_null);
    RUN_TEST(test_255_returns_null);

    RUN_TEST(test_quiet_starts_with_dollar);
    RUN_TEST(test_awake_starts_with_dollar);

    RUN_TEST(test_only_valid_ids_return_non_null);

    // Integration
    RUN_TEST(test_quiet_mood_cmd_parses_to_stop);
    RUN_TEST(test_full_awake_mood_cmd_parses_to_random_on);
    RUN_TEST(test_mid_awake_mood_cmd_parses_to_random_on);
    RUN_TEST(test_awake_plus_mood_cmd_parses_to_random_on);
    RUN_TEST(test_all_valid_moods_produce_parseable_commands);

    return UNITY_END();
}
