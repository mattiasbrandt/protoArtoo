// =============================================================================
// test_native/test_protocol_check_dl.cpp
//
// Unity native tests for DL: (Logic/PSI Mode) dome command validation in
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
// VALID: Basic DL: commands
// =============================================================================

void test_dl_minimal_march() {
    SeqStep steps[] = {
        createDomeStep("DL:LOGIC:MARCH"),
        createEndStep(),
    };
    ProtocolCheckResult r = protocolCheckBranch("steps", steps, 2);
    TEST_ASSERT_TRUE(r.ok);
    TEST_ASSERT_EQUAL_INT(FX_LOGIC_PSI | FX_HOLO, steps[0].effectClass);
}

void test_dl_with_color() {
    SeqStep steps[] = {
        createDomeStep("DL:FLD:NORMAL:RED"),
        createEndStep(),
    };
    ProtocolCheckResult r = protocolCheckBranch("steps", steps, 2);
    TEST_ASSERT_TRUE(r.ok);
    TEST_ASSERT_EQUAL_INT(FX_LOGIC_PSI | FX_HOLO, steps[0].effectClass);
}

void test_dl_with_duration() {
    SeqStep steps[] = {
        createDomeStep("DL:PSI:ALARM:DEFAULT:10"),
        createEndStep(),
    };
    ProtocolCheckResult r = protocolCheckBranch("steps", steps, 2);
    TEST_ASSERT_TRUE(r.ok);
    TEST_ASSERT_EQUAL_INT(FX_LOGIC_PSI | FX_HOLO, steps[0].effectClass);
}

void test_dl_with_color_and_duration() {
    SeqStep steps[] = {
        createDomeStep("DL:LOGIC:MARCH:RED:47"),
        createEndStep(),
    };
    ProtocolCheckResult r = protocolCheckBranch("steps", steps, 2);
    TEST_ASSERT_TRUE(r.ok);
    TEST_ASSERT_EQUAL_INT(FX_LOGIC_PSI | FX_HOLO, steps[0].effectClass);
}

void test_dl_duration_zero() {
    SeqStep steps[] = {
        createDomeStep("DL:LOGIC:MARCH:RED:0"),
        createEndStep(),
    };
    ProtocolCheckResult r = protocolCheckBranch("steps", steps, 2);
    TEST_ASSERT_TRUE(r.ok);
}

void test_dl_duration_ninety_nine() {
    SeqStep steps[] = {
        createDomeStep("DL:LOGIC:MARCH:RED:99"),
        createEndStep(),
    };
    ProtocolCheckResult r = protocolCheckBranch("steps", steps, 2);
    TEST_ASSERT_TRUE(r.ok);
}

void test_dl_target_fld() {
    SeqStep steps[] = {
        createDomeStep("DL:FLD:MARCH"),
        createEndStep(),
    };
    ProtocolCheckResult r = protocolCheckBranch("steps", steps, 2);
    TEST_ASSERT_TRUE(r.ok);
}

void test_dl_target_rld() {
    SeqStep steps[] = {
        createDomeStep("DL:RLD:MARCH"),
        createEndStep(),
    };
    ProtocolCheckResult r = protocolCheckBranch("steps", steps, 2);
    TEST_ASSERT_TRUE(r.ok);
}

void test_dl_target_logic() {
    SeqStep steps[] = {
        createDomeStep("DL:LOGIC:MARCH"),
        createEndStep(),
    };
    ProtocolCheckResult r = protocolCheckBranch("steps", steps, 2);
    TEST_ASSERT_TRUE(r.ok);
}

void test_dl_target_fpsi() {
    SeqStep steps[] = {
        createDomeStep("DL:FPSI:MARCH"),
        createEndStep(),
    };
    ProtocolCheckResult r = protocolCheckBranch("steps", steps, 2);
    TEST_ASSERT_TRUE(r.ok);
}

void test_dl_target_rpsi() {
    SeqStep steps[] = {
        createDomeStep("DL:RPSI:MARCH"),
        createEndStep(),
    };
    ProtocolCheckResult r = protocolCheckBranch("steps", steps, 2);
    TEST_ASSERT_TRUE(r.ok);
}

void test_dl_target_psi() {
    SeqStep steps[] = {
        createDomeStep("DL:PSI:MARCH"),
        createEndStep(),
    };
    ProtocolCheckResult r = protocolCheckBranch("steps", steps, 2);
    TEST_ASSERT_TRUE(r.ok);
}

void test_dl_target_all() {
    SeqStep steps[] = {
        createDomeStep("DL:ALL:MARCH"),
        createEndStep(),
    };
    ProtocolCheckResult r = protocolCheckBranch("steps", steps, 2);
    TEST_ASSERT_TRUE(r.ok);
}

void test_dl_mode_normal() {
    SeqStep steps[] = {
        createDomeStep("DL:LOGIC:NORMAL"),
        createEndStep(),
    };
    ProtocolCheckResult r = protocolCheckBranch("steps", steps, 2);
    TEST_ASSERT_TRUE(r.ok);
}

void test_dl_mode_alarm() {
    SeqStep steps[] = {
        createDomeStep("DL:LOGIC:ALARM"),
        createEndStep(),
    };
    ProtocolCheckResult r = protocolCheckBranch("steps", steps, 2);
    TEST_ASSERT_TRUE(r.ok);
}

void test_dl_mode_failure() {
    SeqStep steps[] = {
        createDomeStep("DL:LOGIC:FAILURE"),
        createEndStep(),
    };
    ProtocolCheckResult r = protocolCheckBranch("steps", steps, 2);
    TEST_ASSERT_TRUE(r.ok);
}

void test_dl_mode_leia() {
    SeqStep steps[] = {
        createDomeStep("DL:LOGIC:LEIA"),
        createEndStep(),
    };
    ProtocolCheckResult r = protocolCheckBranch("steps", steps, 2);
    TEST_ASSERT_TRUE(r.ok);
}

void test_dl_mode_march() {
    SeqStep steps[] = {
        createDomeStep("DL:LOGIC:MARCH"),
        createEndStep(),
    };
    ProtocolCheckResult r = protocolCheckBranch("steps", steps, 2);
    TEST_ASSERT_TRUE(r.ok);
}

void test_dl_mode_flashcolor() {
    SeqStep steps[] = {
        createDomeStep("DL:LOGIC:FLASHCOLOR"),
        createEndStep(),
    };
    ProtocolCheckResult r = protocolCheckBranch("steps", steps, 2);
    TEST_ASSERT_TRUE(r.ok);
}

void test_dl_mode_redalert() {
    SeqStep steps[] = {
        createDomeStep("DL:LOGIC:REDALERT"),
        createEndStep(),
    };
    ProtocolCheckResult r = protocolCheckBranch("steps", steps, 2);
    TEST_ASSERT_TRUE(r.ok);
}

void test_dl_mode_rainbow() {
    SeqStep steps[] = {
        createDomeStep("DL:LOGIC:RAINBOW"),
        createEndStep(),
    };
    ProtocolCheckResult r = protocolCheckBranch("steps", steps, 2);
    TEST_ASSERT_TRUE(r.ok);
}

void test_dl_mode_lightsout() {
    SeqStep steps[] = {
        createDomeStep("DL:LOGIC:LIGHTSOUT"),
        createEndStep(),
    };
    ProtocolCheckResult r = protocolCheckBranch("steps", steps, 2);
    TEST_ASSERT_TRUE(r.ok);
}

void test_dl_color_default() {
    SeqStep steps[] = {
        createDomeStep("DL:LOGIC:MARCH:DEFAULT"),
        createEndStep(),
    };
    ProtocolCheckResult r = protocolCheckBranch("steps", steps, 2);
    TEST_ASSERT_TRUE(r.ok);
}

void test_dl_color_red() {
    SeqStep steps[] = {
        createDomeStep("DL:LOGIC:MARCH:RED"),
        createEndStep(),
    };
    ProtocolCheckResult r = protocolCheckBranch("steps", steps, 2);
    TEST_ASSERT_TRUE(r.ok);
}

void test_dl_color_blue() {
    SeqStep steps[] = {
        createDomeStep("DL:LOGIC:MARCH:BLUE"),
        createEndStep(),
    };
    ProtocolCheckResult r = protocolCheckBranch("steps", steps, 2);
    TEST_ASSERT_TRUE(r.ok);
}

void test_dl_color_green() {
    SeqStep steps[] = {
        createDomeStep("DL:LOGIC:MARCH:GREEN"),
        createEndStep(),
    };
    ProtocolCheckResult r = protocolCheckBranch("steps", steps, 2);
    TEST_ASSERT_TRUE(r.ok);
}

void test_dl_color_white() {
    SeqStep steps[] = {
        createDomeStep("DL:LOGIC:MARCH:WHITE"),
        createEndStep(),
    };
    ProtocolCheckResult r = protocolCheckBranch("steps", steps, 2);
    TEST_ASSERT_TRUE(r.ok);
}

void test_dl_color_yellow() {
    SeqStep steps[] = {
        createDomeStep("DL:LOGIC:MARCH:YELLOW"),
        createEndStep(),
    };
    ProtocolCheckResult r = protocolCheckBranch("steps", steps, 2);
    TEST_ASSERT_TRUE(r.ok);
}

void test_dl_color_orange() {
    SeqStep steps[] = {
        createDomeStep("DL:LOGIC:MARCH:ORANGE"),
        createEndStep(),
    };
    ProtocolCheckResult r = protocolCheckBranch("steps", steps, 2);
    TEST_ASSERT_TRUE(r.ok);
}

void test_dl_color_purple() {
    SeqStep steps[] = {
        createDomeStep("DL:LOGIC:MARCH:PURPLE"),
        createEndStep(),
    };
    ProtocolCheckResult r = protocolCheckBranch("steps", steps, 2);
    TEST_ASSERT_TRUE(r.ok);
}

// =============================================================================
// INVALID: Rejection cases
// =============================================================================

void test_dl_unknown_target() {
    SeqStep steps[] = {
        createDomeStep("DL:INVALID:MARCH"),
        createEndStep(),
    };
    ProtocolCheckResult r = protocolCheckBranch("steps", steps, 2);
    TEST_ASSERT_FALSE(r.ok);
    TEST_ASSERT_TRUE(strstr(r.message, "target") != NULL);
}

void test_dl_unknown_mode() {
    SeqStep steps[] = {
        createDomeStep("DL:LOGIC:INVALID"),
        createEndStep(),
    };
    ProtocolCheckResult r = protocolCheckBranch("steps", steps, 2);
    TEST_ASSERT_FALSE(r.ok);
    TEST_ASSERT_TRUE(strstr(r.message, "mode") != NULL);
}

void test_dl_unknown_color() {
    SeqStep steps[] = {
        createDomeStep("DL:LOGIC:MARCH:PINK"),
        createEndStep(),
    };
    ProtocolCheckResult r = protocolCheckBranch("steps", steps, 2);
    TEST_ASSERT_FALSE(r.ok);
    TEST_ASSERT_TRUE(strstr(r.message, "color") != NULL);
}

void test_dl_duration_out_of_range_100() {
    SeqStep steps[] = {
        createDomeStep("DL:LOGIC:MARCH:RED:100"),
        createEndStep(),
    };
    ProtocolCheckResult r = protocolCheckBranch("steps", steps, 2);
    TEST_ASSERT_FALSE(r.ok);
    TEST_ASSERT_TRUE(strstr(r.message, "duration") != NULL);
}

void test_dl_missing_mode() {
    SeqStep steps[] = {
        createDomeStep("DL:LOGIC"),
        createEndStep(),
    };
    ProtocolCheckResult r = protocolCheckBranch("steps", steps, 2);
    TEST_ASSERT_FALSE(r.ok);
}

void test_dl_extra_trailing_field() {
    SeqStep steps[] = {
        createDomeStep("DL:LOGIC:MARCH:RED:10:EXTRA"),
        createEndStep(),
    };
    ProtocolCheckResult r = protocolCheckBranch("steps", steps, 2);
    TEST_ASSERT_FALSE(r.ok);
}

void test_dl_lowercase_target() {
    SeqStep steps[] = {
        createDomeStep("DL:logic:MARCH"),
        createEndStep(),
    };
    ProtocolCheckResult r = protocolCheckBranch("steps", steps, 2);
    TEST_ASSERT_FALSE(r.ok);
}

void test_dl_lowercase_mode() {
    SeqStep steps[] = {
        createDomeStep("DL:LOGIC:march"),
        createEndStep(),
    };
    ProtocolCheckResult r = protocolCheckBranch("steps", steps, 2);
    TEST_ASSERT_FALSE(r.ok);
}

void test_dl_lowercase_color() {
    SeqStep steps[] = {
        createDomeStep("DL:LOGIC:MARCH:red"),
        createEndStep(),
    };
    ProtocolCheckResult r = protocolCheckBranch("steps", steps, 2);
    TEST_ASSERT_FALSE(r.ok);
}

void test_dl_nonnumeric_duration() {
    SeqStep steps[] = {
        createDomeStep("DL:LOGIC:MARCH:RED:TEN"),
        createEndStep(),
    };
    ProtocolCheckResult r = protocolCheckBranch("steps", steps, 2);
    TEST_ASSERT_FALSE(r.ok);
}

void test_dl_empty_command() {
    SeqStep steps[] = {
        createDomeStep(""),
        createEndStep(),
    };
    ProtocolCheckResult r = protocolCheckBranch("steps", steps, 2);
    TEST_ASSERT_FALSE(r.ok);
}

void test_dl_prefix_only() {
    SeqStep steps[] = {
        createDomeStep("DL:"),
        createEndStep(),
    };
    ProtocolCheckResult r = protocolCheckBranch("steps", steps, 2);
    TEST_ASSERT_FALSE(r.ok);
}

// =============================================================================
// Integration: Mixed commands
// =============================================================================

void test_dl_followed_by_panel_close() {
    SeqStep steps[] = {
        createDomeStep("DL:LOGIC:MARCH", 0),
        createDomeStep(":CL15", 50),
        createEndStep(100),
    };
    ProtocolCheckResult r = protocolCheckBranch("steps", steps, 3);
    TEST_ASSERT_TRUE(r.ok);
}

void test_dl_followed_by_dv_preset() {
    SeqStep steps[] = {
        createDomeStep("DL:LOGIC:MARCH", 0),
        createDomeStep("DV:ROCKMARCH", 50),
        createEndStep(100),
    };
    ProtocolCheckResult r = protocolCheckBranch("steps", steps, 3);
    TEST_ASSERT_TRUE(r.ok);
}

void test_multiple_dl_commands() {
    SeqStep steps[] = {
        createDomeStep("DL:LOGIC:MARCH", 0),
        createDomeStep("DL:PSI:ALARM:BLUE:20", 50),
        createEndStep(100),
    };
    ProtocolCheckResult r = protocolCheckBranch("steps", steps, 3);
    TEST_ASSERT_TRUE(r.ok);
}

// =============================================================================
// Main — Unity test runner
// =============================================================================

int main(int argc, char* argv[]) {
    (void)argc;
    (void)argv;

    UNITY_BEGIN();

    // Valid cases
    RUN_TEST(test_dl_minimal_march);
    RUN_TEST(test_dl_with_color);
    RUN_TEST(test_dl_with_duration);
    RUN_TEST(test_dl_with_color_and_duration);
    RUN_TEST(test_dl_duration_zero);
    RUN_TEST(test_dl_duration_ninety_nine);
    RUN_TEST(test_dl_target_fld);
    RUN_TEST(test_dl_target_rld);
    RUN_TEST(test_dl_target_logic);
    RUN_TEST(test_dl_target_fpsi);
    RUN_TEST(test_dl_target_rpsi);
    RUN_TEST(test_dl_target_psi);
    RUN_TEST(test_dl_target_all);
    RUN_TEST(test_dl_mode_normal);
    RUN_TEST(test_dl_mode_alarm);
    RUN_TEST(test_dl_mode_failure);
    RUN_TEST(test_dl_mode_leia);
    RUN_TEST(test_dl_mode_march);
    RUN_TEST(test_dl_mode_flashcolor);
    RUN_TEST(test_dl_mode_redalert);
    RUN_TEST(test_dl_mode_rainbow);
    RUN_TEST(test_dl_mode_lightsout);
    RUN_TEST(test_dl_color_default);
    RUN_TEST(test_dl_color_red);
    RUN_TEST(test_dl_color_blue);
    RUN_TEST(test_dl_color_green);
    RUN_TEST(test_dl_color_white);
    RUN_TEST(test_dl_color_yellow);
    RUN_TEST(test_dl_color_orange);
    RUN_TEST(test_dl_color_purple);

    // Invalid cases
    RUN_TEST(test_dl_unknown_target);
    RUN_TEST(test_dl_unknown_mode);
    RUN_TEST(test_dl_unknown_color);
    RUN_TEST(test_dl_duration_out_of_range_100);
    RUN_TEST(test_dl_missing_mode);
    RUN_TEST(test_dl_extra_trailing_field);
    RUN_TEST(test_dl_lowercase_target);
    RUN_TEST(test_dl_lowercase_mode);
    RUN_TEST(test_dl_lowercase_color);
    RUN_TEST(test_dl_nonnumeric_duration);
    RUN_TEST(test_dl_empty_command);
    RUN_TEST(test_dl_prefix_only);

    // Integration cases
    RUN_TEST(test_dl_followed_by_panel_close);
    RUN_TEST(test_dl_followed_by_dv_preset);
    RUN_TEST(test_multiple_dl_commands);

    return UNITY_END();
}
