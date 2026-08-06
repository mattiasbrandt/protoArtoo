// =============================================================================
// src/web/api_validation.cpp
//
// Consolidated hardware-validation API endpoint.
//   GET /api/validation — validation-focused snapshot for hardware closure checks
//
// Written against the project-owned WebRequest seam (ADR 0021) and bound by the
// seam route table. The validation snapshot core is reused unchanged.
// =============================================================================

#include "api_validation.h"

#include <ArduinoJson.h>

#include "api_json_response.h"
#include "logging.h"
#include "validation_snapshot.h"

static const char* TAG = "ValidationAPI";

// Ceiling on the validation payload. Far below the RC one because the shape is
// fixed: no per-channel arrays, and at most VALIDATION_RC_SOURCE_CAPACITY (3)
// RC sources. Pinned by test_api_rc_routes and measured there at 888 bytes.
static constexpr size_t VALIDATION_PAYLOAD_MAX = 2048;

void handleValidationGet(WebRequest& req) {
    ValidationSnapshot snap;
    captureValidationSnapshot(&snap);

    JsonDocument doc;
    if (!populateValidationJson(doc, snap)) {
        req.send(500, "application/json",
                 "{\"ok\":false,\"error\":\"validation json build failed\"}");
        return;
    }

    webSendJsonDocument(req, doc, VALIDATION_PAYLOAD_MAX, TAG);
    PA_LOG_DEBUG(TAG, "GET /api/validation");
}
