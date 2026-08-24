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
// Output includes the operator identity plus the complete compile-time Feature
// Availability manifest from board_capabilities.inc and build_flags.inc.
// Returns false if the payload does not fit in buf.
bool formatIdentityJson(char* buf, size_t bufSize, const char* droidName, bool mdnsUseName);

// One fixed upper bound shared by the handler and its native contract test.
// Manifest additions that outgrow it fail serialization instead of allocating.
constexpr size_t IDENTITY_JSON_MAX_BYTES = 384;

void handleIdentityGet(WebRequest& req);
void handleIdentityPost(WebRequest& req);
