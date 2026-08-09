// =============================================================================
// include/drive_frame_emit.h
//
// Drive tick decision step (ADR 0014)  --  pure decision logic for frame emission.
// Extracted from drive.cpp for native testability (ADR 0005).
//
// The driveTickDecide() function encodes the zero-frame continuity invariant:
// a frame to the hoverboard is emitted every tick regardless of failsafe state
// or command freshness. The function takes these inputs to allow comprehensive
// testing across all state combinations; the decision is unconditionally true.
// =============================================================================
#pragma once

#include <cstdint>

// Drive tick action set  --  what the loop should do this iteration
struct DriveTickActions {
    bool shouldEmitFrame = false;  // Zero-frame rule: ALWAYS true
    int16_t speed = 0;             // Arbiter-resolved speed (with failsafe applied)
    int16_t steer = 0;             // Arbiter-resolved steer (with failsafe applied)
};

// Drive tick decision inputs  --  all state the decision needs
struct DriveTickInputs {
    bool failsafeActive = false;   // Any failsafe layer is active
    int16_t arbiterSpeed = 0;      // From DriveArbiter (pre-failsafe value)
    int16_t arbiterSteer = 0;      // From DriveArbiter (pre-failsafe value)
};

// Drive tick decision step: take arbiter output and failsafe state,
// decide whether to emit frame and what speed/steer to send.
//
// SAFETY: shouldEmitFrame is UNCONDITIONALLY true regardless of any input.
// The zero-frame rule requires sending a frame (even zero-valued) every tick.
DriveTickActions driveTickDecide(const DriveTickInputs& in);
