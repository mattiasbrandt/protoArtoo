// =============================================================================
// include/failsafe_gate_test_hooks.h
//
// Native tests only: the failsafe gate's test-facing reads (#466).
//
// Defined in src/failsafe_gate.cpp beside the gate state it reads, and
// declared here rather than in include/failsafe_gate.h because no production
// path calls it: the droid acts on failsafeIsActive() and the robotState
// mirrors, never on which layer ranks first. A test that needs to know which
// layer holds the gate includes this header.
// =============================================================================
#pragma once

#include "failsafe_gate.h"  // FailsafeLayer

// Return the highest-priority active failsafe layer (lowest enum index).
// Returns SBUS_HW as default if none active (caller should check failsafeIsActive first).
FailsafeLayer failsafeActiveReason();
