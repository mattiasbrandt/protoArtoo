// =============================================================================
// include/api_rc.h
//
// RC diagnostics API handlers, written against the project-owned WebRequest
// seam (ADR 0021) and bound by the seam route table.
// =============================================================================
#pragma once

#include "web_request.h"

// GET /api/rc — the RC diagnostics snapshot the RC page renders.
// Payload shape is governed by tasks/rc_diagnostics_contract.md.
void handleRcGet(WebRequest& req);

// POST /api/rc/debug — {"enabled":bool}, toggles verbose RC logging.
void handleRcDebugPost(WebRequest& req);
