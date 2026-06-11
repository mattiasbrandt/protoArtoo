// =============================================================================
// src/tasks/sequence_catalog.cpp
//
// Body-owned DM:* sequence catalog and alias tables (ADR 0004, issue #2).
// Choreography source of truth: issue #2 comment 1 (dome agent spec).
//
// Pure data + lookup; no FreeRTOS or hardware dependencies (natively testable).
// Step timing convention: tMs is absolute from sequence start; steps inside a
// STEP_LOOP body are relative to the iteration start.
// =============================================================================

#include <string.h>

#include "sequence_dispatcher.h"
#include "sequence_engine.h"

#define SEQ_STEPCOUNT(arr) ((uint8_t)(sizeof(arr) / sizeof((arr)[0])))

// =============================================================================
// Flat sequences (slice 1)
// =============================================================================

// DM:VADER — Imperial March visual mode (47 s).
// Holos, logics, and PSI set to MARCH mode; reset at sequence end.
static const SeqStep kVaderSteps[] = {
    SEQ_AUDIO(0, "$M"),                          // Imperial March
    SEQ_DOME(0, FX_LOGIC_PSI, "@HPA0021|47"),    // all holos red 47 s
    SEQ_DOME(0, FX_LOGIC_PSI, "@0T11"),          // MARCH logics
    SEQ_DOME(0, FX_LOGIC_PSI, "@0P11"),          // MARCH PSI
    SEQ_DOME(47000, FX_NONE, "@0T1"),            // reset logics
    SEQ_DOME(47000, FX_NONE, "@0P1"),            // reset PSI
    SEQ_TERM(47000),
};

// DM:HELLO — "Hello There" greeting (4 s).
// Front and rear logic text, then a six-pulse P1 panel wave.
static const SeqStep kHelloSteps[] = {
    SEQ_AUDIO(0, "$H"),                          // happy/greeting clip
    SEQ_DOME(0, FX_NONE, "@1MHello There"),      // front logic text
    SEQ_DOME(0, FX_NONE, "@3MGeneral Kenobi"),
    SEQ_DOME(0, FX_NONE, ":SM0,2200,150"),       // P1 open
    SEQ_DOME(160, FX_NONE, ":SM0,1500,150"),     // P1 half
    SEQ_DOME(320, FX_NONE, ":SM0,2200,150"),     // P1 open
    SEQ_DOME(480, FX_NONE, ":SM0,1500,150"),     // P1 half
    SEQ_DOME(640, FX_NONE, ":SM0,2200,150"),     // P1 open
    SEQ_DOME(800, FX_PANEL, ":SM0,800,150"),     // P1 close
    SEQ_DOME(950, FX_NONE, ":CL00"),             // release panels
    SEQ_TERM(950),
};

// DM:NOD — short acknowledgment: sound + logic text + P1 wave.
// Demonstrates sound-to-motion sync from a single body clock (issue #2).
static const SeqStep kNodSteps[] = {
    SEQ_AUDIO(0, "$H"),                          // ack/happy clip
    SEQ_DOME(0, FX_NONE, "@1MYes"),              // logic text
    SEQ_DOME(0, FX_NONE, ":SM0,2200,150"),       // P1 open
    SEQ_DOME(150, FX_PANEL, ":SM0,800,150"),     // P1 close
    SEQ_DOME(300, FX_NONE, ":CL00"),             // release
    SEQ_TERM(300),
};

// =============================================================================
// Catalog table
// =============================================================================

static const SequenceEntry kCatalog[] = {
    { "DM:VADER", kVaderSteps, SEQ_STEPCOUNT(kVaderSteps), 47000, TOGGLE_NONE, nullptr, 0 },
    { "DM:HELLO", kHelloSteps, SEQ_STEPCOUNT(kHelloSteps), 4000,  TOGGLE_NONE, nullptr, 0 },
    { "DM:NOD",   kNodSteps,   SEQ_STEPCOUNT(kNodSteps),   3000,  TOGGLE_NONE, nullptr, 0 },
};
static constexpr uint8_t kCatalogSize =
    (uint8_t)(sizeof(kCatalog) / sizeof(kCatalog[0]));

// =============================================================================
// Alias table — DM:* names that forward directly to the dome unchanged or
// mapped to a :SE## / $NNN target. No body execution; dome owns these.
// =============================================================================

struct AliasEntry {
    const char* name;
    const char* target;
};

static const AliasEntry kAliases[] = {
    { "DM:STOP",           ":SE00" },
    { "DM:SESCREAM",       ":SE01" },
    { "DM:WAVE",           ":SE02" },
    { "DM:SMIRKWAVE",      ":SE03" },
    { "DM:OCWAVE",         ":SE04" },
    { "DM:BEEPCANTINA",    ":SE05" },
    { "DM:SHORT",          ":SE06" },
    { "DM:SECANTINA",      ":SE07" },
    { "DM:SELEIA",         ":SE08" },
    { "DM:DISCO",          ":SE09" },
    { "DM:SCREAMNOPANEL",  ":SE50" },
    { "DM:SCREAMPANEL",    ":SE51" },
    { "DM:WAVEPANEL",      ":SE52" },
    { "DM:SMIRKWAVEPANEL", ":SE53" },
    { "DM:OPENWAVE",       ":SE54" },
    { "DM:MARCHINGANTS",   ":SE55" },
    { "DM:FAINT",          ":SE56" },
    { "DM:RYTHMIC",        ":SE57" },
    { "DM:HARLEMSHAKE",    "$815"  },
    { "DM:GIRLONFIRE",     "$821"  },
    { "DM:YODA",           "$720"  },
    { "DM:TOPPANELS",      ":SE12" },
    { "DM:WIGGLE",         ":SE16" },
    { "DM:BYEBYE",         ":SE58" },
};
static constexpr uint8_t kAliasSize =
    (uint8_t)(sizeof(kAliases) / sizeof(kAliases[0]));

// =============================================================================
// Lookup — pure, no side effects, native-testable.
// =============================================================================

const SequenceEntry* sequenceCatalogFind(const char* name) {
    if (name == nullptr || name[0] == '\0') {
        return nullptr;
    }
    for (uint8_t i = 0; i < kCatalogSize; ++i) {
        if (strcmp(kCatalog[i].name, name) == 0) {
            return &kCatalog[i];
        }
    }
    return nullptr;
}

SequenceLookupResult sequenceLookup(const char* name) {
    SequenceLookupResult r = { SEQ_FALLBACK, {} };

    if (name == nullptr || name[0] == '\0') {
        return r;
    }

    if (sequenceCatalogFind(name) != nullptr) {
        r.kind = SEQ_CATALOG;
        return r;
    }

    for (uint8_t i = 0; i < kAliasSize; ++i) {
        if (strcmp(kAliases[i].name, name) == 0) {
            r.kind = SEQ_ALIAS;
            strncpy(r.aliasTarget, kAliases[i].target, sizeof(r.aliasTarget) - 1);
            r.aliasTarget[sizeof(r.aliasTarget) - 1] = '\0';
            return r;
        }
    }

    return r;  // SEQ_FALLBACK
}
