// =============================================================================
// include/drive_motion.h
//
// Whether the droid is driving (ADR 0053, #450; operator decision 2026-10-02).
// One definition, because three things turn on it: Resting Behaviour is held
// while it is true (the dome's random movement, the idle chatter), and a
// Reaction may not open a body Part.
//
// The droid is driving while any of these holds:
//   - the resolved drive output is not zero. It is a command, not a
//     measurement, so there is no noise to set a threshold above. A turn on
//     the spot is driving;
//   - the drive reports back, the report is fresh, and either wheel is turning;
//   - the command reached zero less than DRIVE_SETTLE_MS ago.
// The last two are the droid still rolling after the stick was let go: a
// stop that opens a body Part must wait for the droid to have stopped.
//
// Pure: no Arduino, no FreeRTOS, no clock. The caller reads the values under
// robotStateMux, keeps one DriveMotion of its own and passes millis().
// =============================================================================
#pragma once

#include <stdint.h>

// How long after the command reaches zero the droid still counts as driving.
// The operator's figure (2026-10-02), a starting value: nobody has timed a
// droid coasting to rest.
constexpr uint32_t DRIVE_SETTLE_MS = 1500;

// The wheel speed, in RPM as the drive reports it, at and above which a wheel
// counts as turning. A starting value, not a measurement: above the jitter a
// standing hoverboard wheel reports, below a droid being walked along.
constexpr int16_t DRIVE_WHEEL_TURNING_RPM = 10;

struct DriveMotionReading {
    int16_t driveSpeed;  // the resolved drive output
    int16_t driveSteer;
    bool feedbackValid;  // the drive reported back recently enough to be true
    int16_t wheelSpeedL;  // RPM
    int16_t wheelSpeedR;
};

// What one reader remembers between its ticks: when it last saw a command.
struct DriveMotion {
    bool commandSeen;
    uint32_t lastCommandMs;
};

inline bool driveOutputCommanded(int16_t driveSpeed, int16_t driveSteer) {
    return driveSpeed != 0 || driveSteer != 0;
}

inline bool driveWheelTurning(int16_t rpm) {
    return rpm >= DRIVE_WHEEL_TURNING_RPM || rpm <= -DRIVE_WHEEL_TURNING_RPM;
}

// One tick of one reader. Call it every tick, driving or not: the settle is
// measured from the last tick that saw a command.
inline bool driveMotionIsDriving(DriveMotion* motion, const DriveMotionReading& reading,
                                 uint32_t nowMs) {
    if (driveOutputCommanded(reading.driveSpeed, reading.driveSteer)) {
        motion->commandSeen = true;
        motion->lastCommandMs = nowMs;
        return true;
    }
    if (reading.feedbackValid &&
        (driveWheelTurning(reading.wheelSpeedL) || driveWheelTurning(reading.wheelSpeedR))) {
        return true;
    }
    return motion->commandSeen && (uint32_t)(nowMs - motion->lastCommandMs) < DRIVE_SETTLE_MS;
}
