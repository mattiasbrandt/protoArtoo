// =============================================================================
// include/api_estop.h
//
// Estop API endpoints, written against the project-owned WebRequest seam
// (ADR 0021) and bound by the seam route table. Exposed so native tests can
// drive them directly through the host-test backend.
//
// These two are the safety path the admission policy exempts: webPathIsEstop()
// matches both, and webRequestAdmissionDecide() admits them without counting
// them against any cap (include/web_admission.h).
// =============================================================================
#pragma once

#include "web_request.h"

void handleEstopPost(WebRequest& req);
void handleEstopClearPost(WebRequest& req);
