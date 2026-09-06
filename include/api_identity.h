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

#include "config_store.h"  // ConfigSnapshot
#include "web_request.h"

// Format JSON response for identity endpoints.
// Output includes the operator identity plus the complete compile-time Feature
// Availability manifest from board_capabilities.inc and build_flags.inc.
// Returns false if the payload does not fit in buf.
bool formatIdentityJson(char* buf, size_t bufSize, const char* droidName, bool mdnsUseName);

// One fixed upper bound shared by the handler and its native contract test.
// Manifest additions that outgrow it fail serialization instead of allocating.
constexpr size_t IDENTITY_JSON_MAX_BYTES = 384;

// Commit Step (ADR 0036 criterion 1): publishes `working` (already carrying
// the caller's validated droid_name/mdns_use_name - normalizeDroidName() and
// parseBoolValue(), include/api_helpers.h, are the shared pure validators
// both callers run first) to the runtime config cache and persists it to
// NVS, the same sequence handleIdentityPost()'s inline body used to run.
// `working` is read back by the caller afterward (e.g. to render the REST
// response), matching how the audio Commit Steps (include/api_audio.h) take
// their ConfigSnapshot by pointer.
struct IdentitySetCommitOutcome {
    bool persisted = false;  // false -> caller reports "failed to persist identity" (500)
};
IdentitySetCommitOutcome identitySetCommitApplied(ConfigSnapshot* working);

void handleIdentityGet(WebRequest& req);
void handleIdentityPost(WebRequest& req);
