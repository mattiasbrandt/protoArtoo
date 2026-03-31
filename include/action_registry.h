// =============================================================================
// include/action_registry.h
//
// Compile-time registry of all RC-bindable robot actions.
//
// Covers every RobotActionId value (type: action, sources include sbus).
// Status, config, and event entries live in docs/action-registry.yaml only.
//
// Used by:
//   - GET /api/actions (src/web/api_actions.cpp) — serves JSON to the UI
//   - slice:e RC mapping UI dropdowns
//
// To add a new bindable action:
//   1. Add the entry to docs/action-registry.yaml.
//   2. Add the RobotActionId value to include/rc_mapping.h.
//   3. Add an ActionEntry row to src/web/action_registry.cpp.
//   4. Update test_action_registry if count assertions are used.
// =============================================================================
#pragma once

#include <stddef.h>

#include "rc_mapping.h"

// -----------------------------------------------------------------------------
// ActionEntry — one row in the compile-time ACTION_REGISTRY table.
//
// Fields mirror the subset of docs/action-registry.yaml needed at runtime.
// All string pointers are compile-time string literals (flash-safe on ESP32).
// -----------------------------------------------------------------------------
struct ActionEntry {
    RobotActionId id;            // enum value — unique per entry
    const char*   name;          // canonical: "drive.action.speed"
    const char*   display_name;  // short operator label: "Speed"
    const char*   domain;        // "drive" | "dome" | "servo" | "system"
    const char*   description;   // one-line end-user explanation
    bool          safety_critical;  // true for estop and drive safety actions
};

// -----------------------------------------------------------------------------
// ACTION_REGISTRY — flat array of all bindable actions.
// ACTION_REGISTRY_SIZE — element count (not byte count).
//
// Defined in src/web/action_registry.cpp.
// Both are extern const so they live in flash on embedded targets.
// -----------------------------------------------------------------------------
extern const ActionEntry ACTION_REGISTRY[];
extern const size_t      ACTION_REGISTRY_SIZE;
