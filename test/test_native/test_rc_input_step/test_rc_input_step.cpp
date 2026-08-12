// =============================================================================
// test/test_native/test_rc_input_step/test_rc_input_step.cpp
//
// RC Input Step Core: boot planning, watchdog timelines, per-frame safety, and
// zero-frame decisions.
// =============================================================================

#include <unity.h>

#include "rc_input_step.h"

#define RC_INPUT_STANDARD_PWM 0
#define RC_INPUT_SINGLE_SBUS 1
#define RC_INPUT_DUAL_SBUS 2

void setUp() {}
void tearDown() {}

// Build the shared boot-active seam tersely for startup-plan cases. These
// tests call it so each case varies only mode, routing, and the six RC inputs.
RcInputActiveConfig makeActiveRc(uint8_t mode, bool useCh2, bool ch1, bool ch2, bool ch3,
                                  bool ch4, bool ch5, bool ch6) {
    RcInputActiveConfig active = {};
    active.mode = mode;
    active.useCh2 = useCh2;
    active.enableRc[0] = ch1;
    active.enableRc[1] = ch2;
    active.enableRc[2] = ch3;
    active.enableRc[3] = ch4;
    active.enableRc[4] = ch5;
    active.enableRc[5] = ch6;
    return active;
}

// ============================================================================
// Startup Decision
// ============================================================================

void test_task_disabled_at_boot_when_all_rc_components_off() {
    RcInputActiveConfig in = makeActiveRc(RC_INPUT_STANDARD_PWM, false, false, false,
                                           false, false, false, false);

    TEST_ASSERT_FALSE(rcInputStepStartupPlan(in).taskEnabled);
}

void test_task_enabled_at_boot_when_rc_component_1_is_on() {
    RcInputActiveConfig in = makeActiveRc(RC_INPUT_STANDARD_PWM, false, true, false,
                                           false, false, false, false);

    TEST_ASSERT_TRUE(rcInputStepStartupPlan(in).taskEnabled);
}

void test_task_enabled_at_boot_when_rc_component_2_is_on() {
    RcInputActiveConfig in = makeActiveRc(RC_INPUT_STANDARD_PWM, false, false, true,
                                           false, false, false, false);

    TEST_ASSERT_TRUE(rcInputStepStartupPlan(in).taskEnabled);
}

void test_task_enabled_at_boot_when_rc_component_3_is_on() {
    RcInputActiveConfig in = makeActiveRc(RC_INPUT_STANDARD_PWM, false, false, false,
                                           true, false, false, false);

    TEST_ASSERT_TRUE(rcInputStepStartupPlan(in).taskEnabled);
}

void test_task_enabled_at_boot_when_rc_component_4_is_on() {
    RcInputActiveConfig in = makeActiveRc(RC_INPUT_STANDARD_PWM, false, false, false,
                                           false, true, false, false);

    TEST_ASSERT_TRUE(rcInputStepStartupPlan(in).taskEnabled);
}

void test_task_enabled_at_boot_when_rc_component_5_is_on() {
    RcInputActiveConfig in = makeActiveRc(RC_INPUT_STANDARD_PWM, false, false, false,
                                           false, false, true, false);

    TEST_ASSERT_TRUE(rcInputStepStartupPlan(in).taskEnabled);
}

void test_task_enabled_at_boot_when_rc_component_6_is_on() {
    RcInputActiveConfig in = makeActiveRc(RC_INPUT_STANDARD_PWM, false, false, false,
                                           false, false, false, true);

    TEST_ASSERT_TRUE(rcInputStepStartupPlan(in).taskEnabled);
}

void test_single_sbus_parks_when_only_unselected_receiver_is_on() {
    RcInputActiveConfig in = makeActiveRc(RC_INPUT_SINGLE_SBUS, false, false, true,
                                           false, false, false, false);

    TEST_ASSERT_FALSE(rcInputStepStartupPlan(in).taskEnabled);
}

void test_single_sbus_ch2_route_parks_when_only_ch1_is_on() {
    RcInputActiveConfig in = makeActiveRc(RC_INPUT_SINGLE_SBUS, true, true, false,
                                           false, false, false, false);

    TEST_ASSERT_FALSE(rcInputStepStartupPlan(in).taskEnabled);
}

void test_single_sbus_ch1_route_runs_when_ch1_is_on() {
    RcInputActiveConfig in = makeActiveRc(RC_INPUT_SINGLE_SBUS, false, true, false,
                                           false, false, false, false);

    TEST_ASSERT_TRUE(rcInputStepStartupPlan(in).taskEnabled);
}

void test_single_sbus_ch2_route_runs_when_ch2_is_on() {
    RcInputActiveConfig in = makeActiveRc(RC_INPUT_SINGLE_SBUS, true, false, true,
                                           false, false, false, false);

    TEST_ASSERT_TRUE(rcInputStepStartupPlan(in).taskEnabled);
}

void test_dual_sbus_runs_when_ch1_is_on() {
    RcInputActiveConfig in = makeActiveRc(RC_INPUT_DUAL_SBUS, false, true, false,
                                           false, false, false, false);

    TEST_ASSERT_TRUE(rcInputStepStartupPlan(in).taskEnabled);
}

void test_dual_sbus_runs_when_ch2_is_on() {
    RcInputActiveConfig in = makeActiveRc(RC_INPUT_DUAL_SBUS, false, false, true,
                                           false, false, false, false);

    TEST_ASSERT_TRUE(rcInputStepStartupPlan(in).taskEnabled);
}

// ============================================================================
// Drive Watchdog Source Selection (Slice 1)
// ============================================================================

void test_pwm_mode_identifies_no_drive_watchdog_source() {
    RcInputActiveConfig in = makeActiveRc(RC_INPUT_STANDARD_PWM, false, true, false,
                                           false, false, false, false);

    TEST_ASSERT_EQUAL(DriveWatchdogSource::NONE, rcInputStepStartupPlan(in).driveWatchdogSource);
}

void test_single_sbus_ch1_identifies_sbus1_drive_watchdog_source() {
    RcInputActiveConfig in = makeActiveRc(RC_INPUT_SINGLE_SBUS, false, true, false,
                                           false, false, false, false);

    TEST_ASSERT_EQUAL(DriveWatchdogSource::SBUS1,
                      rcInputStepStartupPlan(in).driveWatchdogSource);
}

void test_single_sbus_routed_ch2_identifies_sbus2_routed_drive_watchdog_source() {
    RcInputActiveConfig in = makeActiveRc(RC_INPUT_SINGLE_SBUS, true, false, true,
                                           false, false, false, false);

    TEST_ASSERT_EQUAL(DriveWatchdogSource::SBUS2_ROUTED,
                      rcInputStepStartupPlan(in).driveWatchdogSource);
}

void test_dual_sbus_ch1_identifies_sbus1_drive_watchdog_source() {
    RcInputActiveConfig in = makeActiveRc(RC_INPUT_DUAL_SBUS, false, true, false,
                                           false, false, false, false);

    TEST_ASSERT_EQUAL(DriveWatchdogSource::SBUS1,
                      rcInputStepStartupPlan(in).driveWatchdogSource);
}

void test_dual_sbus_dome_only_identifies_no_drive_watchdog_source() {
    RcInputActiveConfig in = makeActiveRc(RC_INPUT_DUAL_SBUS, false, false, true,
                                           false, false, false, false);

    TEST_ASSERT_EQUAL(DriveWatchdogSource::NONE,
                      rcInputStepStartupPlan(in).driveWatchdogSource);
}

void test_all_disabled_identifies_no_drive_watchdog_source() {
    RcInputActiveConfig in = makeActiveRc(RC_INPUT_SINGLE_SBUS, false, false, false,
                                           false, false, false, false);

    TEST_ASSERT_EQUAL(DriveWatchdogSource::NONE,
                      rcInputStepStartupPlan(in).driveWatchdogSource);
}

// ============================================================================
// Initialization
// ============================================================================

void test_init_zeros_watchdog_state() {
    RcInputStepState state;
    state.sbus1Watchdog.signalLost = true;
    state.sbus2Watchdog.signalLost = true;

    rcInputStepInit(&state);

    TEST_ASSERT_FALSE(state.sbus1Watchdog.signalLost);
    TEST_ASSERT_FALSE(state.sbus2Watchdog.signalLost);
}

// ============================================================================
// SBUS1 Watchdog Timeout to Failsafe Transitions
// ============================================================================

void test_sbus1_watchdog_just_lost_transition() {
    RcInputStepState state = {};
    RcInputStepDriveWatchdogInputs in = {
        .driveDecoderInitialized = true,
        .source = DriveWatchdogSource::SBUS1,
        .lastSbus1Ms = 100,
        .lastSbus2Ms = 0,
        .nowMs = 400,  // >200ms timeout
        .timeoutMs = 200,
    };

    RcInputStepDriveWatchdogActions out = rcInputStepDriveWatchdog(&state, in);

    TEST_ASSERT_EQUAL(SbusWatchdogTransition::JUST_LOST, out.transition);
    TEST_ASSERT_TRUE(out.triggerSbusWatchdog);
    TEST_ASSERT_TRUE(out.submitDriveZeroFrame);
}

void test_sbus1_watchdog_just_restored_transition() {
    RcInputStepState state = {};
    state.sbus1Watchdog.signalLost = true;
    RcInputStepDriveWatchdogInputs in = {
        .driveDecoderInitialized = true,
        .source = DriveWatchdogSource::SBUS1,
        .lastSbus1Ms = 100,
        .lastSbus2Ms = 0,
        .nowMs = 120,  // <200ms timeout
        .timeoutMs = 200,
    };

    RcInputStepDriveWatchdogActions out = rcInputStepDriveWatchdog(&state, in);

    TEST_ASSERT_EQUAL(SbusWatchdogTransition::JUST_RESTORED, out.transition);
    TEST_ASSERT_TRUE(out.clearSbusWatchdog);
}

void test_sbus1_watchdog_ok_clears_watchdog_layer() {
    RcInputStepState state = {};
    state.sbus1Watchdog.signalLost = false;
    RcInputStepDriveWatchdogInputs in = {
        .driveDecoderInitialized = true,
        .source = DriveWatchdogSource::SBUS1,
        .lastSbus1Ms = 100,
        .lastSbus2Ms = 0,
        .nowMs = 120,
        .timeoutMs = 200,
    };

    RcInputStepDriveWatchdogActions out = rcInputStepDriveWatchdog(&state, in);

    TEST_ASSERT_EQUAL(SbusWatchdogTransition::OK, out.transition);
    TEST_ASSERT_FALSE(out.triggerSbusWatchdog);
}

void test_sbus1_disabled_decoder_emits_no_recurring_actions() {
    RcInputStepState state = {};
    state.sbus1Watchdog.signalLost = true;
    RcInputStepDriveWatchdogInputs in = {
        .driveDecoderInitialized = false,
        .source = DriveWatchdogSource::SBUS1,
        .lastSbus1Ms = 100,
        .lastSbus2Ms = 0,
        .nowMs = 500,
        .timeoutMs = 200,
    };

    RcInputStepDriveWatchdogActions out = rcInputStepDriveWatchdog(&state, in);

    TEST_ASSERT_EQUAL(SbusWatchdogTransition::OK, out.transition);
    TEST_ASSERT_FALSE(out.triggerSbusWatchdog);
    TEST_ASSERT_FALSE(out.clearSbusWatchdog);
    TEST_ASSERT_FALSE(out.submitDriveZeroFrame);
    TEST_ASSERT_TRUE(state.sbus1Watchdog.signalLost);
}

void test_sbus1_lost_restored_steady_emits_exactly_one_restore_edge() {
    RcInputStepState state = {};
    RcInputStepDriveWatchdogInputs in = {
        .driveDecoderInitialized = true,
        .source = DriveWatchdogSource::SBUS1,
        .lastSbus1Ms = 100,
        .lastSbus2Ms = 0,
        .nowMs = 400,
        .timeoutMs = 200,
    };

    RcInputStepDriveWatchdogActions lost = rcInputStepDriveWatchdog(&state, in);
    in.lastSbus1Ms = 410;
    in.nowMs = 420;
    RcInputStepDriveWatchdogActions restored = rcInputStepDriveWatchdog(&state, in);
    in.nowMs = 430;
    RcInputStepDriveWatchdogActions steady = rcInputStepDriveWatchdog(&state, in);

    TEST_ASSERT_EQUAL(SbusWatchdogTransition::JUST_LOST, lost.transition);
    TEST_ASSERT_TRUE(lost.triggerSbusWatchdog);
    TEST_ASSERT_TRUE(lost.submitDriveZeroFrame);
    TEST_ASSERT_EQUAL(SbusWatchdogTransition::JUST_RESTORED, restored.transition);
    TEST_ASSERT_TRUE(restored.clearSbusWatchdog);
    TEST_ASSERT_EQUAL(SbusWatchdogTransition::OK, steady.transition);
    TEST_ASSERT_FALSE(steady.clearSbusWatchdog);
}

void test_sbus1_sustained_loss_triggers_and_zeroes_once() {
    RcInputStepState state = {};
    RcInputStepDriveWatchdogInputs in = {
        .driveDecoderInitialized = true,
        .source = DriveWatchdogSource::SBUS1,
        .lastSbus1Ms = 100,
        .lastSbus2Ms = 0,
        .nowMs = 400,
        .timeoutMs = 200,
    };

    RcInputStepDriveWatchdogActions first = rcInputStepDriveWatchdog(&state, in);
    in.nowMs = 500;
    RcInputStepDriveWatchdogActions sustained = rcInputStepDriveWatchdog(&state, in);

    TEST_ASSERT_EQUAL(SbusWatchdogTransition::JUST_LOST, first.transition);
    TEST_ASSERT_TRUE(first.triggerSbusWatchdog);
    TEST_ASSERT_TRUE(first.submitDriveZeroFrame);
    TEST_ASSERT_EQUAL(SbusWatchdogTransition::LOST, sustained.transition);
    TEST_ASSERT_FALSE(sustained.triggerSbusWatchdog);
    TEST_ASSERT_FALSE(sustained.submitDriveZeroFrame);
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

void test_sbus2_tracking_disabled_emits_no_recurring_actions_or_restore() {
    RcInputStepState state = {};
    state.sbus2Watchdog.signalLost = true;
    RcInputStepSbus2WatchdogInputs in = {
        .domeSbusInitialized = false,
        .lastSbus2Ms = 100,
        .nowMs = 500,  // would timeout
        .timeoutMs = 200,
    };

    RcInputStepSbus2WatchdogActions out = rcInputStepSbus2Watchdog(&state, in);

    TEST_ASSERT_EQUAL(SbusWatchdogTransition::OK, out.transition);
    TEST_ASSERT_FALSE(out.setSbus2SignalLost);
    TEST_ASSERT_FALSE(out.clearSbus2SignalLost);
    TEST_ASSERT_FALSE(out.shouldStopDome);
    TEST_ASSERT_TRUE(state.sbus2Watchdog.signalLost);
}

void test_sbus2_lost_restored_steady_emits_one_restore_and_one_stop() {
    RcInputStepState state = {};
    RcInputStepSbus2WatchdogInputs in = {
        .domeSbusInitialized = true,
        .lastSbus2Ms = 100,
        .nowMs = 400,
        .timeoutMs = 200,
    };

    RcInputStepSbus2WatchdogActions lost = rcInputStepSbus2Watchdog(&state, in);
    in.lastSbus2Ms = 410;
    in.nowMs = 420;
    RcInputStepSbus2WatchdogActions restored = rcInputStepSbus2Watchdog(&state, in);
    in.nowMs = 430;
    RcInputStepSbus2WatchdogActions steady = rcInputStepSbus2Watchdog(&state, in);

    TEST_ASSERT_EQUAL(SbusWatchdogTransition::JUST_LOST, lost.transition);
    TEST_ASSERT_TRUE(lost.setSbus2SignalLost);
    TEST_ASSERT_TRUE(lost.shouldStopDome);
    TEST_ASSERT_EQUAL(SbusWatchdogTransition::JUST_RESTORED, restored.transition);
    TEST_ASSERT_TRUE(restored.clearSbus2SignalLost);
    TEST_ASSERT_FALSE(restored.shouldStopDome);
    TEST_ASSERT_EQUAL(SbusWatchdogTransition::OK, steady.transition);
    TEST_ASSERT_FALSE(steady.clearSbus2SignalLost);
    TEST_ASSERT_FALSE(steady.shouldStopDome);
}

void test_sbus2_sustained_loss_stops_once() {
    RcInputStepState state = {};
    RcInputStepSbus2WatchdogInputs in = {
        .domeSbusInitialized = true,
        .lastSbus2Ms = 100,
        .nowMs = 400,
        .timeoutMs = 200,
    };

    RcInputStepSbus2WatchdogActions first = rcInputStepSbus2Watchdog(&state, in);
    in.nowMs = 500;
    RcInputStepSbus2WatchdogActions sustained = rcInputStepSbus2Watchdog(&state, in);

    TEST_ASSERT_EQUAL(SbusWatchdogTransition::JUST_LOST, first.transition);
    TEST_ASSERT_TRUE(first.setSbus2SignalLost);
    TEST_ASSERT_TRUE(first.shouldStopDome);
    TEST_ASSERT_EQUAL(SbusWatchdogTransition::LOST, sustained.transition);
    TEST_ASSERT_FALSE(sustained.setSbus2SignalLost);
    TEST_ASSERT_FALSE(sustained.shouldStopDome);
}

// ============================================================================
// Generalized Drive Watchdog (SBUS1 or routed SBUS2) -- Slice 1
// ============================================================================

void test_drive_watchdog_source_none_returns_empty() {
    RcInputStepState state = {};
    RcInputStepDriveWatchdogInputs in = {
        .driveDecoderInitialized = true,
        .source = DriveWatchdogSource::NONE,
        .lastSbus1Ms = 100,
        .lastSbus2Ms = 100,
        .nowMs = 500,
        .timeoutMs = 200,
    };

    RcInputStepDriveWatchdogActions out = rcInputStepDriveWatchdog(&state, in);

    TEST_ASSERT_EQUAL(SbusWatchdogTransition::OK, out.transition);
    TEST_ASSERT_FALSE(out.triggerSbusWatchdog);
    TEST_ASSERT_FALSE(out.submitDriveZeroFrame);
    TEST_ASSERT_FALSE(out.clearSbusWatchdog);
}

void test_drive_watchdog_sbus1_source_just_lost() {
    RcInputStepState state = {};
    RcInputStepDriveWatchdogInputs in = {
        .driveDecoderInitialized = true,
        .source = DriveWatchdogSource::SBUS1,
        .lastSbus1Ms = 100,
        .lastSbus2Ms = 1000,  // not used
        .nowMs = 400,          // timeout
        .timeoutMs = 200,
    };

    RcInputStepDriveWatchdogActions out = rcInputStepDriveWatchdog(&state, in);

    TEST_ASSERT_EQUAL(SbusWatchdogTransition::JUST_LOST, out.transition);
    TEST_ASSERT_TRUE(out.triggerSbusWatchdog);
    TEST_ASSERT_TRUE(out.submitDriveZeroFrame);
}

void test_drive_watchdog_sbus1_source_just_restored() {
    RcInputStepState state = {};
    state.sbus1Watchdog.signalLost = true;
    RcInputStepDriveWatchdogInputs in = {
        .driveDecoderInitialized = true,
        .source = DriveWatchdogSource::SBUS1,
        .lastSbus1Ms = 100,
        .lastSbus2Ms = 1000,
        .nowMs = 120,  // within timeout
        .timeoutMs = 200,
    };

    RcInputStepDriveWatchdogActions out = rcInputStepDriveWatchdog(&state, in);

    TEST_ASSERT_EQUAL(SbusWatchdogTransition::JUST_RESTORED, out.transition);
    TEST_ASSERT_TRUE(out.clearSbusWatchdog);
    TEST_ASSERT_FALSE(out.triggerSbusWatchdog);
}

void test_drive_watchdog_sbus2_routed_source_just_lost() {
    RcInputStepState state = {};
    RcInputStepDriveWatchdogInputs in = {
        .driveDecoderInitialized = true,
        .source = DriveWatchdogSource::SBUS2_ROUTED,
        .lastSbus1Ms = 1000,  // not used
        .lastSbus2Ms = 100,
        .nowMs = 400,  // timeout
        .timeoutMs = 200,
    };

    RcInputStepDriveWatchdogActions out = rcInputStepDriveWatchdog(&state, in);

    TEST_ASSERT_EQUAL(SbusWatchdogTransition::JUST_LOST, out.transition);
    TEST_ASSERT_TRUE(out.triggerSbusWatchdog);
    TEST_ASSERT_TRUE(out.submitDriveZeroFrame);
}

void test_drive_watchdog_sbus2_routed_source_just_restored() {
    RcInputStepState state = {};
    state.sbus2Watchdog.signalLost = true;
    RcInputStepDriveWatchdogInputs in = {
        .driveDecoderInitialized = true,
        .source = DriveWatchdogSource::SBUS2_ROUTED,
        .lastSbus1Ms = 1000,
        .lastSbus2Ms = 100,
        .nowMs = 120,  // within timeout
        .timeoutMs = 200,
    };

    RcInputStepDriveWatchdogActions out = rcInputStepDriveWatchdog(&state, in);

    TEST_ASSERT_EQUAL(SbusWatchdogTransition::JUST_RESTORED, out.transition);
    TEST_ASSERT_TRUE(out.clearSbusWatchdog);
    TEST_ASSERT_FALSE(out.triggerSbusWatchdog);
}

void test_drive_watchdog_sbus2_routed_sustained_loss_silent() {
    RcInputStepState state = {};
    RcInputStepDriveWatchdogInputs in = {
        .driveDecoderInitialized = true,
        .source = DriveWatchdogSource::SBUS2_ROUTED,
        .lastSbus1Ms = 1000,
        .lastSbus2Ms = 100,
        .nowMs = 400,  // timeout
        .timeoutMs = 200,
    };

    RcInputStepDriveWatchdogActions first = rcInputStepDriveWatchdog(&state, in);
    in.nowMs = 500;
    RcInputStepDriveWatchdogActions sustained = rcInputStepDriveWatchdog(&state, in);

    TEST_ASSERT_EQUAL(SbusWatchdogTransition::JUST_LOST, first.transition);
    TEST_ASSERT_TRUE(first.triggerSbusWatchdog);
    TEST_ASSERT_TRUE(first.submitDriveZeroFrame);
    TEST_ASSERT_EQUAL(SbusWatchdogTransition::LOST, sustained.transition);
    TEST_ASSERT_FALSE(sustained.triggerSbusWatchdog);
    TEST_ASSERT_FALSE(sustained.submitDriveZeroFrame);
}

void test_drive_watchdog_disabled_decoder_emits_no_actions() {
    RcInputStepState state = {};
    RcInputStepDriveWatchdogInputs in = {
        .driveDecoderInitialized = false,  // decoder not running
        .source = DriveWatchdogSource::SBUS2_ROUTED,
        .lastSbus1Ms = 1000,
        .lastSbus2Ms = 100,
        .nowMs = 500,  // would timeout
        .timeoutMs = 200,
    };

    RcInputStepDriveWatchdogActions out = rcInputStepDriveWatchdog(&state, in);

    TEST_ASSERT_EQUAL(SbusWatchdogTransition::OK, out.transition);
    TEST_ASSERT_FALSE(out.triggerSbusWatchdog);
    TEST_ASSERT_FALSE(out.submitDriveZeroFrame);
    TEST_ASSERT_FALSE(out.clearSbusWatchdog);
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
// Per-Frame Decisions: SBUS1 (drive) receiver frames
// ============================================================================

void test_sbus1_frame_failsafe_triggers_hw_zero_frame_and_one_shot_log() {
    RcInputStepSbus1FrameInputs in = {
        .failsafe = true,
        .lostFrame = false,
        .hwFailsafeWasActive = false,
    };

    RcInputStepSbus1FrameActions out = rcInputStepSbus1Frame(in);

    TEST_ASSERT_TRUE(out.triggerSbusHw);
    TEST_ASSERT_TRUE(out.logHwFailsafeAsserted);  // rising edge
    TEST_ASSERT_TRUE(out.submitDriveZeroFrame);
    TEST_ASSERT_FALSE(out.clearSbusHw);
    TEST_ASSERT_FALSE(out.clearSbusWatchdog);
    TEST_ASSERT_FALSE(out.dispatchBindings);

    // Already active: still trigger and zero-frame every frame, but log only once.
    in.hwFailsafeWasActive = true;
    out = rcInputStepSbus1Frame(in);

    TEST_ASSERT_TRUE(out.triggerSbusHw);
    TEST_ASSERT_FALSE(out.logHwFailsafeAsserted);
    TEST_ASSERT_TRUE(out.submitDriveZeroFrame);
}

void test_sbus1_frame_lost_frame_only_counts() {
    RcInputStepSbus1FrameInputs in = {
        .failsafe = false,
        .lostFrame = true,
        .hwFailsafeWasActive = false,
    };

    RcInputStepSbus1FrameActions out = rcInputStepSbus1Frame(in);

    TEST_ASSERT_TRUE(out.incrementLostFrameCount);
    TEST_ASSERT_FALSE(out.triggerSbusHw);
    TEST_ASSERT_FALSE(out.submitDriveZeroFrame);
    TEST_ASSERT_FALSE(out.clearSbusHw);
    TEST_ASSERT_FALSE(out.clearSbusWatchdog);
    TEST_ASSERT_FALSE(out.dispatchBindings);
}

void test_sbus1_frame_clean_clears_failsafes_and_dispatches() {
    RcInputStepSbus1FrameInputs in = {
        .failsafe = false,
        .lostFrame = false,
        .hwFailsafeWasActive = true,
    };

    RcInputStepSbus1FrameActions out = rcInputStepSbus1Frame(in);

    TEST_ASSERT_TRUE(out.clearSbusHw);
    TEST_ASSERT_TRUE(out.clearSbusWatchdog);
    TEST_ASSERT_TRUE(out.dispatchBindings);
    TEST_ASSERT_FALSE(out.triggerSbusHw);
    TEST_ASSERT_FALSE(out.submitDriveZeroFrame);
    TEST_ASSERT_FALSE(out.incrementLostFrameCount);
}

// ============================================================================
// Per-Frame Decisions: SBUS2 (dome) receiver frames
// ============================================================================

void test_sbus2_frame_failsafe_sets_hw_with_one_shot_log() {
    RcInputStepSbus2FrameInputs in = {
        .failsafe = true,
        .lostFrame = false,
        .hwFailsafeWasActive = false,
    };

    RcInputStepSbus2FrameActions out = rcInputStepSbus2Frame(in);

    TEST_ASSERT_TRUE(out.setSbus2HwFailsafe);
    TEST_ASSERT_FALSE(out.clearSbus2HwFailsafe);
    TEST_ASSERT_TRUE(out.logHwFailsafeAsserted);  // rising edge
    TEST_ASSERT_FALSE(out.updateLastSbus2Ms);     // heartbeat suppressed
    TEST_ASSERT_FALSE(out.dispatchBindings);

    in.hwFailsafeWasActive = true;
    out = rcInputStepSbus2Frame(in);

    TEST_ASSERT_TRUE(out.setSbus2HwFailsafe);
    TEST_ASSERT_FALSE(out.logHwFailsafeAsserted);
}

void test_sbus2_frame_lost_frame_suppresses_heartbeat_and_dispatch() {
    RcInputStepSbus2FrameInputs in = {
        .failsafe = false,
        .lostFrame = true,
        .hwFailsafeWasActive = false,
    };

    RcInputStepSbus2FrameActions out = rcInputStepSbus2Frame(in);

    TEST_ASSERT_TRUE(out.incrementLostFrameCount);
    TEST_ASSERT_TRUE(out.clearSbus2HwFailsafe);  // tracks failsafe flag every frame
    TEST_ASSERT_FALSE(out.updateLastSbus2Ms);    // SBUS2 watchdog fires if this persists
    TEST_ASSERT_FALSE(out.dispatchBindings);
    TEST_ASSERT_FALSE(out.logHwFailsafeAsserted);
}

void test_sbus2_frame_clean_heartbeats_and_dispatches() {
    RcInputStepSbus2FrameInputs in = {
        .failsafe = false,
        .lostFrame = false,
        .hwFailsafeWasActive = true,
    };

    RcInputStepSbus2FrameActions out = rcInputStepSbus2Frame(in);

    TEST_ASSERT_TRUE(out.clearSbus2HwFailsafe);
    TEST_ASSERT_TRUE(out.updateLastSbus2Ms);
    TEST_ASSERT_TRUE(out.dispatchBindings);
    TEST_ASSERT_FALSE(out.incrementLostFrameCount);
    TEST_ASSERT_FALSE(out.clearSbus2SignalLost);  // watchdog restore owns this clear
}

// ============================================================================
// Per-Frame Decisions: routed SBUS2 frames (single_sbus + useCh2)
// ============================================================================

void test_sbus2_routed_frame_failsafe_triggers_hw_zero_frame() {
    RcInputStepSbus2FrameInputs in = {
        .failsafe = true,
        .lostFrame = false,
        .hwFailsafeWasActive = false,
    };

    RcInputStepSbus2FrameActions out = rcInputStepSbus2RoutedFrame(in);

    TEST_ASSERT_TRUE(out.setSbus2HwFailsafe);
    TEST_ASSERT_FALSE(out.clearSbus2HwFailsafe);
    // Slice 2: routed path mirrors SBUS1 drive failsafe behavior
    TEST_ASSERT_TRUE(out.triggerSbusHw);
    TEST_ASSERT_TRUE(out.submitDriveZeroFrame);
    TEST_ASSERT_FALSE(out.clearSbusHw);
    TEST_ASSERT_FALSE(out.logRoutedHwFailsafeClearedOnFallingEdge);
    TEST_ASSERT_FALSE(out.updateLastSbus2Ms);
    TEST_ASSERT_FALSE(out.dispatchBindings);
}

void test_sbus2_routed_frame_lost_frame_holds_hw_state() {
    RcInputStepSbus2FrameInputs in = {
        .failsafe = false,
        .lostFrame = true,
        .hwFailsafeWasActive = false,
    };

    RcInputStepSbus2FrameActions out = rcInputStepSbus2RoutedFrame(in);

    TEST_ASSERT_TRUE(out.incrementLostFrameCount);
    TEST_ASSERT_FALSE(out.setSbus2HwFailsafe);
    TEST_ASSERT_FALSE(out.clearSbus2HwFailsafe);  // latched until a clean frame
    TEST_ASSERT_FALSE(out.updateLastSbus2Ms);
    TEST_ASSERT_FALSE(out.dispatchBindings);
}

void test_sbus2_routed_frame_clean_clears_and_dispatches() {
    RcInputStepSbus2FrameInputs in = {
        .failsafe = false,
        .lostFrame = false,
        .hwFailsafeWasActive = false,  // was never in failsafe
    };

    RcInputStepSbus2FrameActions out = rcInputStepSbus2RoutedFrame(in);

    TEST_ASSERT_TRUE(out.clearSbus2HwFailsafe);
    TEST_ASSERT_TRUE(out.clearSbus2SignalLost);  // no watchdog restore on this path
    TEST_ASSERT_TRUE(out.updateLastSbus2Ms);
    TEST_ASSERT_TRUE(out.dispatchBindings);
    TEST_ASSERT_FALSE(out.incrementLostFrameCount);
    // Slice 2: no drive failsafe actions when recovering from a non-failsafe clean state
    TEST_ASSERT_FALSE(out.triggerSbusHw);
    TEST_ASSERT_FALSE(out.submitDriveZeroFrame);
    TEST_ASSERT_FALSE(out.clearSbusHw);
    TEST_ASSERT_FALSE(out.logRoutedHwFailsafeClearedOnFallingEdge);
}

void test_sbus2_routed_frame_clean_after_failsafe_clears_hw() {
    // Rising edge: failsafe flag asserted
    RcInputStepSbus2FrameInputs failsafeIn = {
        .failsafe = true,
        .lostFrame = false,
        .hwFailsafeWasActive = false,
    };

    RcInputStepSbus2FrameActions failsafeOut = rcInputStepSbus2RoutedFrame(failsafeIn);

    TEST_ASSERT_TRUE(failsafeOut.setSbus2HwFailsafe);
    TEST_ASSERT_TRUE(failsafeOut.triggerSbusHw);
    TEST_ASSERT_TRUE(failsafeOut.submitDriveZeroFrame);

    // Falling edge: failsafe flag cleared
    RcInputStepSbus2FrameInputs cleanIn = {
        .failsafe = false,
        .lostFrame = false,
        .hwFailsafeWasActive = true,  // was in failsafe, now recovering
    };

    RcInputStepSbus2FrameActions cleanOut = rcInputStepSbus2RoutedFrame(cleanIn);

    TEST_ASSERT_TRUE(cleanOut.clearSbus2HwFailsafe);
    TEST_ASSERT_TRUE(cleanOut.clearSbus2SignalLost);
    TEST_ASSERT_TRUE(cleanOut.updateLastSbus2Ms);
    TEST_ASSERT_TRUE(cleanOut.dispatchBindings);
    // Slice 2: falling edge triggers clear on the drive path with log
    TEST_ASSERT_TRUE(cleanOut.clearSbusHw);
    TEST_ASSERT_TRUE(cleanOut.logRoutedHwFailsafeClearedOnFallingEdge);
    TEST_ASSERT_FALSE(cleanOut.triggerSbusHw);
    TEST_ASSERT_FALSE(cleanOut.submitDriveZeroFrame);
}

void test_sbus2_routed_sustained_failsafe_triggers_and_zeroes_every_frame() {
    // Rising edge
    RcInputStepSbus2FrameInputs failsafeIn = {
        .failsafe = true,
        .lostFrame = false,
        .hwFailsafeWasActive = false,
    };

    RcInputStepSbus2FrameActions first = rcInputStepSbus2RoutedFrame(failsafeIn);

    TEST_ASSERT_TRUE(first.triggerSbusHw);
    TEST_ASSERT_TRUE(first.submitDriveZeroFrame);

    // Sustained failsafe: still trigger and zero, no log
    failsafeIn.hwFailsafeWasActive = true;
    RcInputStepSbus2FrameActions sustained = rcInputStepSbus2RoutedFrame(failsafeIn);

    TEST_ASSERT_TRUE(sustained.triggerSbusHw);
    TEST_ASSERT_TRUE(sustained.submitDriveZeroFrame);
    TEST_ASSERT_FALSE(sustained.clearSbusHw);
    TEST_ASSERT_FALSE(sustained.logRoutedHwFailsafeClearedOnFallingEdge);
}

void test_sbus2_routed_latch_latches_across_lost_frame() {
    // HIGH safety defect test: edge latch must NOT clear on lost_frame,
    // only on a clean frame. Sequence: failsafe -> lost_frame -> clean
    // must result in SBUS_HW clearing and falling-edge INFO firing on the
    // clean frame (not stuck latched forever).

    // Rising edge: failsafe frame
    RcInputStepSbus2FrameInputs failsafeIn = {
        .failsafe = true,
        .lostFrame = false,
        .hwFailsafeWasActive = false,  // adapter will track this
    };

    RcInputStepSbus2FrameActions failsafeOut = rcInputStepSbus2RoutedFrame(failsafeIn);

    TEST_ASSERT_TRUE(failsafeOut.triggerSbusHw);
    TEST_ASSERT_TRUE(failsafeOut.submitDriveZeroFrame);
    TEST_ASSERT_TRUE(failsafeOut.logHwFailsafeAsserted);  // rising edge WARN
    // Simulate adapter updating: routedHwFailsafeWasActive = true (because failsafe=true)

    // Lost frame: failsafe clear, lost_frame set
    // Adapter must NOT clear the latch on lost_frame
    RcInputStepSbus2FrameInputs lostIn = {
        .failsafe = false,  // lost_frame has failsafe=false
        .lostFrame = true,
        .hwFailsafeWasActive = true,  // latch should still be true from failsafe
    };

    RcInputStepSbus2FrameActions lostOut = rcInputStepSbus2RoutedFrame(lostIn);

    TEST_ASSERT_TRUE(lostOut.incrementLostFrameCount);
    // The step function must NOT trigger or clear on lost_frame
    TEST_ASSERT_FALSE(lostOut.triggerSbusHw);
    TEST_ASSERT_FALSE(lostOut.clearSbusHw);
    TEST_ASSERT_FALSE(lostOut.logRoutedHwFailsafeClearedOnFallingEdge);
    TEST_ASSERT_FALSE(lostOut.logHwFailsafeAsserted);  // no log on lost_frame
    // Adapter must NOT update latch to false on lost_frame
    // (hwFailsafeWasActive remains true from failsafe)

    // Clean frame: failsafe clear, lost_frame clear
    // Falling edge should trigger because hwFailsafeWasActive=true
    RcInputStepSbus2FrameInputs cleanIn = {
        .failsafe = false,
        .lostFrame = false,
        .hwFailsafeWasActive = true,  // still true because latch didn't clear on lost_frame
    };

    RcInputStepSbus2FrameActions cleanOut = rcInputStepSbus2RoutedFrame(cleanIn);

    TEST_ASSERT_TRUE(cleanOut.clearSbus2HwFailsafe);
    TEST_ASSERT_TRUE(cleanOut.clearSbus2SignalLost);
    TEST_ASSERT_TRUE(cleanOut.updateLastSbus2Ms);
    TEST_ASSERT_TRUE(cleanOut.dispatchBindings);
    // Falling edge: clear SBUS_HW and log recovery
    TEST_ASSERT_TRUE(cleanOut.clearSbusHw);
    TEST_ASSERT_TRUE(cleanOut.logRoutedHwFailsafeClearedOnFallingEdge);
    TEST_ASSERT_FALSE(cleanOut.logHwFailsafeAsserted);  // no log on clean frame
}

void test_sbus2_routed_frame_failsafe_logs_warn_on_rising_edge_only() {
    // MEDIUM: rising-edge WARN log must fire once, not on sustained failsafe.

    // Rising edge: log should fire
    RcInputStepSbus2FrameInputs failsafeIn = {
        .failsafe = true,
        .lostFrame = false,
        .hwFailsafeWasActive = false,
    };

    RcInputStepSbus2FrameActions first = rcInputStepSbus2RoutedFrame(failsafeIn);

    TEST_ASSERT_TRUE(first.logHwFailsafeAsserted);
    TEST_ASSERT_TRUE(first.triggerSbusHw);

    // Sustained failsafe: log should NOT fire
    failsafeIn.hwFailsafeWasActive = true;
    RcInputStepSbus2FrameActions sustained = rcInputStepSbus2RoutedFrame(failsafeIn);

    TEST_ASSERT_FALSE(sustained.logHwFailsafeAsserted);
    TEST_ASSERT_TRUE(sustained.triggerSbusHw);  // still triggers, just no log
}

// ============================================================================
// Unity Test Runner
// ============================================================================

int main(void) {
    UNITY_BEGIN();

    RUN_TEST(test_task_disabled_at_boot_when_all_rc_components_off);
    RUN_TEST(test_task_enabled_at_boot_when_rc_component_1_is_on);
    RUN_TEST(test_task_enabled_at_boot_when_rc_component_2_is_on);
    RUN_TEST(test_task_enabled_at_boot_when_rc_component_3_is_on);
    RUN_TEST(test_task_enabled_at_boot_when_rc_component_4_is_on);
    RUN_TEST(test_task_enabled_at_boot_when_rc_component_5_is_on);
    RUN_TEST(test_task_enabled_at_boot_when_rc_component_6_is_on);
    RUN_TEST(test_single_sbus_parks_when_only_unselected_receiver_is_on);
    RUN_TEST(test_single_sbus_ch2_route_parks_when_only_ch1_is_on);
    RUN_TEST(test_single_sbus_ch1_route_runs_when_ch1_is_on);
    RUN_TEST(test_single_sbus_ch2_route_runs_when_ch2_is_on);
    RUN_TEST(test_dual_sbus_runs_when_ch1_is_on);
    RUN_TEST(test_dual_sbus_runs_when_ch2_is_on);

    RUN_TEST(test_pwm_mode_identifies_no_drive_watchdog_source);
    RUN_TEST(test_single_sbus_ch1_identifies_sbus1_drive_watchdog_source);
    RUN_TEST(test_single_sbus_routed_ch2_identifies_sbus2_routed_drive_watchdog_source);
    RUN_TEST(test_dual_sbus_ch1_identifies_sbus1_drive_watchdog_source);
    RUN_TEST(test_dual_sbus_dome_only_identifies_no_drive_watchdog_source);
    RUN_TEST(test_all_disabled_identifies_no_drive_watchdog_source);

    RUN_TEST(test_init_zeros_watchdog_state);

    RUN_TEST(test_sbus1_watchdog_just_lost_transition);
    RUN_TEST(test_sbus1_watchdog_just_restored_transition);
    RUN_TEST(test_sbus1_watchdog_ok_clears_watchdog_layer);
    RUN_TEST(test_sbus1_disabled_decoder_emits_no_recurring_actions);
    RUN_TEST(test_sbus1_lost_restored_steady_emits_exactly_one_restore_edge);
    RUN_TEST(test_sbus1_sustained_loss_triggers_and_zeroes_once);

    RUN_TEST(test_sbus2_watchdog_just_lost_transition);
    RUN_TEST(test_sbus2_watchdog_just_restored_transition);
    RUN_TEST(test_sbus2_tracking_disabled_emits_no_recurring_actions_or_restore);
    RUN_TEST(test_sbus2_lost_restored_steady_emits_one_restore_and_one_stop);
    RUN_TEST(test_sbus2_sustained_loss_stops_once);

    RUN_TEST(test_drive_watchdog_source_none_returns_empty);
    RUN_TEST(test_drive_watchdog_sbus1_source_just_lost);
    RUN_TEST(test_drive_watchdog_sbus1_source_just_restored);
    RUN_TEST(test_drive_watchdog_sbus2_routed_source_just_lost);
    RUN_TEST(test_drive_watchdog_sbus2_routed_source_just_restored);
    RUN_TEST(test_drive_watchdog_sbus2_routed_sustained_loss_silent);
    RUN_TEST(test_drive_watchdog_disabled_decoder_emits_no_actions);

    RUN_TEST(test_sbus1_frame_failsafe_triggers_hw_zero_frame_and_one_shot_log);
    RUN_TEST(test_sbus1_frame_lost_frame_only_counts);
    RUN_TEST(test_sbus1_frame_clean_clears_failsafes_and_dispatches);

    RUN_TEST(test_sbus2_frame_failsafe_sets_hw_with_one_shot_log);
    RUN_TEST(test_sbus2_frame_lost_frame_suppresses_heartbeat_and_dispatch);
    RUN_TEST(test_sbus2_frame_clean_heartbeats_and_dispatches);

    RUN_TEST(test_sbus2_routed_frame_failsafe_triggers_hw_zero_frame);
    RUN_TEST(test_sbus2_routed_frame_lost_frame_holds_hw_state);
    RUN_TEST(test_sbus2_routed_frame_clean_clears_and_dispatches);
    RUN_TEST(test_sbus2_routed_frame_clean_after_failsafe_clears_hw);
    RUN_TEST(test_sbus2_routed_sustained_failsafe_triggers_and_zeroes_every_frame);
    RUN_TEST(test_sbus2_routed_latch_latches_across_lost_frame);
    RUN_TEST(test_sbus2_routed_frame_failsafe_logs_warn_on_rising_edge_only);

    RUN_TEST(test_zero_frame_submitted_on_pwm_signal_lost);
    RUN_TEST(test_zero_frame_submitted_on_sbus_hw_failsafe);
    RUN_TEST(test_zero_frame_submitted_on_both_pwm_and_sbus_fail);
    RUN_TEST(test_zero_frame_not_submitted_when_both_ok);

    return UNITY_END();
}
