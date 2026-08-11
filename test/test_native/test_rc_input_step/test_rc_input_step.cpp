// =============================================================================
// test/test_native/test_rc_input_step/test_rc_input_step.cpp
//
// RC Input Step Core: decoder desired state, receiver-change reinit ordering,
// watchdog transitions, failsafe clears, and zero-frame decisions.
// =============================================================================

#include <unity.h>

#include "rc_input_step.h"

#define RC_INPUT_STANDARD_PWM 0
#define RC_INPUT_SINGLE_SBUS 1
#define RC_INPUT_DUAL_SBUS 2

void setUp() {}
void tearDown() {}

// ============================================================================
// Initialization Tests
// ============================================================================

void test_init_zeros_watchdog_state() {
    RcInputStepState state;
    state.sbus1Watchdog.signalLost = true;
    state.sbus2Watchdog.signalLost = true;
    state.lastUseCh2 = true;

    rcInputStepInit(&state);

    TEST_ASSERT_FALSE(state.sbus1Watchdog.signalLost);
    TEST_ASSERT_FALSE(state.sbus2Watchdog.signalLost);
    TEST_ASSERT_FALSE(state.lastUseCh2);
}

// ============================================================================
// Decoder Desired State: PWM Mode
// ============================================================================

void test_pwm_mode_disables_all_sbus() {
    RcInputStepState state = {};
    RcInputStepTickInputs in = {RC_INPUT_STANDARD_PWM, true, true, false};
    RcInputStepTickActions out = rcInputStepTick(&state, in);

    TEST_ASSERT_FALSE(out.driveSbusDesiredEnabled);
    TEST_ASSERT_FALSE(out.domeSbusDesiredEnabled);
}

void test_pwm_mode_clears_all_failsafe_layers() {
    RcInputStepState state = {};
    RcInputStepTickInputs in = {RC_INPUT_STANDARD_PWM, true, true, false};
    RcInputStepTickActions out = rcInputStepTick(&state, in);

    TEST_ASSERT_TRUE(out.clearSbusWatchdog);
    TEST_ASSERT_TRUE(out.clearSbusHw);
    TEST_ASSERT_TRUE(out.clearSbus2SignalLost);
    TEST_ASSERT_TRUE(out.clearSbus2HwFailsafe);
}

// ============================================================================
// Decoder Desired State: Single SBUS Mode
// ============================================================================

void test_single_sbus_ch1_enabled_drive_only() {
    RcInputStepState state = {};
    RcInputStepTickInputs in = {RC_INPUT_SINGLE_SBUS, true, false, false};
    RcInputStepTickActions out = rcInputStepTick(&state, in);

    TEST_ASSERT_TRUE(out.driveSbusDesiredEnabled);
    TEST_ASSERT_FALSE(out.domeSbusDesiredEnabled);
}

void test_single_sbus_ch2_disabled_nothing() {
    RcInputStepState state = {};
    RcInputStepTickInputs in = {RC_INPUT_SINGLE_SBUS, false, true, false};
    RcInputStepTickActions out = rcInputStepTick(&state, in);

    TEST_ASSERT_FALSE(out.driveSbusDesiredEnabled);
    TEST_ASSERT_FALSE(out.domeSbusDesiredEnabled);
}

void test_single_sbus_use_ch2_false_reads_ch1() {
    RcInputStepState state = {};
    RcInputStepTickInputs in = {RC_INPUT_SINGLE_SBUS, true, true, false};
    RcInputStepTickActions out = rcInputStepTick(&state, in);

    TEST_ASSERT_TRUE(out.driveSbusDesiredEnabled);
    TEST_ASSERT_FALSE(out.domeSbusDesiredEnabled);
}

void test_single_sbus_use_ch2_true_reads_ch2() {
    RcInputStepState state = {};
    RcInputStepTickInputs in = {RC_INPUT_SINGLE_SBUS, true, true, true};
    RcInputStepTickActions out = rcInputStepTick(&state, in);

    TEST_ASSERT_TRUE(out.driveSbusDesiredEnabled);
    TEST_ASSERT_FALSE(out.domeSbusDesiredEnabled);
}

// ============================================================================
// Decoder Desired State: Dual SBUS Mode
// ============================================================================

void test_dual_sbus_ch1_only_drive_only() {
    RcInputStepState state = {};
    RcInputStepTickInputs in = {RC_INPUT_DUAL_SBUS, true, false, false};
    RcInputStepTickActions out = rcInputStepTick(&state, in);

    TEST_ASSERT_TRUE(out.driveSbusDesiredEnabled);
    TEST_ASSERT_FALSE(out.domeSbusDesiredEnabled);
}

void test_dual_sbus_ch2_only_dome_only() {
    RcInputStepState state = {};
    RcInputStepTickInputs in = {RC_INPUT_DUAL_SBUS, false, true, false};
    RcInputStepTickActions out = rcInputStepTick(&state, in);

    TEST_ASSERT_FALSE(out.driveSbusDesiredEnabled);
    TEST_ASSERT_TRUE(out.domeSbusDesiredEnabled);
}

void test_dual_sbus_both_enabled() {
    RcInputStepState state = {};
    RcInputStepTickInputs in = {RC_INPUT_DUAL_SBUS, true, true, false};
    RcInputStepTickActions out = rcInputStepTick(&state, in);

    TEST_ASSERT_TRUE(out.driveSbusDesiredEnabled);
    TEST_ASSERT_TRUE(out.domeSbusDesiredEnabled);
}

void test_dual_sbus_both_disabled() {
    RcInputStepState state = {};
    RcInputStepTickInputs in = {RC_INPUT_DUAL_SBUS, false, false, false};
    RcInputStepTickActions out = rcInputStepTick(&state, in);

    TEST_ASSERT_FALSE(out.driveSbusDesiredEnabled);
    TEST_ASSERT_FALSE(out.domeSbusDesiredEnabled);
}

// ============================================================================
// Receiver-Change Reinit Ordering (End-Before-Begin + Baseline Frozen)
// ============================================================================

void test_single_sbus_usech2_change_from_false_to_true() {
    RcInputStepState state = {};
    state.lastUseCh2 = false;

    RcInputStepTickInputs in = {RC_INPUT_SINGLE_SBUS, true, true, true};
    RcInputStepTickActions out = rcInputStepTick(&state, in);

    TEST_ASSERT_TRUE(out.shouldEndDriveSbus);       // end-before-begin
    TEST_ASSERT_TRUE(out.shouldUpdateLastUseCh2);   // update baseline
}

void test_single_sbus_usech2_change_from_true_to_false() {
    RcInputStepState state = {};
    state.lastUseCh2 = true;

    RcInputStepTickInputs in = {RC_INPUT_SINGLE_SBUS, true, true, false};
    RcInputStepTickActions out = rcInputStepTick(&state, in);

    TEST_ASSERT_TRUE(out.shouldEndDriveSbus);
    TEST_ASSERT_TRUE(out.shouldUpdateLastUseCh2);
}

void test_single_sbus_usech2_stable_no_reinit() {
    RcInputStepState state = {};
    state.lastUseCh2 = false;

    RcInputStepTickInputs in = {RC_INPUT_SINGLE_SBUS, true, true, false};
    RcInputStepTickActions out = rcInputStepTick(&state, in);

    TEST_ASSERT_FALSE(out.shouldEndDriveSbus);
    TEST_ASSERT_FALSE(out.shouldUpdateLastUseCh2);
}

void test_baseline_frozen_across_mode_transitions() {
    RcInputStepState state = {};
    state.lastUseCh2 = false;

    // Change to dual_sbus; useCh2 is irrelevant outside single_sbus
    RcInputStepTickInputs in1 = {RC_INPUT_DUAL_SBUS, true, true, false};
    RcInputStepTickActions out1 = rcInputStepTick(&state, in1);

    TEST_ASSERT_FALSE(out1.shouldUpdateLastUseCh2);  // not in single_sbus mode
    TEST_ASSERT_FALSE(state.lastUseCh2);             // frozen at false

    // Return to single_sbus with useCh2=true; should trigger reinit
    RcInputStepTickInputs in2 = {RC_INPUT_SINGLE_SBUS, true, true, true};
    RcInputStepTickActions out2 = rcInputStepTick(&state, in2);

    TEST_ASSERT_TRUE(out2.shouldEndDriveSbus);
    TEST_ASSERT_TRUE(out2.shouldUpdateLastUseCh2);
}

void test_mode_change_from_single_to_dual_no_false_reinit() {
    RcInputStepState state = {};
    state.lastUseCh2 = true;

    // In single_sbus, useCh2=true, so baseline is true
    RcInputStepTickInputs in1 = {RC_INPUT_SINGLE_SBUS, true, true, true};
    rcInputStepTick(&state, in1);

    // Change to dual_sbus; baseline should NOT update
    RcInputStepTickInputs in2 = {RC_INPUT_DUAL_SBUS, true, true, false};
    RcInputStepTickActions out2 = rcInputStepTick(&state, in2);

    TEST_ASSERT_FALSE(out2.shouldUpdateLastUseCh2);
    TEST_ASSERT_TRUE(state.lastUseCh2);  // still true, frozen
}

// ============================================================================
// Decoder Init/Deinit State Machine
// ============================================================================

void test_drive_sbus_begin_when_desired_enabled_not_initialized() {
    RcInputStepDecoderStateInputs in = {
        .rcInputMode = RC_INPUT_SINGLE_SBUS,
        .driveSbusDesiredEnabled = true,
        .domeSbusDesiredEnabled = false,
        .driveSbusInitialized = false,
        .domeSbusInitialized = false,
        .shouldEndDriveSbus = false,
    };

    RcInputStepDecoderStateActions out = rcInputStepDecoderState(in);

    TEST_ASSERT_TRUE(out.shouldBeginDriveSbus);
    TEST_ASSERT_FALSE(out.shouldEndDriveSbus);
}

void test_drive_sbus_end_when_desired_disabled_initialized() {
    RcInputStepDecoderStateInputs in = {
        .rcInputMode = RC_INPUT_STANDARD_PWM,
        .driveSbusDesiredEnabled = false,
        .domeSbusDesiredEnabled = false,
        .driveSbusInitialized = true,
        .domeSbusInitialized = false,
        .shouldEndDriveSbus = false,
    };

    RcInputStepDecoderStateActions out = rcInputStepDecoderState(in);

    TEST_ASSERT_TRUE(out.shouldEndDriveSbus);
    TEST_ASSERT_FALSE(out.shouldBeginDriveSbus);
}

void test_drive_sbus_end_before_begin_ordering() {
    RcInputStepDecoderStateInputs in = {
        .rcInputMode = RC_INPUT_SINGLE_SBUS,
        .driveSbusDesiredEnabled = true,
        .domeSbusDesiredEnabled = false,
        .driveSbusInitialized = true,
        .domeSbusInitialized = false,
        .shouldEndDriveSbus = true,  // end-before-begin signal
    };

    RcInputStepDecoderStateActions out = rcInputStepDecoderState(in);

    TEST_ASSERT_TRUE(out.shouldEndDriveSbus);
    TEST_ASSERT_FALSE(out.shouldBeginDriveSbus);  // don't begin yet
}

void test_dome_sbus_begin_when_desired_enabled_not_initialized() {
    RcInputStepDecoderStateInputs in = {
        .rcInputMode = RC_INPUT_DUAL_SBUS,
        .driveSbusDesiredEnabled = true,
        .domeSbusDesiredEnabled = true,
        .driveSbusInitialized = true,
        .domeSbusInitialized = false,
        .shouldEndDriveSbus = false,
    };

    RcInputStepDecoderStateActions out = rcInputStepDecoderState(in);

    TEST_ASSERT_TRUE(out.shouldBeginDomeSbus);
    TEST_ASSERT_FALSE(out.shouldEndDomeSbus);
}

void test_dome_sbus_end_when_desired_disabled_initialized() {
    RcInputStepDecoderStateInputs in = {
        .rcInputMode = RC_INPUT_DUAL_SBUS,
        .driveSbusDesiredEnabled = true,
        .domeSbusDesiredEnabled = false,
        .driveSbusInitialized = true,
        .domeSbusInitialized = true,
        .shouldEndDriveSbus = false,
    };

    RcInputStepDecoderStateActions out = rcInputStepDecoderState(in);

    TEST_ASSERT_TRUE(out.shouldEndDomeSbus);
    TEST_ASSERT_FALSE(out.shouldBeginDomeSbus);
}

void test_no_action_when_state_matches_desired() {
    RcInputStepDecoderStateInputs in = {
        .rcInputMode = RC_INPUT_DUAL_SBUS,
        .driveSbusDesiredEnabled = true,
        .domeSbusDesiredEnabled = true,
        .driveSbusInitialized = true,
        .domeSbusInitialized = true,
        .shouldEndDriveSbus = false,
    };

    RcInputStepDecoderStateActions out = rcInputStepDecoderState(in);

    TEST_ASSERT_FALSE(out.shouldBeginDriveSbus);
    TEST_ASSERT_FALSE(out.shouldEndDriveSbus);
    TEST_ASSERT_FALSE(out.shouldBeginDomeSbus);
    TEST_ASSERT_FALSE(out.shouldEndDomeSbus);
}

// ============================================================================
// SBUS1 Watchdog Timeout to Failsafe Transitions
// ============================================================================

void test_sbus1_watchdog_just_lost_transition() {
    RcInputStepState state = {};
    RcInputStepSbus1WatchdogInputs in = {
        .rcInputMode = RC_INPUT_SINGLE_SBUS,
        .useCh2 = false,
        .driveSbusInitialized = true,
        .lastSbus1Ms = 100,
        .nowMs = 400,  // >200ms timeout
        .timeoutMs = 200,
    };

    RcInputStepSbus1WatchdogActions out = rcInputStepSbus1Watchdog(&state, in);

    TEST_ASSERT_EQUAL(SbusWatchdogTransition::JUST_LOST, out.transition);
    TEST_ASSERT_TRUE(out.triggerSbusWatchdog);
    TEST_ASSERT_TRUE(out.submitDriveZeroFrame);
}

void test_sbus1_watchdog_just_restored_transition() {
    RcInputStepState state = {};
    state.sbus1Watchdog.signalLost = true;
    RcInputStepSbus1WatchdogInputs in = {
        .rcInputMode = RC_INPUT_SINGLE_SBUS,
        .useCh2 = false,
        .driveSbusInitialized = true,
        .lastSbus1Ms = 100,
        .nowMs = 120,  // <200ms timeout
        .timeoutMs = 200,
    };

    RcInputStepSbus1WatchdogActions out = rcInputStepSbus1Watchdog(&state, in);

    TEST_ASSERT_EQUAL(SbusWatchdogTransition::JUST_RESTORED, out.transition);
    TEST_ASSERT_TRUE(out.clearSbusWatchdog);
    TEST_ASSERT_TRUE(out.clearSbusHw);
}

void test_sbus1_watchdog_ok_clears_hw_failsafe() {
    RcInputStepState state = {};
    state.sbus1Watchdog.signalLost = false;
    RcInputStepSbus1WatchdogInputs in = {
        .rcInputMode = RC_INPUT_SINGLE_SBUS,
        .useCh2 = false,
        .driveSbusInitialized = true,
        .lastSbus1Ms = 100,
        .nowMs = 120,
        .timeoutMs = 200,
    };

    RcInputStepSbus1WatchdogActions out = rcInputStepSbus1Watchdog(&state, in);

    TEST_ASSERT_EQUAL(SbusWatchdogTransition::OK, out.transition);
    TEST_ASSERT_TRUE(out.clearSbusHw);
    TEST_ASSERT_FALSE(out.triggerSbusWatchdog);
}

void test_sbus1_tracking_disabled_resets_watchdog() {
    RcInputStepState state = {};
    state.sbus1Watchdog.signalLost = true;
    RcInputStepSbus1WatchdogInputs in = {
        .rcInputMode = RC_INPUT_STANDARD_PWM,
        .useCh2 = false,
        .driveSbusInitialized = false,
        .lastSbus1Ms = 100,
        .nowMs = 500,
        .timeoutMs = 200,
    };

    rcInputStepSbus1Watchdog(&state, in);

    TEST_ASSERT_FALSE(state.sbus1Watchdog.signalLost);  // reset
}

void test_sbus1_single_sbus_ch2_not_tracked() {
    RcInputStepState state = {};
    RcInputStepSbus1WatchdogInputs in = {
        .rcInputMode = RC_INPUT_SINGLE_SBUS,
        .useCh2 = true,  // using CH2 for drive, not CH1
        .driveSbusInitialized = true,
        .lastSbus1Ms = 100,
        .nowMs = 500,  // would timeout
        .timeoutMs = 200,
    };

    RcInputStepSbus1WatchdogActions out = rcInputStepSbus1Watchdog(&state, in);

    TEST_ASSERT_FALSE(out.triggerSbusWatchdog);  // tracking not active
}

// ============================================================================
// SBUS2 Watchdog Timeout to Failsafe Transitions
// ============================================================================

void test_sbus2_watchdog_just_lost_transition() {
    RcInputStepState state = {};
    RcInputStepSbus2WatchdogInputs in = {
        .domeSbusInitialized = true,
        .lastSbus2Ms = 100,
        .nowMs = 400,  // >200ms timeout
        .timeoutMs = 200,
    };

    RcInputStepSbus2WatchdogActions out = rcInputStepSbus2Watchdog(&state, in);

    TEST_ASSERT_EQUAL(SbusWatchdogTransition::JUST_LOST, out.transition);
    TEST_ASSERT_TRUE(out.setSbus2SignalLost);
    TEST_ASSERT_TRUE(out.shouldStopDome);
}

void test_sbus2_watchdog_just_restored_transition() {
    RcInputStepState state = {};
    state.sbus2Watchdog.signalLost = true;
    RcInputStepSbus2WatchdogInputs in = {
        .domeSbusInitialized = true,
        .lastSbus2Ms = 100,
        .nowMs = 120,  // <200ms timeout
        .timeoutMs = 200,
    };

    RcInputStepSbus2WatchdogActions out = rcInputStepSbus2Watchdog(&state, in);

    TEST_ASSERT_EQUAL(SbusWatchdogTransition::JUST_RESTORED, out.transition);
    TEST_ASSERT_TRUE(out.clearSbus2SignalLost);
    TEST_ASSERT_FALSE(out.shouldStopDome);
}

void test_sbus2_tracking_disabled_resets_watchdog() {
    RcInputStepState state = {};
    state.sbus2Watchdog.signalLost = true;
    RcInputStepSbus2WatchdogInputs in = {
        .domeSbusInitialized = false,
        .lastSbus2Ms = 100,
        .nowMs = 500,  // would timeout
        .timeoutMs = 200,
    };

    rcInputStepSbus2Watchdog(&state, in);

    TEST_ASSERT_FALSE(state.sbus2Watchdog.signalLost);  // reset
}

// ============================================================================
// Zero-Frame Submission Decisions on Signal Loss
// ============================================================================

void test_zero_frame_submitted_on_pwm_signal_lost() {
    RcInputStepZeroFrameInputs in = {
        .pwmSignalLost = true,
        .sbusHwFailsafe = false,
    };

    RcInputStepZeroFrameActions out = rcInputStepZeroFrame(in);

    TEST_ASSERT_TRUE(out.submitDriveZeroFrame);
}

void test_zero_frame_submitted_on_sbus_hw_failsafe() {
    RcInputStepZeroFrameInputs in = {
        .pwmSignalLost = false,
        .sbusHwFailsafe = true,
    };

    RcInputStepZeroFrameActions out = rcInputStepZeroFrame(in);

    TEST_ASSERT_TRUE(out.submitDriveZeroFrame);
}

void test_zero_frame_submitted_on_both_pwm_and_sbus_fail() {
    RcInputStepZeroFrameInputs in = {
        .pwmSignalLost = true,
        .sbusHwFailsafe = true,
    };

    RcInputStepZeroFrameActions out = rcInputStepZeroFrame(in);

    TEST_ASSERT_TRUE(out.submitDriveZeroFrame);
}

void test_zero_frame_not_submitted_when_both_ok() {
    RcInputStepZeroFrameInputs in = {
        .pwmSignalLost = false,
        .sbusHwFailsafe = false,
    };

    RcInputStepZeroFrameActions out = rcInputStepZeroFrame(in);

    TEST_ASSERT_FALSE(out.submitDriveZeroFrame);
}

// ============================================================================
// Unity Test Runner
// ============================================================================

int main(void) {
    UNITY_BEGIN();

    RUN_TEST(test_init_zeros_watchdog_state);

    RUN_TEST(test_pwm_mode_disables_all_sbus);
    RUN_TEST(test_pwm_mode_clears_all_failsafe_layers);

    RUN_TEST(test_single_sbus_ch1_enabled_drive_only);
    RUN_TEST(test_single_sbus_ch2_disabled_nothing);
    RUN_TEST(test_single_sbus_use_ch2_false_reads_ch1);
    RUN_TEST(test_single_sbus_use_ch2_true_reads_ch2);

    RUN_TEST(test_dual_sbus_ch1_only_drive_only);
    RUN_TEST(test_dual_sbus_ch2_only_dome_only);
    RUN_TEST(test_dual_sbus_both_enabled);
    RUN_TEST(test_dual_sbus_both_disabled);

    RUN_TEST(test_single_sbus_usech2_change_from_false_to_true);
    RUN_TEST(test_single_sbus_usech2_change_from_true_to_false);
    RUN_TEST(test_single_sbus_usech2_stable_no_reinit);
    RUN_TEST(test_baseline_frozen_across_mode_transitions);
    RUN_TEST(test_mode_change_from_single_to_dual_no_false_reinit);

    RUN_TEST(test_drive_sbus_begin_when_desired_enabled_not_initialized);
    RUN_TEST(test_drive_sbus_end_when_desired_disabled_initialized);
    RUN_TEST(test_drive_sbus_end_before_begin_ordering);
    RUN_TEST(test_dome_sbus_begin_when_desired_enabled_not_initialized);
    RUN_TEST(test_dome_sbus_end_when_desired_disabled_initialized);
    RUN_TEST(test_no_action_when_state_matches_desired);

    RUN_TEST(test_sbus1_watchdog_just_lost_transition);
    RUN_TEST(test_sbus1_watchdog_just_restored_transition);
    RUN_TEST(test_sbus1_watchdog_ok_clears_hw_failsafe);
    RUN_TEST(test_sbus1_tracking_disabled_resets_watchdog);
    RUN_TEST(test_sbus1_single_sbus_ch2_not_tracked);

    RUN_TEST(test_sbus2_watchdog_just_lost_transition);
    RUN_TEST(test_sbus2_watchdog_just_restored_transition);
    RUN_TEST(test_sbus2_tracking_disabled_resets_watchdog);

    RUN_TEST(test_zero_frame_submitted_on_pwm_signal_lost);
    RUN_TEST(test_zero_frame_submitted_on_sbus_hw_failsafe);
    RUN_TEST(test_zero_frame_submitted_on_both_pwm_and_sbus_fail);
    RUN_TEST(test_zero_frame_not_submitted_when_both_ok);

    return UNITY_END();
}
