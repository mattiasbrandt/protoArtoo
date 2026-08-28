// =============================================================================
// include/api_console.h
//
// POST /api/console - browser adapter for the Controller Console (ADR 0034).
// Receives command lines from the Live Logs command box, executes them through
// the Console module, and returns Console Records as JSON.
// =============================================================================
#pragma once

#include "web_request.h"

void handleConsolePost(WebRequest& req);
