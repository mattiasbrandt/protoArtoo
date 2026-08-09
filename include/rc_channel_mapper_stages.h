// =============================================================================
// include/rc_channel_mapper_stages.h
//
// Internal pipeline stages for RC channel mapping — exposed for testing.
// Do not depend on these from client code; use rcMapChannels() instead.
// These are implementation seams declared solely to enable unit tests of
// individual mapping stages.
//
// =============================================================================
#pragma once

#include <stdint.h>

#include "rc_channel_mapper.h"
#include "rc_mapping.h"

// Map drive controls: speed + steer (backbone)
// Returns: speedActive && steerActive, sets intent.driveSpeed and intent.driveSteer
bool rcMapDriveControls(const RcChannelSnapshot& snap, const RcMappingConfig& cfg,
                        bool useCh2, RcControlIntent* intent);

// Map dome control: speed (backbone)
// Returns: domeActive, sets intent.domeSpeed
bool rcMapDomeControl(const RcChannelSnapshot& snap, const RcMappingConfig& cfg,
                      bool useCh2, RcControlIntent* intent);

// Map servo controls: arm1 and arm2 switch positions
// Returns: servoActive, sets intent.arm1Cmd and intent.arm2Cmd
bool rcMapServoControls(const RcChannelSnapshot& snap, const RcMappingConfig& cfg,
                        bool useCh2, RcControlIntent* intent);

// Map audio trigger: rising edge detection on sound channel
// Edge detection state is maintained by caller in cfg.prevSoundPressed
// Returns: soundActive, sets intent.audioTrigger and intent.soundPressed
bool rcMapAudioTrigger(const RcChannelSnapshot& snap, const RcMappingConfig& cfg,
                       bool useCh2, RcControlIntent* intent);
