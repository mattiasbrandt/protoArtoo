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

// What executeManualCommand() did with a command, which is three things and
// not two (#376).
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
// No caller can observe SaveFailed today: the only two branches that produce
// it are the "#st"/"#sm" mode keywords, and those are shadowed by the
// Marcduino prefix routing that runs before the keyword resolver -- see the
// comment on them in src/web/api_drive.cpp. Callers handle it because that
// shadowing is a defect waiting to be decided, not a design: the day it is
// repaired, the answer is already right rather than discarded.
enum class ManualCommandResult : uint8_t {
    Unsupported,  // not a command this dispatcher owns -- nothing happened
    Applied,      // executed; either nothing to persist, or the save landed
    SaveFailed,   // executed, but the config save did not reach flash
};

// Execute one manual command: a Marcduino line routed by its prefix, or one of
// the keyword commands (estop, reboot, ...). Answers Unsupported for an
// unrecognized keyword; Marcduino lines are always accepted, since the routing
// table decides whether the body handles or discards them.
//
// Takes a plain C string rather than an Arduino String so the one cross-file
// caller (POST /api/manual-command, api_system.cpp) can hand over a borrowed
// seam parameter without a copy.
ManualCommandResult executeManualCommand(const char* raw);
