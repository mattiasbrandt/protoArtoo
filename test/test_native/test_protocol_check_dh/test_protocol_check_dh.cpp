// =============================================================================
// test_native/test_protocol_check_dh.cpp
//
// Unity native tests for DH: (Holo Effect) dome command validation in
// src/protocol_check.cpp. Tests mirror the client-side validation in
// data/seq_protocol_check.js exactly (issue #11).
// =============================================================================

#include <unity.h>
#include <string.h>

#include "protocol_check.h"
#include "sequence_engine.h"

// Helpers for creating a minimal valid step + branch for testing.
static SeqStep createDomeStep(const char* cmd, uint32_t tMs = 0) {
    SeqStep s = {};
    s.type = STEP_DOME_CMD;
    s.tMs = tMs;
    s.effectClass = FX_NONE;
    strncpy(s.payload, cmd, sizeof(s.payload) - 1);
    s.payload[sizeof(s.payload) - 1] = '\0';
    return s;
}

static SeqStep createEndStep(uint32_t tMs = 100) {
    SeqStep s = {};
    s.type = STEP_END;
    s.tMs = tMs;
    s.effectClass = FX_NONE;
    return s;
}

void setUp(void) {
}

void tearDown(void) {
}

// =============================================================================
// VALID: Basic DH: commands
// =============================================================================

void test_dh_minimal_flash() {
    SeqStep steps[] = {
        createDomeStep("DH:A:FLASH"),
        createEndStep(),
    };
    ProtocolCheckResult r = protocolCheckBranch("steps", steps, 2);
    TEST_ASSERT_TRUE(r.ok);
    TEST_ASSERT_EQUAL_INT(FX_LOGIC_PSI | FX_HOLO, steps[0].effectClass);
}

void test_dh_with_color() {
    SeqStep steps[] = {
        createDomeStep("DH:A:FLASH:RED:10"),
        createEndStep(),
    };
    ProtocolCheckResult r = protocolCheckBranch("steps", steps, 2);
    TEST_ASSERT_TRUE(r.ok);
    TEST_ASSERT_EQUAL_INT(FX_LOGIC_PSI | FX_HOLO, steps[0].effectClass);
}

void test_dh_rainbow_minimal() {
    SeqStep steps[] = {
        createDomeStep("DH:F:RAINBOW"),
        createEndStep(),
    };
    ProtocolCheckResult r = protocolCheckBranch("steps", steps, 2);
    TEST_ASSERT_TRUE(r.ok);
}

void test_dh_wag_with_count() {
    SeqStep steps[] = {
        createDomeStep("DH:A:WAG:DEFAULT:5"),
        createEndStep(),
    };
    ProtocolCheckResult r = protocolCheckBranch("steps", steps, 2);
    TEST_ASSERT_TRUE(r.ok);
}

void test_dh_reset_no_color() {
    SeqStep steps[] = {
        createDomeStep("DH:A:RESET"),
        createEndStep(),
    };
    ProtocolCheckResult r = protocolCheckBranch("steps", steps, 2);
    TEST_ASSERT_TRUE(r.ok);
}

void test_dh_solid_with_blue() {
    SeqStep steps[] = {
        createDomeStep("DH:F:SOLID:BLUE"),
        createEndStep(),
    };
    ProtocolCheckResult r = protocolCheckBranch("steps", steps, 2);
    TEST_ASSERT_TRUE(r.ok);
}

// =============================================================================
// INVALID: Rejection cases (lean set)
// =============================================================================

void test_dh_bad_target() {
    SeqStep steps[] = {
        createDomeStep("DH:X:FLASH"),
        createEndStep(),
    };
    ProtocolCheckResult r = protocolCheckBranch("steps", steps, 2);
    TEST_ASSERT_FALSE(r.ok);
    TEST_ASSERT_TRUE(strstr(r.message, "target") != NULL);
}

void test_dh_bad_effect() {
    SeqStep steps[] = {
        createDomeStep("DH:A:BADFX"),
        createEndStep(),
    };
    ProtocolCheckResult r = protocolCheckBranch("steps", steps, 2);
    TEST_ASSERT_FALSE(r.ok);
    TEST_ASSERT_TRUE(strstr(r.message, "effect") != NULL);
}

void test_dh_bad_color() {
    SeqStep steps[] = {
        createDomeStep("DH:A:FLASH:PINK"),
        createEndStep(),
    };
    ProtocolCheckResult r = protocolCheckBranch("steps", steps, 2);
    TEST_ASSERT_FALSE(r.ok);
    TEST_ASSERT_TRUE(strstr(r.message, "color") != NULL);
}

void test_dh_duration_out_of_range() {
    SeqStep steps[] = {
        createDomeStep("DH:A:FLASH:RED:100"),
        createEndStep(),
    };
    ProtocolCheckResult r = protocolCheckBranch("steps", steps, 2);
    TEST_ASSERT_FALSE(r.ok);
    TEST_ASSERT_TRUE(strstr(r.message, "duration") != NULL || strstr(r.message, "count") != NULL);
}

void test_dh_extra_field() {
    SeqStep steps[] = {
        createDomeStep("DH:A:FLASH:RED:10:EXTRA"),
        createEndStep(),
    };
    ProtocolCheckResult r = protocolCheckBranch("steps", steps, 2);
    TEST_ASSERT_FALSE(r.ok);
}

// =============================================================================
// Main — Unity test runner
// =============================================================================

int main(int argc, char* argv[]) {
    (void)argc;
    (void)argv;

    UNITY_BEGIN();

    // Valid cases
    RUN_TEST(test_dh_minimal_flash);
    RUN_TEST(test_dh_with_color);
    RUN_TEST(test_dh_rainbow_minimal);
    RUN_TEST(test_dh_wag_with_count);
    RUN_TEST(test_dh_reset_no_color);
    RUN_TEST(test_dh_solid_with_blue);

    // Invalid cases
    RUN_TEST(test_dh_bad_target);
    RUN_TEST(test_dh_bad_effect);
    RUN_TEST(test_dh_bad_color);
    RUN_TEST(test_dh_duration_out_of_range);
    RUN_TEST(test_dh_extra_field);

    return UNITY_END();
}
