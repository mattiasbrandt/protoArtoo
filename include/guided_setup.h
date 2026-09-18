// =============================================================================
// include/guided_setup.h
//
// Guided Setup's own state: whether the run has ended, and which of its steps
// the builder was actually shown (#351, #297).
//
// Two facts, and they answer two different questions.
//
// THE RUN. Guided Setup is a takeover, not a destination: it runs once and ends
// either by reaching the last step or by the builder deliberately stopping.
// Skipping IS finishing, so both end it - but "ran it and answered" and "ran it
// and skipped past" are different facts about a droid, and the surfaces that
// report on a droid afterwards are owed the difference. Three states, therefore,
// not two.
//
// THE VISITED RECORD. Every component toggle on this controller defaults false,
// so a category nobody was ever asked about reads "not fitted" - a statement
// about the builder's droid that they never made. The cure is a second record,
// not a different default: a step is VISITED once it has actually been on
// screen, and until then its answer renders as the default it is rather than as
// a confirmation. The pattern and the reason are
// r2d2-astromech-simulator v1.79.0 (175ad1b), src/js/config/wizard.js:2216.
//
// WHICH STEPS EXIST IS NOT FIRMWARE'S QUESTION. The step list lives in the
// browser module that draws the run, and it grows: this file stores the keys it
// is handed and checks their FORM, never their meaning - the same thing
// droidPartIdIsKnown() does for a Part id, minus a vocabulary to check against.
// That is why the record is a key list rather than a bitmask over step indices:
// a step inserted in the middle would re-point every bit after it at a
// different question, silently.
//
// An ABSENT visited record is a controller guided Setup has never drawn on, and
// it is NOT the same as an empty one, which is a real answer - a run in which
// nothing has been shown yet. A controller configured before guided Setup
// existed has no record at all, and reporting every category on it as "never
// asked" would be exactly the untruth this record exists to prevent; the rule
// that reads that case is the browser's, because only the browser knows what
// the steps are. `recorded` is what carries the distinction across the wire.
//
// This structure is deliberately NOT part of ConfigSnapshot, for the reason
// DroidBuildConfig is not: the snapshot crosses three nested stack frames on the
// serial config-write path and its size is pinned to a measured task-stack chain
// (include/config_store.h), and nothing on a real-time path reads a guided run.
// It sits on its own NVS keys beside the Droid Build (include/config_serializer.h).
//
// Pure: no NVS, no FreeRTOS, no Arduino String. Header-only.
// =============================================================================

#pragma once

#include <stddef.h>
#include <stdint.h>
#include <string.h>

// -----------------------------------------------------------------------------
// How wide a record is stored
//
// Declared with headroom rather than sized to today's run, the way
// DROID_DESIGN_ID_MAX is: the step list is the browser's and it grows by design
// - #368 inserts one into the middle of it - so a longer run must be a browser
// edit, never a storage-layout change with a stored-value migration behind it.
// A list that outgrows this is truncated at a comma rather than mid-key, so what
// survives is always a whole answer.
// -----------------------------------------------------------------------------
constexpr size_t GUIDED_SETUP_STEP_KEY_MAX = 12;
constexpr size_t GUIDED_SETUP_STEP_MAX = 16;
constexpr size_t GUIDED_SETUP_VISITED_STR_MAX =
    GUIDED_SETUP_STEP_MAX * (GUIDED_SETUP_STEP_KEY_MAX + 1);

// What "the run has been drawn, and nothing has been shown yet" is written as.
// A record nobody has written and a run that has shown nothing must not read
// alike - the first is a controller guided Setup has never opened on, the second
// is a builder one keystroke into it - and an empty string cannot tell them
// apart. The spelling is the Fitted Parts one (include/droid_build.h), for the
// same reason.
constexpr char GUIDED_SETUP_VISITED_NONE[] = "-";

// -----------------------------------------------------------------------------
// GuidedSetupRun
// Where the run stands. `NOT_RUN` is a run that has not ended - a fresh
// controller, or one a builder is part way through - and it is the only state in
// which guided Setup offers itself.
// -----------------------------------------------------------------------------
enum GuidedSetupRun : uint8_t {
    GUIDED_SETUP_NOT_RUN = 0,
    GUIDED_SETUP_SKIPPED = 1,
    GUIDED_SETUP_COMPLETED = 2,
};

// -----------------------------------------------------------------------------
// GuidedSetupConfig
// The whole record: where the run stands, which steps have been on screen, and
// whether anything was ever written at all.
// -----------------------------------------------------------------------------
struct GuidedSetupConfig {
    GuidedSetupRun run;
    bool recorded;  // false == no record exists; see the header note
    char visited[GUIDED_SETUP_VISITED_STR_MAX + 1];
};

// -----------------------------------------------------------------------------
// What a load had to repair
//
// A stored key whose form this image cannot accept is a damaged record, dropped
// and counted here rather than kept - the same treatment a damaged Servo Output
// row and an unknown Droid Design get. Counted rather than swallowed so the boot
// path can say out loud that a builder's record came back shorter than it went
// in.
// -----------------------------------------------------------------------------
struct GuidedSetupRepairReport {
    size_t stepsDropped;
    bool runRepaired;
};

inline bool guidedSetupRepairReportIsClean(const GuidedSetupRepairReport& report) {
    return report.stepsDropped == 0 && !report.runRepaired;
}

// -----------------------------------------------------------------------------
// The wire spelling of a run state
//
// Tokens rather than the stored number, for the reason every other id in this
// API is a token: firmware and the browser module ship in two separate steps
// (`make ota` and `make uploadfs`), and a number is the one form whose meaning
// can differ at the two ends of the wire.
// -----------------------------------------------------------------------------
inline const char* guidedSetupRunId(GuidedSetupRun run) {
    switch (run) {
        case GUIDED_SETUP_SKIPPED:
            return "skipped";
        case GUIDED_SETUP_COMPLETED:
            return "completed";
        case GUIDED_SETUP_NOT_RUN:
        default:
            return "not-run";
    }
}

// Resolves a wire token. Returns false for anything this image cannot name,
// leaving *out untouched: an unknown state is refused at the door rather than
// stored and puzzled over later.
inline bool guidedSetupRunFromId(const char* id, GuidedSetupRun* out) {
    if (id == nullptr || out == nullptr) {
        return false;
    }
    if (strcmp(id, "not-run") == 0) {
        *out = GUIDED_SETUP_NOT_RUN;
        return true;
    }
    if (strcmp(id, "skipped") == 0) {
        *out = GUIDED_SETUP_SKIPPED;
        return true;
    }
    if (strcmp(id, "completed") == 0) {
        *out = GUIDED_SETUP_COMPLETED;
        return true;
    }
    return false;
}

// A stored number this image does not name is a damaged record rather than a
// fourth state, and it reads as a run that has not ended - the state that offers
// the builder the run again, which is the recoverable way to be wrong here.
inline GuidedSetupRun guidedSetupRunFromStored(uint8_t stored) {
    switch (stored) {
        case GUIDED_SETUP_SKIPPED:
            return GUIDED_SETUP_SKIPPED;
        case GUIDED_SETUP_COMPLETED:
            return GUIDED_SETUP_COMPLETED;
        default:
            return GUIDED_SETUP_NOT_RUN;
    }
}

// -----------------------------------------------------------------------------
// guidedSetupStepKeyCharOk()
// The form a step key may take: lowercase, digits, and a leading underscore for
// a step that is shown rather than asked. Nothing here knows what the keys MEAN
// - this is the check that keeps an arbitrary request string out of NVS and out
// of the JSON payload it comes back in, not a vocabulary.
// -----------------------------------------------------------------------------
inline bool guidedSetupStepKeyCharOk(char c) {
    return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_';
}

inline bool guidedSetupStepKeyIsWellFormed(const char* key) {
    if (key == nullptr) {
        return false;
    }
    const size_t len = strlen(key);
    if (len == 0 || len > GUIDED_SETUP_STEP_KEY_MAX) {
        return false;
    }
    for (size_t i = 0; i < len; ++i) {
        if (!guidedSetupStepKeyCharOk(key[i])) {
            return false;
        }
    }
    return true;
}

// -----------------------------------------------------------------------------
// guidedSetupVisitedHas()
// Whether a step key is in the record. Whole-token match: a list holding
// "domectl" must not answer yes for "dome".
// -----------------------------------------------------------------------------
inline bool guidedSetupVisitedHas(const GuidedSetupConfig& cfg, const char* key) {
    if (key == nullptr || key[0] == '\0') {
        return false;
    }
    const size_t len = strlen(key);
    const char* cursor = cfg.visited;
    while (*cursor != '\0') {
        const char* comma = strchr(cursor, ',');
        const size_t span = (comma != nullptr) ? (size_t)(comma - cursor) : strlen(cursor);
        if (span == len && strncmp(cursor, key, len) == 0) {
            return true;
        }
        if (comma == nullptr) {
            break;
        }
        cursor = comma + 1;
    }
    return false;
}

// -----------------------------------------------------------------------------
// guidedSetupVisitedSet()
// Read a comma-separated visited list into *out, keeping the keys whose form
// this image accepts and dropping the rest. Returns how many were dropped, so a
// caller can report a repaired record rather than silently shortening it.
//
// `raw` nullptr or GUIDED_SETUP_VISITED_NONE both mean "nothing visited"; the
// caller decides what `recorded` should be, because that is a fact about whether
// a record EXISTED, which a value cannot carry.
//
// Duplicates collapse: the browser marks a step visited every time it draws it,
// and a list that grew a copy per redraw would be the record filling up rather
// than the run progressing.
// -----------------------------------------------------------------------------
inline size_t guidedSetupVisitedSet(GuidedSetupConfig* out, const char* raw) {
    if (out == nullptr) {
        return 0;
    }
    out->visited[0] = '\0';
    if (raw == nullptr || raw[0] == '\0' || strcmp(raw, GUIDED_SETUP_VISITED_NONE) == 0) {
        return 0;
    }

    size_t dropped = 0;
    size_t used = 0;
    const char* cursor = raw;
    while (*cursor != '\0') {
        const char* comma = strchr(cursor, ',');
        const size_t len = (comma != nullptr) ? (size_t)(comma - cursor) : strlen(cursor);

        char key[GUIDED_SETUP_STEP_KEY_MAX + 2] = {};
        if (len > 0 && len <= GUIDED_SETUP_STEP_KEY_MAX) {
            memcpy(key, cursor, len);
            key[len] = '\0';
        }

        if (!guidedSetupStepKeyIsWellFormed(key)) {
            // An empty run of commas is not a key anybody asked for, so it is
            // skipped rather than counted as damage; anything else is damage.
            if (len != 0) {
                dropped += 1;
            }
        } else if (guidedSetupVisitedHas(*out, key)) {
            // already carried - see the duplicate note above
        } else if (used + (used > 0 ? 1u : 0u) + len < sizeof(out->visited)) {
            if (used > 0) {
                out->visited[used++] = ',';
            }
            memcpy(out->visited + used, key, len);
            used += len;
            out->visited[used] = '\0';
        } else {
            // Past the declared width. Counted as dropped rather than written
            // part way: a truncated key would name a step nothing has.
            dropped += 1;
        }

        if (comma == nullptr) {
            break;
        }
        cursor = comma + 1;
    }
    return dropped;
}

// What goes to storage: the sentinel when nothing is visited, so the next read
// can tell a written record from one that was never written.
inline const char* guidedSetupVisitedStored(const GuidedSetupConfig& cfg) {
    return cfg.visited[0] == '\0' ? GUIDED_SETUP_VISITED_NONE : cfg.visited;
}

// -----------------------------------------------------------------------------
// guidedSetupDefaults()
// A controller guided Setup has never drawn on: the run has not ended, nothing
// has been shown, and no record exists.
// -----------------------------------------------------------------------------
inline void guidedSetupDefaults(GuidedSetupConfig* out) {
    if (out == nullptr) {
        return;
    }
    out->run = GUIDED_SETUP_NOT_RUN;
    out->recorded = false;
    out->visited[0] = '\0';
}
