// =============================================================================
// include/rc_input_step.h
//
// RC Input Step Core  --  pure startup, watchdog, and per-frame decisions for
// the RC input task.
//
// Startup configuration is staged for the lifetime of the task. The task loop
// gathers runtime inputs, calls these functions, and executes their plain-data
// actions. No FreeRTOS, Arduino, RobotState, hardware I/O, or logging lives here.
//
// Calling contract:
//   1. main projects and publishes the boot-active RC configuration once.
//   2. Startup consumers derive the same plan from that immutable projection.
//   3. Initialize the planned decoders once.
//   4. Call only the watchdog functions whose decoder paths are active.
//   5. Execute returned actions; transition-labelled logs stay edge-gated.
//
// =============================================================================
#pragma once

#include <stdint.h>

#include "rc_input_active_config.h"
#include "sbus_watchdog.h"  // SbusWatchdog, SbusWatchdogTransition

// ============================================================================
// Startup Decision
// ============================================================================

enum class DriveWatchdogSource : uint8_t {
    NONE = 0,            // no drive watchdog needed
    SBUS1 = 1,           // drive heartbeat via lastSbus1Ms (every SBUS drive receiver)
};

struct RcInputStartupPlan {
    bool taskEnabled = false;
    bool driveSbusEnabled = false;
    bool domeSbusEnabled = false;
    DriveWatchdogSource driveWatchdogSource = DriveWatchdogSource::NONE;
};

RcInputStartupPlan rcInputStepStartupPlan(const RcInputActiveConfig& active);

// ============================================================================
// RcInputStepState  --  cross-iteration state, owned by the step.
// Default initialization is the boot state.
// ============================================================================
struct RcInputStepState {
    SbusWatchdog sbus1Watchdog = {};    // drive receiver watchdog
    SbusWatchdog sbus2Watchdog = {};    // dome receiver watchdog
};

void rcInputStepInit(RcInputStepState* state);

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
// Drive Watchdog Phase (SBUS1)
// ============================================================================

struct RcInputStepDriveWatchdogInputs {
    // Decoder and input state
    bool driveDecoderInitialized;  // is drive decoder running?

    // Watchdog source selector
    DriveWatchdogSource source;

    // Timestamps and config
    uint32_t lastSbus1Ms;  // last clean drive frame
    uint32_t nowMs;        // current time
    uint32_t timeoutMs;    // configured timeout
};

struct RcInputStepDriveWatchdogActions {
    // Watchdog transition (tells us what changed)
    SbusWatchdogTransition transition = SbusWatchdogTransition::OK;

    // Failsafe layer decisions
    bool triggerSbusWatchdog = false;  // failsafeTrigger(SBUS_WATCHDOG)
    bool clearSbusWatchdog = false;    // failsafeClear(SBUS_WATCHDOG)

    // Zero-frame submission to drive arbiter on signal loss
    bool submitDriveZeroFrame = false;
    uint32_t zeroFrameSubmitMs = 0;
};

RcInputStepDriveWatchdogActions rcInputStepDriveWatchdog(
    RcInputStepState* state, const RcInputStepDriveWatchdogInputs& in);

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
    // Watchdog heartbeat (robotState.lastSbus1Ms): clean frames only, as on
    // SBUS2. A lost_frame frame carries a held value that looks plausible, and
    // a failsafe frame the receiver's own positions; neither is the radio
    // (#389).
    bool updateLastSbus1Ms = false;
};

RcInputStepSbus1FrameActions rcInputStepSbus1Frame(const RcInputStepSbus1FrameInputs& in);

// The dome decoder's frames (dual_sbus). A single_sbus receiver wired to the
// CH2 header is SBUS1 and takes the SBUS1 frame phase (operator, 2026-10-09 on
// #389).
struct RcInputStepSbus2FrameInputs {
    bool failsafe;             // receiver hardware failsafe flag
    bool lostFrame;            // receiver lost_frame flag
    bool hwFailsafeWasActive;  // robotState.sbus2HwFailsafe before this frame
};

struct RcInputStepSbus2FrameActions {
    bool setSbus2HwFailsafe = false;     // robotState.sbus2HwFailsafe = true
    bool clearSbus2HwFailsafe = false;   // robotState.sbus2HwFailsafe = false
    bool incrementLostFrameCount = false;
    bool updateLastSbus2Ms = false;      // watchdog heartbeat (clean frames only)
    bool logHwFailsafeAsserted = false;  // one-shot warn on rising edge
    bool dispatchBindings = false;       // dispatch SBUS2 bindings for this frame
};

// Dome decoder frames (dual_sbus).
RcInputStepSbus2FrameActions rcInputStepSbus2Frame(const RcInputStepSbus2FrameInputs& in);
