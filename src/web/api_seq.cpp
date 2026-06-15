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
#include "config_store.h"         // ConfigSnapshot, configCacheRead
#include "logging.h"
#include "rc_action_types.h"      // RcTriggerBinding, DOME_ACTION_SEQ
#include "rc_binding_types.h"     // rcBindingSourceToString
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

// GET /api/seq/builtins         — lightweight factory catalog (metadata only).
// GET /api/seq/builtins?name=X   — full JSON v1 of one factory sequence.
//
// The list form carries no step data, so the whole-catalog response stays a few
// hundred bytes and cannot exhaust the fragmented heap mid-send. Serializing all
// factory sequences with their steps into one buffered response was large enough
// to OOM AsyncTCP during delivery, and ESP32's exceptions-disabled libstdc++
// turns the failed allocation into terminate()/abort() (panic reboot). The
// editor fetches full steps per-name only when the operator clones a sequence.
void handleBuiltins(AsyncWebServerRequest* req) {
    const AsyncWebParameter* p = req->getParam("name");
    if (p != nullptr && p->value().length() > 0) {
        // Full single factory sequence (clone source). Always the Factory
        // definition, even if a Retrained Learned Sequence shadows the name.
        const SequenceEntry* e = sequenceCatalogFind(p->value().c_str());
        if (e == nullptr) {
            req->send(404, "application/json",
                      "{\"ok\":false,\"error\":\"not found\"}");
            return;
        }
        auto* stream = req->beginResponseStream("application/json");
        if (stream == nullptr) {
            req->send(500, "application/json",
                      "{\"ok\":false,\"error\":\"alloc\"}");
            return;
        }
        JsonDocument doc;
        seqJsonSerializeObject(doc.to<JsonObject>(), *e, "factory");
        serializeJson(doc, *stream);
        req->send(stream);
        return;
    }

    // Lightweight list: one small row per factory sequence (no step data).
    JsonDocument doc;
    JsonArray arr = doc.to<JsonArray>();
    for (uint8_t i = 0; i < sequenceCatalogCount(); ++i) {
        const SequenceEntry* e = sequenceCatalogAt(i);
        if (e == nullptr) continue;
        JsonObject o = arr.add<JsonObject>();
        o["name"] = e->name;
        o["toggleGroup"] = seqToggleGroupToString(e->toggleGroup);
        o["suppressMs"] = e->suppressMs;
        o["stepCount"] = e->stepCount;
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
    // Look up before allocating the stream so a miss is a clean 404.
    if (seqStoreIndexFind(p->value().c_str()) == nullptr) {
        req->send(404, "application/json",
                  "{\"ok\":false,\"error\":\"not found\"}");
        return;
    }
    auto* stream = req->beginResponseStream("application/json");
    if (stream == nullptr) {
        req->send(500, "application/json", "{\"ok\":false,\"error\":\"alloc\"}");
        return;
    }
    if (!seqStoreStreamFile(p->value().c_str(), *stream)) {
        // The handler owns the stream until send(): beginResponseStream()
        // only allocates it, and nothing reaches the client before send().
        // Discard it and report the (rare) read failure.
        delete stream;
        req->send(500, "application/json",
                  "{\"ok\":false,\"error\":\"read failed\"}");
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
    const char* name = p->value().c_str();
    if (seqStoreIndexFind(name) == nullptr) {
        req->send(404, "application/json",
                  "{\"ok\":false,\"error\":\"not found\"}");
        return;
    }
    if (!seqStoreDelete(name)) {
        req->send(500, "application/json",
                  "{\"ok\":false,\"error\":\"delete failed\"}");
        return;
    }
    PA_LOG_INFO(TAG, "[WEB] Memory Wipe %s", name);

    // Report RC trigger bindings the wipe leaves dangling: unless a Factory
    // Sequence shadows the name, those triggers are silent no-ops from now on
    // (the editor surfaces this; the log keeps it visible regardless).
    JsonDocument doc;
    doc["ok"] = true;
    if (sequenceCatalogFind(name) == nullptr) {
        ConfigSnapshot snap;
        configCacheRead(&snap);
        const RcTriggerBinding* slots[] = {
            &snap.system.rc_arm1,  &snap.system.rc_arm2,  &snap.system.rc_aux1,
            &snap.system.rc_aux2,  &snap.system.rc_aux3,  &snap.system.rc_sound,
            &snap.system.rc_opmode, &snap.system.rc_free0, &snap.system.rc_free1,
            &snap.system.rc_free2, &snap.system.rc_free3,
        };
        JsonArray dangling;
        for (size_t i = 0; i < sizeof(slots) / sizeof(slots[0]); ++i) {
            const RcTriggerBinding& b = *slots[i];
            if (b.target != DOME_ACTION_SEQ ||
                strcmp(b.marcduinoPayload, name) != 0) {
                continue;
            }
            if (dangling.isNull()) {
                dangling = doc["danglingBindings"].to<JsonArray>();
            }
            JsonObject o = dangling.add<JsonObject>();
            o["source"] = rcBindingSourceToString(b.source);
            o["channel"] = b.channel;
            PA_LOG_WARN(TAG, "Memory Wipe %s leaves RC binding %s ch%u dangling",
                        name, rcBindingSourceToString(b.source),
                        (unsigned)b.channel);
        }
    }
    auto* stream = req->beginResponseStream("application/json");
    if (stream == nullptr) {
        req->send(200, "application/json", "{\"ok\":true}");
        return;
    }
    serializeJson(doc, *stream);
    req->send(stream);
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
