// =============================================================================
// src/web/api_actions_json.cpp
//
// GET /api/actions -- the RC-bindable action registry serialized as a JSON
// array, and the handler that serves it through the WebRequest seam
// (ADR 0021).
//
// Kept apart from api_actions.cpp so the ported route lives in a translation
// unit with no web-server, FreeRTOS or RC-dispatch dependency: the host tests
// build and drive it directly, and the cutover has nothing to untangle here.
// api_actions.cpp keeps the still-async POST /api/actions/test route.
//
// The body is written slice by slice against a byte offset rather than
// assembled whole: the registry serializes to ~9 KB, and the JsonDocument
// implementation this replaced exhausted fragmented heap during dashboard
// startup. WebRequest::sendChunked() drives it, so no backend ever holds more
// than one chunk of it. The slice writer itself is shared
// (include/web_json_slice_writer.h) -- the audio catalog and track payloads are
// produced the same way.
// =============================================================================

#include "../../include/api_actions.h"

#include <cstdio>

#include "../../include/action_registry.h"
#include "../../include/logging.h"
#include "../../include/web_json_slice_writer.h"

static const char* TAG = "Actions";

namespace {

void appendActionJson(JsonSliceWriter& writer, const ActionEntry& entry) {
    char id[12];
    std::snprintf(id, sizeof(id), "%d", static_cast<int>(entry.id));

    writer.append("{\"id\":");
    writer.append(id);
    writer.append(",\"name\":");
    writer.appendJsonString(entry.name);
    writer.append(",\"display_name\":");
    writer.appendJsonString(entry.display_name);
    writer.append(",\"domain\":");
    writer.appendJsonString(entry.domain);
    writer.append(",\"description\":");
    writer.appendJsonString(entry.description);
    writer.append(",\"safety_critical\":");
    writer.append(entry.safety_critical ? "true" : "false");
    writer.append(",\"testable\":");
    writer.append(robotActionIsWebTestable(entry.id) ? "true" : "false");
    writer.append(",\"one_shot\":");
    writer.append(robotActionIsOneShotButton(entry.id) ? "true" : "false");
    writer.append(",\"token\":");
    writer.appendJsonString(robotActionIdToString(entry.id));
    writer.append('}');
}

size_t fillActionsResponse(uint8_t* output, size_t capacity, size_t offset) {
    JsonSliceWriter writer(output, capacity, offset);
    writer.append('[');
    for (size_t i = 0; i < ACTION_REGISTRY_SIZE; ++i) {
        if (i > 0) {
            writer.append(',');
        }
        appendActionJson(writer, ACTION_REGISTRY[i]);
    }
    writer.append(']');
    return writer.written();
}

}  // namespace

void handleActionsGet(WebRequest& req) {
    if (!req.sendChunked("application/json", fillActionsResponse)) {
        req.send(500, "application/json",
                 "{\"ok\":false,\"error\":\"response alloc failed\"}");
        return;
    }
    PA_LOG_DEBUG(TAG, "GET /api/actions (%zu entries)", ACTION_REGISTRY_SIZE);
}
