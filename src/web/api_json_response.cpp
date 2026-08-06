// =============================================================================
// src/web/api_json_response.cpp
//
// See include/api_json_response.h for why the serialization buffer is a
// per-request allocation rather than a static one.
// =============================================================================

#include "api_json_response.h"

#include <stdlib.h>

#include "logging.h"

void webSendJsonDocument(WebRequest& req, const JsonDocument& doc, size_t maxBytes,
                         const char* tag) {
    // Measure before allocating: serializeJson() into a short buffer truncates
    // silently, and a truncated payload reaches the page as a parse error that
    // says nothing about why.
    const size_t bytes = measureJson(doc);
    if (bytes >= maxBytes) {
        PA_LOG_WARN(tag, "payload too large (%u bytes >= %u)", (unsigned)bytes,
                    (unsigned)maxBytes);
        req.send(500, "application/json", "{\"ok\":false,\"error\":\"payload too large\"}");
        return;
    }

    char* body = (char*)malloc(bytes + 1);
    if (body == nullptr) {
        PA_LOG_WARN(tag, "response buffer alloc failed (%u bytes)", (unsigned)(bytes + 1));
        req.send(500, "application/json",
                 "{\"ok\":false,\"error\":\"response buffer alloc failed\"}");
        return;
    }

    serializeJson(doc, body, bytes + 1);
    req.send(200, "application/json", body);
    free(body);
}
