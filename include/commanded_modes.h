#pragma once

#include "robot_state.h"

// Sets robotState.stationary, detects the release edge (stationary->driving),
// syncs the config cache, and queues the drive-on-resume audio cue on release.
// Source is recorded for logging/telemetry symmetry with other commanded-mode
// setters; currently not used in logging but accepted for consistent module
// interface across Z3+ setters.
void commandedSetStationary(bool stationary, CommandSource source);
