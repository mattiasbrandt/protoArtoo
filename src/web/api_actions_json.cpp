// =============================================================================
// src/web/api_actions_json.cpp
//
// GET /api/actions -- the RC-bindable action registry serialized as a JSON
// array, and the handler that serves it through the WebRequest seam
// (ADR 0021).
//
// Kept apart from api_actions.cpp so the ported route lives in a translation
// unit with no web-server, FreeRTOS or RC-dispatch dependency: the host tests
// build and drive it directly, and the #91 cutover has nothing to untangle
// here. api_actions.cpp keeps the still-async POST /api/actions/test route.
//
// The body is written slice by slice against a byte offset rather than
// assembled whole: the registry serializes to ~9 KB, and the JsonDocument
// implementation this replaced exhausted fragmented heap during dashboard
// startup. WebRequest::sendChunked() drives it, so no backend ever holds more
// than one chunk of it.
// =============================================================================

#include "../../include/api_actions.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

#include "../../include/action_registry.h"
#include "../../include/logging.h"

static const char* TAG = "Actions";

namespace {

class JsonSliceWriter {
public:
    JsonSliceWriter(uint8_t* output, size_t capacity, size_t offset)
        : output_(output), capacity_(capacity), offset_(offset) {}

    void append(const char* text) {
        append(text, std::strlen(text));
    }

    void append(const char* text, size_t length) {
        if (written_ >= capacity_) {
            logicalOffset_ += length;
            return;
        }

        const size_t segmentEnd = logicalOffset_ + length;
        if (offset_ < segmentEnd) {
            const size_t start = offset_ > logicalOffset_ ? offset_ - logicalOffset_ : 0;
            const size_t count = std::min(length - start, capacity_ - written_);
            std::memcpy(output_ + written_, text + start, count);
            written_ += count;
        }
        logicalOffset_ = segmentEnd;
    }

    void append(char value) {
        append(&value, 1);
    }

    void appendJsonString(const char* value) {
        append('"');
        for (const unsigned char* p = reinterpret_cast<const unsigned char*>(value); *p != '\0'; ++p) {
            switch (*p) {
                case '"': append("\\\""); break;
                case '\\': append("\\\\"); break;
                case '\b': append("\\b"); break;
                case '\f': append("\\f"); break;
                case '\n': append("\\n"); break;
                case '\r': append("\\r"); break;
                case '\t': append("\\t"); break;
                default:
                    if (*p < 0x20) {
                        char escaped[7];
                        std::snprintf(escaped, sizeof(escaped), "\\u%04x", *p);
                        append(escaped);
                    } else {
                        append(reinterpret_cast<const char*>(p), 1);
                    }
                    break;
            }
        }
        append('"');
    }

    size_t written() const {
        return written_;
    }

private:
    uint8_t* output_;
    size_t capacity_;
    size_t offset_;
    size_t logicalOffset_ = 0;
    size_t written_ = 0;
};

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
