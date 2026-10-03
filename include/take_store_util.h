// =============================================================================
// include/take_store_util.h
//
// The take store's figures and file names (#442, ADR 0061): how many takes a
// board keeps, how big one may be, the free space a write leaves, and where a
// take's file sits. Pure: no LittleFS, no FreeRTOS - src/take_store.cpp does
// the I/O around these, the way seq_store.cpp does around seq_store_util.h.
//
// A take is a performance captured off the sticks: the commanded targets of
// the Parts that had puppet strings when it was armed, in a file of its own
// that a Learned Sequence references by id (CONTEXT.md "Take"). The figures
// below are the droid's own: GET /api/take reports all three, so no page
// carries a copy.
// =============================================================================
#pragma once

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "config.h"          // PA_BOARD
#include "protocol_check.h"  // protocolCheckSeqIdValid() - a take's owner is a sequence id
#include "seq_store_util.h"  // SEQ_FILE_MAX_BYTES, PA_SEQ_FILE_MAX_KB

// TAKE FILE CAP: one take's file at most, header and samples. The sequence
// file's cap on each chip (12 KB on the artoo-esp32, 24 KB on the P4), so the
// filesystem arithmetic prices a take exactly as it prices a sequence: a full
// 12,288 B file is 4 written LittleFS blocks (tools/build_budgets.json). It is
// also the RAM a take is captured into (src/take.cpp), so it cannot rise on
// the artoo-esp32 without a heap decision.
static constexpr size_t TAKE_FILE_MAX_BYTES = (size_t)PA_SEQ_FILE_MAX_KB * 1024;
static_assert(TAKE_FILE_MAX_BYTES == SEQ_FILE_MAX_BYTES, "a take's cap is its board's sequence cap");

// TAKE STORE CAP: how many takes the board keeps, every sequence together.
//
//   artoo-esp32: 1 (operator, 2026-09-30, #442). Its 160-block partition is
//   divided between the web image, five Learned Sequences and this one take:
//   the image's ceiling went from 132 to 128 blocks to pay the take's 4, and
//   tools/build_budgets.json carries the replay that proves five sequences
//   and the take all fit under it, in either order.
//
//   firebeetle2: 20, two for each of the ten sequences it stores - a routine
//   built up in passes, dome then panels. Its 10,354,688 B partition is 2,528
//   blocks. Measured with the platform's littlefs-python parameters, a full
//   24,576 B file costs 7 blocks (6 of data and its CTZ pointers), and the
//   save paths replayed over an image at its 256-block budget take ten full
//   sequences and then twenty full takes, every one under its floor, with
//   lfs_fs_size() at 468 blocks of 2,528 afterwards. Nothing on that board
//   makes the number scarce; it is a count a builder can keep in mind.
#if PA_BOARD == PA_BOARD_ARTOO_ESP32
  #define PA_TAKE_STORE_CAP 1
#elif PA_BOARD == PA_BOARD_FIREBEETLE2
  #define PA_TAKE_STORE_CAP 20
#else
  #error "the take store cap has no value for this board"
#endif

static const uint8_t TAKE_STORE_CAP = PA_TAKE_STORE_CAP;

// FREE-SPACE FLOOR: the sequence store's rule, for the same reason - a take is
// written to a temporary file and renamed, so for a moment it is on disk
// twice, and LittleFS spends whole blocks and metadata pairs on every file. A
// take is written only when the filesystem would still have this much free
// beside it.
static constexpr size_t TAKE_FS_FREE_FLOOR = 2 * TAKE_FILE_MAX_BYTES;

// A take's id: what a sequence names it by in its `takes` array, minted by
// the droid when the take is kept. Lowercase letters and digits, the shape of
// a sequence's own stable id, at a fixed eight characters.
static const size_t TAKE_ID_LEN = 8;

inline bool takeIdValid(const char* id) {
    if (id == nullptr || strlen(id) != TAKE_ID_LEN) {
        return false;
    }
    for (size_t i = 0; i < TAKE_ID_LEN; ++i) {
        const char c = id[i];
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9'))) {
            return false;
        }
    }
    return true;
}

// -----------------------------------------------------------------------------
// Where a take's file sits
//
// IN /seq, BESIDE THE SEQUENCES: "/seq/<owner>.<take>.take", where <owner> is
// the stable id of the sequence it belongs to (ADR 0046's id, which a rename
// keeps). A directory of its own would cost a written block and a metadata
// pair the 128-block arithmetic does not have. The owner in the name is what
// lets deleting a sequence delete its takes without opening anything.
//
// TWO SUFFIXES, TWO STATES. A take is written as ".new" when it is kept from a
// performance and becomes ".take" when its sequence is saved naming it. A
// ".new" file is one the builder has not saved into their sequence yet: the
// next take may replace it when the store is full, and a boot deletes it. So a
// performance the builder walked away from never holds the artoo-esp32's one
// take, and what a take's state is lives on the filesystem, not in a table in
// RAM.
// -----------------------------------------------------------------------------
static const char TAKE_SUFFIX_KEPT[] = ".take";
static const char TAKE_SUFFIX_NEW[] = ".new";

// Longest file name: owner (16) + '.' + take (8) + ".take" + terminator.
static const size_t TAKE_FILE_NAME_MAX = 16 + 1 + TAKE_ID_LEN + sizeof(TAKE_SUFFIX_KEPT);

// "<owner>.<take><suffix>" into `out`. False when the owner is not a stable
// sequence id (protocolCheckSeqIdValid(): 1..16 lowercase letters or digits),
// the take is not a take id, or the buffer is too small.
inline bool takeFileName(const char* owner, const char* take, bool kept, char* out, size_t cap) {
    if (!protocolCheckSeqIdValid(owner) || !takeIdValid(take) || out == nullptr) {
        return false;
    }
    const char* suffix = kept ? TAKE_SUFFIX_KEPT : TAKE_SUFFIX_NEW;
    const size_t need = strlen(owner) + 1 + TAKE_ID_LEN + strlen(suffix) + 1;
    if (need > cap) {
        return false;
    }
    strcpy(out, owner);
    strcat(out, ".");
    strcat(out, take);
    strcat(out, suffix);
    return true;
}

// What a file name in /seq says about a take, or false for a name that is not
// a take's (a sequence's .json, the save's temporary file, an owner that is
// not a sequence id).
struct TakeFileNameParts {
    char owner[17];
    char take[TAKE_ID_LEN + 1];
    bool kept;  // ".take"; false for ".new"
};

inline bool takeFileNameParse(const char* name, TakeFileNameParts* out) {
    if (name == nullptr || out == nullptr) {
        return false;
    }
    const size_t len = strlen(name);
    bool kept = false;
    size_t stem = 0;
    if (len > sizeof(TAKE_SUFFIX_KEPT) - 1 &&
        strcmp(name + len - (sizeof(TAKE_SUFFIX_KEPT) - 1), TAKE_SUFFIX_KEPT) == 0) {
        kept = true;
        stem = len - (sizeof(TAKE_SUFFIX_KEPT) - 1);
    } else if (len > sizeof(TAKE_SUFFIX_NEW) - 1 &&
               strcmp(name + len - (sizeof(TAKE_SUFFIX_NEW) - 1), TAKE_SUFFIX_NEW) == 0) {
        stem = len - (sizeof(TAKE_SUFFIX_NEW) - 1);
    } else {
        return false;
    }
    // <owner>.<take>: the take id is the last eight characters before the
    // suffix, after a dot.
    if (stem < TAKE_ID_LEN + 2 || name[stem - TAKE_ID_LEN - 1] != '.') {
        return false;
    }
    const size_t ownerLen = stem - TAKE_ID_LEN - 1;
    if (ownerLen > 16) {
        return false;
    }
    memcpy(out->owner, name, ownerLen);
    out->owner[ownerLen] = '\0';
    memcpy(out->take, name + ownerLen + 1, TAKE_ID_LEN);
    out->take[TAKE_ID_LEN] = '\0';
    out->kept = kept;
    // The owner must be a sequence id, never merely 1..16 characters: a name
    // such as "DM:FOO.<take>.take" would otherwise land as a take that no
    // save, delete or boot ever matches to a sequence, and so never goes.
    return protocolCheckSeqIdValid(out->owner) && takeIdValid(out->take);
}
