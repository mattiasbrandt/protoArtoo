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
#include <cstring>

#include "../../include/action_registry.h"
#include "../../include/api_json_response.h"
#include "../../include/board_outputs.h"
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
    // A row about one Output is named by what the running board prints beside
    // it - "ARM3 Toggle" on the Artoo PCB, "GPIO 4 Toggle" on the FireBeetle 2 -
    // composed here from the row's `{output}` text (ADR 0033 Amendment
    // 2026-09-19). The same bytes every call, so the slice writer's offsets
    // stay true across the chunks of one response.
    char displayName[48];
    char description[96];
    boardOutputComposeText(entry.display_name, strlen(entry.display_name), entry.output,
                           displayName, sizeof(displayName));
    boardOutputComposeText(entry.description, strlen(entry.description), entry.output,
                           description, sizeof(description));

    writer.append(",\"display_name\":");
    writer.appendJsonString(displayName);
    writer.append(",\"domain\":");
    writer.appendJsonString(entry.domain);
    writer.append(",\"description\":");
    writer.appendJsonString(description);
    writer.append(",\"safety_critical\":");
    writer.append(entry.safety_critical ? "true" : "false");
    writer.append(",\"board_capability\":");
    if (entry.board_capability != nullptr) {
        writer.appendJsonString(entry.board_capability);
    } else {
        writer.append("null");
    }
    writer.append(",\"build_flag\":");
    if (entry.build_flag != nullptr) {
        writer.appendJsonString(entry.build_flag);
    } else {
        writer.append("null");
    }
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
        webSendJsonError(req, 500, "response alloc failed");
        return;
    }
    PA_LOG_DEBUG(TAG, "GET /api/actions (%u entries)", (unsigned)ACTION_REGISTRY_SIZE);
}
