// =============================================================================
// include/api_logs.h
//
// GET /api/logs - recent log buffer, written against the project-owned
// WebRequest seam (ADR 0021) and bound by the seam route table. Split out of
// api_status.cpp when it was ported: the log ring is the one part of that file
// with no dependency on WiFi, dome or heap telemetry, so on its own it is a
// translation unit the host tests can build and drive directly.
// =============================================================================
#pragma once

#include "web_request.h"

void handleLogsGet(WebRequest& req);
