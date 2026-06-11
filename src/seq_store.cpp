// =============================================================================
// src/seq_store.cpp
//
// Learned Sequence runtime store — LittleFS I/O (issue #2 slice 3c, ADR 0006).
// Firmware-only; see header for the contract. Pure validation/index/parse live
// in protocol_check / seq_json / seq_store_index and are tested natively.
// =============================================================================

#include "seq_store.h"

#include <Arduino.h>
#include <LittleFS.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <stdlib.h>
#include <string.h>

#include <ArduinoJson.h>

#include "logging.h"
#include "seq_json.h"
#include "seq_store_index.h"

static const char* TAG = "SEQST";
static const char* SEQ_DIR = "/seq";

// Run/boot-scan staging buffers (the dispatcher runs from these via the entry
// returned by seqStoreLoad). One sequence at a time; the boot scan completes
// before the dispatcher task starts. Save validation never uses these.
static SeqStep s_main[96];
static SeqStep s_close[96];

static SemaphoreHandle_t s_mutex = nullptr;

// -----------------------------------------------------------------------------
// Helpers
// -----------------------------------------------------------------------------
static ProtocolCheckResult sok() {
    ProtocolCheckResult r = { true, "", "" };
    return r;
}
static ProtocolCheckResult sfail(const char* field, const char* msg) {
    ProtocolCheckResult r = { false, "", "" };
    strncpy(r.field, field, sizeof(r.field) - 1);
    strncpy(r.message, msg, sizeof(r.message) - 1);
    return r;
}

// "DM:MYSEQ" -> "/seq/DM_MYSEQ.json". Returns false if name is implausible.
static bool nameToPath(const char* name, char* out, size_t cap) {
    if (name == nullptr || strncmp(name, "DM:", 3) != 0) return false;
    int n = snprintf(out, cap, "%s/%s.json", SEQ_DIR, name);
    if (n <= 0 || (size_t)n >= cap) return false;
    for (size_t i = 0; out[i] != '\0'; ++i) {
        if (out[i] == ':') out[i] = '_';
    }
    return true;
}

static bool lock() {
    if (s_mutex == nullptr) return true;  // pre-init: single-threaded boot path
    return xSemaphoreTake(s_mutex, pdMS_TO_TICKS(1000)) == pdTRUE;
}
static void unlock() {
    if (s_mutex != nullptr) xSemaphoreGive(s_mutex);
}

// Build a SequenceEntry over the supplied (already validated) staging branches.
static void buildEntry(const SeqDraft& d, SequenceEntry& out,
                       const char* nameStore) {
    out.name           = nameStore;  // stable storage (index entry name)
    out.steps          = d.steps;
    out.stepCount      = d.stepCount;
    out.suppressMs     = d.suppressMs;
    out.toggleGroup    = d.toggleGroup;
    out.closeSteps     = d.closeSteps;
    out.closeStepCount = d.closeStepCount;
}

// Index a validated draft with its meta. Returns false if the index is full.
static bool indexDraft(const SeqDraft& d, JsonVariantConst root, const char* file) {
    SeqIndexEntry e = {};
    strncpy(e.name, d.name, sizeof(e.name) - 1);
    e.toggleGroup = d.toggleGroup;
    e.suppressMs  = d.suppressMs;
    const char* src = root["meta"]["source"] | "user";
    strncpy(e.source, src, sizeof(e.source) - 1);
    e.modified = root["meta"]["modified"] | false;
    strncpy(e.file, file, sizeof(e.file) - 1);
    return seqStoreIndexAdd(e);
}

// -----------------------------------------------------------------------------
// Init — scan + index
// -----------------------------------------------------------------------------
void seqStoreInit() {
    if (s_mutex == nullptr) {
        s_mutex = xSemaphoreCreateMutex();
    }
    if (!LittleFS.begin(true)) {
        PA_LOG_ERROR(TAG, "LittleFS mount failed; Learned Sequences disabled");
        return;
    }
    if (!LittleFS.exists(SEQ_DIR)) {
        LittleFS.mkdir(SEQ_DIR);
        return;  // nothing to index yet
    }

    seqStoreIndexClear();
    File dir = LittleFS.open(SEQ_DIR);
    if (!dir || !dir.isDirectory()) {
        return;
    }

    uint8_t indexed = 0, skipped = 0;
    for (File f = dir.openNextFile(); f; f = dir.openNextFile()) {
        if (f.isDirectory()) { f.close(); continue; }
        // Basename for the index entry (storage is /seq/<file>).
        const char* nm = f.name();
        const char* base = strrchr(nm, '/');
        char file[40];
        strncpy(file, base ? base + 1 : nm, sizeof(file) - 1);
        file[sizeof(file) - 1] = '\0';

        JsonDocument doc;
        DeserializationError err = deserializeJson(doc, f);
        f.close();
        if (err) {
            PA_LOG_WARN(TAG, "skip %s: parse %s", file, err.c_str());
            ++skipped;
            continue;
        }
        SeqDraft d;
        ProtocolCheckResult r = seqJsonParseVariant(doc.as<JsonVariantConst>(),
                                                    s_main, 96, s_close, 96, d);
        if (r.ok) r = protocolCheck(d);
        if (!r.ok) {
            PA_LOG_WARN(TAG, "skip %s: %s (%s)", file, r.message, r.field);
            ++skipped;
            continue;
        }
        if (!indexDraft(d, doc.as<JsonVariantConst>(), file)) {
            PA_LOG_WARN(TAG, "index full at %s", file);
            break;
        }
        ++indexed;
    }
    dir.close();
    PA_LOG_INFO(TAG, "indexed %u Learned Sequence(s), skipped %u", indexed, skipped);
}

// -----------------------------------------------------------------------------
// Load (dispatcher run path)
// -----------------------------------------------------------------------------
ProtocolCheckResult seqStoreLoad(const char* name, SequenceEntry& out) {
    if (!lock()) return sfail("name", "store busy");

    const SeqIndexEntry* idx = seqStoreIndexFind(name);
    if (idx == nullptr) {
        unlock();
        return sfail("name", "not a Learned Sequence");
    }
    char path[64];
    snprintf(path, sizeof(path), "%s/%s", SEQ_DIR, idx->file);

    File f = LittleFS.open(path, "r");
    if (!f) {
        unlock();
        return sfail("name", "file missing");
    }
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, f);
    f.close();
    if (err) {
        unlock();
        return sfail("json", err.c_str());
    }

    SeqDraft d;
    ProtocolCheckResult r = seqJsonParseVariant(doc.as<JsonVariantConst>(),
                                                s_main, 96, s_close, 96, d);
    if (r.ok) r = protocolCheck(d);  // stamps effectClass into s_main/s_close
    if (r.ok) {
        buildEntry(d, out, idx->name);  // entry name -> stable index storage
    }
    unlock();
    return r;
}

// -----------------------------------------------------------------------------
// Save
// -----------------------------------------------------------------------------
ProtocolCheckResult seqStoreSave(const char* json, size_t len) {
    // Validate into a transient heap staging pair so a running sequence's run
    // buffers (s_main/s_close) are never disturbed.
    SeqStep* tmp = (SeqStep*)malloc(sizeof(SeqStep) * 192);
    if (tmp == nullptr) {
        return sfail("json", "out of memory");
    }

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, json, len);
    ProtocolCheckResult r;
    SeqDraft d;
    if (err) {
        r = sfail("json", err.c_str());
    } else {
        r = seqJsonParseVariant(doc.as<JsonVariantConst>(), tmp, 96, tmp + 96, 96, d);
        if (r.ok) r = protocolCheck(d);
    }
    if (!r.ok) {
        free(tmp);
        return r;
    }

    char path[64];
    if (!nameToPath(d.name, path, sizeof(path))) {
        free(tmp);
        return sfail("name", "invalid name");
    }
    char file[40];
    const char* base = strrchr(path, '/');
    strncpy(file, base ? base + 1 : path, sizeof(file) - 1);
    file[sizeof(file) - 1] = '\0';

    if (!lock()) {
        free(tmp);
        return sfail("name", "store busy");
    }

    // Capacity: 16-file cap (new names only), per-file size, free-space floor.
    const bool isNew = (seqStoreIndexFind(d.name) == nullptr);
    if (isNew && seqStoreIndexCount() >= SEQ_STORE_MAX) {
        unlock();
        free(tmp);
        return sfail("name", "store full (16 sequences max)");
    }
    if (len > SEQ_FILE_MAX_BYTES) {
        unlock();
        free(tmp);
        return sfail("json", "file too large (12 KB max)");
    }
    size_t freeBytes = LittleFS.totalBytes() - LittleFS.usedBytes();
    if (freeBytes < SEQ_FS_FREE_FLOOR + len) {
        unlock();
        free(tmp);
        return sfail("json", "insufficient filesystem space");
    }

    // Capture meta before we drop the transient buffer; then write temp+rename.
    SeqIndexEntry entry = {};
    strncpy(entry.name, d.name, sizeof(entry.name) - 1);
    entry.toggleGroup = d.toggleGroup;
    entry.suppressMs  = d.suppressMs;
    const char* src = doc["meta"]["source"] | "user";
    strncpy(entry.source, src, sizeof(entry.source) - 1);
    entry.modified = doc["meta"]["modified"] | false;
    strncpy(entry.file, file, sizeof(entry.file) - 1);
    free(tmp);

    char tmpPath[72];
    snprintf(tmpPath, sizeof(tmpPath), "%s/.tmp.json", SEQ_DIR);
    File wf = LittleFS.open(tmpPath, "w");
    if (!wf) {
        unlock();
        return sfail("json", "cannot open temp file");
    }
    size_t wrote = wf.write((const uint8_t*)json, len);
    wf.close();
    if (wrote != len) {
        LittleFS.remove(tmpPath);
        unlock();
        return sfail("json", "write failed");
    }
    LittleFS.remove(path);  // ignore result; rename needs the slot free
    if (!LittleFS.rename(tmpPath, path)) {
        LittleFS.remove(tmpPath);
        unlock();
        return sfail("json", "rename failed");
    }

    seqStoreIndexAdd(entry);  // insert or update in place
    unlock();
    return sok();
}

// -----------------------------------------------------------------------------
// Delete (Memory Wipe)
// -----------------------------------------------------------------------------
bool seqStoreDelete(const char* name) {
    char path[64];
    if (!nameToPath(name, path, sizeof(path))) return false;

    if (!lock()) return false;
    bool removedFile = LittleFS.remove(path);
    bool removedIdx = seqStoreIndexRemove(name);
    unlock();
    return removedFile || removedIdx;
}
