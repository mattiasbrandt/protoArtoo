// =============================================================================
// include/api_identity.h
//
// Droid identity API endpoints, written against the project-owned WebRequest
// seam (ADR 0021). The handlers are exposed so native tests can drive them
// directly through the host-test backend, and so the seam route table in
// web_seam_routes.cpp can bind them.
// =============================================================================
#pragma once

#include "web_request.h"

void handleIdentityGet(WebRequest& req);
void handleIdentityPost(WebRequest& req);
