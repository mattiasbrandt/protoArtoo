// =============================================================================
// src/web/api_actions.cpp
//
// GET /api/actions — JSON array of all RC-bindable robot actions.
// POST /api/actions/test — dispatch one action through RC trigger path for testing.
// =============================================================================

#include "../../include/api_actions.h"

#include <ArduinoJson.h>
#include <ESPAsyncWebServer.h>

#include <algorithm>
#include <cstdio>
#include <cstring>

#include "../../include/action_registry.h"
#include "../../include/logging.h"
#include "../../include/rc_input.h"
#include "../../include/robot_state.h"

static const char* TAG = "Actions";

namespace {

bool webControlEnabledForActionTest() {
    taskENTER_CRITICAL(&robotStateMux);
    const bool enabled = robotState.webControlEnabled;
    taskEXIT_CRITICAL(&robotStateMux);
    return enabled;
}

const ActionEntry* findActionEntry(RobotActionId id) {
    for (size_t i = 0; i < ACTION_REGISTRY_SIZE; ++i) {
        if (ACTION_REGISTRY[i].id == id) {
            return &ACTION_REGISTRY[i];
        }
    }
    return nullptr;
}

const char* actionDomainForId(RobotActionId id) {
    const ActionEntry* entry = findActionEntry(id);
    return entry != nullptr ? entry->domain : "unknown";
}

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

void registerActionsRoutes(AsyncWebServer& server) {
    server.on("/api/actions", HTTP_GET, [](AsyncWebServerRequest* req) {
        // Generate directly into AsyncTCP's outgoing chunk. The previous
        // JsonDocument + AsyncResponseStream implementation buffered the full
        // registry and exhausted fragmented heap during dashboard startup.
        auto* response = req->beginChunkedResponse(
            "application/json",
            [](uint8_t* output, size_t capacity, size_t offset) {
                return fillActionsResponse(output, capacity, offset);
            });
        if (response == nullptr) {
            req->send(500, "application/json",
                      "{\"ok\":false,\"error\":\"response alloc failed\"}");
            return;
        }
        req->send(response);
        PA_LOG_DEBUG(TAG, "GET /api/actions (%zu entries)", ACTION_REGISTRY_SIZE);
    });

    server.on("/api/actions/test", HTTP_POST, [](AsyncWebServerRequest* req) {

        String token;
        const AsyncWebParameter* tokenParam = req->getParam("token", true);
        if (tokenParam != nullptr) {
            token = tokenParam->value();
        } else if (req->hasParam("plain", true)) {
            JsonDocument bodyDoc;
            const String rawBody = req->getParam("plain", true)->value();
            if (deserializeJson(bodyDoc, rawBody.c_str())) {
                req->send(400, "application/json",
                          "{\"ok\":false,\"error\":\"invalid json body\"}");
                return;
            }
            JsonVariantConst tokenVar = bodyDoc["token"];
            if (!tokenVar.is<const char*>()) {
                req->send(400, "application/json",
                          "{\"ok\":false,\"error\":\"invalid_action_token\"}");
                return;
            }
            token = tokenVar.as<const char*>();
        }

        token.trim();
        if (token.length() == 0) {
            req->send(400, "application/json",
                      "{\"ok\":false,\"error\":\"invalid_action_token\"}");
            return;
        }

        RobotActionId target = ROBOT_ACTION_NONE;
        if (!parseRobotActionId(token.c_str(), &target)) {
            req->send(400, "application/json",
                      "{\"ok\":false,\"error\":\"invalid_action_token\"}");
            return;
        }

        switch (evaluateActionTestGuard(target, webControlEnabledForActionTest())) {
            case ACTION_TEST_SAFETY_CRITICAL_BLOCKED:
                req->send(403, "application/json",
                          "{\"ok\":false,\"error\":\"safety_critical_blocked\"}");
                return;
            case ACTION_TEST_WEB_CONTROL_DISABLED:
                req->send(423, "application/json",
                          "{\"ok\":false,\"error\":\"web_control_disabled\"}");
                return;
            case ACTION_TEST_ACTION_NOT_TESTABLE:
                req->send(422, "application/json",
                          "{\"ok\":false,\"error\":\"action_not_testable\"}");
                return;
            case ACTION_TEST_ALLOWED:
            default:
                break;
        }

        dispatchRcTriggerActionTest(target, "", true);

        JsonDocument doc;
        doc["ok"] = true;
        doc["token"] = token;
        doc["domain"] = actionDomainForId(target);
        auto* stream = req->beginResponseStream("application/json");
        if (stream == nullptr) {
            req->send(500, "application/json",
                      "{\"ok\":false,\"error\":\"response stream alloc failed\"}");
            return;
        }
        serializeJson(doc, *stream);
        req->send(stream);
    });
}
