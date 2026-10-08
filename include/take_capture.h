// =============================================================================
// include/take_capture.h
//
// Capturing a take, and the take file's encoding (#442, ADR 0061).
//
// WHAT IS CAPTURED. The commanded targets of the Parts that had puppet strings
// when the take was armed: the share of each Part's throw its string sent
// (include/rc_puppet.h), in permille - never the raw stick, and never a width
// or an Output Address. Replay resolves a permille against whichever Output
// drives the Part then, the way a Body Step's howFar is resolved, so a take
// survives a re-address and a re-calibration, and goes through the Output's own
// Motion Profile exactly as the performance did.
//
// SILENCE IS A LONGER FRAME (r2d2-astromech-simulator puppet.js:222-227). The
// targets are sampled at a fixed quantum, TAKE_TICK_MS, and a sample is stored
// only for a Part whose target changed since its last one. Stillness stores
// nothing, and the gap between two samples is how long the Part held. When the
// take stops, the dead air after the last change is cut to TAKE_TAIL_MS.
//
// THE ZERO THAT MEANT TWO THINGS (puppet.js:321-329). Every value a take file
// holds is a position. A Part the take does not cover is not in its file, and
// a Part covered but not yet moved has no sample - absent, never a value that
// means "leave it alone" on one path and "let go" on another. The one sentinel
// below, TAKE_NO_TARGET, lives only in RAM while capturing and is never written.
//
// Pure: no FreeRTOS, no Arduino, no clock. src/take.cpp runs it on RCInputTask
// with millis() passed in.
// =============================================================================
#pragma once

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "droid_parts.h"       // DROID_PART_ID_MAX_LEN
#include "rc_action_types.h"   // RobotActionId
#include "take_store_util.h"   // TAKE_FILE_MAX_BYTES

// The quantum: 20 Hz, the reference's RECMS of 50. The take file records it.
// A string sends at most once a ServoTask frame (20 ms); a sample every 50 ms
// keeps the latest of those, and the Output's Motion Profile carries the move
// between two of them on replay as it carried the string's targets live.
static constexpr uint32_t TAKE_TICK_MS = 50;
static constexpr uint8_t TAKE_RATE_HZ = (uint8_t)(1000 / TAKE_TICK_MS);

// Trailing stillness is cut to this when a take stops (puppet.js:247-250).
static constexpr uint32_t TAKE_TAIL_MS = 300;
static constexpr uint16_t TAKE_TAIL_TICKS = (uint16_t)(TAKE_TAIL_MS / TAKE_TICK_MS);

// The last tick a sample can carry: 16 bits at 20 Hz is 54 minutes. A take
// still running there stops as full.
static constexpr uint16_t TAKE_TICK_MAX = 0xFFFF;

// How many Parts a take covers: one per string, and a string sits in a trigger
// slot (RC_TRIGGER_MAX, include/rc_input_processor.h; src/take.cpp asserts the
// two agree).
static constexpr uint8_t TAKE_PARTS_MAX = 11;
static constexpr size_t TAKE_PART_ID_BYTES = DROID_PART_ID_MAX_LEN + 1;

// A full permille is the open end of the Part's throw (RC_PUPPET_FULL_PERMILLE).
static constexpr uint16_t TAKE_FULL_PERMILLE = 1000;

// In RAM only: a Part armed into the take whose string has sent nothing yet.
static constexpr uint16_t TAKE_NO_TARGET = 0xFFFF;

// -----------------------------------------------------------------------------
// The take file, little-endian, every field fixed-width:
//
//   0   "PATK"            magic
//   4   u8  format        1
//   5   u8  rateHz        TAKE_RATE_HZ
//   6   u8  partCount     1..TAKE_PARTS_MAX
//   7   u8  reserved      0
//   8   u32 lengthTicks   how long the take runs, in ticks of 1000/rateHz ms
//   12  u32 sampleCount
//   16  partCount x 11 B  Droid Parts Catalog ids, NUL-padded
//   ..  sampleCount x u32 tick << 16 | partIndex << 10 | permille
//
// Samples are in tick order; within one tick a Part appears at most once.
// -----------------------------------------------------------------------------
static constexpr uint8_t TAKE_FILE_FORMAT = 1;
static constexpr size_t TAKE_FILE_FIXED_BYTES = 16;
static constexpr size_t TAKE_SAMPLE_BYTES = 4;

inline size_t takeFileHeaderBytes(uint8_t partCount) {
    return TAKE_FILE_FIXED_BYTES + (size_t)partCount * TAKE_PART_ID_BYTES;
}

// The samples a file can hold at the board's cap, whatever its Part count.
static constexpr size_t TAKE_SAMPLES_MAX =
    (TAKE_FILE_MAX_BYTES - (TAKE_FILE_FIXED_BYTES + TAKE_PARTS_MAX * TAKE_PART_ID_BYTES)) /
    TAKE_SAMPLE_BYTES;

inline uint32_t takeSamplePack(uint16_t tick, uint8_t part, uint16_t permille) {
    return ((uint32_t)tick << 16) | ((uint32_t)(part & 0x3F) << 10) | (uint32_t)(permille & 0x3FF);
}
inline uint16_t takeSampleTick(uint32_t s) { return (uint16_t)(s >> 16); }
inline uint8_t takeSamplePart(uint32_t s) { return (uint8_t)((s >> 10) & 0x3F); }
inline uint16_t takeSamplePermille(uint32_t s) { return (uint16_t)(s & 0x3FF); }

// Cue presses: an RC cue fired during the take. Not motion and not in the take
// file - each becomes an ordinary step in the sequence, which the Sequences
// page builds from the action and its payload (ADR 0061, "cue presses become
// ordinary steps beside the take"). A take holds this many; presses past it
// are counted, and the receipt says how many were not kept.
static constexpr uint8_t TAKE_CUES_MAX = 24;

struct TakeCue {
    uint32_t ms;                 // from the moment the take was armed
    RobotActionId action;
    char payload[16];            // the binding's payload (RcTriggerBinding::marcduinoPayload)
};

// Why a take stopped.
enum TakeStop : uint8_t {
    TAKE_STOP_NONE = 0,
    TAKE_STOP_KEEP,    // the builder kept it
    TAKE_STOP_FULL,    // it reached its size bound
    TAKE_STOP_ESTOP,   // the estop latched
};

// One take being captured, and then the take it became. The whole of it is the
// buffer src/take.cpp takes at setup(); nothing here allocates.
struct TakeCapture {
    uint32_t startMs;
    uint8_t partCount;
    char parts[TAKE_PARTS_MAX][TAKE_PART_ID_BYTES];
    uint16_t latest[TAKE_PARTS_MAX];   // the last target each Part's string sent, or TAKE_NO_TARGET
    uint16_t stored[TAKE_PARTS_MAX];   // the last target stored as a sample, or TAKE_NO_TARGET
    uint16_t openTick;                 // the quantum whose targets are still being collected
    uint16_t lastSampleTick;
    uint16_t lengthTicks;              // set when it stops
    bool full;
    uint16_t sampleCount;
    uint8_t cueCount;
    uint16_t cuesPast;                 // presses past TAKE_CUES_MAX, counted and not kept
    TakeCue cues[TAKE_CUES_MAX];
    uint32_t samples[TAKE_SAMPLES_MAX];
};
static_assert(TAKE_SAMPLES_MAX <= 0xFFFF, "sampleCount is 16 bits in RAM");

// -----------------------------------------------------------------------------
// takeCaptureBegin() / takeCaptureAddPart()
// A take armed at `nowMs`, then each Part a string moves added once. A Part a
// second string also names is the same Part (one Part has one string, slice 1;
// a duplicate here would only waste a slot).
// -----------------------------------------------------------------------------
inline void takeCaptureBegin(TakeCapture* c, uint32_t nowMs) {
    c->startMs = nowMs;
    c->partCount = 0;
    c->openTick = 0;
    c->lastSampleTick = 0;
    c->lengthTicks = 0;
    c->full = false;
    c->sampleCount = 0;
    c->cueCount = 0;
    c->cuesPast = 0;
}

inline uint8_t takeCaptureFindPart(const TakeCapture* c, const char* part) {
    for (uint8_t i = 0; i < c->partCount; ++i) {
        if (strncmp(c->parts[i], part, TAKE_PART_ID_BYTES) == 0) {
            return i;
        }
    }
    return TAKE_PARTS_MAX;
}

inline bool takeCaptureAddPart(TakeCapture* c, const char* part) {
    if (part == nullptr || part[0] == '\0' || takeCaptureFindPart(c, part) < TAKE_PARTS_MAX) {
        return false;
    }
    if (c->partCount >= TAKE_PARTS_MAX) {
        return false;
    }
    const uint8_t i = c->partCount++;
    strncpy(c->parts[i], part, TAKE_PART_ID_BYTES - 1);
    c->parts[i][TAKE_PART_ID_BYTES - 1] = '\0';
    c->latest[i] = TAKE_NO_TARGET;
    c->stored[i] = TAKE_NO_TARGET;
    return true;
}

// A string commanded its Part: the latest target in this quantum wins.
inline void takeCaptureTarget(TakeCapture* c, uint8_t partIndex, uint16_t permille) {
    if (partIndex < c->partCount && permille <= TAKE_FULL_PERMILLE) {
        c->latest[partIndex] = permille;
    }
}

inline uint16_t takeCaptureTickAt(const TakeCapture* c, uint32_t nowMs) {
    const uint32_t ticks = (uint32_t)(nowMs - c->startMs) / TAKE_TICK_MS;
    return (ticks > TAKE_TICK_MAX) ? TAKE_TICK_MAX : (uint16_t)ticks;
}

// A cue fired `nowMs`.
inline void takeCaptureCue(TakeCapture* c, uint32_t nowMs, RobotActionId action, const char* payload) {
    if (c->cueCount >= TAKE_CUES_MAX) {
        if (c->cuesPast < 0xFFFF) ++c->cuesPast;
        return;
    }
    TakeCue& cue = c->cues[c->cueCount++];
    cue.ms = (uint32_t)(nowMs - c->startMs);
    cue.action = action;
    memset(cue.payload, 0, sizeof(cue.payload));
    if (payload != nullptr) {
        strncpy(cue.payload, payload, sizeof(cue.payload) - 1);
    }
}

// Store, stamped `tick`, every Part whose target changed since its last sample.
// Marks the take full, and stops storing, at its size bound.
inline void takeCaptureStoreChanges(TakeCapture* c, uint16_t tick) {
    for (uint8_t i = 0; i < c->partCount && !c->full; ++i) {
        if (c->latest[i] == TAKE_NO_TARGET || c->latest[i] == c->stored[i]) {
            continue;
        }
        if (c->sampleCount >= TAKE_SAMPLES_MAX) {
            c->full = true;
            break;
        }
        c->samples[c->sampleCount++] = takeSamplePack(tick, i, c->latest[i]);
        c->stored[i] = c->latest[i];
        c->lastSampleTick = tick;
    }
}

// -----------------------------------------------------------------------------
// takeCaptureAdvance()
// Once every RCInputTask iteration. When the clock has moved into a new
// quantum, the one that closed is stored. Returns true when the take has
// reached its size bound - in samples, or in time at TAKE_TICK_MAX - and must
// stop.
// -----------------------------------------------------------------------------
inline bool takeCaptureAdvance(TakeCapture* c, uint32_t nowMs) {
    const uint16_t tick = takeCaptureTickAt(c, nowMs);
    if (tick > c->openTick) {
        takeCaptureStoreChanges(c, c->openTick);
        c->openTick = tick;
    }
    return c->full || tick >= TAKE_TICK_MAX;
}

// How much of its bound a take has used, in permille: the larger of samples
// and time.
inline uint16_t takeCaptureFill(const TakeCapture* c, uint32_t nowMs) {
    const uint32_t bySamples = (uint32_t)c->sampleCount * 1000u / (uint32_t)TAKE_SAMPLES_MAX;
    const uint32_t byTime = (uint32_t)takeCaptureTickAt(c, nowMs) * 1000u / TAKE_TICK_MAX;
    const uint32_t fill = (bySamples > byTime) ? bySamples : byTime;
    return (uint16_t)((fill > 1000u) ? 1000u : fill);
}

// The fill at which a take says it is nearly full: a tenth of its room left.
static constexpr uint16_t TAKE_NEARLY_FULL_PERMILLE = 900;

// -----------------------------------------------------------------------------
// takeCaptureFinish()
// The take stops at `nowMs`. The open quantum is stored, the trailing
// stillness cut to TAKE_TAIL_MS after the last change, and a Part that never
// moved is dropped from the take - it covers nothing, so it claims nothing.
// A take with no sample at all has lengthTicks 0: there is no take to keep,
// and only its cue presses remain.
// -----------------------------------------------------------------------------
inline void takeCaptureFinish(TakeCapture* c, uint32_t nowMs) {
    takeCaptureStoreChanges(c, c->openTick);
    if (c->sampleCount == 0) {
        c->lengthTicks = 0;
        c->partCount = 0;
        return;
    }
    const uint16_t endTick = takeCaptureTickAt(c, nowMs);
    const uint32_t trimmed = (uint32_t)c->lastSampleTick + TAKE_TAIL_TICKS;
    const uint32_t atLeast = (uint32_t)c->lastSampleTick + 1u;
    uint32_t length = (endTick > atLeast) ? endTick : atLeast;
    if (length > trimmed) length = trimmed;
    c->lengthTicks = (length > TAKE_TICK_MAX) ? TAKE_TICK_MAX : (uint16_t)length;

    // Drop the Parts with no sample, renumbering the rest in order.
    uint8_t remap[TAKE_PARTS_MAX];
    bool moved[TAKE_PARTS_MAX] = {};
    for (uint16_t k = 0; k < c->sampleCount; ++k) {
        moved[takeSamplePart(c->samples[k])] = true;
    }
    uint8_t kept = 0;
    for (uint8_t i = 0; i < c->partCount; ++i) {
        remap[i] = kept;
        if (!moved[i]) continue;
        if (kept != i) {
            memcpy(c->parts[kept], c->parts[i], TAKE_PART_ID_BYTES);
        }
        ++kept;
    }
    if (kept != c->partCount) {
        for (uint16_t k = 0; k < c->sampleCount; ++k) {
            const uint32_t s = c->samples[k];
            c->samples[k] = takeSamplePack(takeSampleTick(s), remap[takeSamplePart(s)],
                                           takeSamplePermille(s));
        }
    }
    c->partCount = kept;
}

inline uint32_t takeTicksToMs(uint32_t ticks, uint8_t rateHz) {
    return (rateHz == 0) ? 0 : ticks * 1000u / rateHz;
}

// -----------------------------------------------------------------------------
// The file header, written and read byte by byte so neither depends on a
// struct's padding.
// -----------------------------------------------------------------------------
inline void takePutU32(uint8_t* p, uint32_t v) {
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}
inline uint32_t takeGetU32(const uint8_t* p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

// The header of a finished capture into `out`, which holds at least
// takeFileHeaderBytes(c->partCount). Returns the bytes written.
inline size_t takeFileWriteHeader(const TakeCapture* c, uint8_t* out) {
    out[0] = 'P';
    out[1] = 'A';
    out[2] = 'T';
    out[3] = 'K';
    out[4] = TAKE_FILE_FORMAT;
    out[5] = TAKE_RATE_HZ;
    out[6] = c->partCount;
    out[7] = 0;
    takePutU32(out + 8, c->lengthTicks);
    takePutU32(out + 12, c->sampleCount);
    for (uint8_t i = 0; i < c->partCount; ++i) {
        uint8_t* id = out + TAKE_FILE_FIXED_BYTES + (size_t)i * TAKE_PART_ID_BYTES;
        memset(id, 0, TAKE_PART_ID_BYTES);
        memcpy(id, c->parts[i], strnlen(c->parts[i], TAKE_PART_ID_BYTES - 1));
    }
    return takeFileHeaderBytes(c->partCount);
}

// What a file's fixed header says. A restore checks a file against this as it
// arrives (src/take_store.cpp) - it is never trusted whole.
struct TakeFileInfo {
    uint8_t rateHz;
    uint8_t partCount;
    uint32_t lengthTicks;
    uint32_t sampleCount;
};

// The fixed 16 bytes: the magic, a format this firmware reads, a rate, a Part
// count in range, and a size inside the board's cap.
inline bool takeFileReadFixed(const uint8_t* in, TakeFileInfo* out) {
    if (in[0] != 'P' || in[1] != 'A' || in[2] != 'T' || in[3] != 'K' || in[4] != TAKE_FILE_FORMAT) {
        return false;
    }
    out->rateHz = in[5];
    out->partCount = in[6];
    out->lengthTicks = takeGetU32(in + 8);
    out->sampleCount = takeGetU32(in + 12);
    if (out->rateHz == 0 || out->rateHz > 50 || out->partCount == 0 ||
        out->partCount > TAKE_PARTS_MAX || out->lengthTicks == 0 ||
        out->lengthTicks > TAKE_TICK_MAX || out->sampleCount == 0) {
        return false;
    }
    return takeFileHeaderBytes(out->partCount) + (size_t)out->sampleCount * TAKE_SAMPLE_BYTES <=
           TAKE_FILE_MAX_BYTES;
}

// One Part id from the header: letters, digits and underscore, 1..10 of them,
// NUL-padded to its 11 bytes.
inline bool takeFilePartIdValid(const uint8_t* id) {
    size_t n = 0;
    while (n < TAKE_PART_ID_BYTES && id[n] != 0) {
        const char c = (char)id[n];
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
              c == '_')) {
            return false;
        }
        ++n;
    }
    if (n == 0 || n >= TAKE_PART_ID_BYTES) {
        return false;
    }
    for (size_t k = n; k < TAKE_PART_ID_BYTES; ++k) {
        if (id[k] != 0) return false;
    }
    return true;
}

// One sample, given the one before it (`prevTick`, 0 for the first): in tick
// order, inside the take, on a Part the header names, at a position on the
// throw.
inline bool takeFileSampleValid(uint32_t s, uint16_t prevTick, const TakeFileInfo& info) {
    return takeSampleTick(s) >= prevTick && takeSampleTick(s) < info.lengthTicks &&
           takeSamplePart(s) < info.partCount && takeSamplePermille(s) <= TAKE_FULL_PERMILLE;
}
