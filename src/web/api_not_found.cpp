// =============================================================================
// src/web/api_not_found.cpp
//
// Unknown-route handler
//   any method, any unmatched path - 404 in the unified JSON error shape
//
// Written against the project-owned WebRequest seam (ADR 0021) and bound by the
// seam route table. See include/api_not_found.h.
// =============================================================================

#include "../../include/api_not_found.h"

#include "../../include/api_json_response.h"

void handleNotFound(WebRequest& req) {
    // Every unmatched path, not only /api/*: a mistyped page URL and a mistyped
    // endpoint are the same event to the server, and one rule needs no path
    // list kept in sync with the route table. Registered routes and existing
    // static files are matched before this runs, so nothing that exists on the
    // device is affected -- only requests that had no answer either way.
    webSendJsonError(req, 404, "not found");
}
