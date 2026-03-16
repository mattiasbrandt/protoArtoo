// =============================================================================
// test/test_native/test_web_drive_timeout/test_web_drive_timeout.cpp
//
// Native unit tests for web drive timeout detection.
// Tests: webDriveTimeoutCheck() for Layer 3 safety (web API drive timeout).
//
// Safety relevance: Web drive commands must timeout to prevent runaway
// robots when browser disconnects or stops sending commands.
// =============================================================================
#include <unity.h>

#include "sbus_math.h"

void setUp() {
}
void tearDown() {
}

// --- webDriveTimeoutCheck() tests ---

void test_web_drive_no_command_yet_is_not_timeout() {
    // lastDriveCommandMs = 0 means no command ever sent - not a timeout
    TEST_ASSERT_FALSE(webDriveTimeoutCheck(0, 1000, 500));
}

void test_web_drive_recent_command_is_not_timeout() {
    // Last command 100ms ago, timeout 500ms - should not timeout
    TEST_ASSERT_FALSE(webDriveTimeoutCheck(900, 1000, 500));
}

void test_web_drive_at_exact_timeout_is_not_timeout() {
    // Last command 500ms ago, timeout 500ms - not timeout yet (> not >=)
    TEST_ASSERT_FALSE(webDriveTimeoutCheck(500, 1000, 500));
}

void test_web_drive_just_within_timeout_is_not_timeout() {
    // Last command 499ms ago, timeout 500ms - should not timeout
    TEST_ASSERT_FALSE(webDriveTimeoutCheck(501, 1000, 500));
}

void test_web_drive_one_ms_over_timeout_is_timeout() {
    // Last command 501ms ago, timeout 500ms - should timeout
    TEST_ASSERT_TRUE(webDriveTimeoutCheck(499, 1000, 500));
}

void test_web_drive_millis_overflow_handles_correctly() {
    // Test millis() overflow scenario
    uint32_t lastCommandMs = 0xFFFFFFFF - 200;  // 200ms before overflow
    uint32_t currentMs = 100;                   // Wrapped around to 100
    // Time elapsed = 300ms < 500ms timeout
    TEST_ASSERT_FALSE(webDriveTimeoutCheck(lastCommandMs, currentMs, 500));
}

void test_web_drive_millis_overflow_timeout_exceeded() {
    // Overflow scenario where timeout is exceeded
    uint32_t lastCommandMs = 0xFFFFFFFF - 600;  // 600ms before overflow
    uint32_t currentMs = 100;                   // Wrapped around to 100
    // Time elapsed = 700ms > 500ms timeout
    TEST_ASSERT_TRUE(webDriveTimeoutCheck(lastCommandMs, currentMs, 500));
}

void test_web_drive_standard_500ms_timeout() {
    // Standard WEB_DRIVE_TIMEOUT_MS = 500ms
    // 499ms should be OK
    TEST_ASSERT_FALSE(webDriveTimeoutCheck(501, 1000, 500));
    // 501ms should timeout
    TEST_ASSERT_TRUE(webDriveTimeoutCheck(499, 1000, 500));
}

void test_web_drive_short_timeout_100ms() {
    // Short timeout for responsive stop
    TEST_ASSERT_FALSE(webDriveTimeoutCheck(901, 1000, 100));  // 99ms OK
    TEST_ASSERT_TRUE(webDriveTimeoutCheck(899, 1000, 100));   // 101ms timeout
}

void test_web_drive_long_timeout_2000ms() {
    // Long timeout for high-latency connections
    // lastDriveCommandMs must be non-zero for timeout calculation
    TEST_ASSERT_FALSE(webDriveTimeoutCheck(1, 2000, 2000));  // 1999ms elapsed, not timeout
    TEST_ASSERT_FALSE(webDriveTimeoutCheck(1, 2001, 2000));  // 2000ms elapsed, not timeout (not >)
    TEST_ASSERT_TRUE(webDriveTimeoutCheck(1, 2002, 2000));   // 2001ms elapsed, timeout
}

int main() {
    UNITY_BEGIN();

    RUN_TEST(test_web_drive_no_command_yet_is_not_timeout);
    RUN_TEST(test_web_drive_recent_command_is_not_timeout);
    RUN_TEST(test_web_drive_at_exact_timeout_is_not_timeout);
    RUN_TEST(test_web_drive_just_within_timeout_is_not_timeout);
    RUN_TEST(test_web_drive_one_ms_over_timeout_is_timeout);
    RUN_TEST(test_web_drive_millis_overflow_handles_correctly);
    RUN_TEST(test_web_drive_millis_overflow_timeout_exceeded);
    RUN_TEST(test_web_drive_standard_500ms_timeout);
    RUN_TEST(test_web_drive_short_timeout_100ms);
    RUN_TEST(test_web_drive_long_timeout_2000ms);

    return UNITY_END();
}
