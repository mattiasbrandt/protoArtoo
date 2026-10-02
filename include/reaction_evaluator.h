// =============================================================================
// include/reaction_evaluator.h
//
// When a Reaction fires (ADR 0053, #450; CONTEXT.md "Reaction").
//
// A Reaction is a trigger binding whose source is a droid condition rather
// than a radio channel (include/rc_binding_types.h). This is the half that
// decides when: each source is a predicate over the droid's own state, read on
// edges, with a baseline taken when the Reaction is armed; each Reaction
// carries its own quiet period; one droid-wide floor separates any two; and
// one gate holds them all back under the estop and Sleep Mode, and holds a
// body Part shut while the droid is driving. What a firing does is the action
// door RC bindings already use (dispatchRcTriggerActionTest(),
// src/tasks/rc_input.cpp), called by ReactionTask (src/tasks/reaction_task.cpp).
//
// Pure: no Arduino, no FreeRTOS, no RobotState, no clock of its own. The task
// reads the state and passes millis(); the rules are here.
// =============================================================================
#pragma once

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "audio_playback_policy.h"  // AUDIO_PLAYBACK_ANTI_SPAM_MS - the beat the floor reuses
#include "drive_motion.h"           // driveOutputIsDriving()
#include "rc_action_types.h"        // RcTriggerBinding, the Reaction accessors

// -----------------------------------------------------------------------------
// The droid-wide floor
//
// The least time between any two Reactions firing, whichever two they are.
// ADR 0053: "The audio path's existing anti-spam beat is the thing to reuse,
// not a second idea of restraint" - so this IS that beat, by name, and not a
// number chosen to sit beside it.
//
// PROVENANCE: 300 ms is what AudioTask already refuses a second sound inside
// (include/audio_playback_policy.h). NOBODY HAS MEASURED A FLOOR FOR
// REACTIONS: no droid has run two of them close enough together to find what
// reads as a droid reacting and what reads as a car alarm. It is a stated
// starting value, the posture the Cadence Floor takes in
// include/sequence_bulk_centre.h, and #355 carries the look at a real droid.
// Do not restate it anywhere as a measured figure.
//
// It is compared on millis(), which does not stall: a quiet period measured on
// a clock that can stop stretches into minutes of silence exactly when the
// droid feels most broken.
// -----------------------------------------------------------------------------
constexpr uint32_t REACTION_FLOOR_MS = AUDIO_PLAYBACK_ANTI_SPAM_MS;

// How often ReactionTask asks. Twenty times a second: a hard stop is caught
// within one drive frame or two of the output reaching zero, and nothing a
// Reaction does needs to be sooner than that.
constexpr uint32_t REACTION_TICK_MS = 50;

// A hard stop is the drive output reaching zero from speed. "From speed" is the
// fastest it was commanded within this long before it got there, so an
// operator easing the stick down over a second is not one. A starting value,
// like every threshold here.
constexpr uint32_t REACTION_HARD_STOP_WINDOW_MS = 300;
constexpr uint8_t REACTION_HARD_STOP_SAMPLES =
    (uint8_t)(REACTION_HARD_STOP_WINDOW_MS / REACTION_TICK_MS);

// One state per trigger slot (include/config_store.h, RC_TRIGGER_SLOT_COUNT;
// ReactionTask asserts the two agree).
constexpr size_t REACTION_SLOT_MAX = 11;

// Whether the drive's feedback frames carry a motor current at all. Not every
// frame does, and one that does not reads as 0, never as absent
// (include/drive_backend.h).
enum class ReactionCurrentReport : uint8_t {
    Unknown = 0,  // no feedback frame decoded yet
    Reported,
    NotReported,
};

// Everything a tick reads, taken in one go by the caller.
struct ReactionInputs {
    uint32_t nowMs;
    bool estop;
    bool sleepMode;
    int16_t driveSpeed;  // the resolved drive output, as DriveTask sent it
    int16_t driveSteer;
    bool feedbackSupported;  // the drive backend reports readings back at all
    bool feedbackValid;      // and one arrived recently enough to be true
    int16_t wheelSpeedL;     // RPM
    int16_t wheelSpeedR;
    int16_t wheelCurrentL;   // A x 100
    int16_t wheelCurrentR;
    ReactionCurrentReport currentReport;
    uint8_t audioPlayState;  // 0 stop, 1 playing, 2 paused, 0xFF unknown
};

// Why a Reaction is not simply armed, in the Availability Family's words
// (CONTEXT.md): `notInThisBuild` is "change it elsewhere", `waiting` is
// "the droid has not been told yet".
enum class ReactionAvailability : uint8_t {
    Ready = 0,
    NoFeedback,     // not in this build: this drive reports nothing back
    NoCurrent,      // not in this build: its frames carry no motor current
    FeedbackStale,  // waiting: no reading from the drive lately
    NoPlayState,    // waiting: the sound module has not said what it is doing
};

// The Availability Family a Reaction is in, as every surface spells it, and
// which of its reasons put it there. nullptr reason: it is armed.
inline const char* reactionAvailabilityFamily(ReactionAvailability availability) {
    switch (availability) {
        case ReactionAvailability::NoFeedback:
        case ReactionAvailability::NoCurrent:
            return "not-in-this-build";
        case ReactionAvailability::FeedbackStale:
        case ReactionAvailability::NoPlayState:
            return "waiting";
        case ReactionAvailability::Ready:
            break;
    }
    return "ready";
}

inline const char* reactionAvailabilityReason(ReactionAvailability availability) {
    switch (availability) {
        case ReactionAvailability::NoFeedback:
            return "no-feedback";
        case ReactionAvailability::NoCurrent:
            return "no-current";
        case ReactionAvailability::FeedbackStale:
            return "feedback-stale";
        case ReactionAvailability::NoPlayState:
            return "no-play-state";
        case ReactionAvailability::Ready:
            break;
    }
    return nullptr;
}

struct ReactionSlotState {
    RcTriggerBinding binding;  // the Reaction this state was armed for
    uint32_t armedAtMs;
    uint32_t lastFiredMs;
    uint16_t fires;            // times it fired since it was armed for this binding
    uint16_t refusals;         // times it was held back because the droid was driving
    bool hasFired;
    bool lastLevel;            // its condition, as last read
    bool pressHeld;            // a press went out whose release is still owed
    ReactionAvailability availability;
};

// The drive, as the conditions need it remembered: when it came to rest, and
// from how fast.
struct ReactionMotion {
    bool moving;
    bool restValid;       // at rest now, having driven since boot
    uint32_t restSinceMs;
    uint16_t stopFromSpeed;  // the fastest it was going just before it stopped
    uint16_t recentSpeed[REACTION_HARD_STOP_SAMPLES];
    uint8_t recentNext;
};

struct ReactionEvaluator {
    ReactionSlotState slots[REACTION_SLOT_MAX];
    ReactionMotion motion;
    bool gateOpen;  // the estop and Sleep Mode both clear, as of the last tick
    bool anyFired;
    uint32_t lastAnyFiredMs;
};

struct ReactionFiring {
    uint8_t slot;
    bool pressed;  // false: the release of a press this Reaction sent earlier
};

struct ReactionOutput {
    ReactionFiring firings[REACTION_SLOT_MAX];
    uint8_t count;
};

// -----------------------------------------------------------------------------
// The rules. Header-only, like include/trigger_debounce.h, so a native test
// runs the real thing without a source file added to its build.
// -----------------------------------------------------------------------------

inline uint16_t reactionMagnitude(int16_t value) {
    const int32_t wide = value;
    return (uint16_t)(wide < 0 ? -wide : wide);
}

inline bool reactionSameBinding(const RcTriggerBinding& a, const RcTriggerBinding& b) {
    return a.source == b.source && a.channel == b.channel && a.target == b.target &&
           strncmp(a.marcduinoPayload, b.marcduinoPayload, sizeof(a.marcduinoPayload)) == 0 &&
           a.min == b.min && a.max == b.max;
}

inline bool reactionIsReaction(const RcTriggerBinding& binding) {
    return rcBindingSourceIsDroidCondition(binding.source) && binding.target != ROBOT_ACTION_NONE;
}

// Follows the drive whether or not anything is armed, so a Reaction armed
// mid-run reads a history that is true.
inline void reactionTrackMotion(ReactionMotion* motion, const ReactionInputs& in) {
    const bool moving = driveOutputIsDriving(in.driveSpeed, in.driveSteer);
    if (moving) {
        if (!motion->moving) {
            motion->restValid = false;
            motion->stopFromSpeed = 0;
        }
        motion->recentSpeed[motion->recentNext] = reactionMagnitude(in.driveSpeed);
        motion->recentNext = (uint8_t)((motion->recentNext + 1) % REACTION_HARD_STOP_SAMPLES);
    } else if (motion->moving) {
        uint16_t fastest = 0;
        for (uint16_t& sample : motion->recentSpeed) {
            if (sample > fastest) {
                fastest = sample;
            }
            sample = 0;
        }
        motion->stopFromSpeed = fastest;
        motion->restSinceMs = in.nowMs;
        motion->restValid = true;
    }
    motion->moving = moving;
}

inline ReactionAvailability reactionAvailabilityOf(const RcTriggerBinding& binding,
                                                   const ReactionInputs& in) {
    switch (binding.source) {
        case RC_BINDING_DROID_WHEEL_SPEED:
        case RC_BINDING_DROID_WHEEL_AMPS:
            if (!in.feedbackSupported) {
                return ReactionAvailability::NoFeedback;
            }
            if (binding.source == RC_BINDING_DROID_WHEEL_AMPS &&
                in.currentReport == ReactionCurrentReport::NotReported) {
                return ReactionAvailability::NoCurrent;
            }
            return in.feedbackValid ? ReactionAvailability::Ready
                                    : ReactionAvailability::FeedbackStale;
        case RC_BINDING_DROID_TRACK:
            return in.audioPlayState == 0xFF ? ReactionAvailability::NoPlayState
                                             : ReactionAvailability::Ready;
        default:
            return ReactionAvailability::Ready;
    }
}

// The condition itself: a level, read on its edges by the caller.
//
// The two that are about the droid stopping count only a stop that happened
// after the Reaction was armed. That is their baseline: a droid already at
// rest when the estop clears has not just come to rest.
inline bool reactionConditionHolds(const ReactionSlotState& slot, const ReactionMotion& motion,
                    const ReactionInputs& in) {
    const RcTriggerBinding& binding = slot.binding;
    const uint16_t threshold = rcReactionThreshold(binding);
    const bool stoppedSinceArmed = !motion.moving && motion.restValid &&
                                   (int32_t)(motion.restSinceMs - slot.armedAtMs) >= 0;
    switch (binding.source) {
        case RC_BINDING_DROID_SPEED:
            return reactionMagnitude(in.driveSpeed) >= threshold;
        case RC_BINDING_DROID_HARD_STOP:
            return stoppedSinceArmed && motion.stopFromSpeed >= threshold;
        case RC_BINDING_DROID_REST:
            // The threshold is tenths of a second at rest.
            return stoppedSinceArmed &&
                   (uint32_t)(in.nowMs - motion.restSinceMs) >= (uint32_t)threshold * 100u;
        case RC_BINDING_DROID_TRACK:
            return in.audioPlayState == 1;
        case RC_BINDING_DROID_WHEEL_SPEED:
            return reactionMagnitude(binding.channel == RC_REACTION_WHEEL_RIGHT ? in.wheelSpeedR
                                                                        : in.wheelSpeedL) >=
                   threshold;
        case RC_BINDING_DROID_WHEEL_AMPS:
            return reactionMagnitude(binding.channel == RC_REACTION_WHEEL_RIGHT ? in.wheelCurrentR
                                                                        : in.wheelCurrentL) >=
                   threshold;
        default:
            return false;
    }
}

inline void reactionEmit(ReactionOutput* out, size_t slot, bool pressed) {
    if (out->count < REACTION_SLOT_MAX) {
        out->firings[out->count].slot = (uint8_t)slot;
        out->firings[out->count].pressed = pressed;
        out->count++;
    }
}

// The same elapsed-time compare AudioTask's anti-spam makes
// (antiSpamAllows(), src/tasks/audio_playback_policy.cpp), against a stored
// last-fired time. The time is never reset when a condition clears: that rule
// would let a condition that flaps fire again every time it came back.
inline bool reactionElapsed(uint32_t nowMs, uint32_t sinceMs, uint32_t periodMs) {
    return (uint32_t)(nowMs - sinceMs) >= periodMs;
}

inline void reactionEvaluatorInit(ReactionEvaluator* ev) {
    if (ev != nullptr) {
        *ev = {};
    }
}

// One tick. `bindings` are the trigger slots as they stand; a slot whose
// source is not a droid condition is not a Reaction and is left alone.
// `out` lists what to send through the action door, in slot order.
//
// `opensBodyPart(target, payload)` answers whether firing that action would
// open a body Part. It is asked only when a Reaction is about to fire while
// the droid is driving, and it is the caller's to answer, because a Sequence's
// steps are the sequence store's to read (reactionOpensBodyPart(),
// src/tasks/reaction_task.cpp). A callable type rather than a function
// pointer, so the call is a direct one the stack walk follows
// (tools/check_task_stack_chains.py does not follow an indirect call).
template <typename OpensBodyPart>
inline void reactionEvaluatorTick(ReactionEvaluator* ev, const RcTriggerBinding* bindings,
                                  size_t count, const ReactionInputs& in,
                                  OpensBodyPart opensBodyPart, ReactionOutput* out) {
    out->count = 0;
    if (ev == nullptr || bindings == nullptr) {
        return;
    }

    reactionTrackMotion(&ev->motion, in);

    // The one gate. The estop and Sleep Mode close it for every Reaction; the
    // driving rule, further down, is the same gate asked about one firing.
    const bool gateOpen = !in.estop && !in.sleepMode;
    if (!gateOpen) {
        ev->gateOpen = false;
        return;
    }
    const bool gateLifted = !ev->gateOpen;
    ev->gateOpen = true;

    const bool driving = driveOutputIsDriving(in.driveSpeed, in.driveSteer);

    for (size_t i = 0; i < count && i < REACTION_SLOT_MAX; ++i) {
        ReactionSlotState& slot = ev->slots[i];
        if (!reactionIsReaction(bindings[i])) {
            slot = {};
            continue;
        }

        // Armed afresh for a Reaction that is new or was edited: nothing it
        // counted belongs to this one.
        const bool edited = !reactionSameBinding(slot.binding, bindings[i]);
        if (edited) {
            slot = {};
            slot.binding = bindings[i];
        }
        slot.availability = reactionAvailabilityOf(slot.binding, in);

        // Arming takes the baseline: the condition as it stands is what the
        // Reaction starts from, so one already true does not fire. A press
        // sent before the gate closed is released if its condition ended
        // while nothing was being read.
        if (edited || gateLifted) {
            slot.armedAtMs = in.nowMs;
            const bool level = slot.availability == ReactionAvailability::Ready &&
                               reactionConditionHolds(slot, ev->motion, in);
            if (slot.pressHeld && !level) {
                reactionEmit(out, i, false);
                slot.pressHeld = false;
            }
            slot.lastLevel = level;
            continue;
        }

        const bool level = slot.availability == ReactionAvailability::Ready &&
                           reactionConditionHolds(slot, ev->motion, in);
        const bool rose = level && !slot.lastLevel;
        const bool fell = !level && slot.lastLevel;
        slot.lastLevel = level;

        if (rose) {
            const uint32_t quietMs = (uint32_t)rcReactionQuietS(slot.binding) * 1000u;
            if (driving &&
                opensBodyPart(slot.binding.target, slot.binding.marcduinoPayload)) {
                // While the droid is driving a Reaction may light, sound and
                // turn the dome, and may not open a body Part (ADR 0053). The
                // edge is spent: it does not fire late once the droid stops.
                if (slot.refusals < UINT16_MAX) {
                    slot.refusals++;
                }
            } else if (slot.hasFired && !reactionElapsed(in.nowMs, slot.lastFiredMs, quietMs)) {
                // Its own quiet period.
            } else if (ev->anyFired &&
                       !reactionElapsed(in.nowMs, ev->lastAnyFiredMs, REACTION_FLOOR_MS)) {
                // The droid-wide floor.
            } else {
                reactionEmit(out, i, true);
                slot.hasFired = true;
                slot.lastFiredMs = in.nowMs;
                if (slot.fires < UINT16_MAX) {
                    slot.fires++;
                }
                ev->anyFired = true;
                ev->lastAnyFiredMs = in.nowMs;
                // A switch action - an Output's toggle - is pressed while the
                // condition holds and released when it ends, as it is on a
                // radio switch. A one-shot has no release.
                slot.pressHeld = !robotActionIsOneShotButton(slot.binding.target);
            }
        } else if (fell && slot.pressHeld) {
            // Closing is never held back: not by a quiet period, not by the
            // floor, and not by driving.
            reactionEmit(out, i, false);
            slot.pressHeld = false;
        }
    }
}
