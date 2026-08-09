// =============================================================================
// src/seq_store.cpp
//
// Learned Sequence runtime store - LittleFS I/O (ADR 0006).
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

// Run buffers — heap-allocated, RIGHT-SIZED to the committed sequence's step
// counts, and freed when the run ends (seqStoreReleaseRun) so an idle body or a
// Factory-only run (Factory steps live in flash) holds ZERO staging RAM. This
// reclaims the former fixed 2 x 96 x sizeof(SeqStep) (~17 KB) static block on a
// heap-constrained board; a 96-step Learned sequence still works but only
// allocates what it uses, and a failed allocation refuses the run gracefully
// (issue #8). Learned Sequences are a minor feature with typically small/few
// uses, so the steady-state win is large. Only the dispatcher task touches these.
static SeqStep* s_runMain  = nullptr;
static SeqStep* s_runClose = nullptr;

// Two-phase load staging (dispatcher task only). seqStorePrepare() parses and
// validates into this transient heap pair; seqStoreCommit() copies it into the
// run buffers once the previous run is drained. s_runName gives the running
// entry a name whose lifetime does not depend on the (mutable) index.
//
// s_staging.main is the "something is staged" sentinel: a staging that survived
// Protocol Check always has at least one main step, because protocolCheck()
// rejects an empty steps branch.
static SeqDraft s_stagedDraft;  // step pointers into s_staging
static char     s_runName[24];

static SemaphoreHandle_t s_mutex = nullptr;

// -----------------------------------------------------------------------------
// Parse staging
//
// Every parse needs SeqStep buffers for the main and close branches. They are
// sized to the payload's own step counts (seqJsonStagingCaps) and taken as two
// separate blocks, so the store never asks the heap for the 96+96-step worst
// case in one piece. That single 18432-byte request exceeded the largest
// contiguous 8-bit block the controller could offer, which failed every save
// regardless of payload size. A typical Learned Sequence now stages
// a few hundred bytes, and the 96-step worst case is two 9216-byte blocks.
// -----------------------------------------------------------------------------
struct SeqStaging {
    SeqStep* main     = nullptr;
    SeqStep* close    = nullptr;
    uint8_t  mainCap  = 0;
    uint8_t  closeCap = 0;
};

static SeqStaging s_staging;

static void stagingFree(SeqStaging& st) {
    free(st.main);
    free(st.close);
    st = SeqStaging();
}

// Allocate staging for `root`. A branch that is empty, missing, or not an array
// gets a null buffer and a zero cap; seqJsonParseVariant()/protocolCheck()
// already report that as a field error rather than dereferencing it. Returns
// false only on a genuine allocation failure, leaving `st` empty.
static bool stagingAlloc(JsonVariantConst root, SeqStaging& st) {
    st = SeqStaging();
    seqJsonStagingCaps(root, st.mainCap, st.closeCap);
    if (st.mainCap > 0) {
        st.main = (SeqStep*)malloc(sizeof(SeqStep) * st.mainCap);
        if (st.main == nullptr) {
            stagingFree(st);
            return false;
        }
    }
    if (st.closeCap > 0) {
        st.close = (SeqStep*)malloc(sizeof(SeqStep) * st.closeCap);
        if (st.close == nullptr) {
            stagingFree(st);
            return false;
        }
    }
    return true;
}

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
    e.valid = true;
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
        JsonVariantConst root = doc.as<JsonVariantConst>();

        // Transient validation staging, one file at a time. Sized to this
        // file's own step counts and released before the next file, so the boot
        // scan's peak is the largest single sequence rather than the worst case,
        // and nothing survives into the dispatcher's lifetime.
        SeqStaging st;
        if (!stagingAlloc(root, st)) {
            PA_LOG_WARN(TAG, "skip %s: staging alloc failed", file);
            ++skipped;
            continue;
        }
        ProtocolCheckResult parseResult =
            seqJsonParseVariant(root, st.main, st.mainCap, st.close, st.closeCap, d);
        // Protocol Check reads the staged steps, so it runs before the staging
        // is released. Everything below this point needs only the draft's
        // metadata, so the step pointers are cleared with the buffers they
        // addressed rather than left dangling.
        ProtocolCheckResult checkResult = parseResult.ok ? protocolCheck(d) : parseResult;
        stagingFree(st);
        d.steps = nullptr;
        d.stepCount = 0;
        d.closeSteps = nullptr;
        d.closeStepCount = 0;

        if (!parseResult.ok) {
            // Unreadable format — cannot extract reliable metadata; skip entirely.
            PA_LOG_WARN(TAG, "skip %s: %s (%s)", file, parseResult.message, parseResult.field);
            ++skipped;
            continue;
        }
        if (!checkResult.ok) {
            // Parseable but fails current contract (e.g. legacy :SM usage).
            // Index as invalid so the UI can surface it for repair/export/delete.
            PA_LOG_WARN(TAG, "index invalid %s: %s (%s)", file,
                        checkResult.message, checkResult.field);
            SeqIndexEntry inv = {};
            strncpy(inv.name, d.name, sizeof(inv.name) - 1);
            inv.toggleGroup = d.toggleGroup;
            inv.suppressMs  = d.suppressMs;
            const char* src = root["meta"]["source"] | "user";
            strncpy(inv.source, src, sizeof(inv.source) - 1);
            inv.modified = root["meta"]["modified"] | false;
            strncpy(inv.file, file, sizeof(inv.file) - 1);
            inv.valid = false;
            if (!seqStoreIndexAdd(inv)) {
                PA_LOG_WARN(TAG, "index full at %s (invalid)", file);
                break;
            }
            ++indexed;
            continue;
        }
        if (!indexDraft(d, root, file)) {
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
    if (s_staging.main != nullptr) {  // a previous prepare was never committed
        stagingFree(s_staging);
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

    JsonVariantConst root = doc.as<JsonVariantConst>();
    SeqStaging st;
    if (!stagingAlloc(root, st)) {
        unlock();
        return pcFail("json", "out of memory");
    }
    SeqDraft d;
    ProtocolCheckResult r =
        seqJsonParseVariant(root, st.main, st.mainCap, st.close, st.closeCap, d);
    if (r.ok) r = protocolCheck(d);  // stamps effectClass into the staging
    unlock();
    if (!r.ok) {
        stagingFree(st);
        return r;
    }
    s_staging = st;
    s_stagedDraft = d;
    return pcOk();
}

bool seqStoreCommit(SequenceEntry& out) {
    if (s_staging.main == nullptr) {
        return false;
    }
    const SeqDraft& d = s_stagedDraft;
    const bool hasClose = (d.closeSteps != nullptr && d.closeStepCount > 0);

    // Free the previous run's buffers (the caller drained that run before
    // committing a new one) and allocate fresh, RIGHT-SIZED buffers for this
    // sequence. Only the dispatcher task runs commit/run/release, so the engine
    // cannot be reading these while we reallocate. A failed allocation refuses
    // the run gracefully — the staging is freed and the caller does not start it.
    seqStoreReleaseRun();
    s_runMain = (SeqStep*)malloc(sizeof(SeqStep) * d.stepCount);
    if (s_runMain == nullptr) {
        PA_LOG_WARN(TAG, "run buffer alloc failed (%u steps, ~%u bytes); run refused",
                    (unsigned)d.stepCount, (unsigned)(sizeof(SeqStep) * d.stepCount));
        stagingFree(s_staging);
        return false;
    }
    if (hasClose) {
        s_runClose = (SeqStep*)malloc(sizeof(SeqStep) * d.closeStepCount);
        if (s_runClose == nullptr) {
            PA_LOG_WARN(TAG, "close-branch alloc failed; run refused");
            seqStoreReleaseRun();  // frees s_runMain
            stagingFree(s_staging);
            return false;
        }
    }

    memcpy(s_runMain, d.steps, sizeof(SeqStep) * d.stepCount);
    if (hasClose) {
        memcpy(s_runClose, d.closeSteps, sizeof(SeqStep) * d.closeStepCount);
    }
    strncpy(s_runName, d.name, sizeof(s_runName) - 1);
    s_runName[sizeof(s_runName) - 1] = '\0';

    out.name           = s_runName;
    out.steps          = s_runMain;
    out.stepCount      = d.stepCount;
    out.suppressMs     = d.suppressMs;
    out.toggleGroup    = d.toggleGroup;
    out.closeSteps     = hasClose ? s_runClose : nullptr;
    out.closeStepCount = hasClose ? d.closeStepCount : 0;

    stagingFree(s_staging);
    return true;
}

// Free the run buffers once a Learned Sequence run has fully drained (the
// dispatcher calls this at sequence end / abort). No-op when idle or after a
// Factory run (Factory steps live in flash, so these stay nullptr). Safe to
// call repeatedly. Only the dispatcher task calls this.
void seqStoreReleaseRun() {
    free(s_runMain);
    s_runMain = nullptr;
    free(s_runClose);
    s_runClose = nullptr;
}

// -----------------------------------------------------------------------------
// Save
// -----------------------------------------------------------------------------
ProtocolCheckResult seqStoreSave(const char* json, size_t len) {
    // Deserialize first: the staging is sized from the payload's own step
    // counts, so there is nothing to allocate until the JSON is readable.
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, json, len);
    if (err) {
        return pcFail("json", err.c_str());
    }
    JsonVariantConst root = doc.as<JsonVariantConst>();

    // Validate into a transient heap staging pair so a running sequence's run
    // buffers (s_main/s_close) are never disturbed.
    SeqStaging st;
    if (!stagingAlloc(root, st)) {
        return pcFail("json", "out of memory");
    }
    SeqDraft d;
    ProtocolCheckResult r =
        seqJsonParseVariant(root, st.main, st.mainCap, st.close, st.closeCap, d);
    if (r.ok) r = protocolCheck(d);
    if (!r.ok) {
        stagingFree(st);
        return r;
    }

    char path[64];
    if (!nameToPath(d.name, path, sizeof(path))) {
        stagingFree(st);
        return pcFail("name", "invalid name");
    }
    char file[40];
    const char* base = strrchr(path, '/');
    strncpy(file, base ? base + 1 : path, sizeof(file) - 1);
    file[sizeof(file) - 1] = '\0';

    if (!lock()) {
        stagingFree(st);
        return pcFail("name", "store busy");
    }

    // Capacity: 16-file cap (new names only), per-file size, free-space floor.
    const bool isNew = (seqStoreIndexFind(d.name) == nullptr);
    const size_t freeBytes = LittleFS.totalBytes() - LittleFS.usedBytes();
    ProtocolCheckResult cap =
        seqStoreCapacityCheck(isNew, seqStoreIndexCount(), len, freeBytes);
    if (!cap.ok) {
        unlock();
        stagingFree(st);
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
    // Nothing reaches this point without passing Protocol Check above, so the
    // entry is valid. Leaving the zero-initialised default would index a
    // just-validated sequence as needing repair until the next boot scan
    // re-read it from flash and corrected the flag.
    entry.valid = true;
    stagingFree(st);

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
// Read one slice of a stored file's raw JSON (GET /api/seq?name=)
// -----------------------------------------------------------------------------
size_t seqStoreReadFileSlice(const char* name, size_t offset, uint8_t* out, size_t capacity) {
    if (out == nullptr || capacity == 0) return 0;
    if (!lock()) return 0;
    const SeqIndexEntry* idx = seqStoreIndexFind(name);
    if (idx == nullptr) {
        unlock();
        return 0;
    }
    char path[64];
    snprintf(path, sizeof(path), "%s/%s", SEQ_DIR, idx->file);
    File f = LittleFS.open(path, "r");
    if (!f) {
        unlock();
        return 0;
    }
    size_t read = 0;
    // A seek past the end is the normal way this ends: the caller walks the
    // offset forward until a slice comes back empty.
    if (f.seek(offset)) {
        const int n = f.read(out, capacity);
        if (n > 0) {
            read = (size_t)n;
        }
    }
    f.close();
    unlock();
    return read;
}
