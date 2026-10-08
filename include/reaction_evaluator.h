// =============================================================================
// include/reaction_evaluator.h
//
// When a Reaction fires (ADR 0053, #450; GLOSSARY.md "Reaction").
//
// A Reaction is a trigger binding whose source is a droid condition rather
// than a radio channel (include/rc_binding_types.h). This is the half that
// decides when: each source is a predicate over the droid's own state, read on
// edges, with a baseline taken when the Reaction is armed; each Reaction
// carries its own quiet period; one droid-wide floor separates any two; and
// one gate holds them all back under the estop, Sleep Mode and a lost radio,
// and holds a body Part shut while the droid is driving. A stop the failsafe
// made is not a stop (ADR 0032). What a firing does is the action
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
#include "drive_motion.h"           // driveMotionIsDriving() - the one definition of driving
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

// A stop counts once the drive output has been at zero this long with no
// failsafe holding it. DriveTask zeroes the output on the tick a radio drops
// or a browser's command expires, and says why in RobotState one 20 ms tick
// later; two of this task's ticks is long enough to have read the reason.
constexpr uint32_t REACTION_STOP_CONFIRM_MS = 2 * REACTION_TICK_MS;

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
    // The radio is lost: the SBUS watchdog or the receiver's own failsafe.
    bool radioLost;
    // A failsafe is holding the drive output at zero: the radio lost, or a
    // browser's drive command expired. A zero it made is not a stop.
    bool failsafeHold;
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

// Why a Reaction is not simply armed. The first four belong to an Availability
// Family (GLOSSARY.md): two are "change it elsewhere", spelled
// `not-in-this-build` on every surface, and two are "waiting". The last three
// are the gate: the droid can sense the condition and is holding every
// Reaction back.
enum class ReactionAvailability : uint8_t {
    Ready = 0,
    NoFeedback,     // not in this build: this drive reports nothing back
    NoCurrent,      // not in this build: its frames carry no motor current
    FeedbackStale,  // waiting: no reading from the drive lately
    NoPlayState,    // waiting: the sound module has not said what it is doing
    HeldEstop,      // held: the estop is latched
    HeldSleep,      // held: Sleep Mode
    HeldRadioLost,  // held: the radio is lost
};

// The state a Reaction is in, as every surface spells it, and which reason put
// it there. nullptr reason: it is armed.
inline const char* reactionAvailabilityFamily(ReactionAvailability availability) {
    switch (availability) {
        case ReactionAvailability::NoFeedback:
        case ReactionAvailability::NoCurrent:
            return "not-in-this-build";
        case ReactionAvailability::FeedbackStale:
        case ReactionAvailability::NoPlayState:
            return "waiting";
        case ReactionAvailability::HeldEstop:
        case ReactionAvailability::HeldSleep:
        case ReactionAvailability::HeldRadioLost:
            return "held";
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
        case ReactionAvailability::HeldEstop:
            return "estop";
        case ReactionAvailability::HeldSleep:
            return "sleep";
        case ReactionAvailability::HeldRadioLost:
            return "radio-lost";
        case ReactionAvailability::Ready:
            break;
    }
    return nullptr;
}

struct ReactionSlotState {
    RcTriggerBinding binding;  // the Reaction this state was armed for
    uint32_t armedAtMs;
    uint32_t lastFiredMs;
    uint16_t fires;            // firings the action door carried out
    uint16_t refusals;         // times it was held back because the droid was driving
    bool armed;                // its baseline is taken; false until the gate is open
    bool sensed;               // the droid could sense its condition at the last tick
    bool hasFired;
    bool lastLevel;            // its condition, as last read
    bool pressHeld;            // a press went out whose release is still owed
    ReactionAvailability availability;
};

// The drive, as the conditions need it remembered: when it came to rest, and
// from how fast.
struct ReactionMotion {
    bool moving;
    bool restValid;       // at rest now, by a stop that counts
    bool stopPending;     // the output reached zero and the stop is not confirmed yet
    uint32_t restSinceMs;
    uint16_t stopFromSpeed;  // the fastest it was going just before it stopped
    uint16_t recentSpeed[REACTION_HARD_STOP_SAMPLES];
    uint8_t recentNext;
};

struct ReactionEvaluator {
    ReactionSlotState slots[REACTION_SLOT_MAX];
    ReactionMotion motion;
    DriveMotion drive;    // this task's own reading of "driving"
    bool wasDriving;
    bool anyFired;
    uint32_t lastAnyFiredMs;
};

// One thing to send through the action door. It carries its own action,
// because a release may be for a Reaction that is no longer in its slot: one
// that was edited or deleted while its press was held.
struct ReactionFiring {
    uint8_t slot;
    bool pressed;  // false: the release of a press this Reaction sent earlier
    RcBindingSource source;
    RobotActionId target;
    char payload[sizeof(RcTriggerBinding::marcduinoPayload)];
};

// At most a release and a press for every slot in one tick.
constexpr size_t REACTION_FIRING_MAX = 2 * REACTION_SLOT_MAX;

struct ReactionOutput {
    ReactionFiring firings[REACTION_FIRING_MAX];
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
//
// A STOP THE FAILSAFE MADE IS NOT A STOP (ADR 0032: a network or radio fault
// has no effect of its own on the droid). The output reaching zero only
// becomes a stop once it has stayed there REACTION_STOP_CONFIRM_MS with no
// failsafe holding it; a radio dropout or an expired browser command inside
// that window voids it, and nothing reads the droid as having come to rest
// until it has driven and stopped again. A hold that begins after the stop was
// confirmed - the browser's own dead-man, half a second after the operator let
// go - changes nothing: the operator stopped the droid, not the timeout.
inline void reactionTrackMotion(ReactionMotion* motion, const ReactionInputs& in) {
    const bool moving = driveOutputCommanded(in.driveSpeed, in.driveSteer);
    if (moving) {
        if (!motion->moving) {
            motion->restValid = false;
            motion->stopPending = false;
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
        motion->stopPending = true;
    }
    motion->moving = moving;

    if (motion->stopPending) {
        if (in.failsafeHold) {
            motion->stopPending = false;
            motion->stopFromSpeed = 0;
        } else if ((uint32_t)(in.nowMs - motion->restSinceMs) >= REACTION_STOP_CONFIRM_MS) {
            motion->stopPending = false;
            motion->restValid = true;
        }
    }
}

// Whether the droid can sense this Reaction's condition, apart from the gate.
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

// The one gate. Any of these holds every Reaction back, and names itself.
inline ReactionAvailability reactionGateHold(const ReactionInputs& in) {
    if (in.estop) {
        return ReactionAvailability::HeldEstop;
    }
    if (in.sleepMode) {
        return ReactionAvailability::HeldSleep;
    }
    if (in.radioLost) {
        return ReactionAvailability::HeldRadioLost;
    }
    return ReactionAvailability::Ready;
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
    const bool rightWheel = binding.channel == RC_REACTION_WHEEL_RIGHT;
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
            return reactionMagnitude(rightWheel ? in.wheelSpeedR : in.wheelSpeedL) >= threshold;
        case RC_BINDING_DROID_WHEEL_AMPS:
            return reactionMagnitude(rightWheel ? in.wheelCurrentR : in.wheelCurrentL) >= threshold;
        default:
            return false;
    }
}

inline void reactionEmit(ReactionOutput* out, size_t slot, const RcTriggerBinding& binding,
                         bool pressed) {
    if (out->count >= REACTION_FIRING_MAX) {
        return;
    }
    ReactionFiring& firing = out->firings[out->count++];
    firing.slot = (uint8_t)slot;
    firing.pressed = pressed;
    firing.source = binding.source;
    firing.target = binding.target;
    memcpy(firing.payload, binding.marcduinoPayload, sizeof(firing.payload));
    firing.payload[sizeof(firing.payload) - 1] = '\0';
}

// The release a held press is owed, sent for the binding that pressed.
inline void reactionRelease(ReactionOutput* out, size_t slot, ReactionSlotState* state) {
    if (state->pressHeld) {
        reactionEmit(out, slot, state->binding, false);
        state->pressHeld = false;
    }
}

// The same elapsed-time compare AudioTask's anti-spam makes
// (antiSpamAllows(), src/tasks/audio_playback_policy.cpp), against a stored
// last-fired time. The time is never reset when a condition clears: that rule
// would let a condition that flaps fire again every time it came back.
inline bool reactionElapsed(uint32_t nowMs, uint32_t sinceMs, uint32_t periodMs) {
    return (uint32_t)(nowMs - sinceMs) >= periodMs;
}

// A Reaction is known by its binding, not by the slot it sits in: a save that
// removes some other binding moves it to an earlier slot, and it is the same
// Reaction there - still inside its quiet period, with the counts it had. So
// before a tick reads the slots, each state follows its binding to wherever
// that now is. What is left unmatched is a Reaction that is new or was edited.
inline void reactionReseatStates(ReactionEvaluator* ev, const RcTriggerBinding* bindings,
                                 size_t count) {
    for (size_t i = 0; i < count; ++i) {
        if (!reactionIsReaction(bindings[i]) ||
            reactionSameBinding(ev->slots[i].binding, bindings[i])) {
            continue;
        }
        for (size_t j = 0; j < count; ++j) {
            if (j != i && reactionSameBinding(ev->slots[j].binding, bindings[i]) &&
                !reactionSameBinding(ev->slots[j].binding, bindings[j])) {
                const ReactionSlotState moved = ev->slots[j];
                ev->slots[j] = ev->slots[i];
                ev->slots[i] = moved;
                break;
            }
        }
    }
}

inline void reactionEvaluatorInit(ReactionEvaluator* ev) {
    if (ev != nullptr) {
        *ev = {};
    }
}

// The action door carried a press out. Counted here, by the caller, because
// only it knows: a press the door refused or could not queue is not a firing.
inline void reactionEvaluatorFired(ReactionEvaluator* ev, const ReactionFiring& firing) {
    if (ev == nullptr || firing.slot >= REACTION_SLOT_MAX || !firing.pressed) {
        return;
    }
    ReactionSlotState& slot = ev->slots[firing.slot];
    if (slot.fires < UINT16_MAX) {
        slot.fires++;
    }
}

// One tick. `bindings` are the trigger slots as they stand; a slot whose
// source is not a droid condition is not a Reaction and is left alone.
// `out` lists what to send through the action door, in order.
//
// `opensBodyPart(target, payload)` answers whether firing that action would
// open a body Part. It is asked only when a Reaction is about to fire while
// the droid is driving, or holds a press when driving starts, and it is the
// caller's to answer, because a Sequence's steps are the sequence store's to
// read (reactionOpensBodyPart(), src/tasks/reaction_task.cpp). A callable type
// rather than a function pointer, so the call is a direct one the stack walk
// follows (tools/check_task_stack_chains.py does not follow an indirect call).
template <typename OpensBodyPart>
inline void reactionEvaluatorTick(ReactionEvaluator* ev, const RcTriggerBinding* bindings,
                                  size_t count, const ReactionInputs& in,
                                  OpensBodyPart opensBodyPart, ReactionOutput* out) {
    out->count = 0;
    if (ev == nullptr || bindings == nullptr) {
        return;
    }
    if (count > REACTION_SLOT_MAX) {
        count = REACTION_SLOT_MAX;
    }

    reactionTrackMotion(&ev->motion, in);

    const DriveMotionReading reading = {in.driveSpeed, in.driveSteer, in.feedbackValid,
                                        in.wheelSpeedL, in.wheelSpeedR};
    const bool driving = driveMotionIsDriving(&ev->drive, reading, in.nowMs);
    const bool drivingBegan = driving && !ev->wasDriving;
    ev->wasDriving = driving;

    // The estop, Sleep Mode and a lost radio close the gate for every
    // Reaction; the driving rule, further down, is the same gate asked about
    // one firing.
    const ReactionAvailability hold = reactionGateHold(in);

    reactionReseatStates(ev, bindings, count);

    for (size_t i = 0; i < count; ++i) {
        ReactionSlotState& slot = ev->slots[i];

        // A Reaction that was deleted or edited is gone, and a press it still
        // held is released first, for the action it pressed: an Output it
        // opened is not left open with nothing to close it.
        if (!reactionIsReaction(bindings[i]) ||
            !reactionSameBinding(slot.binding, bindings[i])) {
            reactionRelease(out, i, &slot);
            slot = {};
            if (!reactionIsReaction(bindings[i])) {
                continue;
            }
            slot.binding = bindings[i];
        }

        const ReactionAvailability sense = reactionAvailabilityOf(slot.binding, in);
        const bool sensed = sense == ReactionAvailability::Ready;

        // Gate closed: nothing is read. Whatever is armed takes its baseline
        // again when the gate opens.
        if (hold != ReactionAvailability::Ready) {
            slot.availability = sensed ? hold : sense;
            slot.armed = false;
            slot.sensed = sensed;
            continue;
        }
        slot.availability = sense;

        const bool level = sensed && reactionConditionHolds(slot, ev->motion, in);

        // Arming takes the baseline: the condition as it stands is what the
        // Reaction starts from, so one already true does not fire. It is taken
        // when the Reaction is new or edited, when the gate opens, and when
        // the droid starts being able to sense the condition - a wheel
        // already turning when its reading comes back has not just started.
        // A press sent before is released if its condition ended meanwhile.
        if (!slot.armed || (sensed && !slot.sensed)) {
            slot.armed = true;
            slot.sensed = sensed;
            slot.armedAtMs = in.nowMs;
            const bool armedLevel = sensed && reactionConditionHolds(slot, ev->motion, in);
            if (!armedLevel) {
                reactionRelease(out, i, &slot);
            }
            slot.lastLevel = armedLevel;
            continue;
        }
        slot.sensed = sensed;

        const bool rose = level && !slot.lastLevel;
        const bool fell = !level && slot.lastLevel;
        slot.lastLevel = level;

        // Closing is never held back: not by a quiet period, not by the
        // floor, and not by driving. And a body Part a Reaction is holding
        // open is closed when the droid starts to drive.
        if (slot.pressHeld &&
            (fell || (drivingBegan &&
                      opensBodyPart(slot.binding.target, slot.binding.marcduinoPayload)))) {
            reactionRelease(out, i, &slot);
        }

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
                reactionEmit(out, i, slot.binding, true);
                slot.hasFired = true;
                slot.lastFiredMs = in.nowMs;
                ev->anyFired = true;
                ev->lastAnyFiredMs = in.nowMs;
                // A switch action - an Output's toggle - is pressed while the
                // condition holds and released when it ends, as it is on a
                // radio switch. A one-shot has no release.
                slot.pressHeld = !robotActionIsOneShotButton(slot.binding.target);
            }
        }
    }
}
