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
#include <Preferences.h>
#include <stdlib.h>
#include <string.h>

#include "api_helpers.h"           // trimAsciiWhitespace
#include "api_json_response.h"
#include "config.h"                // NVS_NAMESPACE
#include "config_cache.h"          // ConfigSnapshot, configCacheRead, rcTriggerSlotsCopy
#include "logging.h"
#include "protocol_check.h"        // PC_NAME_BODY_MAX, protocolCheckSeqNameValid
#include "rc_action_types.h"       // RcTriggerBinding
#include "rc_binding_types.h"      // rcBindingSourceToString
#include "robot_state.h"           // CommandSource
#include "seq_dangling_bindings.h"
#include "seq_last_run_json.h"
#include "seq_json.h"
#include "seq_store.h"
#include "seq_store_index.h"
#include "sequence_dispatcher.h"
#include "sequence_pose.h"          // sequencePoseRefusal() - POST /api/seq/pose
#include "sequence_run_evidence.h"  // GET /api/seq/last-run
#include "web_request_scratch.h"

static const char* TAG = "APISEQ";
static constexpr size_t SEQ_TEST_BODY_MAX = 512;

namespace {

// Response ceilings for webSendJsonDocument(). Sized to the largest payload
// each route can legitimately produce, not to a buffer -- nothing of this size
// is reserved unless the route actually builds that much.
//
// A row-per-sequence listing tops out at SEQ_INDEX_CAPACITY (10) rows, each
// bounded by the index entry's own field sizes -- worked out and asserted
// against the ceiling below (kSeqListWorstCaseBytes). That is the index
// capacity on every board, not the board's save cap (SEQ_STORE_CAP, five on
// the artoo-esp32): a droid over its cap still lists everything it holds. A
// whole sequence with its steps is bounded by the same per-file cap the store
// enforces on save.
//
// The Factory catalog listing is its own ceiling because it grows with the
// catalog rather than with the store, and each row carries a purpose sentence.
// Measured by serializing the rows as the route writes them: 2959 B for the 16
// entries before #354, 4359 B once the seven body routines joined -- past the
// 4 KB above, which answered 500. 6 KB leaves room for another handful. Each
// row's `lengthMs` (#441) is at most 17 B more, about 4.75 KB for those 23.
constexpr size_t kSeqListMaxBytes = 4096;
constexpr size_t kSeqBuiltinsListMaxBytes = 6144;
constexpr size_t kSeqDocumentMaxBytes = SEQ_FILE_MAX_BYTES;
constexpr size_t kSeqErrorMaxBytes = 512;

// GET /api/seq/list, bounded so the index entry and the ceiling move together
// or the build fails: a row that outgrew 4 KB would answer 500 only on a droid
// holding ten sequences with long purposes.
//
// One row at its widest is the keys and punctuation handleSeqListGet() writes
// (kSeqListRowKeys, every boolean spelled `false`, the longer word), plus each
// value at its field's full width. Free text -- the name, the source and the
// purpose -- is counted JSON-escaped to twice its length, the most the
// serializer makes of a byte (a quote or a backslash); the id is letters and
// digits and the toggle group one of seqToggleGroupToString()'s words, "user1"
// the longest. The numbers are a uint32 (10 digits) twice and a uint8 (3).
// With a 40-byte purpose that is 353 B a row and 3532 B for ten, inside 4 KB.
constexpr char kSeqListRowKeys[] =
    "{\"name\":\"\",\"id\":\"\",\"toggleGroup\":\"\",\"suppressMs\":,\"source\":\"\","
    "\"modified\":false,\"valid\":false,\"retrained\":false,\"stepCount\":,"
    "\"lengthMs\":,\"purpose\":\"\",\"purposeCut\":false},";
constexpr size_t kSeqListRowWorstCaseBytes =
    (sizeof(kSeqListRowKeys) - 1u) +
    2u * (sizeof(((SeqIndexEntry*)nullptr)->name) - 1u) +
    (sizeof(((SeqIndexEntry*)nullptr)->id) - 1u) +
    (sizeof("user1") - 1u) +
    2u * (sizeof(((SeqIndexEntry*)nullptr)->source) - 1u) +
    2u * (sizeof(((SeqIndexEntry*)nullptr)->purpose) - 1u) +
    10u + 10u + 3u;
constexpr size_t kSeqListWorstCaseBytes =
    2u + (size_t)SEQ_INDEX_CAPACITY * kSeqListRowWorstCaseBytes;
static_assert(kSeqListWorstCaseBytes < kSeqListMaxBytes,
              "a full index can build a /api/seq/list payload this route would"
              " refuse: raise kSeqListMaxBytes or shrink the index entry");

// GET /api/seq/last-run shares kSeqDocumentMaxBytes, and both of its inputs are
// chip-target specific: the run-evidence ring dimensions set the payload, the
// per-file cap sets the ceiling. Raising one without the other would refuse a
// legitimate response with a 500 (webSendJsonDocument rejects at or above the
// ceiling) and the failure would only appear on a run long enough to fill the
// ring. Bound the payload here so the two move together or the build fails.
//
// Worst case: every retained TX entry and every cleanup entry at full width,
// each JSON-escaped to twice its length (a command may legitimately contain a
// quote or a backslash), plus quotes and a separator; the name and reason the
// same way; and 512 bytes for the fixed keys, the scope and ring-panel arrays
// and the warnings object, which are all small and fixed in number.
constexpr size_t kSeqLastRunWorstCaseBytes =
    (size_t)SEQ_EVID_TX_CAP * (2u * (SEQ_EVID_CMD_LEN - 1u) + 3u) +
    (size_t)SEQ_EVID_CLEANUP_CAP * (2u * (SEQ_EVID_CMD_LEN - 1u) + 3u) +
    2u * (SEQ_EVID_NAME_LEN - 1u) + 2u * (SEQ_EVID_REASON_LEN - 1u) + 512u;
static_assert(kSeqLastRunWorstCaseBytes < kSeqDocumentMaxBytes,
              "the run-evidence ring can build a /api/seq/last-run payload this"
              " route would refuse: raise the per-file cap for this chip target"
              " or shrink the ring");

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
// GET /api/seq?name= - raw stored JSON of one Learned Sequence
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

// -----------------------------------------------------------------------------
// Pinned Sequences (#472): the ones the Dashboard's Sequences rail puts first.
//
// Kept on the droid so every browser shows the same pins and they survive a
// reboot, in a key of their own and NOT in ConfigSnapshot: that struct is
// pinned at 944 B by a static_assert (config_store.h) because it sits in the
// Console task's stack chain. Read and written only here, by the web task,
// with the list on this handler's stack for the length of one request.
//
// Stored as one string, the names joined by commas - a name is DM: and
// [A-Z0-9_], so a comma can never be part of one. Only the form is checked,
// the Stand Down Sequence's rule (config_settings.cpp, standDownSequence): a
// pin naming a Sequence deleted since stays pinned, and the page skips it.
//
// NVS cost, against the artoo namespace that was full on 2026-10-05 (#381 row
// 74): eight names at their longest are 175 chars and a terminator, one
// header entry and six data entries of 32 B, seven; a rewrite holds the old
// seven until the new ones are written, fourteen for a moment. The full dump
// had sixteen entries to reclaim (fifteen erased, one empty) besides its
// reserve page. An empty list removes the key and costs nothing.
// -----------------------------------------------------------------------------
constexpr char kSeqPinsKey[] = "seq_pins";
constexpr uint8_t kSeqPinsMax = 8;
// "DM:", the longest body Protocol Check accepts, and a comma or terminator.
constexpr size_t kSeqPinSlot = 3u + PC_NAME_BODY_MAX + 1u;
constexpr size_t kSeqPinsStoredSize = (size_t)kSeqPinsMax * kSeqPinSlot;
constexpr size_t SEQ_PINS_BODY_MAX = 128;
// {"ok":true,"max":8,"pins":[...]} with eight names, each quoted and
// separated; a name is letters, digits and _ and never escapes.
constexpr size_t kSeqPinsWorstCaseBytes = 40u + (size_t)kSeqPinsMax * (kSeqPinSlot + 3u);
constexpr size_t kSeqPinsMaxBytes = 512;
static_assert(kSeqPinsWorstCaseBytes < kSeqPinsMaxBytes,
              "eight pins build a /api/seq/pins answer this route would refuse");

struct SeqPins {
    char stored[kSeqPinsStoredSize];  // the key's string, as NVS holds it
    uint8_t count;
};

// Reads the key into `pins`. A droid that never pinned has no key, which is
// an empty list. So is a namespace the read-only open cannot find: it does not
// exist until something is first saved, and Preferences::begin() logs any
// other reason it failed itself.
void seqPinsRead(SeqPins& pins) {
    pins.stored[0] = '\0';
    pins.count = 0;
    Preferences prefs;
    if (!prefs.begin(NVS_NAMESPACE, true)) return;
    const String value = prefs.isKey(kSeqPinsKey) ? prefs.getString(kSeqPinsKey, String()) : String();
    prefs.end();
    // Only this file writes the key, and never past kSeqPinsMax names; a
    // value that is longer anyway is cut at the last whole name that fits.
    snprintf(pins.stored, sizeof(pins.stored), "%s", value.c_str());
    if (value.length() >= sizeof(pins.stored)) {
        char* cut = strrchr(pins.stored, ',');
        if (cut != nullptr) *cut = '\0';
    }
    if (pins.stored[0] == '\0') return;
    pins.count = 1;
    for (const char* at = pins.stored; (at = strchr(at, ',')) != nullptr; ++at) ++pins.count;
}

// Whether `name` is one of the stored names, as a whole name.
bool seqPinsHas(const SeqPins& pins, const char* name) {
    const size_t len = strlen(name);
    for (const char* at = pins.stored; *at != '\0';) {
        const char* comma = strchr(at, ',');
        const size_t here = comma != nullptr ? (size_t)(comma - at) : strlen(at);
        if (here == len && strncmp(at, name, len) == 0) return true;
        if (comma == nullptr) break;
        at = comma + 1;
    }
    return false;
}

// Takes `name` out of the stored string, keeping the others in their order.
void seqPinsDrop(SeqPins& pins, const char* name) {
    char kept[kSeqPinsStoredSize] = {};
    size_t used = 0;
    uint8_t count = 0;
    const size_t len = strlen(name);
    for (const char* at = pins.stored; *at != '\0';) {
        const char* comma = strchr(at, ',');
        const size_t here = comma != nullptr ? (size_t)(comma - at) : strlen(at);
        if (!(here == len && strncmp(at, name, len) == 0)) {
            used += (size_t)snprintf(kept + used, sizeof(kept) - used, "%s%.*s", used > 0 ? "," : "",
                                     (int)here, at);
            ++count;
        }
        if (comma == nullptr) break;
        at = comma + 1;
    }
    memcpy(pins.stored, kept, sizeof(kept));
    pins.count = count;
}

// Writes the list back; an empty one removes the key. False when NVS refused
// the write - putString() answers 0 for a full namespace.
bool seqPinsWrite(const SeqPins& pins) {
    Preferences prefs;
    if (!prefs.begin(NVS_NAMESPACE, false)) return false;
    bool ok = false;
    if (pins.stored[0] == '\0') {
        ok = !prefs.isKey(kSeqPinsKey) || prefs.remove(kSeqPinsKey);
    } else {
        ok = prefs.putString(kSeqPinsKey, pins.stored) > 0;
    }
    prefs.end();
    return ok;
}

void sendSeqPins(WebRequest& req, const SeqPins& pins) {
    JsonDocument doc;
    doc["ok"] = true;
    doc["max"] = kSeqPinsMax;
    JsonArray names = doc["pins"].to<JsonArray>();
    char name[kSeqPinSlot] = {};
    for (const char* at = pins.stored; *at != '\0';) {
        const char* comma = strchr(at, ',');
        const size_t here = comma != nullptr ? (size_t)(comma - at) : strlen(at);
        snprintf(name, sizeof(name), "%.*s", (int)here, at);
        names.add(name);  // copied: ArduinoJson duplicates a char* it is handed
        if (comma == nullptr) break;
        at = comma + 1;
    }
    webSendJsonDocument(req, doc, kSeqPinsMaxBytes, TAG);
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
        // The stable id a phrase refers to this sequence by (ADR 0046); absent
        // on one saved before phrases existed.
        if (e->id[0] != '\0') o["id"] = e->id;
        o["toggleGroup"] = seqToggleGroupToString(e->toggleGroup);
        o["suppressMs"] = e->suppressMs;
        o["source"] = e->source;
        o["modified"] = e->modified;
        o["valid"] = e->valid;
        // A Learned Sequence that shadows a Factory one is "Retrained".
        o["retrained"] = (sequenceCatalogFind(e->name) != nullptr);
        // What the list row says without the file (#441): the main branch's
        // step count, how long a run is, and the start of the purpose, with
        // whether the file's purpose runs on past it. Each key is spelled as
        // the Factory listing below spells it, so the page reads one row shape.
        o["stepCount"] = e->steps;
        o["lengthMs"] = e->lengthMs;
        o["purpose"] = e->purpose;
        o["purposeCut"] = e->purposeCut;
    }
    webSendJsonDocument(req, doc, kSeqListMaxBytes, TAG);
}

// GET /api/seq/builtins         - lightweight factory catalog (metadata only).
// GET /api/seq/builtins?name=X   - full JSON v1 of one factory sequence.
//
// The list form carries no step data, so the whole-catalog response stays a few
// kilobytes (kSeqBuiltinsListMaxBytes) and cannot exhaust the fragmented heap
// mid-send. Serializing all
// factory sequences with their steps into one buffered response was large enough
// to OOM the prior async backend during delivery, and ESP32's exceptions-disabled
// libstdc++ turns the failed allocation into terminate()/abort() (panic reboot).
// The editor fetches full steps per-name only when the operator clones a sequence.
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
        o["lengthMs"] = seqStoreRunLengthMs(e->steps, e->stepCount);
        o["purpose"] = (e->purpose != nullptr) ? e->purpose : "";
    }
    webSendJsonDocument(req, doc, kSeqBuiltinsListMaxBytes, TAG);
}

// GET /api/seq?name=  - raw stored JSON of one Learned Sequence.
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

// POST /api/seq  - body: JSON v1; validate + persist.
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

// DELETE /api/seq?name=  - Memory Wipe.
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

// POST /api/seq/test  - run a sequence by name (same ungated path as dome/cmd).
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

// POST /api/seq/pose  {name, t}  - send the droid to one instant of a sequence
// (#440, ADR 0062, include/sequence_pose.h).
//
// The timeline's marker moves silently; this is the separate, deliberate press
// that moves the droid. What arrives is the whole of the press -- the name of a
// saved or Factory sequence and the instant, in ms from its start. The pose at
// that instant and its pace are the Sequence Coordinator's, worked out from
// the stored steps, never taken from the page (the operator's decision,
// 2026-09-30 on #440). It asks no Non-RC Control consent: it commands exactly
// what a normal run commands at that instant (ADR 0062, ADR 0064).
//
// A latched estop or Sleep Mode refuses it, and the answer says why in the
// words sequencePoseRefusal() holds -- the one copy of that rule, which the
// Coordinator asks again when the request reaches it, since a halt can land in
// between. Only a body-owned sequence has steps to take a pose from, so a name
// the dome runs itself is a 404.
void handleSeqPosePost(WebRequest& req) {
    const char* body = requireBody(req, SEQ_TEST_BODY_MAX);
    if (body == nullptr) {
        return;
    }
    JsonDocument doc;
    if (deserializeJson(doc, body)) {
        sendJsonError(req, 400, "invalid json body");
        return;
    }
    char name[kSeqNameBufSize] = {};
    snprintf(name, sizeof(name), "%s", (const char*)(doc["name"] | ""));
    trimAsciiWhitespace(name);
    if (name[0] == '\0' || strncmp(name, "DM:", 3) != 0) {
        sendJsonError(req, 400, "missing or invalid DM:* name");
        return;
    }
    // Whole milliseconds from the start, read wide and signed so a negative or
    // a value past the registry's int32 range is refused rather than wrapped
    // into an instant (docs/action-registry.yaml dome.action.pose-sequence).
    JsonVariantConst t = doc["t"];
    const long long atMs = t.is<long long>() ? t.as<long long>() : -1LL;
    if (atMs < 0 || atMs > 0x7FFFFFFFLL) {
        sendJsonError(req, 400, "t must be whole milliseconds from the start");
        return;
    }
    bool estopLatched = false;
    bool sleepMode = false;
    taskENTER_CRITICAL(&robotStateMux);
    estopLatched = robotState.estop;
    sleepMode = robotState.sleepMode;
    taskEXIT_CRITICAL(&robotStateMux);
    const char* refusal = sequencePoseRefusal(estopLatched, sleepMode);
    if (refusal != nullptr) {
        sendJsonError(req, 409, refusal);
        return;
    }

    if (!sequencePoseRequest(name, (uint32_t)atMs, SRC_WEB_API)) {
        sendJsonError(req, 404, "not a saved or factory sequence");
        return;
    }
    PA_LOG_INFO(TAG, "[WEB] pose %s at %u ms", name, (unsigned)atMs);
    req.send(200, "application/json", "{\"ok\":true}");
}

// GET /api/seq/last-run - machine-readable evidence of the most recent body-owned
// sequence run: what ran, what was sent (bounded TX stream),
// what cleanup was emitted (separate), inferred scopes + ring masks, and whether
// anything went wrong (body-local queue-full/retry counts). Lets agents diff
// against the parity tables instead of the operator visually diffing every run.
void handleSeqLastRunGet(WebRequest& req) {
    // Snapshot target. In the web request scratch rather than on the stack:
    // the record is 2204 B on ESP32 and 8284 B on ESP32-P4 (the ring is sized
    // per chip target, sequence_run_evidence.h), and neither backend's server
    // task has that to spare. This is the second of the two copies that header
    // prices; it holds its bytes only while a request is being answered (#428).
    WebRequestScratch<SeqRunEvidence> scratch;
    if (!scratch) {
        sendJsonError(req, 500, "request scratch unavailable");
        return;
    }
    SeqRunEvidence& ev = *scratch;
    const bool have = seqEvidenceSnapshot(ev);

    JsonDocument doc;
    if (!populateSeqLastRunJson(doc, ev, have)) {
        sendJsonError(req, 500, "last-run response overflow");
        return;
    }
    webSendJsonDocument(req, doc, kSeqDocumentMaxBytes, TAG);
}

// POST /api/seq/stop - non-latching sequence stop.
// Aborts the currently running DM:* sequence via the dispatcher's existing
// abort path (seqEngineAbort + safe staggered dome cleanup). Returns idempotently
// 200 OK even if no sequence is running (no-op). Does not latch or affect other
// subsystems (unlike estop). The web handler signals the dispatcher via a
// transient flag in robotState; the dispatcher clears it after processing.
// A pose press not yet taken is cancelled with it (sequenceStopRequest()).
void handleSeqStopPost(WebRequest& req) {
    sequenceStopRequest();

    PA_LOG_INFO(TAG, "[WEB] stop requested");
    req.send(200, "application/json", "{\"ok\":true}");
}

// GET /api/seq/pins - the pinned Sequences, in the order they were pinned
// (#472). {"ok":true,"max":8,"pins":["DM:VADER", ...]}; nothing pinned is an
// empty list.
void handleSeqPinsGet(WebRequest& req) {
    SeqPins pins;
    seqPinsRead(pins);
    sendSeqPins(req, pins);
}

// POST /api/seq/pins  {name, pinned} - pin or unpin one Sequence, and answer
// the list as it now stands. One name a press rather than the whole list, so
// two browsers pinning at once each keep the other's pin. Pinning a pinned
// name, or unpinning one that is not, changes nothing and answers 200.
void handleSeqPinsPost(WebRequest& req) {
    const char* body = requireBody(req, SEQ_PINS_BODY_MAX);
    if (body == nullptr) {
        return;
    }
    JsonDocument doc;
    if (deserializeJson(doc, body)) {
        sendJsonError(req, 400, "invalid json body");
        return;
    }
    JsonVariantConst pinned = doc["pinned"];
    if (!pinned.is<bool>()) {
        sendJsonError(req, 400, "pinned must be true or false");
        return;
    }
    char name[kSeqNameBufSize] = {};
    snprintf(name, sizeof(name), "%s", (const char*)(doc["name"] | ""));
    trimAsciiWhitespace(name);
    if (!protocolCheckSeqNameValid(name)) {
        sendJsonError(req, 400, "missing or invalid DM:* name");
        return;
    }

    SeqPins pins;
    seqPinsRead(pins);
    const bool has = seqPinsHas(pins, name);
    if (pinned.as<bool>() == has) {
        sendSeqPins(req, pins);
        return;
    }
    if (has) {
        seqPinsDrop(pins, name);
    } else {
        if (pins.count >= kSeqPinsMax) {
            sendJsonError(req, 409, "8 are pinned. Unpin one first.");
            return;
        }
        const size_t used = strlen(pins.stored);
        snprintf(pins.stored + used, sizeof(pins.stored) - used, "%s%s", used > 0 ? "," : "", name);
        ++pins.count;
    }
    if (!seqPinsWrite(pins)) {
        PA_LOG_WARN(TAG, "pins not written: NVS refused %s", kSeqPinsKey);
        sendJsonError(req, 500, "pins not saved: settings storage full");
        return;
    }
    PA_LOG_INFO(TAG, "[WEB] %s %s", has ? "unpin" : "pin", name);
    sendSeqPins(req, pins);
}
