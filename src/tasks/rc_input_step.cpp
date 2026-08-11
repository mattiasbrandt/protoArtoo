// =============================================================================
// src/tasks/rc_input_step.cpp
//
// RC Input Step Core implementation  --  pure decision logic extracted from
// rcInputTask. No Arduino, FreeRTOS, RobotState, hardware I/O, or logging.
// =============================================================================

#include "../../include/rc_input_step.h"

#include <stdint.h>

// RC input mode constants (from config.h; redeclared here for test isolation)
#define RC_INPUT_STANDARD_PWM 0
#define RC_INPUT_SINGLE_SBUS 1
#define RC_INPUT_DUAL_SBUS 2

// ============================================================================
// Initialization
// ============================================================================

void rcInputStepInit(RcInputStepState* state) {
    if (state != nullptr) {
        state->sbus1Watchdog = {};
        state->sbus2Watchdog = {};
        state->lastUseCh2 = false;
    }
}

// ============================================================================
// Decoder Desired State Phase
// ============================================================================

static bool is_drive_sbus_mode(uint8_t mode) {
    return mode == RC_INPUT_SINGLE_SBUS || mode == RC_INPUT_DUAL_SBUS;
}

static bool is_dome_sbus_mode(uint8_t mode) {
    return mode == RC_INPUT_DUAL_SBUS;
}

static bool driveSbusDecoderEnabledForMode(uint8_t mode, bool enableRcCh1, bool enableRcCh2,
                                           bool useCh2) {
    if (!is_drive_sbus_mode(mode)) {
        return false;
    }
    if (mode == RC_INPUT_SINGLE_SBUS) {
        return useCh2 ? enableRcCh2 : enableRcCh1;
    }
    // dual_sbus: drive uses SBUS1 only
    return enableRcCh1;
}

RcInputStepTickActions rcInputStepTick(RcInputStepState* state,
                                       const RcInputStepTickInputs& in) {
    RcInputStepTickActions out = {};

    if (state == nullptr) {
        return out;
    }

    // ---- Compute desired decoder state for current mode/enable combo ----
    out.driveSbusDesiredEnabled =
        driveSbusDecoderEnabledForMode(in.rcInputMode, in.enableRcCh1, in.enableRcCh2, in.useCh2);
    out.domeSbusDesiredEnabled =
        (is_dome_sbus_mode(in.rcInputMode) && in.enableRcCh2);

    // ---- Detect single_sbus receiver selection change (end-before-begin) ----
    // CRITICAL: This detection must run BEFORE init/deinit logic to ensure
    // we end-before-begin rather than init-then-teardown in the same iteration.
    // If useCh2 changes while in single_sbus mode, end the decoder now.
    if (in.rcInputMode == RC_INPUT_SINGLE_SBUS && in.useCh2 != state->lastUseCh2) {
        out.shouldEndDriveSbus = true;
    }

    // Only update lastUseCh2 while in single_sbus mode. Freezing the baseline
    // across mode transitions prevents spurious reinit when returning to single_sbus
    // after useCh2 was changed while in dual_sbus or standard_pwm mode.
    if (in.rcInputMode == RC_INPUT_SINGLE_SBUS) {
        out.shouldUpdateLastUseCh2 = (in.useCh2 != state->lastUseCh2);
    }

    // ---- PWM mode failsafe clears ----
    if (in.rcInputMode == RC_INPUT_STANDARD_PWM) {
        out.clearSbusWatchdog = true;
        out.clearSbusHw = true;
        out.clearSbus2SignalLost = true;
        out.clearSbus2HwFailsafe = true;
    }

    return out;
}

// ============================================================================
// Decoder Init/Deinit State Machine
// ============================================================================

RcInputStepDecoderStateActions rcInputStepDecoderState(
    const RcInputStepDecoderStateInputs& in) {
    RcInputStepDecoderStateActions out = {};

    // Pass through any end-before-begin directive
    out.shouldEndDriveSbus = in.shouldEndDriveSbus;

    // ---- Drive SBUS decoder state machine ----
    // Transition to desired enabled state, if not already there
    if (in.driveSbusDesiredEnabled && !in.driveSbusInitialized) {
        out.shouldBeginDriveSbus = true;
    }
    if (!in.driveSbusDesiredEnabled && in.driveSbusInitialized) {
        out.shouldEndDriveSbus = true;
    }

    // ---- Dome SBUS decoder state machine ----
    if (in.domeSbusDesiredEnabled && !in.domeSbusInitialized) {
        out.shouldBeginDomeSbus = true;
    }
    if (!in.domeSbusDesiredEnabled && in.domeSbusInitialized) {
        out.shouldEndDomeSbus = true;
    }

    return out;
}

// ============================================================================
// SBUS1 (Drive) Watchdog State Machine
// ============================================================================

RcInputStepSbus1WatchdogActions rcInputStepSbus1Watchdog(
    RcInputStepState* state, const RcInputStepSbus1WatchdogInputs& in) {
    RcInputStepSbus1WatchdogActions out = {};

    if (state == nullptr) {
        return out;
    }

    // Determine if SBUS1 watchdog tracking is active for this iteration
    bool sbus1TrackingActive = in.driveSbusInitialized &&
                               !(in.rcInputMode == RC_INPUT_SINGLE_SBUS && in.useCh2);

    if (sbus1TrackingActive) {
        // Invoke the watchdog state machine
        out.transition = sbusWatchdogCheck(&state->sbus1Watchdog, in.lastSbus1Ms, in.nowMs,
                                           in.timeoutMs);

        // Translate watchdog transitions to failsafe actions
        if (out.transition == SbusWatchdogTransition::JUST_LOST) {
            out.triggerSbusWatchdog = true;
            out.submitDriveZeroFrame = true;
            out.zeroFrameSubmitMs = in.nowMs;
        } else if (out.transition == SbusWatchdogTransition::JUST_RESTORED) {
            out.clearSbusWatchdog = true;
            out.clearSbusHw = true;
        } else if (out.transition == SbusWatchdogTransition::OK) {
            out.clearSbusHw = true;
        }
    } else {
        // Tracking disabled: reset watchdog and clear failsafe layers
        sbusWatchdogReset(&state->sbus1Watchdog);
        out.clearSbusWatchdog = true;
        out.clearSbusHw = true;
    }

    return out;
}

// ============================================================================
// SBUS2 (Dome) Watchdog State Machine
// ============================================================================

RcInputStepSbus2WatchdogActions rcInputStepSbus2Watchdog(
    RcInputStepState* state, const RcInputStepSbus2WatchdogInputs& in) {
    RcInputStepSbus2WatchdogActions out = {};

    if (state == nullptr) {
        return out;
    }

    if (in.domeSbusInitialized) {
        // Invoke the watchdog state machine
        out.transition = sbusWatchdogCheck(&state->sbus2Watchdog, in.lastSbus2Ms, in.nowMs,
                                           in.timeoutMs);

        // Translate watchdog transitions to dome actions
        if (out.transition == SbusWatchdogTransition::JUST_LOST) {
            out.setSbus2SignalLost = true;
            out.shouldStopDome = true;
            out.stopDomeMs = in.nowMs;
        } else if (out.transition == SbusWatchdogTransition::JUST_RESTORED) {
            out.clearSbus2SignalLost = true;
        }
    } else {
        // Tracking disabled: reset watchdog
        sbusWatchdogReset(&state->sbus2Watchdog);
    }

    return out;
}

// ============================================================================
// Zero-Frame Submission Phase
// ============================================================================

RcInputStepZeroFrameActions rcInputStepZeroFrame(const RcInputStepZeroFrameInputs& in) {
    RcInputStepZeroFrameActions out = {};

    if (in.pwmSignalLost || in.sbusHwFailsafe) {
        out.submitDriveZeroFrame = true;
        out.submitMs = 0;  // Will be filled in by adapter with millis()
    }

    return out;
}
