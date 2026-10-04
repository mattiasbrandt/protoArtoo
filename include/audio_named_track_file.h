// =============================================================================
// include/audio_named_track_file.h
//
// Which file a CHIRP Named Track was bound to, and whether the card still has
// it there (#447, ADR 0054: "a Named Track pointing at an index whose file
// changed underneath it is a reconciliation the builder resolves, never a
// silent re-point").
//
// A binding is a bank/page/index (include/audio_playback_policy.h,
// AudioChirpSlotBinding). When the builder binds it, the name the card reports
// for that address is recorded beside it as a 32-bit fingerprint
// (include/chirp_binding_keys.h, `fileKey`). After a catalog refresh the same
// address is looked up again: a different name there, or no file at all on a
// card the catalog read whole, is a changed file. Nothing here re-points
// anything; the builder answers by binding again, to the same address (keep
// it) or another one (re-point it), and that bind records the file afresh.
//
// A fingerprint rather than the name: it is enough to say "the file changed",
// and it is 4 bytes in NVS per bound Named Track instead of up to 48. The
// catalog the builder is looking at already carries the name the card reports
// now.
//
// Pure: no NVS, no driver, no FreeRTOS. The caller holds the catalog reader
// lease (include/audio_catalog_gate.h) while it passes the entries in.
// =============================================================================
#pragma once

#include <stdint.h>
#include <string.h>

#include "audio_driver.h"  // AudioCatalogEntry

// Stored where no file was recorded: the binding was made with no catalog to
// read, or before bindings recorded their file. A real fingerprint is never 0
// (audioCatalogNameFingerprint() moves it).
constexpr uint32_t AUDIO_NAMED_TRACK_FILE_UNRECORDED = 0;

// Whether the entry carries a name the card reported. A sound whose GNME reply
// never came is listed as "index_<its index>" (src/drivers/audio_chirp.cpp,
// refreshCatalog()), and fingerprinting that placeholder would later read as a
// changed file the moment the real name arrived. A card file literally named
// "index_<n>" at index n is indistinguishable from the placeholder and is
// treated as unnamed: it is never reported changed, only unchecked.
inline bool audioCatalogNameObserved(const AudioCatalogEntry& entry) {
    static const char kPlaceholder[] = "index_";
    const size_t prefixLen = sizeof(kPlaceholder) - 1;
    if (entry.name[0] == '\0') {
        return false;
    }
    if (strncmp(entry.name, kPlaceholder, prefixLen) != 0) {
        return true;
    }
    uint32_t value = 0;
    const char* digits = entry.name + prefixLen;
    if (*digits == '\0') {
        return true;
    }
    for (const char* c = digits; *c != '\0'; ++c) {
        if (*c < '0' || *c > '9' || value > 65535u) {
            return true;
        }
        value = value * 10u + (uint32_t)(*c - '0');
    }
    return value != entry.index;
}

// FNV-1a over the reported name. AUDIO_NAMED_TRACK_FILE_UNRECORDED when the
// entry carries no reported name.
inline uint32_t audioCatalogNameFingerprint(const AudioCatalogEntry& entry) {
    if (!audioCatalogNameObserved(entry)) {
        return AUDIO_NAMED_TRACK_FILE_UNRECORDED;
    }
    uint32_t hash = 2166136261u;
    for (const char* c = entry.name; *c != '\0'; ++c) {
        hash ^= (uint8_t)*c;
        hash *= 16777619u;
    }
    // 0 means "nothing recorded"; a name that hashes there is moved off it.
    return hash == AUDIO_NAMED_TRACK_FILE_UNRECORDED ? 1u : hash;
}

inline const AudioCatalogEntry* audioCatalogEntryAt(const AudioCatalogEntry* entries,
                                                    uint16_t count, uint8_t bank, char page,
                                                    uint16_t index) {
    if (entries == nullptr) {
        return nullptr;
    }
    for (uint16_t i = 0; i < count; ++i) {
        if (entries[i].bank == bank && entries[i].page == page && entries[i].index == index) {
            return &entries[i];
        }
    }
    return nullptr;
}

// The fingerprint of the file the card lists at an address now, or
// AUDIO_NAMED_TRACK_FILE_UNRECORDED when it lists none there or names it only
// by index.
inline uint32_t audioCatalogFingerprintAt(const AudioCatalogEntry* entries, uint16_t count,
                                          uint8_t bank, char page, uint16_t index) {
    const AudioCatalogEntry* entry = audioCatalogEntryAt(entries, count, bank, page, index);
    return entry != nullptr ? audioCatalogNameFingerprint(*entry) : AUDIO_NAMED_TRACK_FILE_UNRECORDED;
}

enum class AudioNamedTrackFile : uint8_t {
    Unchecked = 0,  // nothing to compare: no file recorded, no catalog, or the
                    // card's name for the address did not come back
    Same,           // the card lists the recorded file at the bound address
    Changed,        // the card lists another file there, or none on a card read whole
};

// Compare a binding's recorded file with what the catalog lists at its address
// now. `catalogWhole` is a ready catalog whose manifest arrived in full and
// whose walk did not stop at the entry cap: only then does an address missing
// from it mean the card has no file there, rather than that the walk did not
// reach it.
inline AudioNamedTrackFile audioNamedTrackFileCompare(uint32_t recorded,
                                                      const AudioCatalogEntry* entries,
                                                      uint16_t count, bool catalogWhole,
                                                      uint8_t bank, char page, uint16_t index) {
    if (recorded == AUDIO_NAMED_TRACK_FILE_UNRECORDED || entries == nullptr) {
        return AudioNamedTrackFile::Unchecked;
    }
    const AudioCatalogEntry* entry = audioCatalogEntryAt(entries, count, bank, page, index);
    if (entry == nullptr) {
        return catalogWhole ? AudioNamedTrackFile::Changed : AudioNamedTrackFile::Unchecked;
    }
    const uint32_t now = audioCatalogNameFingerprint(*entry);
    if (now == AUDIO_NAMED_TRACK_FILE_UNRECORDED) {
        return AudioNamedTrackFile::Unchecked;
    }
    return now == recorded ? AudioNamedTrackFile::Same : AudioNamedTrackFile::Changed;
}

inline const char* audioNamedTrackFileToken(AudioNamedTrackFile file) {
    switch (file) {
        case AudioNamedTrackFile::Same:    return "same";
        case AudioNamedTrackFile::Changed: return "changed";
        case AudioNamedTrackFile::Unchecked:
        default:                           return "unchecked";
    }
}
