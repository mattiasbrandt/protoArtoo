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

#include <string>

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

// Eleven trigger bindings, whatever they fire, beside the three axes: the
// twelfth is refused, as the droid stores eleven (ADR 0070, amended
// 2026-10-10).
void test_a_map_holds_eleven_trigger_bindings_beside_the_three_axes(void) {
    RcMapEntry full[kRcMapMaxEntries] = {};
    for (size_t i = 0; i < kRcMapMaxTriggers; ++i) {
        full[i] = entryOf(RC_BINDING_SBUS1, (uint8_t)(i + 4), SOUND_ACTION_NEXT);
    }
    RcRuleVerdict twelfth =
        rcRuleMapAdd(full, kRcMapMaxTriggers, entryOf(RC_BINDING_SBUS2, 1, SOUND_ACTION_NEXT), RC_INPUT_DUAL_SBUS);
    TEST_ASSERT_EQUAL_STRING("conflict: map exceeds capacity", twelfth.sentence);
    TEST_ASSERT_EQUAL_STRING("map", twelfth.field);
    TEST_ASSERT_FALSE(twelfth.aboutEntry);
    TEST_ASSERT_TRUE(rcRuleMapAdd(full, kRcMapMaxTriggers - 1, entryOf(RC_BINDING_SBUS2, 1, SOUND_ACTION_NEXT),
                                  RC_INPUT_DUAL_SBUS).ok());
    // A Reaction counts as a trigger binding too.
    TEST_ASSERT_FALSE(rcRuleMapAdd(full, kRcMapMaxTriggers, entryOf(RC_BINDING_DROID_REST, 1, SOUND_ACTION_NEXT),
                                   RC_INPUT_DUAL_SBUS).ok());
    // The axes have places of their own: eleven triggers still take all three.
    full[11] = entryOf(RC_BINDING_SBUS1, 1, DRIVE_ACTION_SPEED);
    full[12] = entryOf(RC_BINDING_SBUS1, 2, DRIVE_ACTION_STEER);
    TEST_ASSERT_TRUE(rcRuleMapAdd(full, 13, entryOf(RC_BINDING_SBUS1, 3, DOME_ACTION_SPEED), RC_INPUT_DUAL_SBUS).ok());
}

// An arm or aux toggle or the op mode sits on one RC Channel: two switches
// toggling one arm fight each other. A Reaction is not held to it, and a cue
// may repeat.
void test_a_toggle_on_a_radio_is_bound_once(void) {
    const RobotActionId toggles[] = {SERVO_ACTION_ARM1_TOGGLE, SERVO_ACTION_ARM2_TOGGLE, SERVO_ACTION_AUX1_TOGGLE,
                                     SERVO_ACTION_AUX2_TOGGLE, SERVO_ACTION_AUX3_TOGGLE, SYSTEM_ACTION_OP_MODE};
    for (RobotActionId toggle : toggles) {
        const RcMapEntry prior[] = {entryOf(RC_BINDING_SBUS1, 5, toggle)};
        RcRuleVerdict twice = rcRuleMapAdd(prior, 1, entryOf(RC_BINDING_SBUS2, 6, toggle), RC_INPUT_DUAL_SBUS);
        TEST_ASSERT_FALSE(twice.ok());
        TEST_ASSERT_EQUAL_STRING("map.action", twice.field);
        TEST_ASSERT_TRUE(twice.reason == ApplyRefusalReason::Conflict);
    }
    const RcMapEntry arm[] = {entryOf(RC_BINDING_SBUS1, 5, SERVO_ACTION_ARM1_TOGGLE)};
    TEST_ASSERT_EQUAL_STRING("conflict: arm1_toggle mapped more than once",
                             rcRuleMapAdd(arm, 1, entryOf(RC_BINDING_SBUS1, 6, SERVO_ACTION_ARM1_TOGGLE),
                                          RC_INPUT_DUAL_SBUS).sentence);
    TEST_ASSERT_TRUE(rcRuleMapAdd(arm, 1, entryOf(RC_BINDING_DROID_REST, 1, SERVO_ACTION_ARM1_TOGGLE),
                                  RC_INPUT_DUAL_SBUS).ok());
    const RcMapEntry reaction[] = {entryOf(RC_BINDING_DROID_REST, 1, SERVO_ACTION_ARM1_TOGGLE)};
    TEST_ASSERT_TRUE(rcRuleMapAdd(reaction, 1, entryOf(RC_BINDING_DROID_SPEED, 1, SERVO_ACTION_ARM1_TOGGLE),
                                  RC_INPUT_DUAL_SBUS).ok());
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

// Each end and the centre within what the receiver reports.
void test_calibration_stays_within_what_its_receiver_reports(void) {
    RcBindingConfig sbus = sbusAxis(RC_BINDING_SBUS1, 1);
    sbus.max = 2047;
    TEST_ASSERT_TRUE(rcRuleAxisCalibration(DOME_ACTION_SPEED, sbus).ok());
    sbus.max = 2048;
    RcRuleVerdict high = rcRuleAxisCalibration(DOME_ACTION_SPEED, sbus);
    TEST_ASSERT_EQUAL_STRING("calibration.max", high.field);
    TEST_ASSERT_EQUAL_STRING("out-of-range", applyRefusalReasonToken(high.reason));
    TEST_ASSERT_EQUAL(DOME_ACTION_SPEED, high.axis);

    RcBindingConfig pwm = defaultPwmBinding(1);
    pwm.min = 900;
    pwm.max = 2100;
    TEST_ASSERT_TRUE(rcRuleAxisCalibration(DRIVE_ACTION_SPEED, pwm).ok());
    pwm.min = 899;
    RcRuleVerdict low = rcRuleAxisCalibration(DRIVE_ACTION_SPEED, pwm);
    TEST_ASSERT_EQUAL_STRING("calibration.min", low.field);
    // Judged before the order, so the field named is the value out of range.
    pwm.min = 1000;
    pwm.center = 0xFFFF;
    TEST_ASSERT_EQUAL_STRING("calibration.center", rcRuleAxisCalibration(DRIVE_ACTION_SPEED, pwm).field);
    TEST_ASSERT_EQUAL_STRING("out-of-range",
                             applyRefusalReasonToken(rcRuleAxisCalibration(DRIVE_ACTION_SPEED, pwm).reason));
}

void test_calibration_runs_end_centre_end(void) {
    RcBindingConfig binding = sbusAxis(RC_BINDING_SBUS1, 1);
    TEST_ASSERT_TRUE(rcRuleAxisCalibration(DRIVE_ACTION_SPEED, binding).ok());
    binding.center = binding.max;
    TEST_ASSERT_EQUAL_STRING("calibration needs min < center < max", rcRuleAxisCalibration(DRIVE_ACTION_SPEED, binding).sentence);
}

void test_the_dead_zone_leaves_travel_on_both_sides(void) {
    // 172..992..1811: 820 below the centre, 819 above.
    RcBindingConfig binding = sbusAxis(RC_BINDING_SBUS1, 1);
    binding.deadband = 818;
    TEST_ASSERT_TRUE(rcRuleAxisCalibration(DRIVE_ACTION_SPEED, binding).ok());
    binding.deadband = 819;
    TEST_ASSERT_EQUAL_STRING("calibration leaves no travel past the deadband",
                             rcRuleAxisCalibration(DRIVE_ACTION_SPEED, binding).sentence);
    // Narrower than the whole stick, as the stored form asks, is not enough.
    binding.center = 300;
    binding.deadband = 200;
    TEST_ASSERT_TRUE(rcBindingIsValid(binding));
    TEST_ASSERT_FALSE(rcRuleAxisCalibration(DRIVE_ACTION_SPEED, binding).ok());
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

namespace {

constexpr size_t kMaxCues = 16;

// A stored map as a test holds it, with room for its slots: what the droid
// reads of it is what rcStoredMapKeepRead() leaves.
struct Stored {
    RcBindingConfig driveSpeed;
    RcBindingConfig driveSteer;
    RcBindingConfig domeSpeed;
    RcTriggerBinding cues[kMaxCues];
    size_t cueCount;
    RcStoredMap map() { return {driveSpeed, driveSteer, domeSpeed, cues, cueCount}; }
};

Stored storedOf(const RcTriggerBinding* cues, size_t count) {
    Stored stored = {sbusAxis(RC_BINDING_SBUS1, 1), sbusAxis(RC_BINDING_SBUS1, 2), sbusAxis(RC_BINDING_SBUS1, 3),
                     {}, count};
    for (size_t i = 0; i < count && i < kMaxCues; ++i) {
        stored.cues[i] = cues[i];
    }
    return stored;
}

Stored axesOnly(RcBindingConfig speed, RcBindingConfig steer, RcBindingConfig dome) {
    Stored stored = {speed, steer, dome, {}, 0};
    return stored;
}

// The bits of every bound binding the droid leaves still.
uint32_t unreadOf(const Stored& stored, RcInputMode type) {
    Stored read = stored;
    RcStoredMap map = read.map();
    rcStoredMapKeepRead(&map, type);
    uint32_t unread = 0;
    for (size_t i = 0; i < stored.cueCount; ++i) {
        if (stored.cues[i].source != RC_BINDING_NONE && map.cues[i].source == RC_BINDING_NONE) {
            unread |= rcStoredCueBit(i);
        }
    }
    const RcBindingConfig* const before[] = {&stored.driveSpeed, &stored.driveSteer, &stored.domeSpeed};
    const RcBindingConfig* const after[] = {&map.driveSpeed, &map.driveSteer, &map.domeSpeed};
    const uint32_t bits[] = {kRcStoredDriveSpeedBit, kRcStoredDriveSteerBit, kRcStoredDomeSpeedBit};
    for (size_t i = 0; i < 3; ++i) {
        if (before[i]->source != RC_BINDING_NONE && after[i]->source == RC_BINDING_NONE) {
            unread |= bits[i];
        }
    }
    return unread;
}

RcRuleVerdict whyOf(Stored stored, RcInputMode type, uint32_t bit) {
    return rcStoredMapWhy(stored.map(), type, bit);
}

RcTriggerBinding cueOn(uint8_t channel, RobotActionId action, const char* payload = nullptr,
                       RcBindingSource source = RC_BINDING_SBUS1) {
    return makeRcTriggerBinding(source, channel, action, payload, 172, 992, 1811, 0, false);
}

}  // namespace

void test_a_stored_axis_is_judged_as_a_save_would_judge_it(void) {
    const RcBindingConfig speed = sbusAxis(RC_BINDING_SBUS1, 1);
    const RcBindingConfig steer = sbusAxis(RC_BINDING_SBUS1, 2);
    Stored dome = axesOnly(speed, steer, sbusAxis(RC_BINDING_SBUS2, 1));
    TEST_ASSERT_EQUAL_HEX32(0, unreadOf(dome, RC_INPUT_DUAL_SBUS));
    TEST_ASSERT_EQUAL_HEX32(kRcStoredDomeSpeedBit, unreadOf(dome, RC_INPUT_SINGLE_SBUS));
    TEST_ASSERT_EQUAL_STRING("map.source", whyOf(dome, RC_INPUT_SINGLE_SBUS, kRcStoredDomeSpeedBit).field);
    Stored onOff = axesOnly(speed, steer, sbusAxis(RC_BINDING_SBUS1, 17));
    TEST_ASSERT_EQUAL_STRING("an axis needs a stick channel",
                             whyOf(onOff, RC_INPUT_DUAL_SBUS, kRcStoredDomeSpeedBit).sentence);
    // A stored dead zone that swallows one side: the stored form takes it,
    // the rules do not.
    RcBindingConfig swallowed = sbusAxis(RC_BINDING_SBUS1, 3);
    swallowed.center = 300;
    swallowed.deadband = 200;
    TEST_ASSERT_TRUE(rcBindingIsValid(swallowed));
    Stored dz = axesOnly(speed, steer, swallowed);
    TEST_ASSERT_EQUAL_HEX32(kRcStoredDomeSpeedBit, unreadOf(dz, RC_INPUT_DUAL_SBUS));
    TEST_ASSERT_EQUAL_STRING("calibration leaves no travel past the deadband",
                             whyOf(dz, RC_INPUT_DUAL_SBUS, kRcStoredDomeSpeedBit).sentence);
    // A read binding, and an unbound one, carry no refusal.
    TEST_ASSERT_TRUE(whyOf(dz, RC_INPUT_DUAL_SBUS, kRcStoredDriveSpeedBit).ok());
    TEST_ASSERT_TRUE(whyOf(axesOnly(speed, steer, disabledRcBinding()), RC_INPUT_DUAL_SBUS,
                           kRcStoredDomeSpeedBit).ok());
}

// The drive moves on both sticks or neither, so `read` says both are still
// when one is refused (ADR 0070, amended 2026-10-10). The refused axis carries
// its own refusal; the other waits on it, under a field of its own, so the RC
// page never acts on the wrong axis.
void test_a_drive_axis_whose_partner_is_refused_stays_still_too(void) {
    RcBindingConfig steer = sbusAxis(RC_BINDING_SBUS1, 2);
    steer.deadband = 900;
    Stored ends = axesOnly(sbusAxis(RC_BINDING_SBUS1, 1), steer, disabledRcBinding());
    TEST_ASSERT_EQUAL_HEX32(kRcStoredDriveSpeedBit | kRcStoredDriveSteerBit, unreadOf(ends, RC_INPUT_DUAL_SBUS));
    TEST_ASSERT_EQUAL_STRING("calibration.deadband", whyOf(ends, RC_INPUT_DUAL_SBUS, kRcStoredDriveSteerBit).field);
    RcRuleVerdict waits = whyOf(ends, RC_INPUT_DUAL_SBUS, kRcStoredDriveSpeedBit);
    TEST_ASSERT_EQUAL_STRING("map.drive", waits.field);
    TEST_ASSERT_EQUAL_STRING("conflict", applyRefusalReasonToken(waits.reason));
    TEST_ASSERT_EQUAL(DRIVE_ACTION_SPEED, waits.axis);

    // Both on SBUS2: Speed is refused for the receiver, Steer waits on it.
    Stored sbus2 = axesOnly(sbusAxis(RC_BINDING_SBUS2, 1), sbusAxis(RC_BINDING_SBUS2, 2), disabledRcBinding());
    TEST_ASSERT_EQUAL_HEX32(kRcStoredDriveSpeedBit | kRcStoredDriveSteerBit, unreadOf(sbus2, RC_INPUT_DUAL_SBUS));
    TEST_ASSERT_EQUAL_STRING("drive reads SBUS1, the drive receiver",
                             whyOf(sbus2, RC_INPUT_DUAL_SBUS, kRcStoredDriveSpeedBit).sentence);
    TEST_ASSERT_EQUAL_STRING("map.drive", whyOf(sbus2, RC_INPUT_DUAL_SBUS, kRcStoredDriveSteerBit).field);

    // Split across the two SBUS receivers: the pair rule names Steer, and
    // Speed waits on it.
    Stored split = axesOnly(sbusAxis(RC_BINDING_SBUS1, 1), sbusAxis(RC_BINDING_SBUS2, 2), disabledRcBinding());
    TEST_ASSERT_EQUAL_HEX32(kRcStoredDriveSpeedBit | kRcStoredDriveSteerBit, unreadOf(split, RC_INPUT_DUAL_SBUS));
    TEST_ASSERT_EQUAL_STRING("drive speed and steer must be on the same receiver",
                             whyOf(split, RC_INPUT_DUAL_SBUS, kRcStoredDriveSteerBit).sentence);
    TEST_ASSERT_EQUAL_STRING("map.drive", whyOf(split, RC_INPUT_DUAL_SBUS, kRcStoredDriveSpeedBit).field);

    // A good pair reads; the dome is no part of it.
    Stored good = axesOnly(sbusAxis(RC_BINDING_SBUS1, 1), sbusAxis(RC_BINDING_SBUS1, 2), sbusAxis(RC_BINDING_SBUS2, 3));
    TEST_ASSERT_EQUAL_HEX32(0, unreadOf(good, RC_INPUT_DUAL_SBUS));
    TEST_ASSERT_EQUAL_HEX32(kRcStoredDomeSpeedBit, unreadOf(good, RC_INPUT_SINGLE_SBUS));
}

// One drive axis with no partner moves nothing, so it is not read either.
void test_a_lone_drive_axis_waits_on_the_other(void) {
    Stored lone = axesOnly(sbusAxis(RC_BINDING_SBUS1, 1), disabledRcBinding(), disabledRcBinding());
    TEST_ASSERT_EQUAL_HEX32(kRcStoredDriveSpeedBit, unreadOf(lone, RC_INPUT_DUAL_SBUS));
    TEST_ASSERT_EQUAL_STRING("map.drive", whyOf(lone, RC_INPUT_DUAL_SBUS, kRcStoredDriveSpeedBit).field);
    TEST_ASSERT_TRUE(whyOf(lone, RC_INPUT_DUAL_SBUS, kRcStoredDriveSteerBit).ok());
}

void test_a_stored_cue_is_judged_as_a_save_would_judge_it(void) {
    const RcTriggerBinding arm[] = {cueOn(4, SERVO_ACTION_ARM1_TOGGLE)};
    TEST_ASSERT_EQUAL_HEX32(0, unreadOf(storedOf(arm, 1), RC_INPUT_DUAL_SBUS));
    const RcTriggerBinding pwm[] = {cueOn(4, SERVO_ACTION_ARM1_TOGGLE, nullptr, RC_BINDING_PWM)};
    Stored onPwm = storedOf(pwm, 1);
    onPwm.driveSpeed = defaultPwmBinding(1);
    onPwm.driveSteer = defaultPwmBinding(2);
    onPwm.domeSpeed = disabledRcBinding();
    TEST_ASSERT_EQUAL_STRING("PWM carries only the drive and dome axes",
                             whyOf(onPwm, RC_INPUT_STANDARD_PWM, rcStoredCueBit(0)).sentence);
    const RcTriggerBinding sm[] = {cueOn(6, DOME_ACTION_MARCDUINO_CMD, ":SM01")};
    TEST_ASSERT_EQUAL_STRING(":SM is diagnostic only and cannot be saved as an RC binding",
                             whyOf(storedOf(sm, 1), RC_INPUT_DUAL_SBUS, rcStoredCueBit(0)).sentence);
    // A stored Reaction is judged on the numbers its calibration fields carry.
    const RcTriggerBinding rest[] = {makeRcReactionBinding(RC_BINDING_DROID_REST, 1, SOUND_ACTION_NEXT, nullptr, 20, 5)};
    TEST_ASSERT_EQUAL_HEX32(0, unreadOf(storedOf(rest, 1), RC_INPUT_NOT_FITTED) & rcStoredCueBit(0));
}

// --- a stored map, its bindings against each other ---

// A save cut short by a power loss can leave two bindings on one control:
// both are left still, and the rest of the map reads on.
void test_two_stored_bindings_on_one_rc_channel_both_stay_still(void) {
    const RcTriggerBinding cues[] = {cueOn(4, SERVO_ACTION_ARM1_TOGGLE), cueOn(5, SOUND_ACTION_NEXT)};
    Stored stored = storedOf(cues, 2);
    TEST_ASSERT_EQUAL_HEX32(0, unreadOf(stored, RC_INPUT_DUAL_SBUS));

    // Speed on the arm's channel: both still, and Steer waits on Speed.
    stored.driveSpeed = sbusAxis(RC_BINDING_SBUS1, 4);
    TEST_ASSERT_EQUAL_HEX32(kRcStoredDriveSpeedBit | kRcStoredDriveSteerBit | rcStoredCueBit(0),
                            unreadOf(stored, RC_INPUT_DUAL_SBUS));
    RcRuleVerdict verdict = whyOf(stored, RC_INPUT_DUAL_SBUS, kRcStoredDriveSpeedBit);
    TEST_ASSERT_EQUAL_STRING("conflict: source+channel mapped more than once", verdict.sentence);
    TEST_ASSERT_EQUAL_STRING("map.channel", verdict.field);
    TEST_ASSERT_TRUE(whyOf(stored, RC_INPUT_DUAL_SBUS, rcStoredCueBit(0)).reason == ApplyRefusalReason::Conflict);
    TEST_ASSERT_EQUAL_STRING("map.drive", whyOf(stored, RC_INPUT_DUAL_SBUS, kRcStoredDriveSteerBit).field);
    TEST_ASSERT_TRUE(whyOf(stored, RC_INPUT_DUAL_SBUS, rcStoredCueBit(1)).ok());
    // The same channel number on the other receiver is another control.
    stored.driveSpeed = sbusAxis(RC_BINDING_SBUS1, 1);
    stored.domeSpeed = sbusAxis(RC_BINDING_SBUS2, 4);
    TEST_ASSERT_EQUAL_HEX32(0, unreadOf(stored, RC_INPUT_DUAL_SBUS));
}

void test_two_stored_puppet_strings_on_one_part_both_stay_still(void) {
    const RcTriggerBinding cues[] = {cueOn(7, SERVO_ACTION_PUPPET_PART, "bodyPanel1"),
                                     cueOn(8, SERVO_ACTION_PUPPET_PART, "bodyPanel1"),
                                     cueOn(9, SERVO_ACTION_PUPPET_PART, "bodyPanel2")};
    const Stored stored = storedOf(cues, 3);
    TEST_ASSERT_EQUAL_HEX32(rcStoredCueBit(0) | rcStoredCueBit(1), unreadOf(stored, RC_INPUT_DUAL_SBUS));
    TEST_ASSERT_EQUAL_STRING("conflict: a Part on two puppet strings",
                             whyOf(stored, RC_INPUT_DUAL_SBUS, rcStoredCueBit(1)).sentence);
}

// Two stored bindings of one toggle on a radio both stay still, as a save
// refuses them; a Reaction on the same toggle is not held to it.
void test_a_toggle_stored_twice_on_a_radio_stays_still(void) {
    const RcTriggerBinding cues[] = {cueOn(5, SERVO_ACTION_ARM1_TOGGLE), cueOn(6, SERVO_ACTION_ARM1_TOGGLE),
                                     makeRcReactionBinding(RC_BINDING_DROID_REST, 1, SERVO_ACTION_ARM1_TOGGLE,
                                                           nullptr, 20, 5)};
    const Stored stored = storedOf(cues, 3);
    TEST_ASSERT_EQUAL_HEX32(rcStoredCueBit(0) | rcStoredCueBit(1), unreadOf(stored, RC_INPUT_DUAL_SBUS));
    RcRuleVerdict verdict = whyOf(stored, RC_INPUT_DUAL_SBUS, rcStoredCueBit(1));
    TEST_ASSERT_EQUAL_STRING("conflict: arm1_toggle mapped more than once", verdict.sentence);
    TEST_ASSERT_EQUAL_STRING("map.action", verdict.field);
    TEST_ASSERT_TRUE(whyOf(stored, RC_INPUT_DUAL_SBUS, rcStoredCueBit(2)).ok());
}

// A binding the rules refuse on its own is not read, so it takes no control
// from another: only the read ones are judged against each other.
void test_an_unread_binding_takes_no_control_from_another(void) {
    const RcTriggerBinding cues[] = {cueOn(1, DOME_ACTION_MARCDUINO_CMD, ":SM01")};
    const Stored stored = storedOf(cues, 1);
    TEST_ASSERT_EQUAL_HEX32(rcStoredCueBit(0), unreadOf(stored, RC_INPUT_DUAL_SBUS));
    TEST_ASSERT_TRUE(whyOf(stored, RC_INPUT_DUAL_SBUS, kRcStoredDriveSpeedBit).ok());
    // An SBUS2 dome on one SBUS receiver is unread, and so frees its channel.
    const RcTriggerBinding sbus2[] = {cueOn(3, SOUND_ACTION_NEXT, nullptr, RC_BINDING_SBUS2)};
    Stored single = storedOf(sbus2, 1);
    single.domeSpeed = sbusAxis(RC_BINDING_SBUS2, 3);
    TEST_ASSERT_EQUAL_HEX32(kRcStoredDomeSpeedBit | rcStoredCueBit(0), unreadOf(single, RC_INPUT_SINGLE_SBUS));
    TEST_ASSERT_EQUAL_STRING("map.source", whyOf(single, RC_INPUT_SINGLE_SBUS, kRcStoredDomeSpeedBit).field);
    TEST_ASSERT_EQUAL_HEX32(kRcStoredDomeSpeedBit | rcStoredCueBit(0), unreadOf(single, RC_INPUT_DUAL_SBUS));
    TEST_ASSERT_EQUAL_STRING("map.channel", whyOf(single, RC_INPUT_DUAL_SBUS, kRcStoredDomeSpeedBit).field);
}

// ReactionTask reads only the Reactions a save would take: one the rules
// refuse leaves its slot, and so do two on one droid condition.
void test_a_reaction_the_rules_refuse_leaves_its_slot(void) {
    RcTriggerBinding slots[] = {
        makeRcReactionBinding(RC_BINDING_DROID_TRACK, 1, DOME_ACTION_MARCDUINO_CMD, ":SM0,150,2200", 0, 5),
        makeRcReactionBinding(RC_BINDING_DROID_REST, 1, SOUND_ACTION_NEXT, nullptr, 20, 5),
        makeRcReactionBinding(RC_BINDING_DROID_TRACK, 1, DOME_ACTION_MARCDUINO_CMD, "OP01", 0, 5),
        cueOn(6, SOUND_ACTION_NEXT),
        makeRcReactionBinding(RC_BINDING_DROID_SPEED, 1, SOUND_ACTION_NEXT, nullptr, 50, 5),
        makeRcReactionBinding(RC_BINDING_DROID_SPEED, 1, SOUND_ACTION_RANDOM_HAPPY, nullptr, 50, 5),
    };
    // The stored form's own check lets the :SM Reaction load.
    TEST_ASSERT_TRUE(rcTriggerBindingIsValid(slots[0]));
    RcStoredMap map = {disabledRcBinding(), disabledRcBinding(), disabledRcBinding(), slots, 6};
    rcStoredMapKeepRead(&map, RC_INPUT_DUAL_SBUS);
    TEST_ASSERT_EQUAL(RC_BINDING_NONE, slots[0].source);   // :SM
    TEST_ASSERT_EQUAL(RC_BINDING_DROID_REST, slots[1].source);
    TEST_ASSERT_EQUAL(RC_BINDING_NONE, slots[2].source);   // a command that starts no :, $ or #
    TEST_ASSERT_EQUAL(RC_BINDING_SBUS1, slots[3].source);  // a radio cue the rules take
    TEST_ASSERT_EQUAL(RC_BINDING_NONE, slots[4].source);   // two on one condition
    TEST_ASSERT_EQUAL(RC_BINDING_NONE, slots[5].source);
}

// --- a refusal as data ---

namespace {
std::string acceptsOf(const RcRuleVerdict& verdict) {
    char buf[48] = {};
    return rcRuleFormatAccepts(verdict, buf, sizeof(buf)) ? std::string(buf) : std::string("<none>");
}
}  // namespace

void test_a_refusal_names_its_field_reason_and_what_it_accepts(void) {
    RcRuleVerdict channel = addAlone(entryOf(RC_BINDING_SBUS1, 19, SERVO_ACTION_ARM1_TOGGLE));
    TEST_ASSERT_EQUAL_STRING("map.channel", channel.field);
    TEST_ASSERT_EQUAL_STRING("out-of-range", applyRefusalReasonToken(channel.reason));
    TEST_ASSERT_EQUAL_STRING("1..18", acceptsOf(channel).c_str());

    RcRuleVerdict stick = addAlone(entryOf(RC_BINDING_SBUS1, 17, DRIVE_ACTION_SPEED));
    TEST_ASSERT_EQUAL_STRING("map.channel", stick.field);
    TEST_ASSERT_EQUAL_STRING("1..16", acceptsOf(stick).c_str());

    RcRuleVerdict type = rcRuleMapAdd(nullptr, 0, entryOf(RC_BINDING_SBUS2, 5, SOUND_ACTION_NEXT),
                                      RC_INPUT_SINGLE_SBUS);
    TEST_ASSERT_EQUAL_STRING("map.source", type.field);
    TEST_ASSERT_EQUAL_STRING("sbus1", acceptsOf(type).c_str());
    RcRuleVerdict none = rcRuleMapAdd(nullptr, 0, entryOf(RC_BINDING_SBUS1, 5, SOUND_ACTION_NEXT),
                                      RC_INPUT_NOT_FITTED);
    TEST_ASSERT_EQUAL_STRING("<none>", acceptsOf(none).c_str());

    RcRuleVerdict cue = rcRuleMapAdd(nullptr, 0, entryOf(RC_BINDING_PWM, 4, SERVO_ACTION_ARM1_TOGGLE),
                                     RC_INPUT_STANDARD_PWM);
    TEST_ASSERT_EQUAL_STRING("map.action", cue.field);
    TEST_ASSERT_EQUAL_STRING("drive_speed,drive_steer,dome_speed", acceptsOf(cue).c_str());

    RcMapEntry track = entryOf(RC_BINDING_DROID_TRACK, 1, SOUND_ACTION_NEXT);
    track.threshold = 3;
    TEST_ASSERT_EQUAL_STRING("map.threshold", addAlone(track).field);
    TEST_ASSERT_EQUAL_STRING("0", acceptsOf(addAlone(track)).c_str());
    RcMapEntry amps = entryOf(RC_BINDING_DROID_WHEEL_AMPS, 2, SOUND_ACTION_NEXT);
    amps.threshold = 0;
    TEST_ASSERT_EQUAL_STRING("1..5000", acceptsOf(addAlone(amps)).c_str());
    amps.threshold = kRcMapEntryKeep;
    amps.quietS = 0;
    TEST_ASSERT_EQUAL_STRING("map.quietS", addAlone(amps).field);
    TEST_ASSERT_EQUAL_STRING("1..3600", acceptsOf(addAlone(amps)).c_str());
}

void test_a_conflict_names_its_field_and_accepts_nothing(void) {
    const RcMapEntry prior[] = {entryOf(RC_BINDING_SBUS1, 5, SERVO_ACTION_ARM1_TOGGLE)};
    RcRuleVerdict verdict =
        rcRuleMapAdd(prior, 1, entryOf(RC_BINDING_SBUS1, 5, SOUND_ACTION_NEXT), RC_INPUT_DUAL_SBUS);
    TEST_ASSERT_EQUAL_STRING("map.channel", verdict.field);
    TEST_ASSERT_EQUAL_STRING("conflict", applyRefusalReasonToken(verdict.reason));
    TEST_ASSERT_EQUAL_STRING("<none>", acceptsOf(verdict).c_str());

    RcRuleVerdict split = rcRuleDrive(sbusAxis(RC_BINDING_SBUS1, 1), defaultPwmBinding(2));
    TEST_ASSERT_EQUAL_STRING("map.source", split.field);
    TEST_ASSERT_EQUAL_STRING("conflict", applyRefusalReasonToken(split.reason));
    RcRuleVerdict sbus2 = rcRuleDrive(sbusAxis(RC_BINDING_SBUS2, 1), disabledRcBinding());
    TEST_ASSERT_EQUAL_STRING("out-of-range", applyRefusalReasonToken(sbus2.reason));
    TEST_ASSERT_EQUAL_STRING("sbus1", acceptsOf(sbus2).c_str());

    RcBindingConfig steer = sbusAxis(RC_BINDING_SBUS1, 2);
    steer.deadband = 900;
    RcRuleVerdict deadband = rcRuleAxisCalibration(DRIVE_ACTION_STEER, steer);
    TEST_ASSERT_EQUAL_STRING("calibration.deadband", deadband.field);
    TEST_ASSERT_EQUAL(DRIVE_ACTION_STEER, deadband.axis);
    steer.deadband = 0;
    steer.center = steer.max;
    RcRuleVerdict order = rcRuleAxisCalibration(DOME_ACTION_SPEED, steer);
    TEST_ASSERT_EQUAL_STRING("calibration.center", order.field);
    TEST_ASSERT_EQUAL(DOME_ACTION_SPEED, order.axis);
    TEST_ASSERT_EQUAL(ApplyRefusalReason::None, rcRuleDrive(disabledRcBinding(), disabledRcBinding()).reason);
}

// Every field a rule names is one the words check holds to the browser's
// table (kRcMapRefusalFields, tools/check_setting_words.py).
void test_every_field_a_rule_names_is_declared(void) {
    auto declared = [](const char* field) {
        for (const char* name : kRcMapRefusalFields) {
            if (strcmp(name, field) == 0) return true;
        }
        return false;
    };
    RcMapEntry reaction = entryOf(RC_BINDING_DROID_SPEED, 1, SOUND_ACTION_NEXT);
    reaction.threshold = 0;
    RcMapEntry quiet = entryOf(RC_BINDING_DROID_SPEED, 1, SOUND_ACTION_NEXT);
    quiet.quietS = 0;
    RcBindingConfig swallowed = sbusAxis(RC_BINDING_SBUS1, 1);
    swallowed.deadband = 900;
    RcBindingConfig crossed = sbusAxis(RC_BINDING_SBUS1, 1);
    crossed.center = crossed.max;
    RcBindingConfig outOfRange = sbusAxis(RC_BINDING_SBUS1, 1);
    outOfRange.max = 2048;
    const RcMapEntry toggle[] = {entryOf(RC_BINDING_SBUS1, 8, SERVO_ACTION_AUX1_TOGGLE)};
    Stored lone = axesOnly(sbusAxis(RC_BINDING_SBUS1, 1), disabledRcBinding(), disabledRcBinding());
    const RcMapEntry prior[] = {entryOf(RC_BINDING_SBUS1, 7, SERVO_ACTION_PUPPET_PART, "bodyPanel1"),
                                entryOf(RC_BINDING_SBUS1, 1, DRIVE_ACTION_SPEED)};
    RcMapEntry full[kRcMapMaxEntries] = {};
    const RcRuleVerdict verdicts[] = {
        addAlone(entryOf(RC_BINDING_SBUS1, 19, SOUND_ACTION_NEXT)),
        addAlone(entryOf(RC_BINDING_DROID_SPEED, 1, DRIVE_ACTION_SPEED)),
        addAlone(entryOf(RC_BINDING_DROID_SPEED, 1, SYSTEM_ACTION_ESTOP)),
        addAlone(reaction),
        addAlone(quiet),
        rcRuleMapAdd(nullptr, 0, entryOf(RC_BINDING_SBUS2, 1, SOUND_ACTION_NEXT), RC_INPUT_SINGLE_SBUS),
        rcRuleMapAdd(nullptr, 0, entryOf(RC_BINDING_PWM, 4, SOUND_ACTION_NEXT), RC_INPUT_STANDARD_PWM),
        addAlone(entryOf(RC_BINDING_SBUS1, 17, DRIVE_ACTION_SPEED)),
        addAlone(entryOf(RC_BINDING_SBUS1, 5, DOME_ACTION_MARCDUINO_CMD, ":SM01")),
        addAlone(entryOf(RC_BINDING_SBUS1, 17, SERVO_ACTION_PUPPET_PART, "bodyPanel1")),
        rcRuleMapAdd(prior, 2, entryOf(RC_BINDING_SBUS1, 8, SERVO_ACTION_PUPPET_PART, "bodyPanel1"),
                     RC_INPUT_DUAL_SBUS),
        rcRuleMapAdd(prior, 2, entryOf(RC_BINDING_SBUS1, 7, SOUND_ACTION_NEXT), RC_INPUT_DUAL_SBUS),
        rcRuleMapAdd(prior, 2, entryOf(RC_BINDING_SBUS1, 9, DRIVE_ACTION_SPEED), RC_INPUT_DUAL_SBUS),
        rcRuleMapAdd(full, kRcMapMaxEntries, entryOf(RC_BINDING_SBUS1, 1, SOUND_ACTION_NEXT), RC_INPUT_DUAL_SBUS),
        rcRuleDrive(sbusAxis(RC_BINDING_SBUS1, 1), defaultPwmBinding(2)),
        rcRuleDrive(sbusAxis(RC_BINDING_SBUS2, 1), disabledRcBinding()),
        rcRuleAxisCalibration(DRIVE_ACTION_SPEED, swallowed),
        rcRuleAxisCalibration(DRIVE_ACTION_SPEED, crossed),
        rcRuleAxisCalibration(DRIVE_ACTION_SPEED, outOfRange),
        rcRuleMapAdd(toggle, 1, entryOf(RC_BINDING_SBUS1, 9, SERVO_ACTION_AUX1_TOGGLE), RC_INPUT_DUAL_SBUS),
        rcStoredMapWhy(lone.map(), RC_INPUT_DUAL_SBUS, kRcStoredDriveSpeedBit),
    };
    for (const RcRuleVerdict& verdict : verdicts) {
        TEST_ASSERT_FALSE_MESSAGE(verdict.ok(), "each case is a refusal");
        TEST_ASSERT_TRUE_MESSAGE(declared(verdict.field), verdict.field);
        TEST_ASSERT_TRUE(verdict.reason != ApplyRefusalReason::None);
    }
}

// The receivers a map may bind, and those the drive may use (GET /api/rc/map).
void test_the_receivers_a_map_may_bind_follow_the_receiver_type(void) {
    RcBindingSource out[3] = {};
    TEST_ASSERT_EQUAL_UINT(2, rcMapReceivers(RC_INPUT_DUAL_SBUS, RcMapReceiverUse::Read, out, 3));
    TEST_ASSERT_EQUAL(RC_BINDING_SBUS1, out[0]);
    TEST_ASSERT_EQUAL(RC_BINDING_SBUS2, out[1]);
    TEST_ASSERT_EQUAL_UINT(1, rcMapReceivers(RC_INPUT_DUAL_SBUS, RcMapReceiverUse::Drive, out, 3));
    TEST_ASSERT_EQUAL(RC_BINDING_SBUS1, out[0]);
    TEST_ASSERT_EQUAL_UINT(2, rcMapReceivers(RC_INPUT_DUAL_SBUS, RcMapReceiverUse::Cue, out, 3));
    TEST_ASSERT_EQUAL_UINT(1, rcMapReceivers(RC_INPUT_SINGLE_SBUS, RcMapReceiverUse::Read, out, 3));
    TEST_ASSERT_EQUAL(RC_BINDING_SBUS1, out[0]);
    TEST_ASSERT_EQUAL_UINT(1, rcMapReceivers(RC_INPUT_STANDARD_PWM, RcMapReceiverUse::Drive, out, 3));
    TEST_ASSERT_EQUAL(RC_BINDING_PWM, out[0]);
    // PWM carries no cue (operator, 2026-10-09 on #486).
    TEST_ASSERT_EQUAL_UINT(0, rcMapReceivers(RC_INPUT_STANDARD_PWM, RcMapReceiverUse::Cue, out, 3));
    TEST_ASSERT_EQUAL_UINT(0, rcMapReceivers(RC_INPUT_NOT_FITTED, RcMapReceiverUse::Read, out, 3));
    TEST_ASSERT_EQUAL_UINT(0, rcMapReceivers(RC_INPUT_ELRS, RcMapReceiverUse::Drive, nullptr, 0));
}

// A payload the dispatcher would not send is not stored: it would fire nothing.
void test_a_payload_that_fires_nothing_is_refused(void) {
    RcRuleVerdict seq = addAlone(entryOf(RC_BINDING_SBUS1, 5, DOME_ACTION_MARCDUINO_SEQ, "37"));
    TEST_ASSERT_EQUAL_STRING("map.payload", seq.field);
    TEST_ASSERT_EQUAL_STRING("30..36", acceptsOf(seq).c_str());
    TEST_ASSERT_TRUE(addAlone(entryOf(RC_BINDING_SBUS1, 5, DOME_ACTION_MARCDUINO_SEQ, "31")).ok());
    RcRuleVerdict cmd = addAlone(entryOf(RC_BINDING_SBUS1, 5, DOME_ACTION_MARCDUINO_CMD, "OP01"));
    TEST_ASSERT_EQUAL_STRING("map.payload", cmd.field);
    TEST_ASSERT_TRUE(addAlone(entryOf(RC_BINDING_SBUS1, 5, DOME_ACTION_MARCDUINO_CMD, "$87")).ok());
    // A stored one is judged the same: not read.
    const RcTriggerBinding stored[] = {cueOn(5, DOME_ACTION_MARCDUINO_CMD, "OP01")};
    TEST_ASSERT_EQUAL_HEX32(rcStoredCueBit(0), unreadOf(storedOf(stored, 1), RC_INPUT_DUAL_SBUS));
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
    RUN_TEST(test_a_map_holds_eleven_trigger_bindings_beside_the_three_axes);
    RUN_TEST(test_a_toggle_on_a_radio_is_bound_once);
    RUN_TEST(test_the_drive_reads_one_receiver);
    RUN_TEST(test_the_drive_reads_sbus1_never_sbus2);
    RUN_TEST(test_an_unbound_drive_axis_breaks_no_drive_rule);
    RUN_TEST(test_calibration_stays_within_what_its_receiver_reports);
    RUN_TEST(test_calibration_runs_end_centre_end);
    RUN_TEST(test_the_dead_zone_leaves_travel_on_both_sides);
    RUN_TEST(test_a_map_binds_only_receivers_its_receiver_type_reads);
    RUN_TEST(test_pwm_carries_only_the_drive_and_dome_axes);
    RUN_TEST(test_an_axis_needs_a_stick_channel);
    RUN_TEST(test_a_stored_axis_is_judged_as_a_save_would_judge_it);
    RUN_TEST(test_a_drive_axis_whose_partner_is_refused_stays_still_too);
    RUN_TEST(test_a_lone_drive_axis_waits_on_the_other);
    RUN_TEST(test_a_stored_cue_is_judged_as_a_save_would_judge_it);
    RUN_TEST(test_two_stored_bindings_on_one_rc_channel_both_stay_still);
    RUN_TEST(test_two_stored_puppet_strings_on_one_part_both_stay_still);
    RUN_TEST(test_a_toggle_stored_twice_on_a_radio_stays_still);
    RUN_TEST(test_an_unread_binding_takes_no_control_from_another);
    RUN_TEST(test_a_reaction_the_rules_refuse_leaves_its_slot);
    RUN_TEST(test_a_refusal_names_its_field_reason_and_what_it_accepts);
    RUN_TEST(test_a_conflict_names_its_field_and_accepts_nothing);
    RUN_TEST(test_every_field_a_rule_names_is_declared);
    RUN_TEST(test_the_receivers_a_map_may_bind_follow_the_receiver_type);
    RUN_TEST(test_a_payload_that_fires_nothing_is_refused);
    return UNITY_END();
}
