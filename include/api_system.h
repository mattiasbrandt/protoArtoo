// =============================================================================
// include/api_system.h
//
// System control API endpoints, written against the project-owned WebRequest
// seam (ADR 0021) and bound by the seam route table. Exposed so native tests
// can drive them directly through the host-test backend.
// =============================================================================
#pragma once

#include "web_request.h"

void handleSleepPost(WebRequest& req);
void handleWakePost(WebRequest& req);
void handleManualCommandPost(WebRequest& req);
void handleRebootPost(WebRequest& req);
void handleCoredumpStatusGet(WebRequest& req);
void handleCoredumpGet(WebRequest& req);
void handleCoredumpErasePost(WebRequest& req);
