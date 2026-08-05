// =============================================================================
// include/api_dome.h
//
// Dome layout relay endpoint, written against the project-owned WebRequest
// seam (ADR 0021) and bound by the seam route table. Exposed so native tests
// can drive it directly through the host-test backend.
//
// The dome's motion and command endpoints live in api_drive.cpp, with the rest
// of the drive route group.
// =============================================================================
#pragma once

#include "web_request.h"

// GET /api/dome/layout — dome layout JSON cache relay.
void handleDomeLayoutGet(WebRequest& req);
