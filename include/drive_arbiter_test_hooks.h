// =============================================================================
// include/drive_arbiter_test_hooks.h
//
// Native tests only: put the drive arbiter back to a known state (#466).
//
// Defined in src/drive_arbiter.cpp beside the state it clears, and declared
// here rather than in include/drive_arbiter.h because no production path
// calls it: on the droid the arbiter starts zeroed and only ages out its
// commands. Suites that submit drive intent reset it between cases.
// =============================================================================
#pragma once

// Reset arbiter state. Clears all cached commands.
void driveArbiterReset();
