// =============================================================================
// src/drive_frame_emit.cpp
//
// Drive tick decision step (ADR 0014) — pure decision logic for frame emission.
// Extracted from drive.cpp for native testability (ADR 0005).
// =============================================================================

#include "drive_frame_emit.h"

DriveTickActions driveTickDecide(const DriveTickInputs& in) {
    DriveTickActions actions;

    // Zero-frame rule: ALWAYS emit a frame, every tick, regardless of failsafe state.
    // The hoverboard controller must receive periodic frames to prevent motor drift
    // if RC/web input is stalled or failsafe is active. Even a zero-speed frame
    // must be sent every cycle. This invariant is unconditional and lives here.
    actions.shouldEmitFrame = true;

    // Pass through arbiter-resolved output (failsafe has already zeroed if active).
    // Inputs provided to allow comprehensive test coverage across all state combinations;
    // the emit decision is unconditional on all inputs.
    actions.speed = in.arbiterSpeed;
    actions.steer = in.arbiterSteer;

    // Suppress unused-parameter warnings: inputs are used for test coverage, not logic.
    (void)in.failsafeActive;  // Intentionally unused; emit is unconditional

    return actions;
}
