// =============================================================================
// include/api_identity.h
//
// Droid identity API endpoints, written against the project-owned WebRequest
// seam (ADR 0021). The handlers are exposed so native tests can drive them
// directly through the host-test backend, and so the seam route table in
// web_seam_routes.cpp can bind them.
// =============================================================================
#pragma once

#include <stddef.h>
#include <stdbool.h>

#include "web_request.h"

// Format JSON response for identity endpoints.
// Output: {"droidName":"...","mdnsUseName":<bool>}
// Returns false if the payload does not fit in buf.
bool formatIdentityJson(char* buf, size_t bufSize, const char* droidName, bool mdnsUseName);

void handleIdentityGet(WebRequest& req);
void handleIdentityPost(WebRequest& req);
