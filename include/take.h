// =============================================================================
// include/take.h
//
// Performing a take: arming it, capturing it on RCInputTask, and handing the
// finished take to the store (#442, ADR 0061). FIRMWARE-ONLY. The capture rule
// is the pure include/take_capture.h; the files are include/take_store.h.
//
// WHERE IT RUNS. The targets a take captures are made on RCInputTask (Core 1,
// priority 5), by the puppet strings (include/rc_puppet.h), so the capture runs
// there too: the hooks below are called from rc_input.cpp, take a spinlock for
// a handful of stores, and never allocate, block or touch a file. Everything
// that does - arming, reading the status, writing the file - runs on Core 0
// from the web routes (src/web/api_take.cpp).
//
// THE BUFFER is one TakeCapture, taken once at setup() and only when the boot
// RC mode reads an SBUS receiver - a puppet string exists only on SBUS - the
// precedent rcInputAllocateDecoders() set (#428). Its size is the take file's
// cap plus the cue log: sizeof(TakeCapture) is 12,908 B on the artoo-esp32,
// from internal RAM (it has no PSRAM), and 25,196 B on firebeetle2, from PSRAM
// when the heap offers it. A droid with no SBUS receiver pays nothing.
//
// WHO HOLDS THE BUFFER. One stage, under a spinlock of its own:
//   IDLE        Core 0 may arm: it fills the buffer, then sets PERFORMING.
//   PERFORMING  RCInputTask stores into the buffer; nobody else reads it
//               except a few counters, under the lock.
//   STOPPED     the take ended - kept by the builder, full, or the estop -
//               and RCInputTask will not touch the buffer again.
//   HELD        Core 0 holds the buffer: finishing a take and writing its
//               file, or filling it for a take being armed.
// A take that stopped by itself waits in STOPPED until the Sequences page
// keeps it; arming a new take over it discards it.
//
// No Non-RC Control consent anywhere here: arming, performing and keeping a
// take is RC motion (ADR 0061, ADR 0064).
// =============================================================================
#pragma once

#include <stddef.h>
#include <stdint.h>

#include "rc_action_types.h"  // RobotActionId
#include "rc_input_step.h"    // RcInputStartupPlan
#include "take_capture.h"     // TakeCapture, TakeStop

// setup(): take the capture buffer if the boot RC plan reads SBUS. Call with
// the plan rcInputAllocateDecoders() was given, before RCInputTask starts.
void takeAllocate(const RcInputStartupPlan& plan);

// -----------------------------------------------------------------------------
// RCInputTask (Core 1)
// -----------------------------------------------------------------------------

// A puppet string's target was sent (or handled) for `part`.
void takeOnStringTarget(const char* part, uint16_t permille);

// An RC cue fired: its binding's action and payload, on the press.
void takeOnCue(RobotActionId action, const char* payload);

// Once every RCInputTask iteration, frames or none: closes each quantum, and
// stops the take at its size bound or when the estop latches.
void takeOnLoop(uint32_t nowMs);

// True once after a take is armed: RCInputTask then reports, through
// takeOnStringTarget(), the target each string already holds its Part at, so
// a Part held still across the arm is in the take from its first quantum
// rather than from its first move.
bool takeSeedWanted();

// -----------------------------------------------------------------------------
// Core 0
// -----------------------------------------------------------------------------

// Arm a take for the saved sequence `seqName`. nullptr when it is armed and
// performing; else the reason it is not, written into `refusal`.
const char* takeArm(const char* seqName, char* refusal, size_t refusalCap);

enum TakeStage : uint8_t {
    TAKE_STAGE_IDLE = 0,
    TAKE_STAGE_PERFORMING,
    TAKE_STAGE_STOPPED,
    TAKE_STAGE_HELD,
};

struct TakeStatus {
    bool available;          // the buffer exists on this boot
    TakeStage stage;
    TakeStop why;            // why it stopped, once it has
    char seqName[24];        // the sequence it was armed for
    uint32_t elapsedMs;      // how long it has been performing, or ran
    uint16_t fill;           // permille of its size bound used
    uint16_t samples;
    uint8_t partCount;
    char parts[TAKE_PARTS_MAX][TAKE_PART_ID_BYTES];
    uint8_t cues;
};

void takeStatusRead(TakeStatus* out);

// Keeping: the route stops a performing take, finishes it and owns the buffer
// until takeKeepEnd(). nullptr on success, with `*capture` the finished take
// and the owner's stable id and name filled; else the reason.
struct TakeKeep {
    const TakeCapture* capture;
    TakeStop why;
    char ownerId[17];
    char seqName[24];
};
const char* takeKeepBegin(TakeKeep* out, char* refusal, size_t refusalCap);

// Done with the buffer. `kept` false when writing the file failed: the take
// goes back to STOPPED and can be kept again.
void takeKeepEnd(bool kept);
