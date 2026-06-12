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
// built by seqStoreCommit). One sequence at a time; the boot scan completes
// before the dispatcher task starts. Save validation never uses these.
static SeqStep s_main[96];
static SeqStep s_close[96];

// Two-phase load staging (dispatcher task only). seqStorePrepare() parses and
// validates into this transient heap pair; seqStoreCommit() copies it into the
// run buffers once the previous run is drained. s_runName gives the running
// entry a name whose lifetime does not depend on the (mutable) index.
static SeqStep* s_staged = nullptr;  // malloc'd [192]: main at [0], close at [96]
static SeqDraft s_stagedDraft;       // step pointers into s_staged
static char     s_runName[24];

static SemaphoreHandle_t s_mutex = nullptr;

// -----------------------------------------------------------------------------
// Helpers (result constructors pcOk/pcFail are shared inlines in the header)
// -----------------------------------------------------------------------------

// "DM:MYSEQ" -> "/seq/DM_MYSEQ.json". Returns false if name is implausible.
// File-name mapping is the pure seqStoreNameToFile(); this prefixes the dir.
static bool nameToPath(const char* name, char* out, size_t cap) {
    char file[40];
    if (!seqStoreNameToFile(name, file, sizeof(file))) return false;
    int n = snprintf(out, cap, "%s/%s", SEQ_DIR, file);
    return n > 0 && (size_t)n < cap;
}

static bool lock() {
    if (s_mutex == nullptr) return true;  // pre-init: single-threaded boot path
    return xSemaphoreTake(s_mutex, pdMS_TO_TICKS(1000)) == pdTRUE;
}
static void unlock() {
    if (s_mutex != nullptr) xSemaphoreGive(s_mutex);
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
// Load (dispatcher run path) — two-phase: prepare into heap, commit to buffers
// -----------------------------------------------------------------------------
ProtocolCheckResult seqStorePrepare(const char* name) {
    if (s_staged != nullptr) {  // a previous prepare was never committed
        free(s_staged);
        s_staged = nullptr;
    }
    if (!lock()) return pcFail("name", "store busy");

    const SeqIndexEntry* idx = seqStoreIndexFind(name);
    if (idx == nullptr) {
        unlock();
        return pcFail("name", "not a Learned Sequence");
    }
    char path[64];
    snprintf(path, sizeof(path), "%s/%s", SEQ_DIR, idx->file);

    File f = LittleFS.open(path, "r");
    if (!f) {
        unlock();
        return pcFail("name", "file missing");
    }
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, f);
    f.close();
    if (err) {
        unlock();
        return pcFail("json", err.c_str());
    }

    SeqStep* tmp = (SeqStep*)malloc(sizeof(SeqStep) * 192);
    if (tmp == nullptr) {
        unlock();
        return pcFail("json", "out of memory");
    }
    SeqDraft d;
    ProtocolCheckResult r = seqJsonParseVariant(doc.as<JsonVariantConst>(),
                                                tmp, 96, tmp + 96, 96, d);
    if (r.ok) r = protocolCheck(d);  // stamps effectClass into the staging
    unlock();
    if (!r.ok) {
        free(tmp);
        return r;
    }
    s_staged = tmp;
    s_stagedDraft = d;
    return pcOk();
}

bool seqStoreCommit(SequenceEntry& out) {
    if (s_staged == nullptr) {
        return false;
    }
    const SeqDraft& d = s_stagedDraft;
    // The run buffers are written only here and in the boot scan; the caller
    // (dispatcher task) drains the engine before committing, so this cannot
    // race a running sequence.
    memcpy(s_main, d.steps, sizeof(SeqStep) * d.stepCount);
    const bool hasClose = (d.closeSteps != nullptr && d.closeStepCount > 0);
    if (hasClose) {
        memcpy(s_close, d.closeSteps, sizeof(SeqStep) * d.closeStepCount);
    }
    strncpy(s_runName, d.name, sizeof(s_runName) - 1);
    s_runName[sizeof(s_runName) - 1] = '\0';

    out.name           = s_runName;
    out.steps          = s_main;
    out.stepCount      = d.stepCount;
    out.suppressMs     = d.suppressMs;
    out.toggleGroup    = d.toggleGroup;
    out.closeSteps     = hasClose ? s_close : nullptr;
    out.closeStepCount = hasClose ? d.closeStepCount : 0;

    free(s_staged);
    s_staged = nullptr;
    return true;
}

// -----------------------------------------------------------------------------
// Save
// -----------------------------------------------------------------------------
ProtocolCheckResult seqStoreSave(const char* json, size_t len) {
    // Validate into a transient heap staging pair so a running sequence's run
    // buffers (s_main/s_close) are never disturbed.
    SeqStep* tmp = (SeqStep*)malloc(sizeof(SeqStep) * 192);
    if (tmp == nullptr) {
        return pcFail("json", "out of memory");
    }

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, json, len);
    ProtocolCheckResult r;
    SeqDraft d;
    if (err) {
        r = pcFail("json", err.c_str());
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
        return pcFail("name", "invalid name");
    }
    char file[40];
    const char* base = strrchr(path, '/');
    strncpy(file, base ? base + 1 : path, sizeof(file) - 1);
    file[sizeof(file) - 1] = '\0';

    if (!lock()) {
        free(tmp);
        return pcFail("name", "store busy");
    }

    // Capacity: 16-file cap (new names only), per-file size, free-space floor.
    const bool isNew = (seqStoreIndexFind(d.name) == nullptr);
    const size_t freeBytes = LittleFS.totalBytes() - LittleFS.usedBytes();
    ProtocolCheckResult cap =
        seqStoreCapacityCheck(isNew, seqStoreIndexCount(), len, freeBytes);
    if (!cap.ok) {
        unlock();
        free(tmp);
        return cap;
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
        return pcFail("json", "cannot open temp file");
    }
    size_t wrote = wf.write((const uint8_t*)json, len);
    wf.close();
    if (wrote != len) {
        LittleFS.remove(tmpPath);
        unlock();
        return pcFail("json", "write failed");
    }
    LittleFS.remove(path);  // ignore result; rename needs the slot free
    if (!LittleFS.rename(tmpPath, path)) {
        LittleFS.remove(tmpPath);
        unlock();
        return pcFail("json", "rename failed");
    }

    seqStoreIndexAdd(entry);  // insert or update in place
    unlock();
    return pcOk();
}

// -----------------------------------------------------------------------------
// Delete (Memory Wipe)
// -----------------------------------------------------------------------------
bool seqStoreDelete(const char* name) {
    char path[64];
    if (!nameToPath(name, path, sizeof(path))) return false;

    if (!lock()) return false;
    const bool fileExisted = LittleFS.exists(path);
    if (fileExisted && !LittleFS.remove(path)) {
        // Keep the index entry: the file is still on flash and would be
        // re-indexed at the next boot scan, so reporting success here would
        // let a "deleted" sequence silently resurrect.
        unlock();
        PA_LOG_ERROR(TAG, "Memory Wipe failed: cannot remove %s", path);
        return false;
    }
    const bool removedIdx = seqStoreIndexRemove(name);
    unlock();
    return fileExisted || removedIdx;
}

// -----------------------------------------------------------------------------
// Stream a stored file's raw JSON (GET /api/seq?name=)
// -----------------------------------------------------------------------------
bool seqStoreStreamFile(const char* name, Print& out) {
    if (!lock()) return false;
    const SeqIndexEntry* idx = seqStoreIndexFind(name);
    if (idx == nullptr) {
        unlock();
        return false;
    }
    char path[64];
    snprintf(path, sizeof(path), "%s/%s", SEQ_DIR, idx->file);
    File f = LittleFS.open(path, "r");
    if (!f) {
        unlock();
        return false;
    }
    uint8_t buf[256];
    size_t n;
    while ((n = f.read(buf, sizeof(buf))) > 0) {
        out.write(buf, n);
    }
    f.close();
    unlock();
    return true;
}
