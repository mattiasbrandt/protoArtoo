// =============================================================================
// include/api_drive.h
//
// Drive, mode, web-control and dome-motion API endpoints, written against the
// project-owned WebRequest seam (ADR 0021) and bound by the seam route table.
// Exposed so native tests can drive them directly through the host-test
// backend.
//
// The dome layout relay lives in api_dome.cpp; the dome endpoints here are the
// motion and command paths, which share this group's safety gating.
// =============================================================================
#pragma once

#include <stddef.h>
#include <stdint.h>

#include "web_request.h"

#include "drive_speed_preset.h"

// Format JSON response for speed preset endpoint.
// Output: {"ok":true,"preset":"slow|normal|turbo","speedLimitMax":<0..600>}
// Returns false if the payload does not fit in buf or preset is invalid.
bool formatSpeedPresetResponseJson(char* buf, size_t bufSize, SpeedPresetId preset,
                                   int16_t speedLimitMax);

void handleModePost(WebRequest& req);
void handleDrivePost(WebRequest& req);
void handleSpeedPresetPost(WebRequest& req);
void handleWebControlEnablePost(WebRequest& req);
void handleWebControlDisablePost(WebRequest& req);
void handleDomeCmdPost(WebRequest& req);
void handleDomeSpeedPost(WebRequest& req);

// What executeManualCommand() did with a command, which is four things and
// not two (#376, #379).
//
// It was a bool -- recognized or not -- and the branches that persist the
// commanded mode had no way to say that the persisting failed, so a mode
// change whose NVS write never landed was indistinguishable from one that
// stored and every caller answered success for both. Unsupported and
// SaveFailed are both "do not answer ok", but they are not the same answer:
// one is the operator's command being wrong, the other is the flash being
// full.
//
// SaveFailed means the command DID take effect on the droid; only the store
// missed. Reverting instead is the other option in this tree, and
// saveCommandedMode() (src/web/api_drive.cpp) says why the mode paths do not
// take it.
//
// ShadowedModeKeyword is the answer #379 settled on for "#st"/"#sm". Both
// resolve to a mode branch that the Marcduino prefix routing has claimed since
// the day after they landed, so neither ever ran and the caller was told
// {"ok":true} for a mode change that never happened. They are refused up front
// instead of being moved in front of the routing: POST /api/mode already does
// this job, so a second door into the same room is not worth the Marcduino
// namespace it costs. Every caller must name the working route when it answers
// this one -- a refusal that does not say what to do instead is the failure
// #348 D1 closed.
//
// No caller can observe SaveFailed today, and that is the same shadowing: the
// only two branches that produce it are those mode keywords, which the guard
// above them now refuses. Callers keep handling it because the two arms stay
// (they are the evidence of what the refusal refuses) and because the answer
// is then already right rather than discarded -- see the comment on them in
// src/web/api_drive.cpp.
enum class ManualCommandResult : uint8_t {
    Unsupported,          // not a command this dispatcher owns -- nothing happened
    Applied,              // executed; either nothing to persist, or the save landed
    SaveFailed,           // executed, but the config save did not reach flash
    ShadowedModeKeyword,  // "#st"/"#sm": refused, because the Marcduino '#'
                          // routing claims the line and no mode can change
};

// Execute one manual command: a Marcduino line routed by its prefix, or one of
// the keyword commands (estop, reboot, ...). Answers Unsupported for an
// unrecognized keyword; Marcduino lines are always accepted, since the routing
// table decides whether the body handles or discards them. The two exceptions
// are "#st"/"#sm", which look like keywords but are Marcduino lines the body
// discards -- ShadowedModeKeyword, nothing executed.
//
// Takes a plain C string rather than an Arduino String so the one cross-file
// caller (POST /api/manual-command, api_system.cpp) can hand over a borrowed
// seam parameter without a copy.
ManualCommandResult executeManualCommand(const char* raw);
