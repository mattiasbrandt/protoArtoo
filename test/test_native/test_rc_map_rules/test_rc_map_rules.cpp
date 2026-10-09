// =============================================================================
// test/test_native/test_rc_map_rules/test_rc_map_rules.cpp
//
// The RC Map's rules (ADR 0070, include/rc_map_rules.h), one rule per case,
// through the module's own interface: which RC Receivers a receiver type
// reads, what an entry may be and conflict with, the drive pair, and an
// axis's calibration.
// =============================================================================
#include <unity.h>

#include <stdio.h>
#include <string.h>

#include "rc_map_rules.h"
#include "robot_state.h"

namespace {

RcReceiverSetup setupFor(RcInputMode mode, bool ch1, bool ch2, bool useCh2 = false) {
    RcReceiverSetup setup = {};
    setup.mode = mode;
    setup.enableRc[0] = ch1;
    setup.enableRc[1] = ch2;
    setup.useCh2 = useCh2;
    return setup;
}

RcMapEntry entryOf(RcBindingSource source, uint8_t channel, RobotActionId action,
                   const char* payload = "") {
    RcMapEntry entry = {};
    entry.source = source;
    entry.channel = channel;
    entry.action = action;
    snprintf(entry.payload, sizeof(entry.payload), "%s", payload);
    entry.threshold = kRcMapEntryKeep;
    entry.quietS = kRcMapEntryKeep;
    return entry;
}

RcRuleVerdict addAlone(const RcMapEntry& entry) {
    return rcRuleMapAdd(nullptr, 0, entry, RC_INPUT_DUAL_SBUS);
}

RcBindingConfig sbusAxis(RcBindingSource source, uint8_t channel) {
    return defaultSbusBinding(source, channel);
}

}  // namespace

void setUp(void) {}
void tearDown(void) {}

// --- which RC Receivers a receiver type reads ---

void test_single_sbus_reads_sbus1_on_the_header_it_is_wired_to(void) {
    TEST_ASSERT_TRUE(rcReceiverReads(RC_BINDING_SBUS1, setupFor(RC_INPUT_SINGLE_SBUS, true, true)));
    TEST_ASSERT_FALSE(rcReceiverReads(RC_BINDING_SBUS1, setupFor(RC_INPUT_SINGLE_SBUS, false, true)));
    // On the CH2 header it is still SBUS1, gated by CH2's enable.
    TEST_ASSERT_TRUE(rcReceiverReads(RC_BINDING_SBUS1, setupFor(RC_INPUT_SINGLE_SBUS, false, true, true)));
    TEST_ASSERT_FALSE(rcReceiverReads(RC_BINDING_SBUS1, setupFor(RC_INPUT_SINGLE_SBUS, true, false, true)));
}

void test_single_sbus_never_reads_sbus2_or_pwm(void) {
    TEST_ASSERT_FALSE(rcReceiverReads(RC_BINDING_SBUS2, setupFor(RC_INPUT_SINGLE_SBUS, true, true)));
    TEST_ASSERT_FALSE(rcReceiverReads(RC_BINDING_SBUS2, setupFor(RC_INPUT_SINGLE_SBUS, true, true, true)));
    TEST_ASSERT_FALSE(rcReceiverReads(RC_BINDING_PWM, setupFor(RC_INPUT_SINGLE_SBUS, true, true)));
}

void test_dual_sbus_reads_each_receiver_its_enable_allows(void) {
    TEST_ASSERT_TRUE(rcReceiverReads(RC_BINDING_SBUS1, setupFor(RC_INPUT_DUAL_SBUS, true, false)));
    TEST_ASSERT_FALSE(rcReceiverReads(RC_BINDING_SBUS2, setupFor(RC_INPUT_DUAL_SBUS, true, false)));
    TEST_ASSERT_FALSE(rcReceiverReads(RC_BINDING_SBUS1, setupFor(RC_INPUT_DUAL_SBUS, false, true)));
    TEST_ASSERT_TRUE(rcReceiverReads(RC_BINDING_SBUS2, setupFor(RC_INPUT_DUAL_SBUS, false, true)));
    // The CH2-header pick is single_sbus's; dual ignores it.
    TEST_ASSERT_TRUE(rcReceiverReads(RC_BINDING_SBUS1, setupFor(RC_INPUT_DUAL_SBUS, true, false, true)));
}

void test_pwm_reads_when_any_rc_channel_is_enabled(void) {
    RcReceiverSetup setup = setupFor(RC_INPUT_STANDARD_PWM, false, false);
    TEST_ASSERT_FALSE(rcReceiverReads(RC_BINDING_PWM, setup));
    setup.enableRc[5] = true;
    TEST_ASSERT_TRUE(rcReceiverReads(RC_BINDING_PWM, setup));
    TEST_ASSERT_FALSE(rcReceiverReads(RC_BINDING_SBUS1, setup));
    TEST_ASSERT_FALSE(rcReceiverReads(RC_BINDING_PWM, setupFor(RC_INPUT_DUAL_SBUS, true, true)));
}

void test_elrs_not_fitted_and_droid_conditions_read_nothing(void) {
    const RcBindingSource sources[] = {RC_BINDING_PWM, RC_BINDING_SBUS1, RC_BINDING_SBUS2};
    for (RcBindingSource source : sources) {
        TEST_ASSERT_FALSE(rcReceiverReads(source, setupFor(RC_INPUT_ELRS, true, true)));
        TEST_ASSERT_FALSE(rcReceiverReads(source, setupFor(RC_INPUT_NOT_FITTED, true, true)));
    }
    TEST_ASSERT_FALSE(rcReceiverReads(RC_BINDING_NONE, setupFor(RC_INPUT_DUAL_SBUS, true, true)));
    TEST_ASSERT_FALSE(rcReceiverReads(RC_BINDING_DROID_SPEED, setupFor(RC_INPUT_DUAL_SBUS, true, true)));
}

// --- one entry on its own ---

void test_an_rc_channel_outside_its_receiver_is_refused(void) {
    TEST_ASSERT_EQUAL_STRING("channel out of range",
                             addAlone(entryOf(RC_BINDING_SBUS1, 19, SERVO_ACTION_ARM1_TOGGLE)).sentence);
    TEST_ASSERT_EQUAL_STRING("channel out of range",
                             addAlone(entryOf(RC_BINDING_PWM, 7, SERVO_ACTION_ARM1_TOGGLE)).sentence);
    TEST_ASSERT_TRUE(addAlone(entryOf(RC_BINDING_SBUS1, 18, SERVO_ACTION_ARM1_TOGGLE)).ok());
    TEST_ASSERT_FALSE(addAlone(entryOf(RC_BINDING_SBUS1, 19, SERVO_ACTION_ARM1_TOGGLE)).aboutEntry);
}

void test_a_droid_condition_takes_no_axis_and_not_the_three_it_may_not_fire(void) {
    RcRuleVerdict axis = addAlone(entryOf(RC_BINDING_DROID_SPEED, 1, DOME_ACTION_SPEED));
    TEST_ASSERT_EQUAL_STRING("a droid condition cannot drive an axis", axis.sentence);
    TEST_ASSERT_TRUE(axis.aboutEntry);
    const RobotActionId refused[] = {SYSTEM_ACTION_ESTOP, SYSTEM_ACTION_OP_MODE,
                                     DRIVE_ACTION_SPEED_PRESET_CYCLE};
    for (RobotActionId action : refused) {
        TEST_ASSERT_EQUAL_STRING("action not allowed on a droid condition",
                                 addAlone(entryOf(RC_BINDING_DROID_SPEED, 1, action)).sentence);
    }
    TEST_ASSERT_TRUE(addAlone(entryOf(RC_BINDING_DROID_SPEED, 1, SOUND_ACTION_RANDOM_HAPPY)).ok());
}

void test_a_reaction_number_outside_its_range_is_refused_and_one_left_out_is_not(void) {
    RcMapEntry entry = entryOf(RC_BINDING_DROID_SPEED, 1, SOUND_ACTION_RANDOM_HAPPY);
    entry.threshold = 0;
    TEST_ASSERT_EQUAL_STRING("threshold out of range", addAlone(entry).sentence);
    entry.threshold = rcReactionThresholdMax(RC_BINDING_DROID_SPEED) + 1;
    TEST_ASSERT_EQUAL_STRING("threshold out of range", addAlone(entry).sentence);
    entry.threshold = rcReactionThresholdMax(RC_BINDING_DROID_SPEED);
    TEST_ASSERT_TRUE(addAlone(entry).ok());
    entry.quietS = RC_REACTION_QUIET_MAX_S + 1;
    TEST_ASSERT_EQUAL_STRING("quiet period out of range", addAlone(entry).sentence);
    entry.quietS = kRcMapEntryKeep;
    TEST_ASSERT_TRUE(addAlone(entry).ok());
    // A condition with no threshold takes only 0.
    RcMapEntry track = entryOf(RC_BINDING_DROID_TRACK, 1, SOUND_ACTION_RANDOM_HAPPY);
    track.threshold = 1;
    TEST_ASSERT_EQUAL_STRING("threshold out of range", addAlone(track).sentence);
    track.threshold = 0;
    TEST_ASSERT_TRUE(addAlone(track).ok());
}

void test_sm_is_diagnostic_only(void) {
    RcRuleVerdict verdict = addAlone(entryOf(RC_BINDING_SBUS1, 5, DOME_ACTION_MARCDUINO_CMD, ":SM01"));
    TEST_ASSERT_EQUAL_STRING(":SM is diagnostic only and cannot be saved as an RC binding", verdict.sentence);
    TEST_ASSERT_TRUE(addAlone(entryOf(RC_BINDING_SBUS1, 5, DOME_ACTION_MARCDUINO_CMD, ":OP01")).ok());
}

void test_a_puppet_string_needs_an_sbus_stick_channel(void) {
    const char* const stick = "a puppet string needs an SBUS stick channel (CH1-CH16)";
    TEST_ASSERT_EQUAL_STRING(stick, addAlone(entryOf(RC_BINDING_SBUS1, 17, SERVO_ACTION_PUPPET_PART,
                                                     "bodyPanel1")).sentence);
    // On PWM the string is a cue PWM does not carry.
    TEST_ASSERT_EQUAL_STRING("PWM carries only the drive and dome axes",
                             rcRuleMapAdd(nullptr, 0,
                                          entryOf(RC_BINDING_PWM, 3, SERVO_ACTION_PUPPET_PART, "bodyPanel1"),
                                          RC_INPUT_STANDARD_PWM).sentence);
    TEST_ASSERT_TRUE(addAlone(entryOf(RC_BINDING_SBUS2, 16, SERVO_ACTION_PUPPET_PART, "bodyPanel1")).ok());
}

// --- an entry against the ones before it ---

void test_one_rc_channel_holds_one_job(void) {
    const RcMapEntry prior[] = {entryOf(RC_BINDING_SBUS1, 5, SERVO_ACTION_ARM1_TOGGLE)};
    RcRuleVerdict verdict = rcRuleMapAdd(prior, 1, entryOf(RC_BINDING_SBUS1, 5, SOUND_ACTION_NEXT), RC_INPUT_DUAL_SBUS);
    TEST_ASSERT_EQUAL_STRING("conflict: source+channel mapped more than once", verdict.sentence);
    TEST_ASSERT_TRUE(verdict.aboutEntry);
    TEST_ASSERT_TRUE(rcRuleMapAdd(prior, 1, entryOf(RC_BINDING_SBUS2, 5, SOUND_ACTION_NEXT), RC_INPUT_DUAL_SBUS).ok());
}

void test_each_axis_is_bound_once(void) {
    const RcMapEntry prior[] = {entryOf(RC_BINDING_SBUS1, 1, DRIVE_ACTION_SPEED),
                                entryOf(RC_BINDING_SBUS1, 2, DRIVE_ACTION_STEER),
                                entryOf(RC_BINDING_SBUS1, 4, DOME_ACTION_SPEED)};
    TEST_ASSERT_EQUAL_STRING("conflict: drive_speed mapped more than once",
                             rcRuleMapAdd(prior, 3, entryOf(RC_BINDING_SBUS1, 7, DRIVE_ACTION_SPEED), RC_INPUT_DUAL_SBUS).sentence);
    TEST_ASSERT_EQUAL_STRING("conflict: drive_steer mapped more than once",
                             rcRuleMapAdd(prior, 3, entryOf(RC_BINDING_SBUS1, 7, DRIVE_ACTION_STEER), RC_INPUT_DUAL_SBUS).sentence);
    TEST_ASSERT_EQUAL_STRING("conflict: dome_speed mapped more than once",
                             rcRuleMapAdd(prior, 3, entryOf(RC_BINDING_SBUS1, 7, DOME_ACTION_SPEED), RC_INPUT_DUAL_SBUS).sentence);
    // A cue may repeat: two buttons can play the same sound.
    const RcMapEntry cue[] = {entryOf(RC_BINDING_SBUS1, 5, SOUND_ACTION_NEXT)};
    TEST_ASSERT_TRUE(rcRuleMapAdd(cue, 1, entryOf(RC_BINDING_SBUS1, 6, SOUND_ACTION_NEXT), RC_INPUT_DUAL_SBUS).ok());
}

void test_one_part_has_one_puppet_string(void) {
    const RcMapEntry prior[] = {entryOf(RC_BINDING_SBUS1, 7, SERVO_ACTION_PUPPET_PART, "bodyPanel1")};
    TEST_ASSERT_EQUAL_STRING("conflict: a Part on two puppet strings",
                             rcRuleMapAdd(prior, 1, entryOf(RC_BINDING_SBUS1, 8, SERVO_ACTION_PUPPET_PART,
                                                            "bodyPanel1"), RC_INPUT_DUAL_SBUS).sentence);
    TEST_ASSERT_TRUE(rcRuleMapAdd(prior, 1, entryOf(RC_BINDING_SBUS1, 8, SERVO_ACTION_PUPPET_PART,
                                                    "bodyPanel2"), RC_INPUT_DUAL_SBUS).ok());
}

void test_a_map_holds_at_most_fourteen_entries(void) {
    RcMapEntry full[kRcMapMaxEntries] = {};
    for (size_t i = 0; i < kRcMapMaxEntries; ++i) {
        full[i] = entryOf(RC_BINDING_SBUS1, (uint8_t)(i + 1), SOUND_ACTION_NEXT);
    }
    RcRuleVerdict verdict =
        rcRuleMapAdd(full, kRcMapMaxEntries, entryOf(RC_BINDING_SBUS2, 1, SOUND_ACTION_NEXT), RC_INPUT_DUAL_SBUS);
    TEST_ASSERT_EQUAL_STRING("conflict: map exceeds capacity", verdict.sentence);
    TEST_ASSERT_FALSE(verdict.aboutEntry);
    TEST_ASSERT_TRUE(
        rcRuleMapAdd(full, kRcMapMaxEntries - 1, entryOf(RC_BINDING_SBUS2, 1, SOUND_ACTION_NEXT), RC_INPUT_DUAL_SBUS).ok());
}

// --- the drive pair ---

void test_the_drive_reads_one_receiver(void) {
    RcRuleVerdict verdict = rcRuleDrive(sbusAxis(RC_BINDING_SBUS1, 1), defaultPwmBinding(2));
    TEST_ASSERT_EQUAL_STRING("drive speed and steer must be on the same receiver", verdict.sentence);
    TEST_ASSERT_EQUAL(DRIVE_ACTION_STEER, verdict.axis);
    TEST_ASSERT_TRUE(rcRuleDrive(sbusAxis(RC_BINDING_SBUS1, 1), sbusAxis(RC_BINDING_SBUS1, 2)).ok());
    TEST_ASSERT_TRUE(rcRuleDrive(defaultPwmBinding(1), defaultPwmBinding(2)).ok());
}

void test_the_drive_reads_sbus1_never_sbus2(void) {
    RcRuleVerdict both = rcRuleDrive(sbusAxis(RC_BINDING_SBUS2, 1), sbusAxis(RC_BINDING_SBUS2, 2));
    TEST_ASSERT_EQUAL_STRING("drive reads SBUS1, the drive receiver", both.sentence);
    TEST_ASSERT_EQUAL(DRIVE_ACTION_SPEED, both.axis);
    RcRuleVerdict steer = rcRuleDrive(disabledRcBinding(), sbusAxis(RC_BINDING_SBUS2, 2));
    TEST_ASSERT_EQUAL_STRING("drive reads SBUS1, the drive receiver", steer.sentence);
    TEST_ASSERT_EQUAL(DRIVE_ACTION_STEER, steer.axis);
}

void test_an_unbound_drive_axis_breaks_no_drive_rule(void) {
    TEST_ASSERT_TRUE(rcRuleDrive(disabledRcBinding(), disabledRcBinding()).ok());
    TEST_ASSERT_TRUE(rcRuleDrive(sbusAxis(RC_BINDING_SBUS1, 1), disabledRcBinding()).ok());
}

// --- an axis's calibration ---

void test_calibration_runs_end_centre_end(void) {
    RcBindingConfig binding = sbusAxis(RC_BINDING_SBUS1, 1);
    TEST_ASSERT_TRUE(rcRuleAxisCalibration(binding).ok());
    binding.center = binding.max;
    TEST_ASSERT_EQUAL_STRING("calibration needs min < center < max", rcRuleAxisCalibration(binding).sentence);
}

void test_the_dead_zone_leaves_travel_on_both_sides(void) {
    // 172..992..1811: 820 below the centre, 819 above.
    RcBindingConfig binding = sbusAxis(RC_BINDING_SBUS1, 1);
    binding.deadband = 818;
    TEST_ASSERT_TRUE(rcRuleAxisCalibration(binding).ok());
    binding.deadband = 819;
    TEST_ASSERT_EQUAL_STRING("calibration leaves no travel past the deadband",
                             rcRuleAxisCalibration(binding).sentence);
    // Narrower than the whole stick, as the stored form asks, is not enough.
    binding.center = 300;
    binding.deadband = 200;
    TEST_ASSERT_TRUE(rcBindingIsValid(binding));
    TEST_ASSERT_FALSE(rcRuleAxisCalibration(binding).ok());
}

// --- the receiver type the map is for ---

void test_a_map_binds_only_receivers_its_receiver_type_reads(void) {
    const char* const notRead = "the RC Receiver type does not read this source";
    RcMapEntry sbus2 = entryOf(RC_BINDING_SBUS2, 5, SOUND_ACTION_NEXT);
    TEST_ASSERT_TRUE(rcRuleMapAdd(nullptr, 0, sbus2, RC_INPUT_DUAL_SBUS).ok());
    RcRuleVerdict single = rcRuleMapAdd(nullptr, 0, sbus2, RC_INPUT_SINGLE_SBUS);
    TEST_ASSERT_EQUAL_STRING(notRead, single.sentence);
    TEST_ASSERT_TRUE(single.aboutEntry);
    TEST_ASSERT_EQUAL_STRING(notRead, rcRuleMapAdd(nullptr, 0, entryOf(RC_BINDING_PWM, 1, DRIVE_ACTION_SPEED),
                                                   RC_INPUT_DUAL_SBUS).sentence);
    TEST_ASSERT_EQUAL_STRING(notRead, rcRuleMapAdd(nullptr, 0, entryOf(RC_BINDING_SBUS1, 1, DRIVE_ACTION_SPEED),
                                                   RC_INPUT_NOT_FITTED).sentence);
    // A Reaction is the droid's own, whatever radio is fitted.
    TEST_ASSERT_TRUE(rcRuleMapAdd(nullptr, 0, entryOf(RC_BINDING_DROID_REST, 1, SOUND_ACTION_NEXT),
                                  RC_INPUT_NOT_FITTED).ok());
}

void test_pwm_carries_only_the_drive_and_dome_axes(void) {
    const RobotActionId axes[] = {DRIVE_ACTION_SPEED, DRIVE_ACTION_STEER, DOME_ACTION_SPEED};
    for (RobotActionId axis : axes) {
        TEST_ASSERT_TRUE(rcRuleMapAdd(nullptr, 0, entryOf(RC_BINDING_PWM, 1, axis), RC_INPUT_STANDARD_PWM).ok());
    }
    RcRuleVerdict cue = rcRuleMapAdd(nullptr, 0, entryOf(RC_BINDING_PWM, 4, SERVO_ACTION_ARM1_TOGGLE),
                                     RC_INPUT_STANDARD_PWM);
    TEST_ASSERT_EQUAL_STRING("PWM carries only the drive and dome axes", cue.sentence);
    TEST_ASSERT_TRUE(cue.aboutEntry);
}

void test_an_axis_needs_a_stick_channel(void) {
    TEST_ASSERT_EQUAL_STRING("an axis needs a stick channel",
                             addAlone(entryOf(RC_BINDING_SBUS1, 17, DRIVE_ACTION_SPEED)).sentence);
    TEST_ASSERT_EQUAL_STRING("an axis needs a stick channel",
                             addAlone(entryOf(RC_BINDING_SBUS2, 18, DOME_ACTION_SPEED)).sentence);
    TEST_ASSERT_TRUE(addAlone(entryOf(RC_BINDING_SBUS1, 16, DRIVE_ACTION_SPEED)).ok());
    // A cue may sit on an on/off channel.
    TEST_ASSERT_TRUE(addAlone(entryOf(RC_BINDING_SBUS1, 17, SERVO_ACTION_ARM1_TOGGLE)).ok());
}

// --- the same rules on read ---

void test_a_stored_axis_is_judged_as_a_save_would_judge_it(void) {
    RcBindingConfig dome = sbusAxis(RC_BINDING_SBUS2, 1);
    TEST_ASSERT_TRUE(rcRuleStoredAxis(DOME_ACTION_SPEED, dome, RC_INPUT_DUAL_SBUS).ok());
    TEST_ASSERT_FALSE(rcRuleStoredAxis(DOME_ACTION_SPEED, dome, RC_INPUT_SINGLE_SBUS).ok());
    RcBindingConfig onOff = sbusAxis(RC_BINDING_SBUS1, 17);
    TEST_ASSERT_EQUAL_STRING("an axis needs a stick channel",
                             rcRuleStoredAxis(DOME_ACTION_SPEED, onOff, RC_INPUT_DUAL_SBUS).sentence);
    // A stored dead zone that swallows one side: the stored form takes it,
    // the rules do not.
    RcBindingConfig swallowed = sbusAxis(RC_BINDING_SBUS1, 3);
    swallowed.center = 300;
    swallowed.deadband = 200;
    TEST_ASSERT_TRUE(rcBindingIsValid(swallowed));
    TEST_ASSERT_EQUAL_STRING("calibration leaves no travel past the deadband",
                             rcRuleStoredAxis(DOME_ACTION_SPEED, swallowed, RC_INPUT_DUAL_SBUS).sentence);
}

void test_a_stored_drive_names_the_axis_a_rule_refuses(void) {
    RcBindingConfig steer = sbusAxis(RC_BINDING_SBUS1, 2);
    steer.deadband = 900;
    RcRuleVerdict verdict = rcRuleStoredDrive(sbusAxis(RC_BINDING_SBUS1, 1), steer, RC_INPUT_DUAL_SBUS);
    TEST_ASSERT_EQUAL_STRING("calibration leaves no travel past the deadband", verdict.sentence);
    TEST_ASSERT_EQUAL(DRIVE_ACTION_STEER, verdict.axis);
    RcRuleVerdict sbus2 = rcRuleStoredDrive(sbusAxis(RC_BINDING_SBUS2, 1), sbusAxis(RC_BINDING_SBUS2, 2),
                                            RC_INPUT_DUAL_SBUS);
    TEST_ASSERT_EQUAL_STRING("drive reads SBUS1, the drive receiver", sbus2.sentence);
    TEST_ASSERT_TRUE(rcRuleStoredDrive(sbusAxis(RC_BINDING_SBUS1, 1), sbusAxis(RC_BINDING_SBUS1, 2),
                                       RC_INPUT_SINGLE_SBUS).ok());
    TEST_ASSERT_TRUE(rcRuleStoredDrive(disabledRcBinding(), disabledRcBinding(), RC_INPUT_DUAL_SBUS).ok());
}

void test_a_stored_cue_is_judged_as_a_save_would_judge_it(void) {
    RcTriggerBinding arm = makeRcTriggerBinding(RC_BINDING_SBUS1, 4, SERVO_ACTION_ARM1_TOGGLE, nullptr,
                                                172, 992, 1811, 0, false);
    TEST_ASSERT_TRUE(rcRuleStoredCue(arm, RC_INPUT_DUAL_SBUS).ok());
    arm.source = RC_BINDING_PWM;
    TEST_ASSERT_EQUAL_STRING("PWM carries only the drive and dome axes",
                             rcRuleStoredCue(arm, RC_INPUT_STANDARD_PWM).sentence);
    RcTriggerBinding sm = makeRcTriggerBinding(RC_BINDING_SBUS1, 6, DOME_ACTION_MARCDUINO_CMD, ":SM01",
                                               172, 992, 1811, 0, false);
    TEST_ASSERT_EQUAL_STRING(":SM is diagnostic only and cannot be saved as an RC binding",
                             rcRuleStoredCue(sm, RC_INPUT_DUAL_SBUS).sentence);
    // A stored Reaction is judged on the numbers its calibration fields carry.
    RcTriggerBinding rest = makeRcReactionBinding(RC_BINDING_DROID_REST, 1, SOUND_ACTION_NEXT, nullptr, 20, 5);
    TEST_ASSERT_TRUE(rcRuleStoredCue(rest, RC_INPUT_NOT_FITTED).ok());
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_single_sbus_reads_sbus1_on_the_header_it_is_wired_to);
    RUN_TEST(test_single_sbus_never_reads_sbus2_or_pwm);
    RUN_TEST(test_dual_sbus_reads_each_receiver_its_enable_allows);
    RUN_TEST(test_pwm_reads_when_any_rc_channel_is_enabled);
    RUN_TEST(test_elrs_not_fitted_and_droid_conditions_read_nothing);
    RUN_TEST(test_an_rc_channel_outside_its_receiver_is_refused);
    RUN_TEST(test_a_droid_condition_takes_no_axis_and_not_the_three_it_may_not_fire);
    RUN_TEST(test_a_reaction_number_outside_its_range_is_refused_and_one_left_out_is_not);
    RUN_TEST(test_sm_is_diagnostic_only);
    RUN_TEST(test_a_puppet_string_needs_an_sbus_stick_channel);
    RUN_TEST(test_one_rc_channel_holds_one_job);
    RUN_TEST(test_each_axis_is_bound_once);
    RUN_TEST(test_one_part_has_one_puppet_string);
    RUN_TEST(test_a_map_holds_at_most_fourteen_entries);
    RUN_TEST(test_the_drive_reads_one_receiver);
    RUN_TEST(test_the_drive_reads_sbus1_never_sbus2);
    RUN_TEST(test_an_unbound_drive_axis_breaks_no_drive_rule);
    RUN_TEST(test_calibration_runs_end_centre_end);
    RUN_TEST(test_the_dead_zone_leaves_travel_on_both_sides);
    RUN_TEST(test_a_map_binds_only_receivers_its_receiver_type_reads);
    RUN_TEST(test_pwm_carries_only_the_drive_and_dome_axes);
    RUN_TEST(test_an_axis_needs_a_stick_channel);
    RUN_TEST(test_a_stored_axis_is_judged_as_a_save_would_judge_it);
    RUN_TEST(test_a_stored_drive_names_the_axis_a_rule_refuses);
    RUN_TEST(test_a_stored_cue_is_judged_as_a_save_would_judge_it);
    return UNITY_END();
}
