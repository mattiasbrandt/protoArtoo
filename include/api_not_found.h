// =============================================================================
// include/api_not_found.h
//
// The handler for a request that matched no route and no static file, written
// against the project-owned WebRequest seam (ADR 0021) and bound by the seam
// route table through webRegisterNotFoundRoute(). Exposed so native tests can
// drive it directly through the host-test backend.
//
// Without it the backend answers with its own text/html body, which is the one
// reply a client can get that is not the unified JSON error shape docs/api.md
// documents for every other error.
// =============================================================================
#pragma once

#include "web_request.h"

void handleNotFound(WebRequest& req);
