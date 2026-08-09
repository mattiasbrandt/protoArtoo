#pragma once

#include "robot_state.h"

// Sets robotState.stationary, detects the release edge (stationary->driving),
// syncs the config cache, and queues the drive-on-resume audio cue on release.
// Source is reserved for logging/telemetry symmetry with other commanded-mode
// setters; currently not used (each setter body does (void)source) but kept
// in the signature for a consistent module interface.
void commandedSetStationary(bool stationary, CommandSource source);

// Sets robotState.sleepMode and sleepSinceMs under critical section.
// Returns true if sleepMode actually changed (false->true or true->false).
// Sets sleepSinceMs = now on entry, 0 on exit. Does not broadcast status or log  -- 
// caller decides what to do with the changed result (some callers unconditionally
// broadcast regardless).
bool commandedSetSleep(bool sleep, CommandSource source);

// Sets robotState.webControlEnabled under critical section.
void commandedSetWebControl(bool enabled, CommandSource source);

// Sets robotState.rcDebugMode under critical section.
void commandedSetRcDebug(bool enabled, CommandSource source);

// Sets robotState.activeMood under critical section.
void commandedSetActiveMood(uint8_t moodId, CommandSource source);
