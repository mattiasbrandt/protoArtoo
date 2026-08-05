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

#include "web_request.h"

void handleModePost(WebRequest& req);
void handleDrivePost(WebRequest& req);
void handleSpeedPresetPost(WebRequest& req);
void handleWebControlEnablePost(WebRequest& req);
void handleWebControlDisablePost(WebRequest& req);
void handleDomeCmdPost(WebRequest& req);
void handleDomeSpeedPost(WebRequest& req);

// Execute one manual command: a Marcduino line routed by its prefix, or one of
// the keyword commands (estop, reboot, ...). Returns false for an unrecognized
// keyword; Marcduino lines are always accepted, since the routing table decides
// whether the body handles or discards them.
//
// Takes a plain C string rather than an Arduino String so the one cross-file
// caller (POST /api/manual-command, api_system.cpp) can hand over a borrowed
// seam parameter without a copy.
bool executeManualCommand(const char* raw);
