// =============================================================================
// src/rc_channel_mapper.cpp
//
// Pure RC channel mapping implementation  --  converts raw channel snapshots to
// control intent without FreeRTOS, mutex, or RobotState coupling.
//
// =============================================================================

#include "rc_channel_mapper.h"
#include "rc_map_rules.h"
#include "rc_mapping.h"
#include "rc_pwm_helpers.h"

// ============================================================================
// Helper: Read raw value from channel snapshot (PWM or SBUS)
// ============================================================================

static bool readChannelRaw(const RcChannelSnapshot& snap, const RcBindingConfig& binding,
                           int* outRaw) {
    if (outRaw == nullptr) {
        return false;
    }

    // Validate binding against snapshot mode
    if (binding.source == RC_BINDING_NONE) {
        return false;
    }

    // PWM bindings read from channels 1-6 (indices 0-5)
    if (binding.source == RC_BINDING_PWM) {
        if (snap.mode != RC_INPUT_STANDARD_PWM) {
            return false;
        }
        if (binding.channel < 1 || binding.channel > 6) {
            return false;
        }
        int raw = snap.channels[binding.channel - 1];
        if (!rcPwmPulseIsValid((uint32_t)raw)) {
            return false;
        }
        *outRaw = raw;
        return true;
    }

    // SBUS bindings read from snapshot channels
    if (binding.source == RC_BINDING_SBUS1 || binding.source == RC_BINDING_SBUS2) {
        // In SBUS mode, snapshot contains all 18 channels
        if (snap.mode != RC_INPUT_SINGLE_SBUS && snap.mode != RC_INPUT_DUAL_SBUS) {
            return false;
        }
        if (binding.channel < 1 || binding.channel > 18) {
            return false;
        }
        *outRaw = snap.channels[binding.channel - 1];
        return true;
    }

    return false;
}

// ============================================================================
// Helper: Check if a binding reads this snapshot
// ============================================================================
//
// A binding reads a snapshot only when its receiver type reads its source
// (rcReceiverReads(), the RC Map's rules - the same answer /api/rc and
// /api/validation report) AND the frame came from that source. For PWM it also
// validates the channel range (1-6).
//
static bool bindingSourceActiveForMode(const RcBindingConfig& binding, const RcChannelSnapshot& snap,
                                       const RcMappingConfig& cfg) {
    if (snap.source != RC_BINDING_NONE && binding.source != snap.source) {
        return false;
    }
    if (binding.source == RC_BINDING_PWM && (binding.channel < 1 || binding.channel > 6)) {
        return false;
    }
    RcReceiverSetup setup = {};
    setup.mode = snap.mode;
    for (size_t i = 0; i < 6; ++i) {
        setup.enableRc[i] = cfg.enableRc[i];
    }
    setup.useCh2 = cfg.useCh2;
    return rcReceiverReads(binding.source, setup);
}

bool rcMapBindingReadsSnapshot(const RcBindingConfig& binding, const RcChannelSnapshot& snap,
                               const RcMappingConfig& cfg) {
    int raw = 0;
    return snap.valid && rcBindingIsValid(binding) && bindingSourceActiveForMode(binding, snap, cfg) &&
           readChannelRaw(snap, binding, &raw);
}

// ============================================================================
// Mapping Stages  --  independently testable seams in the pipeline
// ============================================================================

// Map drive controls: speed + steer (backbone)
// Returns: speedActive && steerActive, sets intent.driveSpeed and intent.driveSteer
bool rcMapDriveControls(const RcChannelSnapshot& snap, const RcMappingConfig& cfg,
                        RcControlIntent* intent) {
    int rawSpeed = 0;
    int rawSteer = 0;
    bool speedActive = false;
    bool steerActive = false;

    // Check if drive mappings are valid. A drive pair the RC Map's rules
    // refuse never reads: a drive a droid stored on SBUS2 before #483 stays
    // still rather than running on a receiver without the drive watchdog and
    // the hardware-failsafe stop (POST /api/rc/map refuses one now).
    if (rcBindingIsValid(cfg.driveSpeed) && rcBindingIsValid(cfg.driveSteer) &&
        rcRuleDrive(cfg.driveSpeed, cfg.driveSteer).ok()) {
        // Check if speed binding is active for this mode
        if (bindingSourceActiveForMode(cfg.driveSpeed, snap, cfg) &&
            readChannelRaw(snap, cfg.driveSpeed, &rawSpeed)) {
            speedActive = true;
        }

        // Check if steer binding is active for this mode
        if (bindingSourceActiveForMode(cfg.driveSteer, snap, cfg) &&
            readChannelRaw(snap, cfg.driveSteer, &rawSteer)) {
            steerActive = true;
        }
    }

    // Only emit drive commands if both speed and steer are available
    if (speedActive && steerActive) {
        float normalizedSpeed = applyRcAnalogCalibration(rawSpeed, cfg.driveSpeed, nullptr);
        float normalizedSteer = applyRcAnalogCalibration(rawSteer, cfg.driveSteer, nullptr);

        // Clamp to [-1, +1] before scaling by maxOut
        if (normalizedSpeed < -1.0f) normalizedSpeed = -1.0f;
        if (normalizedSpeed > 1.0f) normalizedSpeed = 1.0f;
        if (normalizedSteer < -1.0f) normalizedSteer = -1.0f;
        if (normalizedSteer > 1.0f) normalizedSteer = 1.0f;

        intent->driveSpeed = (int16_t)(normalizedSpeed * cfg.maxOut);
        intent->driveSteer = (int16_t)(normalizedSteer * cfg.maxOut);
    } else {
        // If either is inactive, zero both (safe state)
        intent->driveSpeed = 0;
        intent->driveSteer = 0;
    }

    return speedActive && steerActive;
}

// Map dome control: speed (backbone)
// Returns: domeActive, sets intent.domeSpeed
bool rcMapDomeControl(const RcChannelSnapshot& snap, const RcMappingConfig& cfg,
                      RcControlIntent* intent) {
    int rawDome = 0;
    if (cfg.enableDome && rcBindingIsValid(cfg.domeSpeed) &&
        bindingSourceActiveForMode(cfg.domeSpeed, snap, cfg) &&
        readChannelRaw(snap, cfg.domeSpeed, &rawDome)) {
        float normalizedDome = applyRcAnalogCalibration(rawDome, cfg.domeSpeed, nullptr);

        // Clamp to [-1, +1] before scaling
        if (normalizedDome < -1.0f) normalizedDome = -1.0f;
        if (normalizedDome > 1.0f) normalizedDome = 1.0f;

        intent->domeSpeed = (int16_t)(normalizedDome * cfg.maxOut);
        return true;
    }
    intent->domeSpeed = 0;
    return false;
}

// Map servo controls: arm1 and arm2 switch positions
// Returns: servoActive, sets intent.arm1Cmd and intent.arm2Cmd
bool rcMapServoControls(const RcChannelSnapshot& snap, const RcMappingConfig& cfg,
                        RcControlIntent* intent) {
    // Convert switch state to servo command: LOW -> close, MID -> neutral, HIGH -> open
    // For v1.0.0: aux1, aux2, aux3 remain unmapped (out of scope).
    int rawArm1 = 0;
    int rawArm2 = 0;
    bool arm1Active = false;
    bool arm2Active = false;

    if (cfg.enableArm1 && rcBindingIsValid(cfg.arm1) &&
        bindingSourceActiveForMode(cfg.arm1, snap, cfg) &&
        readChannelRaw(snap, cfg.arm1, &rawArm1)) {
        RcSwitchState arm1State = rcAnalogToSwitchState(rawArm1, cfg.arm1);
        if (arm1State == RC_SWITCH_HIGH) {
            intent->arm1Cmd = RC_SERVO_OPEN;
        } else if (arm1State == RC_SWITCH_LOW) {
            intent->arm1Cmd = RC_SERVO_CLOSE;
        } else {
            intent->arm1Cmd = RC_SERVO_NEUTRAL;
        }
        arm1Active = true;
    } else {
        intent->arm1Cmd = RC_SERVO_NO_CHANGE;
    }

    if (cfg.enableArm2 && rcBindingIsValid(cfg.arm2) &&
        bindingSourceActiveForMode(cfg.arm2, snap, cfg) &&
        readChannelRaw(snap, cfg.arm2, &rawArm2)) {
        RcSwitchState arm2State = rcAnalogToSwitchState(rawArm2, cfg.arm2);
        if (arm2State == RC_SWITCH_HIGH) {
            intent->arm2Cmd = RC_SERVO_OPEN;
        } else if (arm2State == RC_SWITCH_LOW) {
            intent->arm2Cmd = RC_SERVO_CLOSE;
        } else {
            intent->arm2Cmd = RC_SERVO_NEUTRAL;
        }
        arm2Active = true;
    } else {
        intent->arm2Cmd = RC_SERVO_NO_CHANGE;
    }

    return arm1Active || arm2Active;
}

// Map audio trigger: rising edge detection on sound channel
// Edge detection state is maintained by caller in cfg.prevSoundPressed
// Returns: soundActive, sets intent.audioTrigger and intent.soundPressed
bool rcMapAudioTrigger(const RcChannelSnapshot& snap, const RcMappingConfig& cfg,
                       RcControlIntent* intent) {
    // Audio fires on rising edge: transition from LOW/MID to HIGH.
    // Token is a static Marcduino command string ("$87" = random general sound).
    intent->audioTrigger = nullptr;
    intent->soundPressed = false;
    int rawSound = 0;

    if (cfg.enableSound && rcBindingIsValid(cfg.sound) &&
        bindingSourceActiveForMode(cfg.sound, snap, cfg) &&
        readChannelRaw(snap, cfg.sound, &rawSound)) {
        RcSwitchState soundState = rcAnalogToSwitchState(rawSound, cfg.sound);
        intent->soundPressed = (soundState == RC_SWITCH_HIGH);
        // Rising edge detection
        if (intent->soundPressed && !cfg.prevSoundPressed) {
            intent->audioTrigger = "$87";  // Random general sound trigger
        }
        return true;
    }
    // If binding invalid, reset state to prevent stuck trigger on re-enable
    return false;
}

// ============================================================================
// Main Pure Mapping Function
// ============================================================================

RcControlIntent rcMapChannels(const RcChannelSnapshot& snap, const RcMappingConfig& cfg) {
    RcControlIntent intent = {};
    intent.valid = false;
    intent.audioTrigger = nullptr;

    // Snapshot must be valid
    if (!snap.valid) {
        return intent;
    }

    // Apply mapping stages. Each returns whether it had an active, valid
    // binding for this snapshot and therefore contributed to the intent.
    bool driveActive = rcMapDriveControls(snap, cfg, &intent);
    bool domeActive = rcMapDomeControl(snap, cfg, &intent);
    bool servoActive = rcMapServoControls(snap, cfg, &intent);
    bool soundActive = rcMapAudioTrigger(snap, cfg, &intent);
    intent.driveActive = driveActive;
    intent.domeActive = domeActive;
    intent.soundActive = soundActive;

    // Validity: any stage that produced an intent makes this intent valid.
    //
    // Every stage already gates on binding validity and source-mode match, so
    // this is exactly "a configured binding was active for this snapshot".
    // Servo and sound count: an intent carrying only an arm toggle or only an
    // audio trigger is as real as one carrying drive output, and reporting it
    // invalid would mean anything that later gates on this field silently
    // drops those actions.
    //
    // Dome is taken from the stage result rather than re-derived from
    // intent.domeSpeed: rcMapDomeControl() already honours cfg.enableDome, and
    // a centred dome stick is an active binding reporting zero, not an absent
    // one.
    intent.valid = driveActive || domeActive || servoActive || soundActive;

    return intent;
}
