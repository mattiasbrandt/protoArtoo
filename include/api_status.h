// =============================================================================
// include/api_status.h
//
// Status and telemetry API endpoints, all ported to the project-owned
// WebRequest seam (ADR 0021) and bound by the seam route table.
// =============================================================================
#pragma once

#include "web_request.h"

void handleWifiGet(WebRequest& req);

// GET /api/status. Ported ahead of the rest of its route group because the
// admission counters it carries are what the load harness and the migration
// scorecard read; without it the guard's evidence is unobservable.
void handleStatusGet(WebRequest& req);

void handleHealthGet(WebRequest& req);
void handleSerialGet(WebRequest& req);
