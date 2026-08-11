// =============================================================================
// include/rc_input_step.h
//
// RC Input Step Core (ADR 0005/0014)  --  the RC input task's pure per-tick
// decision module.
//
// Extracts the state-machine decisions from rcInputTask (modes, decoder enables,
// watchdog transitions, failsafe state changes, zero-frame decisions) into a
// testable pure core. No FreeRTOS, no Arduino, no RobotState, no hardware I/O.
//
// The task loop (rcInputTask in rc_input.cpp) is the adapter: it gathers inputs,
// calls the step functions, and executes the returned plain-data actions.
//
// Calling contract (simplified one-iteration sketch):
//   1. Gather config (mode, enables, watchdog state, timestamps)
//   2. rcInputStepTick() -> actions for decoders (enable/disable)
//   3. rcInputStepSbus1Watchdog() -> SBUS1 failsafe transitions
//   4. rcInputStepSbus2Watchdog() -> SBUS2 state updates
//   5. Execute actions: RMT begin/end, failsafe layers, zero-frame submit, etc.
//
// =============================================================================
#pragma once

#include <stdint.h>

#include "failsafe_gate.h"  // FailsafeLayer only (enum)
#include "sbus_watchdog.h"  // SbusWatchdog, SbusWatchdogTransition

// ============================================================================
// RcInputStepState  --  cross-iteration state, owned by the step.
// Default initialization is the boot state.
// ============================================================================
struct RcInputStepState {
    SbusWatchdog sbus1Watchdog = {};  // drive receiver watchdog
    SbusWatchdog sbus2Watchdog = {};  // dome receiver watchdog
    bool lastUseCh2 = false;          // frozen baseline for single_sbus reinit
};

// ============================================================================
// Decoder Desired State Phase
// ============================================================================

struct RcInputStepTickInputs {
    // Configuration snapshot (per-tick)
    uint8_t rcInputMode;  // RC_INPUT_STANDARD_PWM, SINGLE_SBUS, DUAL_SBUS
    bool enableRcCh1;     // enable SBUS1 receiver
    bool enableRcCh2;     // enable SBUS2 receiver
    bool useCh2;          // single_sbus only: use GPIO13 (SBUS2) instead of GPIO15 (SBUS1)
};

struct RcInputStepTickActions {
    // Desired decoder state (what should be enabled for current mode/config)
    bool driveSbusDesiredEnabled = false;  // should drive SBUS decoder be active?
    bool domeSbusDesiredEnabled = false;   // should dome SBUS decoder be active?

    // Receiver-change reinit ordering (end-before-begin)
    bool shouldEndDriveSbus = false;       // end drive decoder before re-init
    bool shouldUpdateLastUseCh2 = false;   // update frozen baseline (single_sbus only)

    // Per-mode failsafe clears (PWM mode only)
    bool clearSbusWatchdog = false;        // clear FailsafeLayer::SBUS_WATCHDOG
    bool clearSbusHw = false;              // clear FailsafeLayer::SBUS_HW
    bool clearSbus2SignalLost = false;     // clear robotState.sbus2SignalLost
    bool clearSbus2HwFailsafe = false;     // clear robotState.sbus2HwFailsafe
};

void rcInputStepInit(RcInputStepState* state);
RcInputStepTickActions rcInputStepTick(RcInputStepState* state,
                                       const RcInputStepTickInputs& in);

// ============================================================================
// Decoder Init/Deinit Phase
// ============================================================================

struct RcInputStepDecoderStateInputs {
    // Current mode and desired state (from prior tick or rcInputStepTick)
    uint8_t rcInputMode;
    bool driveSbusDesiredEnabled;
    bool domeSbusDesiredEnabled;

    // Current initialized state of decoders
    bool driveSbusInitialized;
    bool domeSbusInitialized;

    // Receiver-change end-before-begin ordering
    bool shouldEndDriveSbus;
};

struct RcInputStepDecoderStateActions {
    bool shouldBeginDriveSbus = false;
    bool shouldEndDriveSbus = false;   // may be set from input or by this function
    bool shouldBeginDomeSbus = false;
    bool shouldEndDomeSbus = false;
};

RcInputStepDecoderStateActions rcInputStepDecoderState(
    const RcInputStepDecoderStateInputs& in);

// ============================================================================
// SBUS1 (Drive) Watchdog Phase
// ============================================================================

struct RcInputStepSbus1WatchdogInputs {
    // Current mode and tracking configuration
    uint8_t rcInputMode;
    bool useCh2;  // single_sbus: are we using GPIO13 (SBUS2)?

    // Decoder and input state
    bool driveSbusInitialized;  // is decoder running?

    // Watchdog timestamps and config
    uint32_t lastSbus1Ms;  // last valid frame timestamp
    uint32_t nowMs;        // current time
    uint32_t timeoutMs;    // configured timeout
};

struct RcInputStepSbus1WatchdogActions {
    // Watchdog transition (tells us what changed)
    SbusWatchdogTransition transition = SbusWatchdogTransition::OK;

    // Failsafe layer decisions
    bool triggerSbusWatchdog = false;      // failsafeTrigger(SBUS_WATCHDOG)
    bool triggerSbusHw = false;            // failsafeTrigger(SBUS_HW) on HW failsafe
    bool clearSbusWatchdog = false;        // failsafeClear(SBUS_WATCHDOG)
    bool clearSbusHw = false;              // failsafeClear(SBUS_HW)

    // Zero-frame submission to drive arbiter on signal loss
    bool submitDriveZeroFrame = false;
    uint32_t zeroFrameSubmitMs = 0;
};

RcInputStepSbus1WatchdogActions rcInputStepSbus1Watchdog(
    RcInputStepState* state, const RcInputStepSbus1WatchdogInputs& in);

// ============================================================================
// SBUS2 (Dome) Watchdog Phase
// ============================================================================

struct RcInputStepSbus2WatchdogInputs {
    // Decoder and input state
    bool domeSbusInitialized;  // is decoder running?

    // Watchdog timestamps and config
    uint32_t lastSbus2Ms;  // last valid frame timestamp
    uint32_t nowMs;        // current time
    uint32_t timeoutMs;    // configured timeout
};

struct RcInputStepSbus2WatchdogActions {
    // Watchdog transition
    SbusWatchdogTransition transition = SbusWatchdogTransition::OK;

    // Signal-lost state update (for robotState.sbus2SignalLost)
    bool setSbus2SignalLost = false;
    bool clearSbus2SignalLost = false;

    // Dome command: stop dome on timeout
    bool shouldStopDome = false;
    uint32_t stopDomeMs = 0;
};

RcInputStepSbus2WatchdogActions rcInputStepSbus2Watchdog(
    RcInputStepState* state, const RcInputStepSbus2WatchdogInputs& in);

// ============================================================================
// Per-Frame Phase  --  decisions taken on each received SBUS frame.
//
// Only the receiver flags and prior failsafe state cross the boundary; the
// 16-channel frame payload stays in the adapter (RobotState channel copies).
// ============================================================================

struct RcInputStepSbus1FrameInputs {
    bool failsafe;             // receiver hardware failsafe flag
    bool lostFrame;            // receiver lost_frame flag
    bool hwFailsafeWasActive;  // robotState.sbusHwFailsafe before this frame
};

struct RcInputStepSbus1FrameActions {
    bool triggerSbusHw = false;          // failsafeTrigger(SBUS_HW)
    bool logHwFailsafeAsserted = false;  // one-shot warn on rising edge
    bool submitDriveZeroFrame = false;   // driveArbiterSubmit(RC, 0, 0, now)
    bool incrementLostFrameCount = false;
    bool clearSbusHw = false;            // failsafeClear(SBUS_HW)
    bool clearSbusWatchdog = false;      // failsafeClear(SBUS_WATCHDOG)
    bool dispatchBindings = false;       // dispatch SBUS1 bindings for this frame
};

RcInputStepSbus1FrameActions rcInputStepSbus1Frame(const RcInputStepSbus1FrameInputs& in);

// Shared by the two SBUS2 frame sources: the dome decoder (dual_sbus) and the
// drive decoder routed to GPIO13 (single_sbus + useCh2).
struct RcInputStepSbus2FrameInputs {
    bool failsafe;             // receiver hardware failsafe flag
    bool lostFrame;            // receiver lost_frame flag
    bool hwFailsafeWasActive;  // robotState.sbus2HwFailsafe before this frame
};

struct RcInputStepSbus2FrameActions {
    bool setSbus2HwFailsafe = false;     // robotState.sbus2HwFailsafe = true
    bool clearSbus2HwFailsafe = false;   // robotState.sbus2HwFailsafe = false
    bool clearSbus2SignalLost = false;   // robotState.sbus2SignalLost = false
    bool incrementLostFrameCount = false;
    bool updateLastSbus2Ms = false;      // watchdog heartbeat (clean frames only)
    bool logHwFailsafeAsserted = false;  // one-shot warn on rising edge
    bool dispatchBindings = false;       // dispatch SBUS2 bindings for this frame
};

// Dome decoder frames (dual_sbus).
RcInputStepSbus2FrameActions rcInputStepSbus2Frame(const RcInputStepSbus2FrameInputs& in);

// Drive decoder frames routed as SBUS2 (single_sbus + useCh2).
RcInputStepSbus2FrameActions rcInputStepSbus2RoutedFrame(const RcInputStepSbus2FrameInputs& in);

// ============================================================================
// Zero-Frame Submission Phase (PWM and HW failsafe paths)
// ============================================================================

struct RcInputStepZeroFrameInputs {
    bool pwmSignalLost = false;   // PWM mode signal lost check
    bool sbusHwFailsafe = false;  // SBUS HW failsafe flag from receiver
};

struct RcInputStepZeroFrameActions {
    bool submitDriveZeroFrame = false;
    uint32_t submitMs = 0;
};

RcInputStepZeroFrameActions rcInputStepZeroFrame(const RcInputStepZeroFrameInputs& in);
