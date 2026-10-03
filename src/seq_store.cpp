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
#include "sequence_dispatcher.h"  // sequenceCatalogFind() - a Factory phrase
#include "sequence_gesture.h"     // seqStepsMayOpenBodyPart() - the index row's flag
#include "take_replay.h"          // a run's takes, staged with its steps (#442)
#include "take_store.h"           // a sequence's takes live and die with it (#442)

static const char* TAG = "SEQST";
static const char* SEQ_DIR = "/seq";

// Run buffers  --  heap-allocated, RIGHT-SIZED to the committed sequence's step
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

// A run's takes (#442 slice 3): read off the sequence's `takes` array when it
// is loaded to run, into the one heap block the Coordinator plays them from
// (include/take_replay.h). Staged with the steps and handed on with them:
// seqStoreCommit() moves the staged block to s_runTakes, and
// seqStoreClaimRunTakes() gives it to the Coordinator, which frees it when the
// run ends. A block nobody claims - a pose loads a sequence only for its
// steps - is freed with the run buffers. The takes a load could not get the
// memory for travel the same way, as a count, and the Coordinator says so: a
// log line here would sit under seqStorePrepare()'s frame, on the deepest
// route the Coordinator has (ADR 0040).
static TakeReplayRun* s_stagedTakes = nullptr;
static TakeReplayRun* s_runTakes    = nullptr;
static SeqStoreTakesUnplayed s_stagedTakesUnplayed = {};
static SeqStoreTakesUnplayed s_runTakesUnplayed    = {};

static void takesFree(TakeReplayRun** takes) {
    free(*takes);
    *takes = nullptr;
}

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

// The stable id a phrase refers to this sequence by (ADR 0046), or "" for one
// that has none. seqJsonParseVariant() has already refused a malformed one.
static void copyStableId(JsonVariantConst root, char* out, size_t cap) {
    out[0] = '\0';
    const char* id = root["id"] | (const char*)nullptr;
    if (id != nullptr && protocolCheckSeqIdValid(id)) {
        strncpy(out, id, cap - 1);
        out[cap - 1] = '\0';
    }
}

// The takes a sequence holds (#442, ADR 0061): an optional top-level array,
//   "takes": [ {"id": "k3f9q2ab", "t": 0}, {"id": "p0x7m2cd", "t": 900, "from": 400, "to": 5200}, ... ]
// each naming one take file of this sequence by its id, and placing it `t` ms
// from the start. A take trimmed on the timeline also says the part of it that
// plays, `from` and `to` in ms into the take, each optional (absent: its start,
// its end; include/take_replay.h, A TRIMMED TAKE). The store does not know a
// take's length, so a `to` past it is the take's end, as the replay reads it.
// The Rehearsal and the timeline read the rest; the store reads
// the ids, so a save keeps the takes named and drops the others
// (takeStoreSequenceSaved()), and a run's load reads the array again for the
// takes to play (stageTakes()). Refused here rather than stored half-read: no
// other check sees this array (the engine and the parser ignore it, and the
// browser's Protocol Check does not read it). A sequence saved before takes
// existed has no array and saves exactly as it did.
// A `takes` entry's millisecond field: whole ms from 0 to INT32_MAX, -1 when
// it is absent and -2 when it is something else.
static long long takeMsField(JsonVariantConst v) {
    if (v.isNull()) return -1;
    const long long ms = v.is<long long>() ? v.as<long long>() : -2;
    return (ms < 0 || ms > 0x7FFFFFFFLL) ? -2 : ms;
}

static ProtocolCheckResult readTakeRefs(JsonVariantConst root, TakeRefs* out) {
    out->count = 0;
    JsonVariantConst takes = root["takes"];
    if (takes.isNull()) return pcOk();
    if (!takes.is<JsonArrayConst>()) return pcFail("takes", "takes must be a list");
    JsonArrayConst arr = takes.as<JsonArrayConst>();
    if (arr.size() > TAKE_STORE_CAP) {
        return pcFail("takes", "more takes than this droid keeps");
    }
    for (JsonVariantConst each : arr) {
        const char* id = each["id"] | (const char*)nullptr;
        if (!each.is<JsonObjectConst>() || !takeIdValid(id)) {
            return pcFail("takes", "a take's id must be 8 lowercase letters or digits");
        }
        if (takeMsField(each["t"]) < 0) {
            return pcFail("takes", "a take's t must be whole milliseconds from the start");
        }
        const long long from = takeMsField(each["from"]);
        const long long to = takeMsField(each["to"]);
        if (from == -2 || to == -2 || to == 0 || (to > 0 && from >= to)) {
            return pcFail("takes", "a take's from and to must be whole milliseconds into it, from before to");
        }
        for (uint8_t k = 0; k < out->count; ++k) {
            if (strcmp(out->ids[k], id) == 0) return pcFail("takes", "a take is named twice");
        }
        memcpy(out->ids[out->count], id, TAKE_ID_LEN + 1);
        ++out->count;
    }
    return pcOk();
}

// `src` added to `into`, each id once. One that does not fit sets *overflow.
static void takeRefsMerge(TakeRefs* into, const TakeRefs& src, bool* overflow) {
    for (uint8_t i = 0; i < src.count; ++i) {
        bool have = false;
        for (uint8_t k = 0; k < into->count && !have; ++k) have = strcmp(into->ids[k], src.ids[i]) == 0;
        if (have) continue;
        if (into->count >= TAKE_STORE_CAP) {
            *overflow = true;
            continue;
        }
        memcpy(into->ids[into->count++], src.ids[i], TAKE_ID_LEN + 1);
    }
}

// Whether an indexed sequence other than `name` carries the stable id `id`.
static bool idSharedWith(const char* id, const char* name) {
    for (uint8_t i = 0; i < seqStoreIndexCount(); ++i) {
        const SeqIndexEntry* e = seqStoreIndexAt(i);
        if (e != nullptr && strcmp(e->id, id) == 0 && strcmp(e->name, name) != 0) return true;
    }
    return false;
}

// -----------------------------------------------------------------------------
// A sequence inside a sequence (ADR 0046)
//
// On SAVE, protocolCheckNesting() walks the phrases a sequence names through
// nestLookup(), which answers from the index and each phrase's own file.
//
// On LOAD - every run and every pose - the phrases are SPLICED into the
// staging: each phrase step becomes its phrase's steps, timed from where the
// phrase step sits, its end step dropped. Loaded then, never at save, so
// editing a phrase changes what every caller does with nothing re-saved. The
// engine therefore runs one flat branch with one cursor: the "stack" ADR 0046
// names is these passes, one per level, bounded by PC_NEST_DEPTH_MAX. Each
// spliced step keeps the effect class its own phrase's Protocol Check stamped,
// so terminal cleanup unions every level's classes.
// -----------------------------------------------------------------------------

// Resolve a reference the way a run of it would: a Learned Sequence first (by
// id, or by name, which is how a Learned one shadows a Factory one), then the
// Factory catalog by name. Fills the path of a Learned one, or the entry of a
// Factory one.
static bool nestResolve(const char* ref, char* path, size_t pathCap, const SequenceEntry** factory) {
    *factory = nullptr;
    const SeqIndexEntry* e = seqStoreIndexFindRef(ref);
    if (e != nullptr) {
        snprintf(path, pathCap, "%s/%s", SEQ_DIR, e->file);
        return true;
    }
    if (strncmp(ref, "DM:", 3) == 0) {
        *factory = sequenceCatalogFind(ref);
        return *factory != nullptr;
    }
    return false;
}

// The save-time lookup (SeqNestLookup). The store's lock is held by the caller.
static void nestLookup(const char* ref, SeqNestInfo* out, void* /*ctx*/) {
    memset(out, 0, sizeof(*out));
    char path[64];
    const SequenceEntry* factory = nullptr;
    if (!nestResolve(ref, path, sizeof(path), &factory)) return;
    if (factory != nullptr) {
        // Factory steps carry no phrases.
        out->found = true;
        out->toggle = factory->toggleGroup != TOGGLE_NONE;
        out->stepCount = factory->stepCount;
        return;
    }
    File f = LittleFS.open(path, "r");
    if (!f) return;
    JsonDocument doc;
    const DeserializationError err = deserializeJson(doc, f);
    f.close();
    if (err) return;
    out->found = true;
    SeqToggleGroup grp = TOGGLE_NONE;
    out->toggle = seqToggleGroupFromString(doc["toggleGroup"] | "none", grp) && grp != TOGGLE_NONE;
    JsonArrayConst steps = doc["steps"].as<JsonArrayConst>();
    out->stepCount = (uint8_t)((steps.size() > 255) ? 255 : steps.size());
    for (JsonObjectConst step : steps) {
        const char* type = step["type"] | "";
        const char* phrase = step["ref"] | (const char*)nullptr;
        if (strcmp(type, "sequence") == 0 && phrase != nullptr && out->refCount < PC_NEST_REFS_MAX) {
            strncpy(out->refs[out->refCount], phrase, PC_SEQ_REF_MAX);
            out->refs[out->refCount][PC_SEQ_REF_MAX] = '\0';
            out->refCount++;
        }
    }
}

// The index entry of a parsed draft. The one place an entry is filled - a save
// and the boot scan both come through here - so no writer can leave a list
// row a field behind. Called while the draft's step buffers are alive: the
// step count and the length are read from them.
static void fillIndexEntry(SeqIndexEntry& e, const SeqDraft& d, JsonVariantConst root,
                           const char* file, bool valid) {
    e = {};
    strncpy(e.name, d.name, sizeof(e.name) - 1);
    e.toggleGroup = d.toggleGroup;
    e.suppressMs  = d.suppressMs;
    const char* src = root["meta"]["source"] | "user";
    strncpy(e.source, src, sizeof(e.source) - 1);
    e.modified = root["meta"]["modified"] | false;
    strncpy(e.file, file, sizeof(e.file) - 1);
    copyStableId(root, e.id, sizeof(e.id));
    e.valid = valid;
    e.steps = d.stepCount;
    e.lengthMs = seqStoreRunLengthMs(d.steps, d.stepCount);
    e.purposeCut = seqStoreCutPurpose(root["meta"]["purpose"] | (const char*)nullptr,
                                      e.purpose, sizeof(e.purpose));
    e.mayOpenBody = seqStepsMayOpenBodyPart(d.steps, d.stepCount) ||
                    seqStepsMayOpenBodyPart(d.closeSteps, d.closeStepCount);
}

bool seqStoreMayOpenBodyPart(const char* name) {
    if (name == nullptr || name[0] == '\0') {
        return false;
    }
    if (s_mutex == nullptr || xSemaphoreTake(s_mutex, 0) != pdTRUE) {
        return true;  // not ready or busy: fail closed
    }
    const SeqIndexEntry* learned = seqStoreIndexFind(name);
    const bool isLearned = learned != nullptr;
    const bool learnedMay = isLearned && learned->mayOpenBody;
    xSemaphoreGive(s_mutex);
    if (isLearned) {
        return learnedMay;
    }
    // The Factory catalog is constant data: no lock.
    if (const SequenceEntry* factory = sequenceCatalogFind(name)) {
        return seqStepsMayOpenBodyPart(factory->steps, factory->stepCount) ||
               seqStepsMayOpenBodyPart(factory->closeSteps, factory->closeStepCount);
    }
    return false;
}

// -----------------------------------------------------------------------------
// Init  --  scan + index
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

    // The takes each sequence names, gathered while the directory is open and
    // settled once it is closed (a walk never edits the directory it reads).
    // Boot only, from the heap, and given back before this returns.
    struct BootTakes {
        char owner[PC_SEQ_ID_MAX + 1];
        TakeRefs refs;
        bool overflow;  // the union outgrew TakeRefs: delete nothing of this id
    };
    BootTakes* bootTakes = (BootTakes*)calloc(SEQ_INDEX_CAPACITY, sizeof(BootTakes));
    uint8_t bootTakeCount = 0;

    uint8_t indexed = 0, skipped = 0;
    for (File f = dir.openNextFile(); f; f = dir.openNextFile()) {
        if (f.isDirectory()) { f.close(); continue; }
        // Basename for the index entry (storage is /seq/<file>).
        const char* nm = f.name();
        const char* base = strrchr(nm, '/');
        char file[40];
        strncpy(file, base ? base + 1 : nm, sizeof(file) - 1);
        file[sizeof(file) - 1] = '\0';
        // A take's file, or a take written half way: not a sequence, and
        // the take store's to settle (takeStoreInit() below).
        TakeFileNameParts take;
        if (takeFileNameParse(file, &take) || strcmp(file, ".tmp.take") == 0) {
            f.close();
            continue;
        }

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
        if (!parseResult.ok) {
            // Unreadable format  --  cannot extract reliable metadata; skip entirely.
            stagingFree(st);
            PA_LOG_WARN(TAG, "skip %s: %s (%s)", file, parseResult.message, parseResult.field);
            ++skipped;
            continue;
        }
        // Protocol Check and the index entry both read the staged steps, so
        // both come before the staging is released; nothing below needs the
        // draft again. A file that parses but fails the current contract
        // (e.g. legacy :SM usage) is indexed as invalid, so the UI can surface
        // it for repair/export/delete.
        ProtocolCheckResult checkResult = protocolCheck(d);
        SeqIndexEntry entry;
        fillIndexEntry(entry, d, root, file, checkResult.ok);
        stagingFree(st);
        // One record per id, its refs the union of every file carrying that
        // id: a rename leaves two files sharing one, and reconciling each on
        // its own would delete the takes the other names.
        TakeRefs refs;
        if (bootTakes != nullptr && entry.id[0] != '\0' && readTakeRefs(root, &refs).ok) {
            uint8_t at = 0;
            while (at < bootTakeCount && strcmp(bootTakes[at].owner, entry.id) != 0) ++at;
            if (at == bootTakeCount && bootTakeCount < SEQ_INDEX_CAPACITY) {
                memcpy(bootTakes[at].owner, entry.id, sizeof(bootTakes[at].owner));
                ++bootTakeCount;
            }
            if (at < bootTakeCount) {
                takeRefsMerge(&bootTakes[at].refs, refs, &bootTakes[at].overflow);
            }
        }

        if (!checkResult.ok) {
            PA_LOG_WARN(TAG, "index invalid %s: %s (%s)", file,
                        checkResult.message, checkResult.field);
        }
        if (!seqStoreIndexAdd(entry)) {
            PA_LOG_WARN(TAG, "index full at %s%s", file, checkResult.ok ? "" : " (invalid)");
            break;
        }
        ++indexed;
    }
    dir.close();
    PA_LOG_INFO(TAG, "indexed %u Learned Sequence(s), skipped %u", indexed, skipped);

    // Each sequence's takes as its file names them - a save cut short after
    // the sequence was written and before its takes were settled is finished
    // here - then whatever no sequence holds goes (takeStoreInit()).
    if (bootTakes == nullptr) {
        PA_LOG_WARN(TAG, "no memory to read the sequences' takes; takes not saved into a sequence are removed");
    }
    for (uint8_t i = 0; i < bootTakeCount; ++i) {
        takeStoreSequenceSaved(bootTakes[i].owner, bootTakes[i].refs, !bootTakes[i].overflow);
    }
    free(bootTakes);
    takeStoreInit();
}

// -----------------------------------------------------------------------------
// Load (dispatcher run path)  --  two-phase: prepare into heap, commit to buffers
// -----------------------------------------------------------------------------

// The takes `root` names, staged for its run, or nullptr when it names none or
// has no stable id to find their files by. The array was checked when the
// sequence was saved (readTakeRefs()); an entry this read cannot use is passed
// over rather than costing the run. Takes past what this board keeps, and a
// run that cannot have the memory, are counted for the Coordinator to report,
// and the run plays its steps. Out of line, and it logs nothing: seqStorePrepare()'s frame heads
// the Coordinator's deepest route (ADR 0040), and this one sits beside the
// parse under it, never on top of it.
static __attribute__((noinline)) TakeReplayRun* stageTakes(JsonVariantConst root) {
    char owner[sizeof(TakeReplayRun::owner)];
    copyStableId(root, owner, sizeof(owner));
    JsonArrayConst arr = root["takes"].as<JsonArrayConst>();
    const size_t named = arr.size();
    if (named == 0 || owner[0] == '\0') return nullptr;
    const uint8_t cap = (named < TAKE_STORE_CAP) ? (uint8_t)named : TAKE_STORE_CAP;
    const size_t over = named - cap;
    s_stagedTakesUnplayed.overCap = (over > 0xFF) ? (uint8_t)0xFF : (uint8_t)over;
    TakeReplayRun* run = (TakeReplayRun*)calloc(1, takeReplayRunBytes(cap));
    if (run == nullptr) {
        s_stagedTakesUnplayed.noMemory = cap;
        return nullptr;
    }
    memcpy(run->owner, owner, sizeof(owner));
    for (JsonVariantConst each : arr) {
        const char* id = each["id"] | (const char*)nullptr;
        const long long at = takeMsField(each["t"]);
        const long long from = takeMsField(each["from"]);
        const long long to = takeMsField(each["to"]);
        if (run->count >= cap || !takeIdValid(id) || at < 0 || from == -2 || to == -2) continue;
        TakeReplay* take = takeReplayAt(run, run->count++);
        memcpy(take->id, id, TAKE_ID_LEN + 1);
        take->atMs = (uint32_t)at;
        take->fromMs = (from < 0) ? 0u : (uint32_t)from;
        take->toMs = (to < 0) ? TAKE_REPLAY_WHOLE : (uint32_t)to;
    }
    if (run->count == 0) takesFree(&run);
    return run;
}

ProtocolCheckResult seqStorePrepare(const char* name) {
    if (s_staging.main != nullptr) {  // a previous prepare was never committed
        stagingFree(s_staging);
    }
    takesFree(&s_stagedTakes);
    s_stagedTakesUnplayed = {};
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
    if (!r.ok) {
        unlock();
        stagingFree(st);
        return r;
    }
    // The parent goes straight into the statics, so this frame's locals - the
    // document, the file, the draft and the staging - are reused for each
    // phrase rather than doubled: the frame sits on the Sequence
    // Coordinator's measured chain (ADR 0040).
    s_staging = st;
    s_stagedDraft = d;
    // Read before the phrases are spliced in: the splice reuses `doc` for each
    // phrase, and a phrase's takes are not the run's - only its steps are.
    s_stagedTakes = stageTakes(root);

    // Splice the phrases in, one level per pass (see "A sequence inside a
    // sequence" above). A phrase that is gone, or no longer passes Protocol
    // Check, is reported and left out, and the rest runs; one past the depth
    // bound likewise. A run that would not fit is refused.
    for (uint8_t branch = 0; branch < 2 && r.ok; ++branch) {
        SeqStep** buf = (branch == 0) ? &s_staging.main : &s_staging.close;
        uint8_t* count = (branch == 0) ? &s_stagedDraft.stepCount : &s_stagedDraft.closeStepCount;
        const bool fits = seqStoreSplicePhrases(
            buf, count,
            [&](const char* ref, bool deep, const SeqStep** child, uint8_t* childCount) {
                const SequenceEntry* factory = nullptr;
                const bool found = !deep && nestResolve(ref, path, sizeof(path), &factory);
                st = SeqStaging();
                if (factory != nullptr) {
                    *child = factory->steps;
                    *childCount = factory->stepCount;
                } else if (found) {
                    f = LittleFS.open(path, "r");
                    doc.clear();
                    err = f ? deserializeJson(doc, f) : DeserializationError(DeserializationError::InvalidInput);
                    if (f) f.close();
                    root = doc.as<JsonVariantConst>();
                    if (!err && stagingAlloc(root, st)) {
                        ProtocolCheckResult cr =
                            seqJsonParseVariant(root, st.main, st.mainCap, st.close, st.closeCap, d);
                        if (cr.ok) cr = protocolCheck(d);  // stamps the phrase's own classes
                        if (cr.ok) {
                            *child = d.steps;
                            *childCount = d.stepCount;
                        }
                    }
                }
                if (*child == nullptr) {
                    PA_LOG_WARN(TAG, "%s: sequence %s left out - %s", name, ref,
                                deep ? "nested deeper than 3" : "not on this droid, or it no longer checks");
                }
            },
            [&]() { stagingFree(st); });
        if (!fits) r = pcFail("steps", "too many steps once its sequences are inside, or out of memory");
    }
    s_stagedDraft.steps = s_staging.main;
    s_stagedDraft.closeSteps = s_staging.close;
    unlock();
    if (!r.ok) {
        stagingFree(s_staging);
        takesFree(&s_stagedTakes);
        return r;
    }
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
    // the run gracefully  --  the staging is freed and the caller does not start it.
    seqStoreReleaseRun();
    s_runMain = (SeqStep*)malloc(sizeof(SeqStep) * d.stepCount);
    if (s_runMain == nullptr) {
        PA_LOG_WARN(TAG, "run buffer alloc failed (%u steps, ~%u bytes); run refused",
                    (unsigned)d.stepCount, (unsigned)(sizeof(SeqStep) * d.stepCount));
        stagingFree(s_staging);
        takesFree(&s_stagedTakes);
        return false;
    }
    if (hasClose) {
        s_runClose = (SeqStep*)malloc(sizeof(SeqStep) * d.closeStepCount);
        if (s_runClose == nullptr) {
            PA_LOG_WARN(TAG, "close-branch alloc failed; run refused");
            seqStoreReleaseRun();  // frees s_runMain
            stagingFree(s_staging);
            takesFree(&s_stagedTakes);
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

    s_runTakes = s_stagedTakes;  // released above, so nothing is lost here
    s_stagedTakes = nullptr;
    s_runTakesUnplayed = s_stagedTakesUnplayed;
    stagingFree(s_staging);
    return true;
}

TakeReplayRun* seqStoreClaimRunTakes(SeqStoreTakesUnplayed* unplayed) {
    TakeReplayRun* takes = s_runTakes;
    s_runTakes = nullptr;
    *unplayed = s_runTakesUnplayed;
    s_runTakesUnplayed = {};
    return takes;
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
    takesFree(&s_runTakes);
    s_runTakesUnplayed = {};
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

    // The takes it names, and the id that owns them: a take file is named by
    // its sequence's stable id, so a sequence with takes must carry one.
    TakeRefs takeRefs;
    char selfId[PC_SEQ_ID_MAX + 1];
    copyStableId(root, selfId, sizeof(selfId));
    r = readTakeRefs(root, &takeRefs);
    if (r.ok && takeRefs.count > 0 && selfId[0] == '\0') {
        r = pcFail("takes", "a sequence holding takes needs its id");
    }
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

    // A sequence holding sequences: each exists and is not a toggle, nothing
    // reaches back round to this one, no path is too deep, and the whole run
    // fits (ADR 0046). Checked under the lock, against the files as they are.
    {
        ProtocolCheckResult nest = protocolCheckNesting(d, selfId, d.name, nestLookup, nullptr);
        if (!nest.ok) {
            unlock();
            stagingFree(st);
            return nest;
        }
    }

    // Capacity: the board's store cap (new names only), per-file size,
    // free-space floor -- seqStoreCapacityCheck(), include/seq_store_util.h.
    const bool isNew = (seqStoreIndexFind(d.name) == nullptr);
    const size_t freeBytes = LittleFS.totalBytes() - LittleFS.usedBytes();
    ProtocolCheckResult cap =
        seqStoreCapacityCheck(isNew, seqStoreIndexCount(), len, freeBytes);
    if (!cap.ok) {
        unlock();
        stagingFree(st);
        return cap;
    }

    // Capture the index entry before we drop the transient buffer; then write
    // temp+rename. Nothing reaches this point without passing Protocol Check
    // above, so the entry is valid: indexing a just-validated sequence as
    // needing repair would stand until the next boot scan re-read it from
    // flash and corrected the flag.
    SeqIndexEntry entry;
    fillIndexEntry(entry, d, root, file, true);
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

    // Its takes as it names them now: those kept, any other of its own gone -
    // unless another sequence carries the same id (a rename is a save under a
    // new name and leaves the old file), when the others may be that one's
    // and only this file's are promoted. After the lock, never inside it
    // (include/take_store.h).
    if (selfId[0] != '\0') {
        takeStoreSequenceSaved(selfId, takeRefs, !idSharedWith(selfId, d.name));
    }
    return pcOk();
}

// -----------------------------------------------------------------------------
// Delete (Memory Wipe)
// -----------------------------------------------------------------------------
bool seqStoreDelete(const char* name) {
    char path[64];
    if (!nameToPath(name, path, sizeof(path))) return false;

    if (!lock()) return false;
    char ownerId[PC_SEQ_ID_MAX + 1] = {};
    if (const SeqIndexEntry* e = seqStoreIndexFind(name)) {
        memcpy(ownerId, e->id, sizeof(ownerId));
    }
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
    // Its takes go with it - unless another sequence still carries the same
    // id (one saved under a new name keeps its id), whose takes they are too.
    const bool idStillHeld = ownerId[0] == '\0' || seqStoreIndexFindRef(ownerId) != nullptr;
    unlock();
    if (!idStillHeld) {
        takeStoreSequenceDeleted(ownerId);
    }
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
