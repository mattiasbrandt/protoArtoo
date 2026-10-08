// =============================================================================
// include/seq_tempo.h
//
// A Sequence Tempo, and the arithmetic that turns a beat into the millisecond
// the engine runs (ADR 0058, ADR 0060, #438).
//
// A sequence may carry a tempo as a top-level optional key at `format: 1`, so
// no sequence saved before it is orphaned. A step placed on a beat keeps the
// beat BESIDE its millisecond: the beat is what the builder meant, and the
// millisecond is what the engine runs. The beat is resolved here, when the
// sequence is parsed, and never reaches SeqStep -- so the engine, its single
// timing cursor, the pose planner and the Rehearsal's expansion all see the
// plain milliseconds they always did, and nothing about run-time timing is
// less deterministic than before. Changing the BPM therefore re-resolves every
// beat-placed step on the next load and leaves every millisecond-placed step
// where it was, with nothing to undo.
//
// The grid is the reference's (r2d2-astromech-simulator v1.79.0
// src/js/maestro/music.js:44-46): beat k sits at `phase + k * 60/bpm`,
// `barLen` beats make a bar, and `barPhase` is the beat index a bar starts on.
// Times here are milliseconds, like every other time in the format; the
// reference's are seconds. The downbeat is SET BY THE BUILDER (ADR 0060) --
// a tapped tempo's first tap is it -- because the reference's bar fit is
// degenerate and always answers 0.
//
// The BPM is held in tenths, which is the precision the format writes it at
// (one decimal, as the reference rounds it), so the conversion is integer
// arithmetic and the browser can do exactly the same sum: a beat that resolves
// to 1846 ms here resolves to 1846 ms on the screen that placed it.
//
// Pure: no Arduino, no JSON, no heap. The wire codec (src/seq_json.cpp) fills
// a SeqTempo and asks Protocol Check whether it is well formed
// (protocolCheckTempo()) before a single beat is resolved against it.
// =============================================================================

#pragma once

#include <stdint.h>
#include <string.h>

// Where the tempo came from (ADR 0058). Every source is stored with its name,
// because "how sure was it" means something different for each and the
// surface says which.
enum SeqTempoSource : uint8_t {
    SEQ_TEMPO_TYPED    = 0,  // the builder typed a number
    SEQ_TEMPO_TAPPED   = 1,  // the builder tapped along to the track on the droid
    SEQ_TEMPO_ANALYSED = 2,  // the ported analyzer read the builder's own copy
    SEQ_TEMPO_SOURCE_COUNT = 3,
};

inline const char* seqTempoSourceToString(uint8_t source) {
    switch (source) {
        case SEQ_TEMPO_TAPPED:   return "tapped";
        case SEQ_TEMPO_ANALYSED: return "analysed";
        case SEQ_TEMPO_TYPED:
        default:                 return "typed";
    }
}

// The wire token -> the value, walking the spelling above so the two
// directions have one home.
inline bool seqTempoSourceFromString(const char* s, uint8_t& out) {
    if (s == nullptr) return false;
    for (uint8_t source = 0; source < SEQ_TEMPO_SOURCE_COUNT; ++source) {
        if (strcmp(seqTempoSourceToString(source), s) == 0) {
            out = source;
            return true;
        }
    }
    return false;
}

// -----------------------------------------------------------------------------
// Bounds. Derived from limits this model already accepts, not chosen.
//
//   BPM  a beat lasts between PC_LOOP_PERIOD_MIN (100 ms) and
//        PC_LOOP_PERIOD_MAX (60000 ms) -- the shortest and longest repeat the
//        format already allows -- so a tempo runs from 1 to 600 BPM. Held in
//        tenths.
//   BEAT the longest a sequence can run is PC_SUPPRESS_MAX_MS (120000 ms), and
//        at the fastest tempo that is 1200 beats. A beat past it could only
//        ever place a step after the end the sequence is allowed to have.
//   BAR  sixteen beats. A bar longer than that is a phrase; this is the one
//        figure here that is chosen rather than derived.
//
// These mirror protocol_check.h's own numbers by value because this header
// is pure and protocol_check.h includes it; the static_asserts in
// src/protocol_check.cpp are what keep the two from drifting.
// -----------------------------------------------------------------------------
constexpr uint16_t SEQ_TEMPO_BPM_TENTHS_MIN = 10;     // 1.0 BPM   = 60000 ms a beat
constexpr uint16_t SEQ_TEMPO_BPM_TENTHS_MAX = 6000;   // 600.0 BPM = 100 ms a beat
constexpr uint16_t SEQ_TEMPO_BEAT_MAX       = 1200;
constexpr uint8_t  SEQ_TEMPO_BAR_LEN_MAX    = 16;
constexpr uint8_t  SEQ_TEMPO_BAR_LEN_DEFAULT = 4;
constexpr uint32_t SEQ_TEMPO_PHASE_MAX_MS   = 120000;
// A track is not a sequence, and can run longer than one: an hour is the
// bound, so a value past it is a unit mistake rather than a long song.
constexpr uint32_t SEQ_TEMPO_DURATION_MAX_MS = 3600000;
// Confidence is 0..1 on every source, held in thousandths.
constexpr uint16_t SEQ_TEMPO_CONFIDENCE_MAX = 1000;
// A hash is lowercase hex, and only the analysed route has one.
constexpr uint8_t  SEQ_TEMPO_HASH_MAX_LEN   = 64;

struct SeqTempo {
    uint16_t bpmTenths;           // 1200 = 120.0 BPM
    uint32_t phaseMs;             // where beat 0 sits, from the sequence start
    uint8_t  barLen;              // beats in a bar
    uint8_t  barPhase;            // the beat index a bar starts on
    uint32_t durationMs;          // how long the track runs; 0 when unknown
    uint8_t  source;              // SeqTempoSource
    uint16_t confidencePermille;  // 0..1000
    bool     hasHash;             // a hash was stored beside it
};

// -----------------------------------------------------------------------------
// seqTempoBeatMs() / seqTempoSpanMs()
// Where beat `beat` falls, and how long `beats` beats last, in whole
// milliseconds, rounded half up: (2 * beat * 600000 + tenths) / (2 * tenths)
// is round(beat * 60000 / bpm). The browser writes the same sum
// (Math.round(beat * 600000 / bpmTenths)), so both sides land on one number.
//
// Both assume a tempo protocolCheckTempo() has accepted: a zero BPM is not a
// tempo, and the guard below only keeps a caller that skipped the check from
// dividing by it.
// -----------------------------------------------------------------------------
inline uint32_t seqTempoSpanMs(const SeqTempo& tempo, uint32_t beats) {
    if (tempo.bpmTenths == 0) return 0;
    const uint64_t numerator = 2ull * (uint64_t)beats * 600000ull + (uint64_t)tempo.bpmTenths;
    return (uint32_t)(numerator / (2ull * (uint64_t)tempo.bpmTenths));
}

inline uint32_t seqTempoBeatMs(const SeqTempo& tempo, uint32_t beat) {
    return tempo.phaseMs + seqTempoSpanMs(tempo, beat);
}
