// =============================================================================
// include/api_validation.h
//
// Hardware-validation API handler, written against the project-owned
// WebRequest seam (ADR 0021) and bound by the seam route table.
// =============================================================================
#pragma once

#include "web_request.h"

// GET /api/validation - validation-focused snapshot for hardware closure checks.
void handleValidationGet(WebRequest& req);
