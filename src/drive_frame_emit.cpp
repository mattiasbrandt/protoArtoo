// =============================================================================
// src/drive_frame_emit.cpp
//
// Pure decision logic for drive frame emission continuity.
// Extracted from drive.cpp for native testability (ADR 0005).
// =============================================================================

#include "drive_frame_emit.h"

bool driveFrameShouldEmit(bool failsafeActive, bool commandFresh) {
    // Zero-frame rule: always emit a frame, every tick, regardless of state.
    // The hoverboard controller must receive periodic frames to prevent motor drift
    // if RC/web input is stalled or failsafe is active. Even a zero-speed frame
    // (failsafe applies speed=0) must be sent every cycle.
    //
    // Inputs are provided to allow the decision to be tested against all state
    // combinations, but the result is unconditionally true: frame emission is
    // not gated on failsafe state or command freshness.
    (void)failsafeActive;  // intentionally unused; decision is unconditional
    (void)commandFresh;    // intentionally unused; decision is unconditional
    return true;
}
