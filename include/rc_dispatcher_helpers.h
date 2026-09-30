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

#include "marcduino_ownership.h"  // MarcduinoRouteOutcome
#include "rc_action_dispatcher.h"

// Forward declarations  --  headers NOT included here to avoid coupling rc_input.cpp
// to the world. Each helper includes only what it needs.
struct RcProcessorOutput;
struct RcMappingConfig;
struct RcTriggerBinding;

// CommandSource is defined in robot_state.h (Arduino/FreeRTOS-heavy); forward
// declared here rather than included, preserving this header's existing
// "include only what you need" discipline (see file header above). The fixed
// uint8_t underlying type makes the forward declaration well-formed.
enum CommandSource : uint8_t;

// Outcome of dispatching one already-classified RcActionResult
// (rc_action_dispatcher.h) to its owning queue(s)/state-setter(s). Distinct
// from RcActionResult itself, which only describes *what should happen*;
// this describes what actually happened when acting on it. This module
// stays adapter-agnostic - it knows nothing about Console Records or JSON;
// each adapter (console_module.cpp, api_actions.cpp) maps this onto its own
// wire vocabulary (docs/console-protocol.md s.3.3 for Console; #220).
//
// The last three are a Marcduino binding's line the body owns and did not run
// (Command Ownership, include/marcduino_router.h): never queue-full, which is
// what that branch used to answer for every line it swallowed (#449).
enum class RcDispatchOutcome : uint8_t {
    kQueued = 0,       // every side effect the result called for was accepted
                       // (a Marcduino line forwarded to the dome counts: it
                       // was handed on, and what the dome does is its own)
    kQueueFull,        // at least one owning queue/dispatch call refused, or
                       // a line for the dome could not be queued (link down)
    kBlockedByState,   // the result carried no dispatchable effect at all
                       // (e.g. an unconfigured sound-category range) -
                       // nothing was even attempted, distinct from queue-full
    kBlockedByEstop,   // the body owns the line and refused it: estop latched
    kOutputUndriven,   // the body owns the line, and nothing drives the
                       // Output it names this boot (#364)
    kNotExecutable,    // the body owns the line but this path cannot run it:
                       // a malformed one (an :MV with no value), or a Mood,
                       // which writes flash and is not applied from RCInputTask
};

// A routed Marcduino line's outcome, in this module's vocabulary.
inline RcDispatchOutcome rcDispatchOutcomeForMarcduinoRoute(MarcduinoRouteOutcome route) {
    switch (route) {
        case MarcduinoRouteOutcome::Applied:
        case MarcduinoRouteOutcome::Forwarded:
            return RcDispatchOutcome::kQueued;
        case MarcduinoRouteOutcome::DomeLinkDown:
        case MarcduinoRouteOutcome::DomeQueueFull:
        case MarcduinoRouteOutcome::QueueFull:
            return RcDispatchOutcome::kQueueFull;
        case MarcduinoRouteOutcome::BlockedByEstop:
            return RcDispatchOutcome::kBlockedByEstop;
        case MarcduinoRouteOutcome::OutputUndriven:
            return RcDispatchOutcome::kOutputUndriven;
        case MarcduinoRouteOutcome::NotRun:
            break;
    }
    return RcDispatchOutcome::kNotExecutable;
}

// Dispatch drive commands from RC backbone intent.
// Logs queue-full conditions; returns true if dispatch succeeded.
void rcDispatchDrive(int16_t driveSpeed, int16_t driveSteer, bool shouldStop);

// Dispatch dome commands from RC backbone intent.
// Logs queue-full conditions.
void rcDispatchDome(int domeRawFiltered, const RcMappingConfig& mapping, bool domeFiltered);

// Dispatch audio trigger from RC backbone intent.
// Logs queue-full conditions.
void rcDispatchAudioTrigger(const char* audioTrigger);

// Dispatch a single RcActionResult, attributing every queued command to src.
// Used by processTriggerAction (single action dispatch) and trigger loop dispatch.
// Note: this does NOT dispatch system modes (estop, sleep, stationary, speed preset);
// those are handled separately in rcDispatchTriggerResults for the trigger loop.
// Returns kQueued only when every side effect the result carried was accepted;
// see RcDispatchOutcome above. The live RC callers (unchanged behavior) may
// ignore the return value, as before this ticket.
RcDispatchOutcome rcDispatchSingleAction(const RcActionResult& res, CommandSource src);

// Dispatch Tier 2 trigger results: audio, servo, dome, marcduino, system modes.
// Consumes the full RcActionResult array for all triggers. Always attributes
// to SRC_SBUS internally (the trigger loop is RC-only).
// Logs queue-full and dispatch conditions per trigger.
void rcDispatchTriggerResults(const RcProcessorOutput& output,
                              const RcTriggerBinding* triggers);
