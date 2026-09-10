// =============================================================================
// test/test_native/test_drive_backend/test_drive_backend.cpp
//
// The Foot Drive backend seam (include/drive_backend.h). The seam takes
// (speed, steer) and the hoverboard wire takes (steer, speed), so the encode
// step is where a transposition would land -- and a transposition there turns
// a throttle command into a spin on a real droid without failing a build.
// =============================================================================
#include <unity.h>

#include <cstring>

#include "config.h"
#include "drive_backend.h"
#include "hoverboard_uart.h"

void setUp() {}
void tearDown() {}

void test_encode_maps_speed_and_steer_onto_the_wire_fields() {
    uint8_t buf[DRIVE_BACKEND_FRAME_MAX_BYTES] = {};
    const size_t len = driveBackendEncode(buf, sizeof(buf), /*speed=*/123, /*steer=*/-45);
    TEST_ASSERT_EQUAL_UINT(8, len);

    uint16_t start = 0;
    int16_t steer = 0;
    int16_t speed = 0;
    uint16_t checksum = 0;
    memcpy(&start, buf + 0, sizeof(start));
    memcpy(&steer, buf + 2, sizeof(steer));
    memcpy(&speed, buf + 4, sizeof(speed));
    memcpy(&checksum, buf + 6, sizeof(checksum));

    TEST_ASSERT_EQUAL_HEX16(0xABCD, start);
    TEST_ASSERT_EQUAL_INT16(-45, steer);
    TEST_ASSERT_EQUAL_INT16(123, speed);
    TEST_ASSERT_EQUAL_HEX16(calcHoverboardChecksum(-45, 123), checksum);
}

void test_encode_refuses_a_buffer_too_small_for_a_whole_frame() {
    uint8_t buf[DRIVE_BACKEND_FRAME_MAX_BYTES] = {};
    memset(buf, 0xEE, sizeof(buf));

    TEST_ASSERT_EQUAL_UINT(0, driveBackendEncode(buf, 7, 100, 100));
    // A partial frame on the wire is worse than no frame: the far end would
    // resync mid-command. Nothing may be written when the answer is 0.
    for (size_t i = 0; i < sizeof(buf); ++i) {
        TEST_ASSERT_EQUAL_HEX8(0xEE, buf[i]);
    }
    TEST_ASSERT_EQUAL_UINT(0, driveBackendEncode(nullptr, sizeof(buf), 100, 100));
}

// The same relation include/drive_backend.h static_asserts, asserted at
// runtime so a suite failure names it rather than a build error naming a macro
// expansion. The generic tick is what guarantees continuity; the backend only
// declares how long its far end will wait.
void test_the_generic_tick_feeds_the_backend_inside_its_own_deadline() {
    TEST_ASSERT_LESS_OR_EQUAL_UINT16(kDriveBackend.continuityDeadlineMs, DRIVE_FRAME_PERIOD_MS);
    TEST_ASSERT_GREATER_THAN_UINT16(0, DRIVE_FRAME_PERIOD_MS);
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_encode_maps_speed_and_steer_onto_the_wire_fields);
    RUN_TEST(test_encode_refuses_a_buffer_too_small_for_a_whole_frame);
    RUN_TEST(test_the_generic_tick_feeds_the_backend_inside_its_own_deadline);
    return UNITY_END();
}
