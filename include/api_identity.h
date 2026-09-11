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

#include "component_registry.h"  // ComponentCategoryId
#include "config_store.h"         // ConfigSnapshot
#include "web_request.h"

// Format JSON response for identity endpoints.
// Output includes the operator identity plus the complete compile-time Feature
// Availability manifest from board_capabilities.inc and build_flags.inc, and
// the Board Lanes from board_lanes.inc -- where the running firmware routes
// each signal, so no operator surface keeps its own copy of one board's wiring
// (CONTEXT.md "Board Lane").
// Returns false if the payload does not fit in buf.
bool formatIdentityJson(char* buf, size_t bufSize, const char* droidName, bool mdnsUseName);

// One fixed upper bound shared by the handler and its native contract test.
// Manifest additions that outgrow it fail serialization instead of allocating.
constexpr size_t IDENTITY_JSON_MAX_BYTES = 512;

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

// -----------------------------------------------------------------------------
// The Component Registry lineup, on its own route.
//
// It is identity's payload -- firmware is the runtime source of the lineup and
// the `data/` copy is only a fallback (ADR 0042 as amended 2026-09-09) -- but
// not identity's response: the manifest above is bounded at
// IDENTITY_JSON_MAX_BYTES with roughly 50 B spare, and the lineup runs to
// around 3 KB. It streams by offset instead, so no backend holds it whole.
// -----------------------------------------------------------------------------

// Pin each family's active Component Member for the send that follows.
// sendChunked() re-walks the body once per chunk, so the value has to be
// snapshotted before the send rather than read live inside it
// (include/web_json_slice_writer.h). Call once per category that has one,
// immediately before sendChunked(); a category never pinned reports
// active_member null.
void componentRegistryJsonPinActiveMember(ComponentCategoryId category, uint8_t memberValue);

// WebResponseBodyFiller for the lineup. Reads the registry tables and whatever
// componentRegistryJsonPinActiveMember() last pinned.
size_t fillComponentRegistryJson(uint8_t* output, size_t capacity, size_t offset);

void handleComponentsGet(WebRequest& req);
