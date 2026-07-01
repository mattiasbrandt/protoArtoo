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
#include <stdlib.h>
#include <string.h>

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
#include "sequence_engine.h"        // seqEngineRingPanelCount/Number
#include "sequence_run_evidence.h"  // GET /api/seq/last-run

static const char* TAG = "APISEQ";
static constexpr size_t SEQ_TEST_BODY_MAX = 512;

namespace {

typedef void (*SeqBodyHandler)(AsyncWebServerRequest* req, const char* body, size_t len);

void captureJsonBody(AsyncWebServerRequest* req, uint8_t* data, size_t len, size_t index,
                     size_t total, size_t maxBytes, SeqBodyHandler handler) {
    if (index == 0) {
        if (total == 0) {
            req->send(400, "application/json",
                      "{\"ok\":false,\"error\":\"missing JSON body\"}");
            req->_tempObject = nullptr;
            return;
        }
        if (total > maxBytes) {
            req->send(413, "application/json",
                      "{\"ok\":false,\"error\":\"payload too large\"}");
            req->_tempObject = nullptr;
            return;
        }
        char* body = (char*)malloc(total + 1);
        if (body == nullptr) {
            req->send(500, "application/json",
                      "{\"ok\":false,\"error\":\"request buffer alloc failed\"}");
            req->_tempObject = nullptr;
            return;
        }
        req->_tempObject = body;
    }

    char* body = (char*)req->_tempObject;
    if (body == nullptr) {
        return;
    }

    if ((index + len) > total) {
        free(body);
        req->_tempObject = nullptr;
        req->send(400, "application/json",
                  "{\"ok\":false,\"error\":\"invalid body chunks\"}");
        return;
    }

    if (len > 0) {
        memcpy(body + index, data, len);
    }

    if ((index + len) != total) {
        return;
    }

    body[total] = '\0';
    handler(req, body, total);
    free(body);
    req->_tempObject = nullptr;
}

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
        o["valid"] = e->valid;
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
        o["purpose"] = (e->purpose != nullptr) ? e->purpose : "";
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
void handleSaveBody(AsyncWebServerRequest* req, const char* body, size_t len) {
    ProtocolCheckResult r = seqStoreSave(body, len);
    if (!r.ok) {
        sendCheckError(req, r);
        return;
    }
    PA_LOG_INFO(TAG, "[WEB] saved Learned Sequence (%u bytes)",
                (unsigned)len);
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
void handleTestName(AsyncWebServerRequest* req, String name) {
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

void handleTestBody(AsyncWebServerRequest* req, const char* body, size_t len) {
    JsonDocument doc;
    if (deserializeJson(doc, body, len)) {
        req->send(400, "application/json",
                  "{\"ok\":false,\"error\":\"invalid json body\"}");
        return;
    }
    String name = (const char*)(doc["name"] | "");
    handleTestName(req, name);
}

void handleTestNoBody(AsyncWebServerRequest* req) {
    String name;
    const AsyncWebParameter* nameParam = req->getParam("name", true);
    if (nameParam != nullptr) {
        name = nameParam->value();
    }
    name.trim();
    if (name.length() == 0 || strncmp(name.c_str(), "DM:", 3) != 0) {
        req->send(400, "application/json",
                  "{\"ok\":false,\"error\":\"missing or invalid DM:* name\"}");
        return;
    }
    handleTestName(req, name);
}

// GET /api/seq/last-run — machine-readable evidence of the most recent body-owned
// sequence run (issue #2 task #6): what ran, what was sent (bounded TX stream),
// what cleanup was emitted (separate), inferred scopes + ring masks, and whether
// anything went wrong (drop/retry counts). Lets agents diff against the parity
// tables instead of the operator visually diffing every run.
void handleLastRun(AsyncWebServerRequest* req) {
    static SeqRunEvidence ev;  // ~5 KB snapshot target; static avoids a large stack frame
    const bool have = seqEvidenceSnapshot(ev);

    auto* stream = req->beginResponseStream("application/json");
    if (stream == nullptr) {
        req->send(500, "application/json", "{\"ok\":false,\"error\":\"alloc\"}");
        return;
    }
    JsonDocument doc;
    doc["valid"] = have;
    if (!have) {
        doc["note"] = "no sequence run recorded since boot";
        serializeJson(doc, *stream);
        req->send(stream);
        return;
    }

    doc["name"] = ev.name;
    doc["source"] = ev.source;
    doc["outcome"] = seqRunOutcomeName(ev.outcome);
    doc["running"] = (ev.outcome == SEQ_RUN_RUNNING);
    if (ev.reason[0] != '\0') doc["reason"] = ev.reason;
    doc["startMs"] = ev.startMs;
    if (ev.endMs != 0) doc["endMs"] = ev.endMs;

    JsonArray scopes = doc["fxScopes"].to<JsonArray>();
    if (ev.fxScopes & SEQ_EVID_FX_PANEL)     scopes.add("panel");
    if (ev.fxScopes & SEQ_EVID_FX_LOGIC_PSI) scopes.add("logic_psi");
    if (ev.fxScopes & SEQ_EVID_FX_HOLO)      scopes.add("holo");
    if (ev.fxScopes & SEQ_EVID_FX_AUDIO)     scopes.add("audio");
    if (ev.fxScopes & SEQ_EVID_FX_DOME_SEQ)  scopes.add("dome_seq");

    // Ring masks rendered as panel-number arrays (bit i -> ring panel number).
    JsonArray netOpen = doc["netOpenRingPanels"].to<JsonArray>();
    JsonArray touched = doc["touchedRingPanels"].to<JsonArray>();
    const uint8_t rc = seqEngineRingPanelCount();
    for (uint8_t i = 0; i < rc; ++i) {
        const int n = seqEngineRingPanelNumber(i);
        if (n < 0) continue;
        if (ev.netOpenRingMask & (uint16_t)(1u << i)) netOpen.add(n);
        if (ev.touchedRingMask & (uint16_t)(1u << i)) touched.add(n);
    }

    // Cleanup commands, captured separately from the general stream.
    JsonObject cu = doc["cleanup"].to<JsonObject>();
    cu["count"] = ev.cleanupCount;
    cu["total"] = ev.cleanupTotalCount;
    cu["truncated"] = ev.cleanupTruncated;
    JsonArray cuArr = cu["cmds"].to<JsonArray>();
    for (uint8_t i = 0; i < ev.cleanupCount; ++i) cuArr.add(ev.cleanup[i]);

    // General TX stream (bounded ring), oldest stored entry first.
    JsonObject tx = doc["tx"].to<JsonObject>();
    tx["total"] = ev.txTotalCount;
    tx["overflow"] = ev.txOverflowCount;  // commands dropped from the ring
    tx["capacity"] = (uint16_t)SEQ_EVID_TX_CAP;
    tx["truncated"] = (ev.txOverflowCount > 0);
    JsonArray txArr = tx["recent"].to<JsonArray>();
    const uint16_t stored =
        (ev.txTotalCount < SEQ_EVID_TX_CAP) ? ev.txTotalCount : SEQ_EVID_TX_CAP;
    const uint8_t start =
        (ev.txTotalCount < SEQ_EVID_TX_CAP) ? 0 : ev.txHead;
    for (uint16_t k = 0; k < stored; ++k) {
        txArr.add(ev.tx[(uint8_t)((start + k) % SEQ_EVID_TX_CAP)]);
    }

    // "Did it know anything went wrong" — counter deltas over the run window.
    JsonObject warn = doc["warnings"].to<JsonObject>();
    warn["domeQueueDropDelta"] = ev.domeQueueDropDelta;
    warn["dispatchRetryCount"] = ev.dispatchRetryCount;

    serializeJson(doc, *stream);
    req->send(stream);
}

// POST /api/seq/stop — non-latching sequence stop (issue #17).
// Aborts the currently running DM:* sequence via the dispatcher's existing
// abort path (seqEngineAbort + safe staggered dome cleanup). Returns idempotently
// 200 OK even if no sequence is running (no-op). Does not latch or affect other
// subsystems (unlike estop). The web handler signals the dispatcher via a
// transient flag in robotState; the dispatcher clears it after processing.
void handleStop(AsyncWebServerRequest* req) {
    taskENTER_CRITICAL(&robotStateMux);
    robotState.seqStopRequested = true;
    taskEXIT_CRITICAL(&robotStateMux);

    PA_LOG_INFO(TAG, "[WEB] stop requested");
    req->send(200, "application/json", "{\"ok\":true}");
}

}  // namespace

void registerSeqRoutes(AsyncWebServer& server) {
    server.on("/api/seq/list", HTTP_GET, handleList);
    server.on("/api/seq/builtins", HTTP_GET, handleBuiltins);
    server.on(
        "/api/seq/test", HTTP_POST,
        [](AsyncWebServerRequest* req) {
            if (req->contentLength() == 0) {
                handleTestNoBody(req);
            }
        },
        NULL,
        [](AsyncWebServerRequest* req, uint8_t* data, size_t len, size_t index, size_t total) {
            captureJsonBody(req, data, len, index, total, SEQ_TEST_BODY_MAX, handleTestBody);
        });
    server.on("/api/seq/stop", HTTP_POST, handleStop);
    server.on("/api/seq/last-run", HTTP_GET, handleLastRun);
    server.on("/api/seq", HTTP_GET, handleGetOne);
    server.on(
        "/api/seq", HTTP_POST,
        [](AsyncWebServerRequest* req) {
            if (req->contentLength() == 0) {
                req->send(400, "application/json",
                          "{\"ok\":false,\"error\":\"missing JSON body\"}");
            }
        },
        NULL,
        [](AsyncWebServerRequest* req, uint8_t* data, size_t len, size_t index, size_t total) {
            captureJsonBody(req, data, len, index, total, SEQ_FILE_MAX_BYTES, handleSaveBody);
        });
    server.on("/api/seq", HTTP_DELETE, handleDelete);
}
