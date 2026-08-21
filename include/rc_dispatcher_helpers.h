// =============================================================================
// include/rc_dispatcher_helpers.h
//
// RC dispatch helpers  --  decouple rc_input.cpp from subsystem queue APIs.
// Each helper consumes its slice of RcActionResult, encapsulating all calls
// to a specific subsystem. rc_input.cpp calls helpers and stops including
// subsystem headers solely for dispatch.
//
// These are pure dispatch wrappers: they queue commands, log, and handle
// queue-full conditions. They do NOT alter rc_input.cpp's control flow
// or real-time constraints.
//
// =============================================================================
#pragma once

#include "rc_action_dispatcher.h"

// Forward declarations  --  headers NOT included here to avoid coupling rc_input.cpp
// to the world. Each helper includes only what it needs.
struct RcProcessorOutput;
struct RcMappingConfig;
struct RcTriggerBinding;

// Dispatch drive commands from RC backbone intent.
// Logs queue-full conditions; returns true if dispatch succeeded.
void rcDispatchDrive(int16_t driveSpeed, int16_t driveSteer, bool shouldStop);

// Dispatch dome commands from RC backbone intent.
// Logs queue-full conditions.
void rcDispatchDome(int domeRawFiltered, const RcMappingConfig& mapping, bool domeFiltered);

// Dispatch audio trigger from RC backbone intent.
// Logs queue-full conditions.
void rcDispatchAudioTrigger(const char* audioTrigger);

// Dispatch a single RcActionResult.
// Used by processTriggerAction (single action dispatch) and trigger loop dispatch.
// Note: this does NOT dispatch system modes (estop, sleep, stationary, speed preset);
// those are handled separately in rcDispatchTriggerResults for the trigger loop.
void rcDispatchSingleAction(const RcActionResult& res);

// Dispatch Tier 2 trigger results: audio, servo, dome, marcduino, system modes.
// Consumes the full RcActionResult array for all triggers.
// Logs queue-full and dispatch conditions per trigger.
void rcDispatchTriggerResults(const RcProcessorOutput& output,
                              const RcTriggerBinding* triggers);
