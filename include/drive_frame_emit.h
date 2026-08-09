// =============================================================================
// include/drive_frame_emit.h
//
// Pure decision logic for drive frame emission continuity.
// Extracted from drive.cpp for native testability (ADR 0005).
// =============================================================================
#pragma once

#include <cstdint>

// Drive frame emission decision.
// Returns true if a frame should be emitted on this tick.
// SAFETY: Zero-frame continuity — the hoverboard receives a frame every tick
// regardless of failsafe state, command availability, or other conditions.
// This prevents drift if command input is stalled or failsafe is active.
//
// Inputs encode the state the decision should NOT gate on:
//   failsafeActive: whether any failsafe layer is currently active (frame sent anyway)
//   commandFresh:   whether the latest motion command is still within timeout (frame sent anyway)
//
// Both inputs are provided so the decision can be tested against all state combinations.
bool driveFrameShouldEmit(bool failsafeActive, bool commandFresh);
