// =============================================================================
// src/web/api_actions.cpp
//
// GET /api/actions — JSON array of all RC-bindable robot actions.
//
// Returns one object per ActionEntry in ACTION_REGISTRY:
//   { "id": <int>, "name": "<canonical>", "display_name": "<label>",
//     "domain": "<domain>", "description": "<text>",
//     "safety_critical": <bool> }
//
// Clients (RC mapping UI) use "id" (numeric RobotActionId) when saving
// a binding; "display_name" is shown in the dropdown.
// =============================================================================

#include "../../include/api_actions.h"

#include <ArduinoJson.h>
#include <ESPAsyncWebServer.h>

#include "../../include/action_registry.h"
#include "../../include/logging.h"

static const char* TAG = "Actions";

void registerActionsRoutes(AsyncWebServer& server) {
    server.on("/api/actions", HTTP_GET, [](AsyncWebServerRequest* req) {
        // Each entry serializes to ~120 bytes; 14 entries ~1700 bytes.
        // JsonDocument with default allocator handles this comfortably.
        JsonDocument doc;
        JsonArray arr = doc.to<JsonArray>();

        for (size_t i = 0; i < ACTION_REGISTRY_SIZE; ++i) {
            const ActionEntry& e = ACTION_REGISTRY[i];
            JsonObject obj = arr.add<JsonObject>();
            obj["id"]              = static_cast<int>(e.id);
            obj["name"]            = e.name;
            obj["display_name"]    = e.display_name;
            obj["domain"]          = e.domain;
            obj["description"]     = e.description;
            obj["safety_critical"] = e.safety_critical;
            obj["token"]          = robotActionIdToString(e.id);
        }

        auto* stream = req->beginResponseStream("application/json");
        if (stream == nullptr) {
            req->send(500, "application/json",
                      "{\"ok\":false,\"error\":\"response stream alloc failed\"}");
            return;
        }
        serializeJson(doc, *stream);
        req->send(stream);
        PA_LOG_DEBUG(TAG, "GET /api/actions (%zu entries)", ACTION_REGISTRY_SIZE);
    });
}
