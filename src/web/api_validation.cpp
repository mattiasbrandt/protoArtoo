// =============================================================================
// src/web/api_validation.cpp
//
// Consolidated hardware-validation API endpoint.
//   GET /api/validation — validation-focused snapshot for Phase 4 closure checks
// =============================================================================

#include "api_validation.h"

#include <ArduinoJson.h>

#include "logging.h"
#include "validation_snapshot.h"

static const char* TAG = "ValidationAPI";

void registerValidationRoutes(AsyncWebServer& server) {
    server.on("/api/validation", HTTP_GET, [](AsyncWebServerRequest* req) {
        ValidationSnapshot snap;
        captureValidationSnapshot(&snap);

        JsonDocument doc;
        if (!populateValidationJson(doc, snap)) {
            req->send(500, "application/json",
                      "{\"ok\":false,\"error\":\"validation json build failed\"}");
            return;
        }

        auto* stream = req->beginResponseStream("application/json");
        if (stream == nullptr) {
            req->send(500, "application/json",
                      "{\"ok\":false,\"error\":\"response stream alloc failed\"}");
            return;
        }

        serializeJson(doc, *stream);
        req->send(stream);
        PA_LOG_DEBUG(TAG, "GET /api/validation");
    });
}
