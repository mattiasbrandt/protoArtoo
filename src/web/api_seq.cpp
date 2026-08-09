// =============================================================================
// src/web/api_seq.cpp
//
// Learned Sequence REST API (ADR 0006). See header for the route list. The
// store (seq_store) owns LittleFS + Protocol Check; this layer
// is transport only. Code/API identifiers stay neutral; operator-facing theming
// (Factory/Learned/Retrained/...) lives in the editor and docs.
//
// Ported to the WebRequest seam (ADR 0021): every handler here is a
// void(WebRequest&) bound by the seam route table, and no vendor request type
// appears in this file.
// =============================================================================

#include "api_seq.h"

#include <ArduinoJson.h>
#include <stdlib.h>
#include <string.h>

#include "api_helpers.h"           // trimAsciiWhitespace
#include "api_json_response.h"
#include "config_cache.h"          // ConfigSnapshot, configCacheRead, rcTriggerSlotsCopy
#include "logging.h"
#include "rc_action_types.h"       // RcTriggerBinding
#include "rc_binding_types.h"      // rcBindingSourceToString
#include "robot_state.h"           // CommandSource
#include "seq_dangling_bindings.h"
#include "seq_last_run_json.h"
#include "seq_json.h"
#include "seq_store.h"
#include "seq_store_index.h"
#include "sequence_dispatcher.h"
#include "sequence_run_evidence.h"  // GET /api/seq/last-run

static const char* TAG = "APISEQ";
static constexpr size_t SEQ_TEST_BODY_MAX = 512;

namespace {

// Response ceilings for webSendJsonDocument(). Sized to the largest payload
// each route can legitimately produce, not to a buffer -- nothing of this size
// is reserved unless the route actually builds that much.
//
// A row-per-sequence listing tops out at SEQ_STORE_MAX (16) rows of roughly
// 130 bytes, so 4 KB is comfortable headroom. A whole sequence with its steps
// is bounded by the same per-file cap the store enforces on save.
constexpr size_t kSeqListMaxBytes = 4096;
constexpr size_t kSeqDocumentMaxBytes = SEQ_FILE_MAX_BYTES;
constexpr size_t kSeqErrorMaxBytes = 512;

// Longest name the store indexes (SeqIndexEntry::name), plus a terminator.
// Sized larger than the field it validates against so an over-long name still
// reaches the DM:* check as an over-long string rather than a valid-looking
// truncation -- the rule WebRequest::param() documents.
constexpr size_t kSeqNameBufSize = 64;

// -----------------------------------------------------------------------------
// Shared response shapes
// -----------------------------------------------------------------------------

void sendJsonError(WebRequest& req, int code, const char* message) {
    JsonDocument doc;
    doc["ok"] = false;
    doc["error"] = message;
    webSendJsonDocument(req, doc, kSeqErrorMaxBytes, TAG, code);
}

// A Protocol Check / store failure, as a field-level 400. The message is
// generated text rather than operator input, but it goes out through the JSON
// serializer regardless so quoting can never depend on that staying true.
void sendCheckError(WebRequest& req, const ProtocolCheckResult& r) {
    JsonDocument doc;
    doc["ok"] = false;
    doc["field"] = r.field;
    doc["error"] = r.message;
    webSendJsonDocument(req, doc, kSeqErrorMaxBytes, TAG, 400);
}

// Read a request body that the backend was willing to buffer, or answer the
// client and return nullptr.
//
// The three outcomes are kept distinct on purpose, because they were distinct
// before the port and the migration's parity criterion is per-status-code:
//   nothing declared          -> 400, the client sent no body
//   declared over the cap     -> 413, the client sent too much
//   declared, in range, gone  -> 500, we could not hold what it sent
// Collapsing the last into the 400 would report our own allocation failure as
// the client's malformed request.
const char* requireBody(WebRequest& req, size_t maxBytes) {
    const size_t declared = req.contentLength();
    if (declared == 0) {
        sendJsonError(req, 400, "missing JSON body");
        return nullptr;
    }
    if (declared > maxBytes) {
        sendJsonError(req, 413, "payload too large");
        return nullptr;
    }
    const char* body = req.body();
    if (body == nullptr) {
        PA_LOG_WARN(TAG, "declared %u byte body did not survive buffering", (unsigned)declared);
        sendJsonError(req, 500, "request buffer alloc failed");
        return nullptr;
    }
    return body;
}

// -----------------------------------------------------------------------------
// GET /api/seq?name= — raw stored JSON of one Learned Sequence
//
// The body comes off LittleFS a slice at a time (WebRequest::sendChunked), so
// a file that runs to SEQ_FILE_MAX_BYTES never exists whole in RAM. The filler
// is a plain function pointer with no context argument, so the name it is
// serving lives at file scope -- the same shape api_actions_json.cpp uses.
// Both device backends dispatch handlers from a single task, so one pending
// name is race-free.
// -----------------------------------------------------------------------------
// Sized to the store's own name field rather than to kSeqNameBufSize: only a
// name that already matched an index entry ever reaches here, so the request
// buffer's deliberate over-sizing buys nothing and permanent DRAM is the
// scarcest budget on this target (api_json_response.h).
char s_streamName[sizeof(((SeqIndexEntry*)nullptr)->name)] = {};

size_t seqFileFiller(uint8_t* out, size_t capacity, size_t offset) {
    return seqStoreReadFileSlice(s_streamName, offset, out, capacity);
}

}  // namespace

// GET /api/seq/list
void handleSeqListGet(WebRequest& req) {
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
    webSendJsonDocument(req, doc, kSeqListMaxBytes, TAG);
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
void handleSeqBuiltinsGet(WebRequest& req) {
    char name[kSeqNameBufSize];
    if (req.param("name", name, sizeof(name)) && name[0] != '\0') {
        // Full single factory sequence (clone source). Always the Factory
        // definition, even if a Retrained Learned Sequence shadows the name.
        const SequenceEntry* e = sequenceCatalogFind(name);
        if (e == nullptr) {
            sendJsonError(req, 404, "not found");
            return;
        }
        JsonDocument doc;
        seqJsonSerializeObject(doc.to<JsonObject>(), *e, "factory");
        webSendJsonDocument(req, doc, kSeqDocumentMaxBytes, TAG);
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
    webSendJsonDocument(req, doc, kSeqListMaxBytes, TAG);
}

// GET /api/seq?name=  — raw stored JSON of one Learned Sequence.
void handleSeqGet(WebRequest& req) {
    char name[kSeqNameBufSize];
    if (!req.param("name", name, sizeof(name)) || name[0] == '\0') {
        sendJsonError(req, 400, "missing name parameter");
        return;
    }
    // Look up before starting the response so a miss is a clean 404: once a
    // chunked body is on the wire there is no status code left to change.
    const SeqIndexEntry* entry = seqStoreIndexFind(name);
    if (entry == nullptr) {
        sendJsonError(req, 404, "not found");
        return;
    }
    // Copied from the matched index entry rather than from the request buffer:
    // the two are deliberately different sizes (the request buffer is oversized
    // so an over-long name fails validation instead of truncating into a match),
    // and copying entry-to-entry is the only version that is bounded by
    // construction rather than by argument.
    static_assert(sizeof(s_streamName) == sizeof(entry->name),
                  "stream name buffer must match the index entry it copies");
    memcpy(s_streamName, entry->name, sizeof(s_streamName));
    s_streamName[sizeof(s_streamName) - 1] = '\0';
    if (!req.sendChunked("application/json", seqFileFiller)) {
        sendJsonError(req, 500, "read failed");
    }
}

// POST /api/seq  — body: JSON v1; validate + persist.
void handleSeqPost(WebRequest& req) {
    const char* body = requireBody(req, SEQ_FILE_MAX_BYTES);
    if (body == nullptr) {
        return;
    }
    // The declared length, not strlen(): the backend buffers exactly
    // contentLength() bytes, and measuring the buffer instead would silently
    // truncate at an embedded NUL -- the HTTP layer's byte count is the one
    // the store must persist.
    const size_t len = req.contentLength();
    ProtocolCheckResult r = seqStoreSave(body, len);
    if (!r.ok) {
        sendCheckError(req, r);
        return;
    }
    PA_LOG_INFO(TAG, "[WEB] saved Learned Sequence (%u bytes)", (unsigned)len);
    req.send(200, "application/json", "{\"ok\":true}");
}

// DELETE /api/seq?name=  — Memory Wipe.
void handleSeqDelete(WebRequest& req) {
    char name[kSeqNameBufSize];
    if (!req.param("name", name, sizeof(name)) || name[0] == '\0') {
        sendJsonError(req, 400, "missing name parameter");
        return;
    }
    if (seqStoreIndexFind(name) == nullptr) {
        sendJsonError(req, 404, "not found");
        return;
    }
    if (!seqStoreDelete(name)) {
        sendJsonError(req, 500, "delete failed");
        return;
    }
    PA_LOG_INFO(TAG, "[WEB] Memory Wipe %s", name);

    // Report RC trigger bindings the wipe leaves dangling: unless a Factory
    // Sequence shadows the name, those triggers are silent no-ops from now on
    // (the editor surfaces this; the log keeps it visible regardless). The
    // scan rules live in seqDanglingBindings(); this shell shapes the JSON
    // and replays the warnings.
    JsonDocument doc;
    doc["ok"] = true;
    ConfigSnapshot snap;
    configCacheRead(&snap);
    RcTriggerBinding slots[RC_TRIGGER_SLOT_COUNT];
    const size_t slotCount = rcTriggerSlotsCopy(snap.system, slots, RC_TRIGGER_SLOT_COUNT);
    SeqDanglingBinding dangling[RC_TRIGGER_SLOT_COUNT];
    const size_t danglingCount =
        seqDanglingBindings(name, sequenceCatalogFind(name) != nullptr, slots,
                            slotCount, dangling, RC_TRIGGER_SLOT_COUNT);
    if (danglingCount > 0) {
        JsonArray arr = doc["danglingBindings"].to<JsonArray>();
        for (size_t i = 0; i < danglingCount; ++i) {
            JsonObject o = arr.add<JsonObject>();
            o["source"] = rcBindingSourceToString(dangling[i].source);
            o["channel"] = dangling[i].channel;
            PA_LOG_WARN(TAG, "Memory Wipe %s leaves RC binding %s ch%u dangling",
                        name, rcBindingSourceToString(dangling[i].source),
                        (unsigned)dangling[i].channel);
        }
    }
    webSendJsonDocument(req, doc, kSeqListMaxBytes, TAG);
}

// POST /api/seq/test  — run a sequence by name (same ungated path as dome/cmd).
//
// The name arrives either as a form field or inside a JSON body, because both
// clients exist: data/seq.js and data/dome_control.js post JSON, and the older
// form-encoded shape is still accepted. Both device backends parse a form body
// into parameters and leave only an unparsed body for body(), so "parameter
// first, then JSON" resolves the two without either backend special-casing a
// content type.
void handleSeqTestPost(WebRequest& req) {
    char name[kSeqNameBufSize] = {};
    if (!req.param("name", name, sizeof(name)) || name[0] == '\0') {
        if (req.contentLength() == 0) {
            sendJsonError(req, 400, "missing or invalid DM:* name");
            return;
        }
        const char* body = requireBody(req, SEQ_TEST_BODY_MAX);
        if (body == nullptr) {
            return;
        }
        JsonDocument doc;
        if (deserializeJson(doc, body)) {
            sendJsonError(req, 400, "invalid json body");
            return;
        }
        snprintf(name, sizeof(name), "%s", (const char*)(doc["name"] | ""));
    }

    trimAsciiWhitespace(name);
    if (name[0] == '\0' || strncmp(name, "DM:", 3) != 0) {
        sendJsonError(req, 400, "missing or invalid DM:* name");
        return;
    }
    if (!sequenceStart(name, SRC_WEB_API)) {
        sendJsonError(req, 503, "sequence queue full");
        return;
    }
    PA_LOG_INFO(TAG, "[WEB] test %s", name);
    req.send(200, "application/json", "{\"ok\":true}");
}

// GET /api/seq/last-run — machine-readable evidence of the most recent body-owned
// sequence run: what ran, what was sent (bounded TX stream),
// what cleanup was emitted (separate), inferred scopes + ring masks, and whether
// anything went wrong (body-local queue-full/retry counts). Lets agents diff
// against the parity tables instead of the operator visually diffing every run.
void handleSeqLastRunGet(WebRequest& req) {
    static SeqRunEvidence ev;  // ~5 KB snapshot target; static avoids a large stack frame
    const bool have = seqEvidenceSnapshot(ev);

    JsonDocument doc;
    if (!populateSeqLastRunJson(doc, ev, have)) {
        sendJsonError(req, 500, "last-run response overflow");
        return;
    }
    webSendJsonDocument(req, doc, kSeqDocumentMaxBytes, TAG);
}

// POST /api/seq/stop — non-latching sequence stop.
// Aborts the currently running DM:* sequence via the dispatcher's existing
// abort path (seqEngineAbort + safe staggered dome cleanup). Returns idempotently
// 200 OK even if no sequence is running (no-op). Does not latch or affect other
// subsystems (unlike estop). The web handler signals the dispatcher via a
// transient flag in robotState; the dispatcher clears it after processing.
void handleSeqStopPost(WebRequest& req) {
    taskENTER_CRITICAL(&robotStateMux);
    robotState.seqStopRequested = true;
    taskEXIT_CRITICAL(&robotStateMux);

    PA_LOG_INFO(TAG, "[WEB] stop requested");
    req.send(200, "application/json", "{\"ok\":true}");
}
