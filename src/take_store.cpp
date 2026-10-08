// =============================================================================
// src/take_store.cpp
//
// The take store - LittleFS I/O for take files (#442, ADR 0061). Firmware-only;
// the contract is include/take_store.h.
//
// No table in RAM: the directory is the record. A take's owner, its id and
// whether its sequence has been saved naming it are all in its file name
// (take_store_util.h), so every question here is a walk of /seq, which holds
// at most ten sequences and the board's takes. A walk never edits the
// directory it is reading: a find returns the first match with the directory
// closed, the caller acts on it, and the next find starts over.
// =============================================================================

#include "take_store.h"

#include <Arduino.h>
#include <LittleFS.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <stdio.h>
#include <string.h>

#include <new>

#include "logging.h"
#include "seq_store_index.h"  // seqStoreIndexFindRef() - a take's owner by stable id

static const char* TAG = "TAKEST";
static const char* SEQ_DIR = "/seq";
static const char* TMP_PATH = "/seq/.tmp.take";

static SemaphoreHandle_t s_mutex = nullptr;

// Created on first use: the first use is seqStoreInit() at boot, before any
// task that could race it exists.
static bool lock() {
    if (s_mutex == nullptr) {
        s_mutex = xSemaphoreCreateMutex();
        if (s_mutex == nullptr) return false;
    }
    return xSemaphoreTake(s_mutex, pdMS_TO_TICKS(1000)) == pdTRUE;
}
static void unlock() {
    if (s_mutex != nullptr) xSemaphoreGive(s_mutex);
}

static bool takePath(const TakeFileNameParts& t, bool kept, char* out, size_t cap) {
    char name[TAKE_FILE_NAME_MAX];
    if (!takeFileName(t.owner, t.take, kept, name, sizeof(name))) return false;
    const int n = snprintf(out, cap, "%s/%s", SEQ_DIR, name);
    return n > 0 && (size_t)n < cap;
}

// The takes in /seq that `match` accepts: how many, or - given `first` - the
// first of them, the walk stopping there, with the directory closed again.
// A plain function and a context rather than a template: one walk in the
// image, not one per question (the artoo-esp32's flash is the scarcer budget).
using TakeMatch = bool (*)(const TakeFileNameParts& take, const void* ctx);

static uint8_t walkTakes(TakeMatch match, const void* ctx, TakeFileNameParts* first) {
    File dir = LittleFS.open(SEQ_DIR);
    if (!dir || !dir.isDirectory()) return 0;
    uint8_t n = 0;
    for (File f = dir.openNextFile(); f; f = dir.openNextFile()) {
        const bool isDir = f.isDirectory();
        const char* nm = f.name();
        const char* base = strrchr(nm, '/');
        TakeFileNameParts t;
        const bool hit = !isDir && takeFileNameParse(base ? base + 1 : nm, &t) && match(t, ctx);
        f.close();
        if (!hit) continue;
        if (n < 0xFF) ++n;
        if (first != nullptr) {
            *first = t;
            break;
        }
    }
    dir.close();
    return n;
}

static bool findTake(TakeMatch match, const void* ctx, TakeFileNameParts* out) {
    return walkTakes(match, ctx, out) > 0;
}

static bool anyTake(const TakeFileNameParts&, const void*) { return true; }
static bool unsavedTake(const TakeFileNameParts& t, const void*) { return !t.kept; }
static bool ownedBy(const TakeFileNameParts& t, const void* owner) {
    return strcmp(t.owner, (const char*)owner) == 0;
}

static uint8_t countTakes() {
    return walkTakes(anyTake, nullptr, nullptr);
}

static bool removeTake(const TakeFileNameParts& t) {
    char path[64];
    if (!takePath(t, t.kept, path, sizeof(path))) return false;
    if (!LittleFS.remove(path)) {
        PA_LOG_ERROR(TAG, "cannot remove %s", path);
        return false;
    }
    return true;
}

static size_t freeBytes() {
    const size_t total = LittleFS.totalBytes();
    const size_t used = LittleFS.usedBytes();
    return (total > used) ? total - used : 0;
}

static bool refsName(const TakeRefs& refs, const char* take) {
    for (uint8_t i = 0; i < refs.count; ++i) {
        if (strcmp(refs.ids[i], take) == 0) return true;
    }
    return false;
}

// The refusal for a store with every slot taken by a kept take: which sequence
// holds it, so the builder knows where to go and take it out.
static const char* fullRefusal(char* out, size_t cap) {
    TakeFileNameParts held;
    const SeqIndexEntry* holder = nullptr;
    if (TAKE_STORE_CAP == 1 && findTake(anyTake, nullptr, &held)) {
        holder = seqStoreIndexFindRef(held.owner);
    }
    if (holder != nullptr) {
        snprintf(out, cap, "This droid keeps one take, and %s holds it.", holder->name);
    } else {
        snprintf(out, cap, "This droid keeps %u takes, and every one is in a sequence.",
                 (unsigned)TAKE_STORE_CAP);
    }
    return out;
}

static const char* noSpaceRefusal(char* out, size_t cap) {
    snprintf(out, cap, "Not enough free space on the droid to keep a take.");
    return out;
}

// -----------------------------------------------------------------------------
// Boot
// -----------------------------------------------------------------------------
void takeStoreInit() {
    if (!lock()) return;
    // A take written half way - power lost mid-write, or an upload that never
    // finished - holds a take's blocks under a name nothing else removes, and
    // on the artoo-esp32 that is the room its one take needs.
    if (LittleFS.exists(TMP_PATH) && !LittleFS.remove(TMP_PATH)) {
        PA_LOG_ERROR(TAG, "cannot remove %s", TMP_PATH);
    }
    uint8_t removed = 0;
    TakeFileNameParts t;
    // A ".new" take is a performance its builder never saved into a sequence,
    // and a take whose owner is not indexed belongs to a sequence that is gone
    // (or that failed to parse, which the boot scan skips the same way).
    while (findTake(
        [](const TakeFileNameParts& each, const void*) {
            return !each.kept || seqStoreIndexFindRef(each.owner) == nullptr;
        },
        nullptr, &t)) {
        if (!removeTake(t)) break;
        ++removed;
    }
    const uint8_t held = countTakes();
    unlock();
    PA_LOG_INFO(TAG, "%u take(s) kept, %u removed at boot", (unsigned)held, (unsigned)removed);
}

uint8_t takeStoreHeld() {
    if (!lock()) return 0;
    const uint8_t n = countTakes();
    unlock();
    return n;
}

// -----------------------------------------------------------------------------
// Room
// -----------------------------------------------------------------------------
const char* takeStoreRoomRefusal(char* out, size_t cap) {
    if (!lock()) {
        snprintf(out, cap, "The droid's storage is busy. Try again.");
        return out;
    }
    const char* refusal = nullptr;
    TakeFileNameParts unsaved;
    if (countTakes() >= TAKE_STORE_CAP) {
        // A take not yet saved into its sequence will make way at the keep,
        // and frees its own blocks when it does.
        if (!findTake(unsavedTake, nullptr, &unsaved)) {
            // Asked when a take is armed, so it names the way to free one.
            refusal = fullRefusal(out, cap);
            strncat(out, " Remove a take on its sequence's Sequence tab and save it.",
                    cap - strlen(out) - 1);
        }
    } else if (freeBytes() < TAKE_FS_FREE_FLOOR + TAKE_FILE_MAX_BYTES) {
        refusal = noSpaceRefusal(out, cap);
    }
    unlock();
    return refusal;
}

// -----------------------------------------------------------------------------
// Keep a performance
// -----------------------------------------------------------------------------

// Eight lowercase letters and digits, not already a take of `owner`.
static bool mintTakeId(const char* owner, char* out) {
    static const char kAlphabet[] = "abcdefghijklmnopqrstuvwxyz0123456789";
    for (uint8_t attempt = 0; attempt < 8; ++attempt) {
        for (size_t i = 0; i < TAKE_ID_LEN; ++i) {
            out[i] = kAlphabet[esp_random() % (sizeof(kAlphabet) - 1)];
        }
        out[TAKE_ID_LEN] = '\0';
        TakeFileNameParts t = {};
        strncpy(t.owner, owner, sizeof(t.owner) - 1);
        memcpy(t.take, out, TAKE_ID_LEN + 1);
        char kept[64];
        char fresh[64];
        if (takePath(t, true, kept, sizeof(kept)) && takePath(t, false, fresh, sizeof(fresh)) &&
            !LittleFS.exists(kept) && !LittleFS.exists(fresh)) {
            return true;
        }
    }
    return false;
}

const char* takeStoreWriteNew(const char* owner, const TakeCapture& capture, char* idOut,
                              char* replacedOut, char* refusal, size_t refusalCap) {
    replacedOut[0] = '\0';
    idOut[0] = '\0';
    const size_t headerBytes = takeFileHeaderBytes(capture.partCount);
    const size_t sampleBytes = (size_t)capture.sampleCount * TAKE_SAMPLE_BYTES;
    if (capture.partCount == 0 || capture.sampleCount == 0 ||
        headerBytes + sampleBytes > TAKE_FILE_MAX_BYTES) {
        snprintf(refusal, refusalCap, "The take is empty.");
        return refusal;
    }
    if (!lock()) {
        snprintf(refusal, refusalCap, "The droid's storage is busy. Try again.");
        return refusal;
    }

    if (countTakes() >= TAKE_STORE_CAP) {
        TakeFileNameParts unsaved;
        if (!findTake(unsavedTake, nullptr, &unsaved)) {
            fullRefusal(refusal, refusalCap);
            unlock();
            return refusal;
        }
        if (!removeTake(unsaved)) {
            snprintf(refusal, refusalCap, "The droid could not make room for the take.");
            unlock();
            return refusal;
        }
        memcpy(replacedOut, unsaved.take, TAKE_ID_LEN + 1);
    }
    if (freeBytes() < TAKE_FS_FREE_FLOOR + headerBytes + sampleBytes) {
        noSpaceRefusal(refusal, refusalCap);
        unlock();
        return refusal;
    }

    TakeFileNameParts target = {};
    strncpy(target.owner, owner, sizeof(target.owner) - 1);
    char path[64];
    if (!mintTakeId(owner, target.take) || !takePath(target, false, path, sizeof(path))) {
        snprintf(refusal, refusalCap, "The droid could not name the take.");
        unlock();
        return refusal;
    }

    uint8_t header[TAKE_FILE_FIXED_BYTES + TAKE_PARTS_MAX * TAKE_PART_ID_BYTES];
    takeFileWriteHeader(&capture, header);
    File wf = LittleFS.open(TMP_PATH, "w");
    if (!wf) {
        snprintf(refusal, refusalCap, "The droid could not open a file for the take.");
        unlock();
        return refusal;
    }
    // The samples go out as they sit in RAM: both chips are little-endian,
    // which is the file's byte order (take_capture.h).
    const size_t wrote = wf.write(header, headerBytes) +
                         wf.write((const uint8_t*)capture.samples, sampleBytes);
    wf.close();
    if (wrote != headerBytes + sampleBytes || !LittleFS.rename(TMP_PATH, path)) {
        LittleFS.remove(TMP_PATH);
        snprintf(refusal, refusalCap, "The droid could not write the take.");
        unlock();
        return refusal;
    }
    unlock();
    memcpy(idOut, target.take, TAKE_ID_LEN + 1);
    PA_LOG_INFO(TAG, "kept take %s of %s: %u Part(s), %u sample(s), %u B", idOut, owner,
                (unsigned)capture.partCount, (unsigned)capture.sampleCount,
                (unsigned)(headerBytes + sampleBytes));
    return nullptr;
}

// -----------------------------------------------------------------------------
// The sequence store's hooks
// -----------------------------------------------------------------------------
void takeStoreSequenceSaved(const char* owner, const TakeRefs& refs, bool dropOthers) {
    if (owner == nullptr || owner[0] == '\0' || !lock()) return;
    TakeFileNameParts t;
    // Each pass acts on one take of `owner` that is not yet as the save says:
    // named and still ".new" -> kept; not named -> deleted (when it may). A
    // named, kept take matches nothing, nor - without `dropOthers` - an
    // unnamed one, so the loop ends when every take of `owner` is settled.
    struct Saved {
        const char* owner;
        const TakeRefs* refs;
        bool dropOthers;
    } saved = {owner, &refs, dropOthers};
    while (findTake(
        [](const TakeFileNameParts& each, const void* ctx) {
            const Saved& sv = *(const Saved*)ctx;
            if (strcmp(each.owner, sv.owner) != 0) return false;
            const bool named = refsName(*sv.refs, each.take);
            return named ? !each.kept : sv.dropOthers;
        },
        &saved, &t)) {
        if (refsName(refs, t.take)) {
            char from[64];
            char to[64];
            if (!takePath(t, false, from, sizeof(from)) || !takePath(t, true, to, sizeof(to)) ||
                !LittleFS.rename(from, to)) {
                PA_LOG_ERROR(TAG, "cannot keep take %s of %s", t.take, owner);
                break;
            }
        } else {
            if (!removeTake(t)) break;
            PA_LOG_INFO(TAG, "take %s left %s and is deleted", t.take, owner);
        }
    }
    unlock();
}

void takeStoreSequenceDeleted(const char* owner) {
    if (owner == nullptr || owner[0] == '\0' || !lock()) return;
    TakeFileNameParts t;
    while (findTake(ownedBy, owner, &t)) {
        if (!removeTake(t)) break;
        PA_LOG_INFO(TAG, "take %s deleted with its sequence", t.take);
    }
    unlock();
}

// -----------------------------------------------------------------------------
// Reading a take
// -----------------------------------------------------------------------------

// The path of take `take` of `owner` as it stands, kept or not.
static bool existingPath(const char* owner, const char* take, char* out, size_t cap) {
    TakeFileNameParts t = {};
    if (owner == nullptr || take == nullptr || strlen(owner) >= sizeof(t.owner) ||
        !takeIdValid(take)) {
        return false;
    }
    strcpy(t.owner, owner);
    strcpy(t.take, take);
    if (takePath(t, true, out, cap) && LittleFS.exists(out)) return true;
    return takePath(t, false, out, cap) && LittleFS.exists(out);
}

bool takeStoreHas(const char* owner, const char* take) {
    if (!lock()) return false;
    char path[64];
    const bool has = existingPath(owner, take, path, sizeof(path));
    unlock();
    return has;
}

size_t takeStoreReadSlice(const char* owner, const char* take, size_t offset, uint8_t* out,
                          size_t capacity) {
    if (out == nullptr || capacity == 0 || !lock()) return 0;
    char path[64];
    size_t read = 0;
    if (existingPath(owner, take, path, sizeof(path))) {
        File f = LittleFS.open(path, "r");
        if (f) {
            // Past the end is how a read ends: the caller walks the offset on
            // until a slice comes back empty.
            if (f.seek(offset)) {
                const int n = f.read(out, capacity);
                if (n > 0) read = (size_t)n;
            }
            f.close();
        }
    }
    unlock();
    return read;
}

// The one file read on (take_store.h), and whether a begin succeeded: the
// lock is held while it is set, and only the end that clears it gives the
// lock back.
static File s_reader;
static bool s_reading = false;

bool takeStoreReadBegin(const char* owner, const char* take, size_t offset) {
    if (s_reading || !lock()) return false;
    char path[64];
    if (existingPath(owner, take, path, sizeof(path))) {
        s_reader = LittleFS.open(path, "r");
        if (s_reader && s_reader.seek(offset)) {
            s_reading = true;
            return true;
        }
        s_reader.close();
    }
    unlock();
    return false;
}

size_t takeStoreReadOn(uint8_t* out, size_t capacity) {
    if (!s_reading) return 0;
    const int n = s_reader.read(out, capacity);
    return (n > 0) ? (size_t)n : 0;
}

void takeStoreReadEnd() {
    if (!s_reading) return;
    s_reader.close();
    s_reading = false;
    unlock();
}

// -----------------------------------------------------------------------------
// A take file arriving in pieces
//
// Its state is taken from the heap when a restore begins and given back when
// it finishes - a Core 0 web request's bounded allocation, held only for the
// length of one upload - rather than kept as a static every boot pays for.
// -----------------------------------------------------------------------------
namespace {

struct RestoreState {
    bool bad;
    char reason[128];
    TakeFileNameParts target;
    File file;
    uint8_t header[TAKE_FILE_FIXED_BYTES + TAKE_PARTS_MAX * TAKE_PART_ID_BYTES];
    size_t headerGot;
    size_t headerBytes;
    TakeFileInfo info;
    uint8_t word[TAKE_SAMPLE_BYTES];
    uint8_t wordGot;
    uint32_t samplesSeen;
    uint16_t prevTick;
    // The store is full and a take not yet saved into its sequence will make
    // way - at the finish, once this file has passed every check, never
    // before: a refused upload must not have cost the builder that take.
    bool makeRoom;
};

RestoreState* s_restore = nullptr;
// A restore begun with no memory for its state: the finish says so.
bool s_restoreNoMemory = false;

const char kNoMemory[] = "The droid has no memory free to restore a take.";

void restoreFail(const char* reason) {
    if (!s_restore->bad) {
        s_restore->bad = true;
        snprintf(s_restore->reason, sizeof(s_restore->reason), "%s", reason);
    }
}

// Ends a restore that will not be finished: a second one begun over it, or a
// finish that has said its piece.
void restoreRelease() {
    if (s_restore == nullptr) return;
    if (s_restore->file) s_restore->file.close();
    s_restore->~RestoreState();
    free(s_restore);
    s_restore = nullptr;
}

}  // namespace

const char* takeStoreRestoreBegin(const char* fileName, char* refusal, size_t refusalCap) {
    if (s_restore != nullptr) {
        restoreRelease();
        LittleFS.remove(TMP_PATH);
    }
    s_restoreNoMemory = false;
    void* storage = malloc(sizeof(RestoreState));
    if (storage == nullptr) {
        s_restoreNoMemory = true;
        snprintf(refusal, refusalCap, "%s", kNoMemory);
        return refusal;
    }
    s_restore = new (storage) RestoreState();
    TakeFileNameParts t;
    if (!takeFileNameParse(fileName, &t) || !t.kept) {
        restoreFail("The file's name is not a take's.");
    } else if (seqStoreIndexFindRef(t.owner) == nullptr) {
        restoreFail("The sequence that holds this take is not on the droid.");
    }
    if (!s_restore->bad && lock()) {
        s_restore->target = t;
        char path[64];
        const bool replacing = existingPath(t.owner, t.take, path, sizeof(path));
        TakeFileNameParts unsaved;
        if (!replacing && countTakes() >= TAKE_STORE_CAP) {
            if (!findTake(unsavedTake, nullptr, &unsaved)) {
                char full[128];
                restoreFail(fullRefusal(full, sizeof(full)));
            } else {
                s_restore->makeRoom = true;
            }
        }
        if (!s_restore->bad) {
            s_restore->file = LittleFS.open(TMP_PATH, "w");
            if (!s_restore->file) restoreFail("The droid could not open a file for the take.");
        }
        unlock();
    } else if (!s_restore->bad) {
        restoreFail("The droid's storage is busy. Try again.");
    }
    if (s_restore->bad) {
        snprintf(refusal, refusalCap, "%s", s_restore->reason);
        // Kept, refused, until the finish: the pieces still arrive and are
        // dropped, and the finish answers with this reason.
        return refusal;
    }
    return nullptr;
}

void takeStoreRestoreAppend(const uint8_t* data, size_t len) {
    if (s_restore == nullptr || s_restore->bad || data == nullptr) return;
    RestoreState& r = *s_restore;
    size_t at = 0;

    // The header: the fixed part first, then the Part ids it says follow.
    while (at < len && (r.headerBytes == 0 || r.headerGot < r.headerBytes)) {
        r.header[r.headerGot++] = data[at++];
        if (r.headerBytes == 0 && r.headerGot == TAKE_FILE_FIXED_BYTES) {
            if (!takeFileReadFixed(r.header, &r.info)) {
                restoreFail("The file is not a take this droid can read.");
                return;
            }
            const size_t total = takeFileHeaderBytes(r.info.partCount) +
                                 (size_t)r.info.sampleCount * TAKE_SAMPLE_BYTES;
            // With a take still to make way, its blocks are not free yet: the
            // file itself must fit now, and the floor holds once it has gone.
            const size_t need = r.makeRoom ? total : TAKE_FS_FREE_FLOOR + total;
            if (freeBytes() < need) {
                restoreFail("Not enough free space on the droid to keep a take.");
                return;
            }
            r.headerBytes = takeFileHeaderBytes(r.info.partCount);
        }
        if (r.headerBytes != 0 && r.headerGot == r.headerBytes) {
            for (uint8_t i = 0; i < r.info.partCount; ++i) {
                if (!takeFilePartIdValid(r.header + TAKE_FILE_FIXED_BYTES +
                                         (size_t)i * TAKE_PART_ID_BYTES)) {
                    restoreFail("The take names a Part this droid cannot read.");
                    return;
                }
            }
            if (r.file.write(r.header, r.headerBytes) != r.headerBytes) {
                restoreFail("The droid could not write the take.");
                return;
            }
        }
    }

    // The samples, each checked as it completes, written a run at a time.
    const size_t runStart = at;
    for (; at < len; ++at) {
        r.word[r.wordGot++] = data[at];
        if (r.wordGot < TAKE_SAMPLE_BYTES) continue;
        r.wordGot = 0;
        const uint32_t s = takeGetU32(r.word);
        if (r.samplesSeen >= r.info.sampleCount || !takeFileSampleValid(s, r.prevTick, r.info)) {
            restoreFail("The take's samples are out of order or out of range.");
            return;
        }
        r.prevTick = takeSampleTick(s);
        ++r.samplesSeen;
    }
    if (len > runStart && r.file.write(data + runStart, len - runStart) != len - runStart) {
        restoreFail("The droid could not write the take.");
    }
}

const char* takeStoreRestoreFinish(char* refusal, size_t refusalCap) {
    if (s_restore == nullptr) {
        snprintf(refusal, refusalCap, "%s", s_restoreNoMemory ? kNoMemory : "No take file arrived.");
        s_restoreNoMemory = false;
        return refusal;
    }
    RestoreState& r = *s_restore;
    if (!r.bad && (r.headerBytes == 0 || r.headerGot < r.headerBytes || r.wordGot != 0 ||
                   r.samplesSeen != r.info.sampleCount)) {
        restoreFail("The take file is cut short.");
    }
    if (r.file) r.file.close();
    if (!r.bad) {
        if (lock()) {
            char kept[64];
            char fresh[64];
            TakeFileNameParts unsaved;
            if (r.makeRoom && countTakes() >= TAKE_STORE_CAP) {
                if (!findTake(unsavedTake, nullptr, &unsaved) || !removeTake(unsaved)) {
                    restoreFail("The droid could not make room for the take.");
                }
            }
            if (!r.bad && takePath(r.target, true, kept, sizeof(kept)) &&
                takePath(r.target, false, fresh, sizeof(fresh))) {
                LittleFS.remove(kept);
                LittleFS.remove(fresh);
                if (!LittleFS.rename(TMP_PATH, kept)) {
                    restoreFail("The droid could not write the take.");
                }
            } else if (!r.bad) {
                restoreFail("The file's name is not a take's.");
            }
            unlock();
        } else {
            restoreFail("The droid's storage is busy. Try again.");
        }
    }
    if (r.bad) {
        LittleFS.remove(TMP_PATH);
        snprintf(refusal, refusalCap, "%s", r.reason);
        restoreRelease();
        return refusal;
    }
    PA_LOG_INFO(TAG, "restored take %s of %s", r.target.take, r.target.owner);
    restoreRelease();
    return nullptr;
}
