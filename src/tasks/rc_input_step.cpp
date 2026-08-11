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
    }
}

// ============================================================================
// Startup Decision
// ============================================================================

// Identify modes that route drive input through SBUS. The startup-plan builder
// calls this so hardware decisions remain pure and independent of config/NVS.
static bool is_drive_sbus_mode(uint8_t mode) {
    return mode == RC_INPUT_SINGLE_SBUS || mode == RC_INPUT_DUAL_SBUS;
}

// Identify modes that dedicate the second decoder to dome input. The
// startup-plan builder calls this to keep the #167 single-SBUS route excluded.
static bool is_dome_sbus_mode(uint8_t mode) {
    return mode == RC_INPUT_DUAL_SBUS;
}

// Decide whether the boot-active mode, route, and drive RC toggles need a drive
// decoder. rcInputStepStartupPlan calls this before main initializes hardware.
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

// Project the immutable boot-active RC configuration into decoder, watchdog,
// and task decisions. main and rcInputTask call this from the same published
// active snapshot so startup hardware and runtime dispatch cannot diverge.
RcInputStartupPlan rcInputStepStartupPlan(const RcInputActiveConfig& active) {
    RcInputStartupPlan out = {};

    out.driveSbusEnabled =
        driveSbusDecoderEnabledForMode(active.mode, active.enableRc[0], active.enableRc[1],
                                       active.useCh2);
    out.domeSbusEnabled = is_dome_sbus_mode(active.mode) && active.enableRc[1];
    out.sbus1WatchdogEnabled =
        out.driveSbusEnabled &&
        !(active.mode == RC_INPUT_SINGLE_SBUS && active.useCh2);

    if (active.mode == RC_INPUT_STANDARD_PWM) {
        out.taskEnabled = active.enableRc[0] || active.enableRc[1] || active.enableRc[2] ||
                          active.enableRc[3] || active.enableRc[4] || active.enableRc[5];
    } else {
        out.taskEnabled = out.driveSbusEnabled || out.domeSbusEnabled;
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
    }

    return out;
}

// ============================================================================
// Per-Frame Decision Phase
// ============================================================================

RcInputStepSbus1FrameActions rcInputStepSbus1Frame(const RcInputStepSbus1FrameInputs& in) {
    RcInputStepSbus1FrameActions out = {};

    if (in.failsafe) {
        // Layer 1: hardware failsafe flag from receiver firmware.
        out.triggerSbusHw = true;
        out.logHwFailsafeAsserted = !in.hwFailsafeWasActive;
        out.submitDriveZeroFrame = true;
    } else if (in.lostFrame) {
        out.incrementLostFrameCount = true;
    } else {
        out.clearSbusHw = true;
        out.clearSbusWatchdog = true;
        out.dispatchBindings = true;
    }

    return out;
}

RcInputStepSbus2FrameActions rcInputStepSbus2Frame(const RcInputStepSbus2FrameInputs& in) {
    RcInputStepSbus2FrameActions out = {};

    // sbus2HwFailsafe tracks the receiver flag on every frame, including
    // lost_frame ones (failsafe=false there clears it).
    out.setSbus2HwFailsafe = in.failsafe;
    out.clearSbus2HwFailsafe = !in.failsafe;
    out.logHwFailsafeAsserted = in.failsafe && !in.hwFailsafeWasActive;
    out.incrementLostFrameCount = in.lostFrame;

    // Suppress dispatch (and watchdog heartbeat) on any receiver-side signal
    // quality event: hardware failsafe OR lost_frame.
    // - failsafe: receiver outputting programmed failsafe positions.
    // - lost_frame: receiver missed a TX packet; outputs hold/failsafe position
    //   with lost_frame=true, failsafe=false. Without this guard, the programmed
    //   hold position (ch1=389 = -89%) would be dispatched to the dome task.
    // Suppressing the watchdog heartbeat on both events means the SBUS2 watchdog
    // fires and stops the dome if either condition persists.
    bool suppress = in.failsafe || in.lostFrame;
    out.updateLastSbus2Ms = !suppress;
    out.dispatchBindings = !suppress;

    return out;
}

RcInputStepSbus2FrameActions rcInputStepSbus2RoutedFrame(const RcInputStepSbus2FrameInputs& in) {
    RcInputStepSbus2FrameActions out = {};

    // Routed path (drive decoder reading GPIO13): sbus2HwFailsafe latches across
    // lost_frame events and only clears on a clean frame, and a clean frame also
    // clears sbus2SignalLost directly (no SBUS2 watchdog restore runs for this
    // path when the dome decoder is not initialized).
    if (in.failsafe) {
        out.setSbus2HwFailsafe = true;
    } else if (in.lostFrame) {
        out.incrementLostFrameCount = true;
    } else {
        out.clearSbus2HwFailsafe = true;
        out.clearSbus2SignalLost = true;
        out.updateLastSbus2Ms = true;
        out.dispatchBindings = true;
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
