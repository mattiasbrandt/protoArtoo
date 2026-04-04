// =============================================================================
// test/test_native/test_system_sounds/test_system_sounds.cpp
//
// Native unit tests for system_sounds helpers.
// Covers: drive mode bucket classification and guarded queue behavior.
// =============================================================================

#include <unity.h>

#include "system_sounds.h"

namespace {

static int g_queueCallCount = 0;
static uint16_t g_lastTrack = 0;
static CommandSource g_lastSource = SRC_NONE;

void resetQueueSpy() {
    g_queueCallCount = 0;
    g_lastTrack = 0;
    g_lastSource = SRC_NONE;
}

bool queueSpyTrue(uint16_t track, CommandSource src) {
    g_queueCallCount++;
    g_lastTrack = track;
    g_lastSource = src;
    return true;
}

bool queueSpyFalse(uint16_t track, CommandSource src) {
    g_queueCallCount++;
    g_lastTrack = track;
    g_lastSource = src;
    return false;
}

}  // namespace

void setUp() {
    resetQueueSpy();
}

void tearDown() {
}

void test_queue_track_zero_is_noop_and_false() {
    bool queued = queueSystemSoundTrack(0, queueSpyTrue, SRC_INTERNAL);
    TEST_ASSERT_FALSE(queued);
    TEST_ASSERT_EQUAL_INT(0, g_queueCallCount);
}

void test_queue_nonzero_calls_callback_once_with_track_and_source() {
    bool queued = queueSystemSoundTrack(42, queueSpyTrue, SRC_WEB_API);
    TEST_ASSERT_TRUE(queued);
    TEST_ASSERT_EQUAL_INT(1, g_queueCallCount);
    TEST_ASSERT_EQUAL_UINT16(42, g_lastTrack);
    TEST_ASSERT_EQUAL_UINT8(SRC_WEB_API, g_lastSource);
}

void test_queue_nonzero_propagates_false_callback_result() {
    bool queued = queueSystemSoundTrack(77, queueSpyFalse, SRC_INTERNAL);
    TEST_ASSERT_FALSE(queued);
    TEST_ASSERT_EQUAL_INT(1, g_queueCallCount);
    TEST_ASSERT_EQUAL_UINT16(77, g_lastTrack);
    TEST_ASSERT_EQUAL_UINT8(SRC_INTERNAL, g_lastSource);
}

void test_queue_nonzero_with_null_callback_is_false_and_noop() {
    bool queued = queueSystemSoundTrack(12, nullptr, SRC_INTERNAL);
    TEST_ASSERT_FALSE(queued);
    TEST_ASSERT_EQUAL_INT(0, g_queueCallCount);
}

void test_classify_clamps_below_zero_to_slow() {
    TEST_ASSERT_EQUAL_UINT8(SYSTEM_DRIVE_MODE_SLOW, classifySystemDriveMode(-1.0f));
}

void test_classify_boundaries_and_clamps_above_one() {
    TEST_ASSERT_EQUAL_UINT8(SYSTEM_DRIVE_MODE_SLOW, classifySystemDriveMode(0.0f));
    TEST_ASSERT_EQUAL_UINT8(SYSTEM_DRIVE_MODE_SLOW, classifySystemDriveMode(0.339f));
    TEST_ASSERT_EQUAL_UINT8(SYSTEM_DRIVE_MODE_NORMAL, classifySystemDriveMode(0.34f));
    TEST_ASSERT_EQUAL_UINT8(SYSTEM_DRIVE_MODE_NORMAL, classifySystemDriveMode(0.669f));
    TEST_ASSERT_EQUAL_UINT8(SYSTEM_DRIVE_MODE_TURBO, classifySystemDriveMode(0.67f));
    TEST_ASSERT_EQUAL_UINT8(SYSTEM_DRIVE_MODE_TURBO, classifySystemDriveMode(1.0f));
    TEST_ASSERT_EQUAL_UINT8(SYSTEM_DRIVE_MODE_TURBO, classifySystemDriveMode(2.0f));
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_queue_track_zero_is_noop_and_false);
    RUN_TEST(test_queue_nonzero_calls_callback_once_with_track_and_source);
    RUN_TEST(test_queue_nonzero_propagates_false_callback_result);
    RUN_TEST(test_queue_nonzero_with_null_callback_is_false_and_noop);
    RUN_TEST(test_classify_clamps_below_zero_to_slow);
    RUN_TEST(test_classify_boundaries_and_clamps_above_one);
    return UNITY_END();
}
