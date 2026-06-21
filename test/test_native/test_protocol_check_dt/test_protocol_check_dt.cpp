// =============================================================================
// test_native/test_protocol_check_dt.cpp
//
// Unity native tests for DT: (Logic Text) dome command validation in
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
// VALID: Basic DT: commands
// =============================================================================

void test_dt_simple_text() {
    SeqStep steps[] = {
        createDomeStep("DT:FLD:DEFAULT:10:0:Hi"),
        createEndStep(),
    };
    ProtocolCheckResult r = protocolCheckBranch("steps", steps, 2);
    TEST_ASSERT_TRUE(r.ok);
    TEST_ASSERT_EQUAL_INT(FX_LOGIC_PSI | FX_HOLO, steps[0].effectClass);
}

void test_dt_with_newline() {
    SeqStep steps[] = {
        createDomeStep("DT:FLD:DEFAULT:10:0:You're%0AWonderful"),
        createEndStep(),
    };
    ProtocolCheckResult r = protocolCheckBranch("steps", steps, 2);
    TEST_ASSERT_TRUE(r.ok);
    TEST_ASSERT_EQUAL_INT(FX_LOGIC_PSI | FX_HOLO, steps[0].effectClass);
}

void test_dt_logic_target() {
    SeqStep steps[] = {
        createDomeStep("DT:LOGIC:RED:0:9:Test"),
        createEndStep(),
    };
    ProtocolCheckResult r = protocolCheckBranch("steps", steps, 2);
    TEST_ASSERT_TRUE(r.ok);
}

void test_dt_rld_target() {
    SeqStep steps[] = {
        createDomeStep("DT:RLD:BLUE:5:3:Hello"),
        createEndStep(),
    };
    ProtocolCheckResult r = protocolCheckBranch("steps", steps, 2);
    TEST_ASSERT_TRUE(r.ok);
}

void test_dt_max_duration() {
    SeqStep steps[] = {
        createDomeStep("DT:FLD:DEFAULT:99:0:Text"),
        createEndStep(),
    };
    ProtocolCheckResult r = protocolCheckBranch("steps", steps, 2);
    TEST_ASSERT_TRUE(r.ok);
}

void test_dt_max_speed() {
    SeqStep steps[] = {
        createDomeStep("DT:FLD:DEFAULT:10:9:Text"),
        createEndStep(),
    };
    ProtocolCheckResult r = protocolCheckBranch("steps", steps, 2);
    TEST_ASSERT_TRUE(r.ok);
}

void test_dt_percent_encoding() {
    SeqStep steps[] = {
        createDomeStep("DT:FLD:RED:8:0:General%20Kenobi"),
        createEndStep(),
    };
    ProtocolCheckResult r = protocolCheckBranch("steps", steps, 2);
    TEST_ASSERT_TRUE(r.ok);
}

void test_dt_multiple_colors() {
    SeqStep steps[] = {
        createDomeStep("DT:FLD:GREEN:5:2:Test"),
        createDomeStep("DT:RLD:YELLOW:3:1:Next"),
        createEndStep(),
    };
    ProtocolCheckResult r = protocolCheckBranch("steps", steps, 3);
    TEST_ASSERT_TRUE(r.ok);
}

// =============================================================================
// INVALID: Rejection cases
// =============================================================================

void test_dt_unknown_target() {
    SeqStep steps[] = {
        createDomeStep("DT:INVALID:DEFAULT:10:0:Text"),
        createEndStep(),
    };
    ProtocolCheckResult r = protocolCheckBranch("steps", steps, 2);
    TEST_ASSERT_FALSE(r.ok);
    TEST_ASSERT_TRUE(strstr(r.message, "target") != NULL);
}

void test_dt_unknown_color() {
    SeqStep steps[] = {
        createDomeStep("DT:FLD:PINK:10:0:Text"),
        createEndStep(),
    };
    ProtocolCheckResult r = protocolCheckBranch("steps", steps, 2);
    TEST_ASSERT_FALSE(r.ok);
    TEST_ASSERT_TRUE(strstr(r.message, "color") != NULL);
}

void test_dt_duration_out_of_range() {
    SeqStep steps[] = {
        createDomeStep("DT:FLD:DEFAULT:100:0:Text"),
        createEndStep(),
    };
    ProtocolCheckResult r = protocolCheckBranch("steps", steps, 2);
    TEST_ASSERT_FALSE(r.ok);
    TEST_ASSERT_TRUE(strstr(r.message, "duration") != NULL);
}

void test_dt_speed_out_of_range() {
    SeqStep steps[] = {
        createDomeStep("DT:FLD:DEFAULT:10:10:Text"),
        createEndStep(),
    };
    ProtocolCheckResult r = protocolCheckBranch("steps", steps, 2);
    TEST_ASSERT_FALSE(r.ok);
    TEST_ASSERT_TRUE(strstr(r.message, "speed") != NULL);
}

void test_dt_two_newlines() {
    SeqStep steps[] = {
        createDomeStep("DT:FLD:DEFAULT:10:0:A%0AB%0AC"),
        createEndStep(),
    };
    ProtocolCheckResult r = protocolCheckBranch("steps", steps, 2);
    TEST_ASSERT_FALSE(r.ok);
    TEST_ASSERT_TRUE(strstr(r.message, "newline") != NULL);
}

void test_dt_decoded_too_long() {
    // This would decode to 33 chars, exceeding limit of 32
    SeqStep steps[] = {
        createDomeStep("DT:FLD:DEFAULT:10:0:1234567890123456789012345678901234"),
        createEndStep(),
    };
    ProtocolCheckResult r = protocolCheckBranch("steps", steps, 2);
    // May fail on length or other validation
    TEST_ASSERT_FALSE(r.ok);
}

void test_dt_bad_escape() {
    SeqStep steps[] = {
        createDomeStep("DT:FLD:DEFAULT:10:0:Bad%ZZ"),
        createEndStep(),
    };
    ProtocolCheckResult r = protocolCheckBranch("steps", steps, 2);
    TEST_ASSERT_FALSE(r.ok);
    TEST_ASSERT_TRUE(strstr(r.message, "encoding") != NULL);
}

void test_dt_empty_text() {
    SeqStep steps[] = {
        createDomeStep("DT:FLD:DEFAULT:10:0:"),
        createEndStep(),
    };
    ProtocolCheckResult r = protocolCheckBranch("steps", steps, 2);
    TEST_ASSERT_FALSE(r.ok);
    TEST_ASSERT_TRUE(strstr(r.message, "empty") != NULL);
}

// =============================================================================
// Main — Unity test runner
// =============================================================================

int main(int argc, char* argv[]) {
    (void)argc;
    (void)argv;

    UNITY_BEGIN();

    // Valid cases
    RUN_TEST(test_dt_simple_text);
    RUN_TEST(test_dt_with_newline);
    RUN_TEST(test_dt_logic_target);
    RUN_TEST(test_dt_rld_target);
    RUN_TEST(test_dt_max_duration);
    RUN_TEST(test_dt_max_speed);
    RUN_TEST(test_dt_percent_encoding);
    RUN_TEST(test_dt_multiple_colors);

    // Invalid cases
    RUN_TEST(test_dt_unknown_target);
    RUN_TEST(test_dt_unknown_color);
    RUN_TEST(test_dt_duration_out_of_range);
    RUN_TEST(test_dt_speed_out_of_range);
    RUN_TEST(test_dt_two_newlines);
    RUN_TEST(test_dt_decoded_too_long);
    RUN_TEST(test_dt_bad_escape);
    RUN_TEST(test_dt_empty_text);

    return UNITY_END();
}
