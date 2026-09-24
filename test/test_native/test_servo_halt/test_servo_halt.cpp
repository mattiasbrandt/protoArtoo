// =============================================================================
// test/test_native/test_servo_halt/test_servo_halt.cpp
//
// When a halt lets go of every Servo Output (ADR 0043, #417).
//
// The case that matters is the one a single `estop || sleep` edge missed: Sleep
// Mode already on, a person moves an Output (allowed while asleep), then the
// estop latches. That estop has to release the Output, and say it was the
// estop.
// =============================================================================
#include <unity.h>

#include "servo_halt.h"

void setUp() {}
void tearDown() {}

static const ServoHaltFlags kRunning = {false, false};
static const ServoHaltFlags kAsleep = {false, true};
static const ServoHaltFlags kEstop = {true, false};
static const ServoHaltFlags kEstopAsleep = {true, true};

void test_an_estop_while_asleep_releases_every_output_as_the_estop() {
    const ServoHaltEdge edge = servoHaltEdge(kAsleep, kEstopAsleep);
    TEST_ASSERT_TRUE(edge.release);
    TEST_ASSERT_EQUAL_UINT8(SERVO_LIMP_ESTOP, edge.reason);
}

void test_each_halt_releases_on_its_own_edge() {
    TEST_ASSERT_TRUE(servoHaltEdge(kRunning, kEstop).release);
    TEST_ASSERT_EQUAL_UINT8(SERVO_LIMP_ESTOP, servoHaltEdge(kRunning, kEstop).reason);
    TEST_ASSERT_TRUE(servoHaltEdge(kRunning, kAsleep).release);
    TEST_ASSERT_EQUAL_UINT8(SERVO_LIMP_SLEEP, servoHaltEdge(kRunning, kAsleep).reason);
    TEST_ASSERT_TRUE(servoHaltEdge(kRunning, kEstopAsleep).release);
    TEST_ASSERT_EQUAL_UINT8(SERVO_LIMP_ESTOP, servoHaltEdge(kRunning, kEstopAsleep).reason);
}

// On the edge only: a halt that stands releases nothing further, so a person's
// move in Sleep Mode can still move, and nothing is published every frame.
void test_a_halt_that_stands_releases_nothing_further() {
    TEST_ASSERT_FALSE(servoHaltEdge(kAsleep, kAsleep).release);
    TEST_ASSERT_FALSE(servoHaltEdge(kEstop, kEstop).release);
    TEST_ASSERT_FALSE(servoHaltEdge(kEstopAsleep, kEstopAsleep).release);
    TEST_ASSERT_FALSE(servoHaltEdge(kRunning, kRunning).release);
    // Leaving a halt is not entering one.
    TEST_ASSERT_FALSE(servoHaltEdge(kEstopAsleep, kAsleep).release);
    TEST_ASSERT_FALSE(servoHaltEdge(kAsleep, kRunning).release);
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_an_estop_while_asleep_releases_every_output_as_the_estop);
    RUN_TEST(test_each_halt_releases_on_its_own_edge);
    RUN_TEST(test_a_halt_that_stands_releases_nothing_further);
    return UNITY_END();
}
