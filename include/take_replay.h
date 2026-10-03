// =============================================================================
// include/take_replay.h
//
// Replaying a sequence's takes beside its steps (#442 slice 3, ADR 0061).
//
// WHAT PLAYS. A Learned Sequence names its takes in its `takes` array, each
// with the moment it starts (`t`, ms from the run's start). When the sequence
// runs, the Sequence Coordinator plays each take from its file: every sample
// is the permille target the string commanded when it was performed, sent to
// ServoTask as a SERVO_CMD_PUPPET at the Output's own Motion Profile - the
// path the string itself took (include/rc_puppet.h), so a retarget keeps its
// speed (servoMotionRetarget()) and the replay goes through the same ramp the
// performance did. The Part is resolved to its Output when each target goes,
// never before, so a take survives a re-address; a Part nothing drives is
// reported as part-not-assigned once and the rest of the take plays.
//
// STREAMED, NOT LOADED. A take file is up to 12 KB (24 KB on the P4). Each
// take holds TAKE_REPLAY_BUF_SAMPLES samples at a time, read from LittleFS
// through the slice-2 reader (takeStoreReadSlice()) and checked as they
// arrive with the slice-2 checks (take_capture.h), and the whole state is one
// heap block taken when the run is loaded and given back when it ends.
//
// THE RULES, written once here and applied by takeReplayOutranked() and the
// Coordinator (src/tasks/sequence_dispatcher.cpp):
//   - A take COVERS a Part from its first sample of that Part until the take's
//     length runs out. Before its first sample it says nothing about the Part,
//     and once it is over it says nothing more: the Part stays where the last
//     sample put it - a commanded position, never a release. Output Release
//     then applies as it does after any move.
//   - LATER TAKE WINS. Where two takes cover one Part at once, the one later
//     in the sequence's `takes` array moves it. When the later one stops
//     covering it, the earlier one, if it still does, sends its own target
//     again: the later take wins only where they overlap.
//   - A STEP WINS. A Body Step's, a Gesture's or a flutter's move of a Part
//     holds every take off that Part until the move is over (the flutter's
//     whole swing). The take then moves the Part again on its next change; it
//     does not send its held target back, which would undo the step.
//   - THE END STEP CUTS. A take still playing when its run reaches its end
//     step stops there, as a Gesture and a flutter do, and commands nothing.
//
// Pure: no FreeRTOS, no Arduino, no clock, no file. The Coordinator does the
// reading and the sending, with the time passed in.
// =============================================================================
#pragma once

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "droid_parts.h"       // DROID_PART_COUNT
#include "take_capture.h"      // the file format, TAKE_NO_TARGET
#include "take_store_util.h"   // TAKE_ID_LEN

// Samples one take holds in RAM at a time. A take stores only changes, so a
// tick with every one of its Parts moving needs at most TAKE_PARTS_MAX; eight
// is one file read per tick at that worst, and one per several ticks
// otherwise.
static constexpr uint8_t TAKE_REPLAY_BUF_SAMPLES = 8;

// How long after a step's move is sent the takes stay off its Part even with
// the Output not yet reported moving: two ServoTask frames, so the move is
// taken before the Output's moving flag is trusted.
static constexpr uint32_t TAKE_REPLAY_STEP_SETTLE_MS = 40;

enum TakeReplayState : uint8_t {
    TAKE_REPLAY_WAITING = 0,  // its start has not come; its file is not open yet
    TAKE_REPLAY_PLAYING,
    TAKE_REPLAY_OVER,         // its length ran out, or its file could not be read
};

// One take of the running sequence.
struct TakeReplay {
    char id[TAKE_ID_LEN + 1];
    uint8_t state;                       // TakeReplayState
    uint8_t rateHz;
    uint8_t partCount;
    uint32_t atMs;                       // where it starts, from the run's start
    uint16_t lengthTicks;
    uint16_t sampleCount;
    uint16_t samplesRead;                // taken from the file into `buf` so far
    uint16_t prevTick;                   // the last sample read, for the order check
    uint8_t bufAt;
    uint8_t bufCount;
    uint16_t held;                       // bit per Part: a step's move holds it
    uint16_t told;                       // bit per Part: part-not-assigned said
    uint32_t buf[TAKE_REPLAY_BUF_SAMPLES];
    uint8_t part[TAKE_PARTS_MAX];        // catalog index, DROID_PART_COUNT for an id it lacks
    uint16_t cur[TAKE_PARTS_MAX];        // its target now, TAKE_NO_TARGET before the first
    uint16_t sent[TAKE_PARTS_MAX];       // the target last sent, TAKE_NO_TARGET when one is owed
};
static_assert(TAKE_PARTS_MAX <= 16, "held and told are 16-bit masks");

// The takes of one run: this header, then `count` TakeReplay entries, in one
// heap block (takeReplayAt()).
struct TakeReplayRun {
    char owner[17];        // the sequence's stable id, the take files' owner
    uint8_t count;
    uint32_t startMs;      // the run's start
    uint32_t stepAtMs;     // the last step move that held a Part
};
static_assert(sizeof(TakeReplayRun) % alignof(TakeReplay) == 0, "entries follow aligned");

inline size_t takeReplayRunBytes(uint8_t count) {
    return sizeof(TakeReplayRun) + (size_t)count * sizeof(TakeReplay);
}

inline TakeReplay* takeReplayAt(TakeReplayRun* run, uint8_t i) {
    return reinterpret_cast<TakeReplay*>(run + 1) + i;
}

// The header facts the file's fixed 16 bytes gave, onto the entry, ready to
// play: every Part not yet covered and owed nothing.
inline void takeReplayBegin(TakeReplay* t, const TakeFileInfo& info) {
    t->rateHz = info.rateHz;
    t->partCount = info.partCount;
    t->lengthTicks = (uint16_t)info.lengthTicks;
    t->sampleCount = (uint16_t)info.sampleCount;
    t->samplesRead = 0;
    t->prevTick = 0;
    t->bufAt = 0;
    t->bufCount = 0;
    t->held = 0;
    t->told = 0;
    for (uint8_t p = 0; p < TAKE_PARTS_MAX; ++p) {
        t->cur[p] = TAKE_NO_TARGET;
        t->sent[p] = TAKE_NO_TARGET;
    }
    t->state = TAKE_REPLAY_PLAYING;
}

inline TakeFileInfo takeReplayInfo(const TakeReplay& t) {
    return TakeFileInfo{t.rateHz, t.partCount, t.lengthTicks, t.sampleCount};
}

// -----------------------------------------------------------------------------
// takeReplayConsume()
// Take every buffered sample due by `runMs` into the Parts' current targets.
// Returns true when the buffer ran dry with samples still in the file - the
// Coordinator refills it and asks again - and false when every due sample is
// taken. Ends the take once its length has run out.
// -----------------------------------------------------------------------------
inline bool takeReplayConsume(TakeReplay* t, uint32_t runMs) {
    const uint32_t intoMs = runMs - t->atMs;
    if (intoMs >= takeTicksToMs(t->lengthTicks, t->rateHz)) {
        t->state = TAKE_REPLAY_OVER;
        return false;
    }
    const uint32_t tick = intoMs * t->rateHz / 1000u;
    while (t->bufAt < t->bufCount) {
        const uint32_t s = t->buf[t->bufAt];
        if (takeSampleTick(s) > tick) {
            return false;
        }
        t->cur[takeSamplePart(s)] = takeSamplePermille(s);
        ++t->bufAt;
    }
    return t->samplesRead < t->sampleCount;
}

// Whether take `t` covers the Part at catalog index `part` now, and where.
inline bool takeReplayCovers(const TakeReplay& t, uint8_t part, uint8_t* at) {
    if (t.state != TAKE_REPLAY_PLAYING) return false;
    for (uint8_t p = 0; p < t.partCount; ++p) {
        if (t.part[p] == part && t.cur[p] != TAKE_NO_TARGET) {
            *at = p;
            return true;
        }
    }
    return false;
}

// Whether a take later in the array than `i` covers its Part `p` now: then
// that one moves it, and take `i` owes its own target once it stops (LATER
// TAKE WINS, above).
inline bool takeReplayOutranked(TakeReplayRun* run, uint8_t i, uint8_t p) {
    const uint8_t part = takeReplayAt(run, i)->part[p];
    uint8_t at = 0;
    for (uint8_t j = (uint8_t)(i + 1); j < run->count; ++j) {
        if (takeReplayCovers(*takeReplayAt(run, j), part, &at)) return true;
    }
    return false;
}

// A step's move of the Part at catalog index `part` was sent at `nowMs`: every
// take holds off it (A STEP WINS, above).
inline void takeReplayStepMoved(TakeReplayRun* run, size_t part, uint32_t nowMs) {
    if (run == nullptr || part >= DROID_PART_COUNT) return;
    for (uint8_t i = 0; i < run->count; ++i) {
        TakeReplay* t = takeReplayAt(run, i);
        for (uint8_t p = 0; p < t->partCount; ++p) {
            if (t->part[p] == part) t->held |= (uint16_t)(1u << p);
        }
    }
    run->stepAtMs = nowMs;
}
