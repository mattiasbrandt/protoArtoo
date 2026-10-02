// =============================================================================
// include/drive_motion.h
//
// Whether the droid is driving, from the resolved drive output DriveTask
// leaves in RobotState (ADR 0053, #450). One definition, because three things
// turn on it: Resting Behaviour is held while it is true (the dome's random
// movement, the idle chatter), and a Reaction may not open a body Part.
//
// The output is a command, not a measurement, so it has no noise to set a
// threshold above: zero is at rest and anything else is driving. A turn on the
// spot is driving.
//
// Pure: no Arduino, no FreeRTOS. The caller reads the two values under
// robotStateMux.
// =============================================================================
#pragma once

#include <stdint.h>

inline bool driveOutputIsDriving(int16_t driveSpeed, int16_t driveSteer) {
    return driveSpeed != 0 || driveSteer != 0;
}
