// =============================================================================
// src/web/api_seq.cpp
//
// Learned Sequence REST API (issue #2 slice 3d, ADR 0006). See header for the
// route list. The store (seq_store) owns LittleFS + Protocol Check; this layer
// is transport only. Code/API identifiers stay neutral; operator-facing theming
// (Factory/Learned/Retrained/...) lives in the editor and docs.
// =============================================================================

#include "api_seq.h"

#include <ArduinoJson.h>
#include <ESPAsyncWebServer.h>

#include "api_helpers.h"
#include "logging.h"
#include "robot_state.h"          // CommandSource
#include "seq_json.h"
#include "seq_store.h"
#include "seq_store_index.h"
#include "sequence_dispatcher.h"

static const char* TAG = "APISEQ";

namespace {

// Send a Protocol Check / store failure as a field-level 400.
void sendCheckError(AsyncWebServerRequest* req, const ProtocolCheckResult& r) {
    JsonDocument doc;
    doc["ok"] = false;
    doc["field"] = r.field;
    doc["error"] = r.message;
    auto* stream = req->beginResponseStream("application/json");
    if (stream == nullptr) {
        req->send(400, "application/json", "{\"ok\":false,\"error\":\"alloc\"}");
        return;
    }
    stream->setCode(400);
    serializeJson(doc, *stream);
    req->send(stream);  // 400 with field-level body
}

// GET /api/seq/list
void handleList(AsyncWebServerRequest* req) {
    JsonDocument doc;
    JsonArray arr = doc.to<JsonArray>();
    for (uint8_t i = 0; i < seqStoreIndexCount(); ++i) {
        const SeqIndexEntry* e = seqStoreIndexAt(i);
        if (e == nullptr) continue;
        JsonObject o = arr.add<JsonObject>();
        o["name"] = e->name;
        o["toggleGroup"] = seqToggleGroupToString(e->toggleGroup);
        o["suppressMs"] = e->suppressMs;
        o["source"] = e->source;
        o["modified"] = e->modified;
        // A Learned Sequence that shadows a Factory one is "Retrained".
        o["retrained"] = (sequenceCatalogFind(e->name) != nullptr);
    }
    auto* stream = req->beginResponseStream("application/json");
    if (stream == nullptr) {
        req->send(500, "application/json", "{\"ok\":false,\"error\":\"alloc\"}");
        return;
    }
    serializeJson(doc, *stream);
    req->send(stream);
}

// GET /api/seq/builtins — factory catalog serialized to JSON v1.
void handleBuiltins(AsyncWebServerRequest* req) {
    JsonDocument doc;
    JsonArray arr = doc.to<JsonArray>();
    for (uint8_t i = 0; i < sequenceCatalogCount(); ++i) {
        const SequenceEntry* e = sequenceCatalogAt(i);
        if (e == nullptr) continue;
        seqJsonSerializeObject(arr.add<JsonObject>(), *e, "factory");
    }
    auto* stream = req->beginResponseStream("application/json");
    if (stream == nullptr) {
        req->send(500, "application/json", "{\"ok\":false,\"error\":\"alloc\"}");
        return;
    }
    serializeJson(doc, *stream);
    req->send(stream);
}

// GET /api/seq?name=  — raw stored JSON of one Learned Sequence.
void handleGetOne(AsyncWebServerRequest* req) {
    const AsyncWebParameter* p = req->getParam("name");
    if (p == nullptr || p->value().length() == 0) {
        req->send(400, "application/json",
                  "{\"ok\":false,\"error\":\"missing name parameter\"}");
        return;
    }
    auto* stream = req->beginResponseStream("application/json");
    if (stream == nullptr) {
        req->send(500, "application/json", "{\"ok\":false,\"error\":\"alloc\"}");
        return;
    }
    if (!seqStoreStreamFile(p->value().c_str(), *stream)) {
        // The stream may already hold partial output; safest is a fresh 404.
        req->send(404, "application/json",
                  "{\"ok\":false,\"error\":\"not found\"}");
        return;
    }
    req->send(stream);
}

// POST /api/seq  — body: JSON v1; validate + persist.
void handleSave(AsyncWebServerRequest* req) {
    if (!req->hasParam("plain", true)) {
        req->send(400, "application/json",
                  "{\"ok\":false,\"error\":\"missing JSON body\"}");
        return;
    }
    const String& body = req->getParam("plain", true)->value();
    ProtocolCheckResult r = seqStoreSave(body.c_str(), body.length());
    if (!r.ok) {
        sendCheckError(req, r);
        return;
    }
    PA_LOG_INFO(TAG, "[WEB] saved Learned Sequence (%u bytes)",
                (unsigned)body.length());
    req->send(200, "application/json", "{\"ok\":true}");
}

// DELETE /api/seq?name=  — Memory Wipe.
void handleDelete(AsyncWebServerRequest* req) {
    const AsyncWebParameter* p = req->getParam("name");
    if (p == nullptr || p->value().length() == 0) {
        req->send(400, "application/json",
                  "{\"ok\":false,\"error\":\"missing name parameter\"}");
        return;
    }
    if (!seqStoreDelete(p->value().c_str())) {
        req->send(404, "application/json",
                  "{\"ok\":false,\"error\":\"not found\"}");
        return;
    }
    PA_LOG_INFO(TAG, "[WEB] Memory Wipe %s", p->value().c_str());
    req->send(200, "application/json", "{\"ok\":true}");
}

// POST /api/seq/test  — run a sequence by name (same ungated path as dome/cmd).
void handleTest(AsyncWebServerRequest* req) {
    String name;
    const AsyncWebParameter* nameParam = req->getParam("name", true);
    if (nameParam != nullptr) {
        name = nameParam->value();
    } else if (req->hasParam("plain", true)) {
        JsonDocument doc;
        if (deserializeJson(doc, req->getParam("plain", true)->value().c_str())) {
            req->send(400, "application/json",
                      "{\"ok\":false,\"error\":\"invalid json body\"}");
            return;
        }
        name = (const char*)(doc["name"] | "");
    }
    name.trim();
    if (name.length() == 0 || strncmp(name.c_str(), "DM:", 3) != 0) {
        req->send(400, "application/json",
                  "{\"ok\":false,\"error\":\"missing or invalid DM:* name\"}");
        return;
    }
    if (!sequenceStart(name.c_str(), SRC_WEB_API)) {
        req->send(503, "application/json",
                  "{\"ok\":false,\"error\":\"sequence queue full\"}");
        return;
    }
    PA_LOG_INFO(TAG, "[WEB] test %s", name.c_str());
    req->send(200, "application/json", "{\"ok\":true}");
}

}  // namespace

void registerSeqRoutes(AsyncWebServer& server) {
    server.on("/api/seq/list", HTTP_GET, handleList);
    server.on("/api/seq/builtins", HTTP_GET, handleBuiltins);
    server.on("/api/seq/test", HTTP_POST, handleTest);
    server.on("/api/seq", HTTP_GET, handleGetOne);
    server.on("/api/seq", HTTP_POST, handleSave);
    server.on("/api/seq", HTTP_DELETE, handleDelete);
}
