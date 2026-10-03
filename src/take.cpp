// =============================================================================
// src/take.cpp
//
// Performing a take (#442, ADR 0061). The contract, and who holds the buffer
// when, is include/take.h.
// =============================================================================

#include "take.h"

#include <Arduino.h>
#include <esp_heap_caps.h>
#include <freertos/FreeRTOS.h>

#include <new>

#include "config_cache.h"         // trigger slots, RC mode, SBUS timeout
#include "config_store.h"         // RC_TRIGGER_SLOT_COUNT
#include "logging.h"
#include "rc_input_processor.h"   // RC_TRIGGER_MAX
#include "rc_puppet.h"            // rcPuppetChannelCanMove()
#include "robot_state.h"          // robotState, robotStateMux
#include "seq_store_index.h"      // the sequence a take is armed for
#include "take_store.h"           // room for the take

static const char* TAG = "TAKE";

static_assert(TAKE_PARTS_MAX >= RC_TRIGGER_MAX,
              "a take must cover every string the trigger slots can hold");

// The buffer a take is captured into: taken by the arm that needs it and given
// back when the take is kept, so an idle droid holds none of it (critic, #442
// round 1). Written only under s_takeMux, and only by Core 0.
//
// WHY A CORE 1 HOOK NEVER SEES IT FREED. Every hook reads s_capture inside
// s_takeMux and only while the stage is PERFORMING, and the stage is PERFORMING
// only between an arm that published a live buffer and the stop that ends it.
// Core 0 frees the buffer in one place, takeKeepEnd(), and only after setting
// the stage to IDLE and the pointer to null under the same lock. A hook either
// holds the lock first - and the buffer is still there - or after, and finds the
// stage IDLE and touches nothing.
static TakeCapture* s_capture = nullptr;

// The stage and what was armed, under s_takeMux. Small and fixed; the buffer
// above is what is big.
static portMUX_TYPE s_takeMux = portMUX_INITIALIZER_UNLOCKED;
static TakeStage s_stage = TAKE_STAGE_IDLE;
static TakeStop s_why = TAKE_STOP_NONE;
static uint32_t s_stopMs = 0;
static bool s_finished = false;
static bool s_seedWanted = false;
static char s_ownerId[17] = {};
static char s_seqName[24] = {};

// -----------------------------------------------------------------------------
// RCInputTask
// -----------------------------------------------------------------------------
void takeOnStringTarget(const char* part, uint16_t permille) {
    if (part == nullptr) return;
    taskENTER_CRITICAL(&s_takeMux);
    if (s_stage == TAKE_STAGE_PERFORMING && s_capture != nullptr) {
        takeCaptureTarget(s_capture, takeCaptureFindPart(s_capture, part), permille);
    }
    taskEXIT_CRITICAL(&s_takeMux);
}

void takeOnCue(RobotActionId action, const char* payload) {
    const uint32_t nowMs = millis();
    taskENTER_CRITICAL(&s_takeMux);
    if (s_stage == TAKE_STAGE_PERFORMING && s_capture != nullptr) {
        takeCaptureCue(s_capture, nowMs, action, payload);
    }
    taskEXIT_CRITICAL(&s_takeMux);
}

void takeOnLoop(uint32_t nowMs) {
    bool estop = false;
    taskENTER_CRITICAL(&robotStateMux);
    estop = robotState.estop;
    taskEXIT_CRITICAL(&robotStateMux);

    TakeStop stopped = TAKE_STOP_NONE;
    taskENTER_CRITICAL(&s_takeMux);
    if (s_stage == TAKE_STAGE_PERFORMING && s_capture != nullptr) {
        // The quantum closes first, so an estop keeps what the last one held.
        const bool full = takeCaptureAdvance(s_capture, nowMs);
        if (estop) {
            stopped = TAKE_STOP_ESTOP;
        } else if (full) {
            stopped = TAKE_STOP_FULL;
        }
        if (stopped != TAKE_STOP_NONE) {
            s_stage = TAKE_STAGE_STOPPED;
            s_why = stopped;
            s_stopMs = nowMs;
            s_finished = false;
        }
    }
    taskEXIT_CRITICAL(&s_takeMux);
    if (stopped == TAKE_STOP_ESTOP) {
        PA_LOG_INFO(TAG, "take stopped by the estop; what was performed is kept");
    } else if (stopped == TAKE_STOP_FULL) {
        PA_LOG_INFO(TAG, "take stopped: full");
    }
}

bool takeSeedWanted() {
    taskENTER_CRITICAL(&s_takeMux);
    const bool wanted = s_seedWanted && s_stage == TAKE_STAGE_PERFORMING;
    s_seedWanted = false;
    taskEXIT_CRITICAL(&s_takeMux);
    return wanted;
}

// -----------------------------------------------------------------------------
// Arming
// -----------------------------------------------------------------------------
static const char* refuse(char* out, size_t cap, const char* why) {
    snprintf(out, cap, "%s", why);
    return out;
}

const char* takeArm(const char* seqName, char* refusal, size_t refusalCap) {
    taskENTER_CRITICAL(&s_takeMux);
    const TakeStage stage = s_stage;
    taskEXIT_CRITICAL(&s_takeMux);
    if (stage == TAKE_STAGE_PERFORMING || stage == TAKE_STAGE_HELD) {
        return refuse(refusal, refusalCap, "A take is already running.");
    }

    bool estop = false;
    uint32_t lastSbus1Ms = 0;
    uint32_t lastSbus2Ms = 0;
    bool sbus1Failed = false;
    bool sbus2Failed = false;
    taskENTER_CRITICAL(&robotStateMux);
    estop = robotState.estop;
    lastSbus1Ms = robotState.lastSbus1Ms;
    lastSbus2Ms = robotState.lastSbus2Ms;
    // Every frame stamps its receiver's time, a hardware-failsafe frame too:
    // a receiver whose transmitter is off keeps sending them. So a fresh
    // stamp is not enough; the receiver must also not be in failsafe or lost.
    sbus1Failed = robotState.sbusSignalLost || robotState.sbusHwFailsafe;
    sbus2Failed = robotState.sbus2SignalLost || robotState.sbus2HwFailsafe;
    taskEXIT_CRITICAL(&robotStateMux);
    if (estop) {
        return refuse(refusal, refusalCap, "The estop is latched. Clear it to perform.");
    }

    // A droid with no radio is told that first: setting a channel on the RC
    // page would not help it.
    RcInputActiveConfig active = {};
    configCacheReadActiveRcInput(&active);
    if (active.mode == RC_INPUT_NOT_FITTED) {
        return refuse(refusal, refusalCap, "No radio is fitted, so there is nothing to perform with.");
    }

    // The strings, as the RC Map holds them now: an SBUS channel that can
    // move, set to Perform a Part, naming one (include/rc_puppet.h).
    RcTriggerBinding slots[RC_TRIGGER_SLOT_COUNT];
    const size_t slotCount = configCacheReadRcTriggerSlots(slots, RC_TRIGGER_SLOT_COUNT);
    bool onSbus1 = false;
    bool onSbus2 = false;
    uint8_t strings = 0;
    for (size_t i = 0; i < slotCount; ++i) {
        const RcTriggerBinding& b = slots[i];
        if (b.target != SERVO_ACTION_PUPPET_PART || b.marcduinoPayload[0] == '\0' ||
            !rcPuppetChannelCanMove(b.source, b.channel)) {
            continue;
        }
        ++strings;
        onSbus1 = onSbus1 || b.source == RC_BINDING_SBUS1;
        onSbus2 = onSbus2 || b.source == RC_BINDING_SBUS2;
    }
    if (strings == 0) {
        return refuse(refusal, refusalCap,
                      "No RC Channel is set to Perform a Part. Set one on the RC page.");
    }

    // Frames arriving on a receiver a string reads, within the watchdog's
    // timeout as the builder set it. A receiver the controller does not read
    // has none.
    const uint32_t nowMs = millis();
    const uint32_t timeoutMs = configCacheSbusTimeoutMs();
    const bool sbus1Live = onSbus1 && !sbus1Failed && lastSbus1Ms != 0 &&
                           (uint32_t)(nowMs - lastSbus1Ms) <= timeoutMs;
    const bool sbus2Live = onSbus2 && !sbus2Failed && lastSbus2Ms != 0 &&
                           (uint32_t)(nowMs - lastSbus2Ms) <= timeoutMs;
    if (!sbus1Live && !sbus2Live) {
        return refuse(refusal, refusalCap,
                      "No frames are arriving from the radio. Switch it on and check the receiver.");
    }

    // The sequence the take belongs to, by its stable id, which the file
    // name carries: a sequence the droid holds, saved since ids existed.
    const SeqIndexEntry* seq = seqStoreIndexFind(seqName);
    if (seq == nullptr || seq->id[0] == '\0') {
        return refuse(refusal, refusalCap, "Save the sequence first.");
    }
    char ownerId[sizeof(s_ownerId)];
    char ownerName[sizeof(s_seqName)];
    strncpy(ownerId, seq->id, sizeof(ownerId) - 1);
    ownerId[sizeof(ownerId) - 1] = '\0';
    strncpy(ownerName, seq->name, sizeof(ownerName) - 1);
    ownerName[sizeof(ownerName) - 1] = '\0';

    if (takeStoreRoomRefusal(refusal, refusalCap) != nullptr) {
        return refusal;
    }

    // The buffer is Core 0's while the stage is not PERFORMING: fill it, then
    // hand it over. A take that stopped and was never kept is discarded here.
    taskENTER_CRITICAL(&s_takeMux);
    const bool mayArm = s_stage == TAKE_STAGE_IDLE || s_stage == TAKE_STAGE_STOPPED;
    if (mayArm) {
        s_stage = TAKE_STAGE_HELD;  // this arm's while it fills the buffer
    }
    taskEXIT_CRITICAL(&s_takeMux);
    if (!mayArm) {
        return refuse(refusal, refusalCap, "A take is already running.");
    }

    // The buffer: the one a stopped take that is now discarded still holds, or
    // a fresh one. PSRAM where the board has it (firebeetle2), internal RAM
    // otherwise - read and written from task context only, never an ISR.
    TakeCapture* capture = s_capture;
    if (capture == nullptr) {
        void* storage = heap_caps_malloc_prefer(sizeof(TakeCapture), 2,
                                                MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT,
                                                MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
        if (storage == nullptr) {
            taskENTER_CRITICAL(&s_takeMux);
            s_stage = TAKE_STAGE_IDLE;
            taskEXIT_CRITICAL(&s_takeMux);
            PA_LOG_WARN(TAG, "no memory for a take (%u B)", (unsigned)sizeof(TakeCapture));
            return refuse(refusal, refusalCap,
                          "The droid has no memory free for a take. Stop any sequence that is "
                          "running, close other pages open on the droid, and try again.");
        }
        capture = new (storage) TakeCapture();
    }

    takeCaptureBegin(capture, millis());
    for (size_t i = 0; i < slotCount; ++i) {
        const RcTriggerBinding& b = slots[i];
        if (b.target == SERVO_ACTION_PUPPET_PART && rcPuppetChannelCanMove(b.source, b.channel)) {
            takeCaptureAddPart(capture, b.marcduinoPayload);
        }
    }

    taskENTER_CRITICAL(&s_takeMux);
    s_capture = capture;
    memcpy(s_ownerId, ownerId, sizeof(s_ownerId));
    memcpy(s_seqName, ownerName, sizeof(s_seqName));
    s_why = TAKE_STOP_NONE;
    s_finished = false;
    s_seedWanted = true;
    s_stage = TAKE_STAGE_PERFORMING;
    taskEXIT_CRITICAL(&s_takeMux);
    PA_LOG_INFO(TAG, "take armed for %s: %u Part(s), %u B", ownerName, (unsigned)capture->partCount,
                (unsigned)sizeof(TakeCapture));
    return nullptr;
}

// -----------------------------------------------------------------------------
// Status
// -----------------------------------------------------------------------------
void takeStatusRead(TakeStatus* out) {
    *out = {};
    const uint32_t nowMs = millis();
    taskENTER_CRITICAL(&s_takeMux);
    out->stage = s_stage;
    out->why = s_why;
    memcpy(out->seqName, s_seqName, sizeof(out->seqName));
    if ((s_stage == TAKE_STAGE_PERFORMING || s_stage == TAKE_STAGE_STOPPED) && s_capture != nullptr) {
        const uint32_t endMs = (s_stage == TAKE_STAGE_PERFORMING) ? nowMs : s_stopMs;
        out->elapsedMs = (uint32_t)(endMs - s_capture->startMs);
        out->fill = takeCaptureFill(s_capture, endMs);
        out->samples = s_capture->sampleCount;
        out->cues = s_capture->cueCount;
        out->partCount = s_capture->partCount;
        memcpy(out->parts, s_capture->parts, sizeof(out->parts));
    }
    taskEXIT_CRITICAL(&s_takeMux);
}

// -----------------------------------------------------------------------------
// Keeping
// -----------------------------------------------------------------------------
const char* takeKeepBegin(TakeKeep* out, char* refusal, size_t refusalCap) {
    *out = {};
    const uint32_t nowMs = millis();
    bool ok = false;
    taskENTER_CRITICAL(&s_takeMux);
    if (s_stage == TAKE_STAGE_PERFORMING) {
        // The builder kept it: it ends now, where it stands.
        s_stage = TAKE_STAGE_STOPPED;
        s_why = TAKE_STOP_KEEP;
        s_stopMs = nowMs;
        s_finished = false;
    }
    if (s_stage == TAKE_STAGE_STOPPED && s_capture != nullptr) {
        s_stage = TAKE_STAGE_HELD;
        ok = true;
        out->why = s_why;
        memcpy(out->ownerId, s_ownerId, sizeof(out->ownerId));
        memcpy(out->seqName, s_seqName, sizeof(out->seqName));
    }
    const bool finished = s_finished;
    const uint32_t stopMs = s_stopMs;
    taskEXIT_CRITICAL(&s_takeMux);
    if (!ok) {
        return refuse(refusal, refusalCap, "No take is running.");
    }
    // Outside the lock: the buffer is this caller's while HELD, and finishing
    // walks every sample.
    if (!finished) {
        takeCaptureFinish(s_capture, stopMs);
        taskENTER_CRITICAL(&s_takeMux);
        s_finished = true;
        taskEXIT_CRITICAL(&s_takeMux);
    }
    out->capture = s_capture;
    return nullptr;
}

// The one place the buffer is freed: kept, the take is done with it. The
// stage goes IDLE and the pointer null under the lock first, and the memory
// goes after it (see s_capture). Not kept, the take keeps its buffer and goes
// back to STOPPED, to be kept again.
void takeKeepEnd(bool kept) {
    TakeCapture* release = nullptr;
    taskENTER_CRITICAL(&s_takeMux);
    if (s_stage == TAKE_STAGE_HELD) {
        s_stage = kept ? TAKE_STAGE_IDLE : TAKE_STAGE_STOPPED;
        if (kept) {
            s_why = TAKE_STOP_NONE;
            s_seqName[0] = '\0';
            s_ownerId[0] = '\0';
            release = s_capture;
            s_capture = nullptr;
        }
    }
    taskEXIT_CRITICAL(&s_takeMux);
    if (release != nullptr) {
        release->~TakeCapture();
        heap_caps_free(release);
    }
}
