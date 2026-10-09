// =============================================================================
// include/rc_channel_mapper.h
//
// Pure RC channel mapping module  --  converts raw channel snapshots to control
// intent without FreeRTOS, mutex, or RobotState coupling.
//
// This module is designed to be unit-testable in isolation, accepting only
// input parameters and returning output structures with no side effects.
//
// =============================================================================
#pragma once

#include <stdint.h>

#include "rc_mapping.h"
#include "robot_state.h"

// ============================================================================
// Input: Channel snapshot (raw channel data from receiver)
// ============================================================================

struct RcChannelSnapshot {
    // Raw channel values indexed by channel number:
    // - PWM: pulse width in microseconds
    // - SBUS: 0-indexed into [172..1811] range
    // channels[0] = channel 1, channels[17] = channel 18
    int16_t channels[18];

    // Whether this snapshot contains valid data
    bool valid;

    // Current input mode (determines how channels are interpreted)
    RcInputMode mode;

    // The receiver this frame came from. A binding reads a frame only from its
    // own receiver, so in dual_sbus the dome receiver's sticks never reach a
    // drive binding and the drive receiver's never reach the dome (#389).
    // RC_BINDING_NONE leaves the frame unattributed: every source the mode
    // enables may read it.
    RcBindingSource source;
};

// ============================================================================
// Servo command types for RC mapper output
// ============================================================================
enum RcServoCommand : uint8_t {
    RC_SERVO_NO_CHANGE = 0,  // No command; skip dispatch
    RC_SERVO_OPEN = 1,       // Open position (HIGH state)
    RC_SERVO_CLOSE = 2,      // Close position (LOW state)
    RC_SERVO_NEUTRAL = 3,    // Neutral/mid position
};

// ============================================================================
// Output: Control intent (what the operator wants the robot to do)
// ============================================================================

struct RcControlIntent {
    // Analog backbone controls (normalized -1000..+1000, where 0 = neutral)
    int16_t driveSpeed;       // -1000 = full reverse, +1000 = full forward, 0 = stop
    int16_t driveSteer;       // -1000 = full left, +1000 = full right, 0 = straight
    int16_t domeSpeed;        // -1000 = full reverse, +1000 = full forward, 0 = stop

    // Servo command targets (enum; dispatched to servo command queue by rc_input_task)
    // RC_SERVO_NO_CHANGE means skip dispatch; others translate to SERVO_CMD_* + positionUs
    RcServoCommand arm1Cmd;
    RcServoCommand arm2Cmd;

    // Audio trigger token (static const string or null)
    // Points to a static token string if an audio trigger fired; nullptr otherwise
    const char* audioTrigger;

    // Sound switch state (valid only if sound binding is active and enabled)
    // Used by dispatch path to update lastSoundPressed for next iteration
    bool soundPressed;

    // Whether the drive (speed AND steer) and dome bindings read this frame.
    // A frame from a receiver the binding does not read leaves them false, and
    // the dispatcher then sends nothing for them rather than a zero (#389).
    bool driveActive;
    bool domeActive;
    // Whether the sound binding read this frame (soundPressed is meaningful).
    bool soundActive;

    // Whether any mapping stage produced an intent from this snapshot: true if
    // at least one configured binding was active and matched the input mode,
    // across drive, dome, servo AND sound. False means the snapshot was
    // invalid, or no binding applied to it.
    //
    // Not a motion predicate: an intent carrying only a servo toggle or only an
    // audio trigger is valid. Anything that gates dispatch on this field must
    // keep working for those.
    bool valid;
};

// ============================================================================
// Configuration: Mapping settings extracted from RobotState
// ============================================================================

struct RcMappingConfig {
    // Enable flags for each receiver channel and feature
    bool enableRc[6];          // Channels 1-6 enabled
    bool enableDome;
    bool enableArm1;
    bool enableArm2;
    bool enableSound;

    // Speed limit applied to all analog outputs
    int16_t maxOut;

    // Backbone channel bindings (drive/dome/servos)
    RcBindingConfig driveSpeed;
    RcBindingConfig driveSteer;
    RcBindingConfig domeSpeed;
    RcBindingConfig arm1;
    RcBindingConfig arm2;
    RcBindingConfig sound;

    // Edge detection state for audio trigger (caller maintains this across calls)
    bool prevSoundPressed;

    // single_sbus only: the receiver is wired to the CH2 header rather than CH1
    // (the sbusRecvCh2 Setting). It is still SBUS1 (operator, 2026-10-09 on
    // #389); the flag only picks which header's enable gates it.
    bool useCh2;
};

// ============================================================================
// Pure Mapping Function
// ============================================================================

// rcMapChannels()  --  convert raw channel snapshot to control intent
//
// Pure function:
//   - No global state reads
//   - No mutex access
//   - No FreeRTOS calls
//   - No dynamic allocation
//   - No side effects
//
// Parameters:
//   snap    --  raw channel snapshot from receiver
//   cfg     --  mapping configuration extracted from RobotState
//
// Returns:
//   RcControlIntent with interpreted control values and audio trigger
//
// Notes:
//   - Output values are clamped to [-maxOut..+maxOut]
//   - Dead-zone and reverse polarity applied per binding config
//   - Audio trigger is a static const string pointer or nullptr
//   - Validity depends on input snapshot validity and binding validity, and is
//     set from all four stage results (drive, dome, servo, sound) - not from
//     drive and dome alone
//
RcControlIntent rcMapChannels(const RcChannelSnapshot& snap, const RcMappingConfig& cfg);

// rcMapBindingReadsSnapshot()  --  whether a binding reads this snapshot: its
// source is enabled for the mode, the frame came from that source (when the
// snapshot names one), and the channel is in range. The same rule every
// mapping stage applies, for callers that read a binding's raw value
// themselves (the dome input filter).
bool rcMapBindingReadsSnapshot(const RcBindingConfig& binding, const RcChannelSnapshot& snap,
                               const RcMappingConfig& cfg);
