// =============================================================================
// include/api_system.h
//
// System control API endpoints, written against the project-owned WebRequest
// seam (ADR 0021) and bound by the seam route table. Exposed so native tests
// can drive them directly through the host-test backend.
// =============================================================================
#pragma once

#include <stddef.h>
#include <stdbool.h>

#include "web_request.h"

// Format JSON response for sleep/wake control endpoints.
// Output: {"ok":true,"sleepMode":<bool>,"changed":<bool>}
// Returns false if the payload does not fit in buf.
bool formatSleepControlJson(char* buf, size_t bufSize, bool sleepMode, bool changed);

void handleSleepPost(WebRequest& req);
void handleWakePost(WebRequest& req);
void handleManualCommandPost(WebRequest& req);
void handleRebootPost(WebRequest& req);
void handleCoredumpStatusGet(WebRequest& req);
void handleCoredumpGet(WebRequest& req);
void handleCoredumpErasePost(WebRequest& req);
