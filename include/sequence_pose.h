// =============================================================================
// include/sequence_pose.h
//
// Send the droid to one moment of a saved routine (#440, ADR 0062): the pose
// at an instant, worked out from the stored sequence, and the cursor the
// Sequence Coordinator walks to reach it.
//
// One operator at the bench presses for it, which is what makes it
// legitimate: the timeline's marker moves freely and silently, and only this
// separate press sends anything. What arrives is a sequence name and an
// instant (the operator's decision, 2026-09-30 on #440). The pose itself --
// which Parts, where to, in what order and how far apart -- is worked out and
// paced HERE, never in the browser, because a safe pace a page held is one a
// hand-edited client could walk around (CONTEXT.md "Cadence Floor").
//
// WHAT THE POSE COMMANDS is everything the routine has at that instant
// (operator, 2026-09-30): each dome panel and body Part to where the last step
// before the instant left it, each light in the mode it is in then, and the
// sound that would be playing, started from its beginning, because the sound
// modules cannot seek. A Part the routine has not yet moved by the instant is
// not commanded. It needs no new bound: every command is one a normal run of
// the same routine sends (ADR 0062). Four things it deliberately does not
// command, each because the routine does not say:
//   - a panel whose last word is a flutter (:OF). Where a flutter leaves a
//     panel is the dome's to know, and the engine records it as uncertain too
//     (recordRingOpenState(), src/tasks/sequence_engine.cpp);
//   - a random step's panel. The pick is made at run time, so no instant has
//     one;
//   - a dome turn. It is a motion over time, not a position to go to;
//   - a raw light code (@..., *...). Its target is the dome's addressing, not
//     a Part; the four structured light modes (DV:, DL:, DT:, DH:) are.
// A light mode is started again with its whole duration, as the sound is.
//
// PAST THE END it is what the engine leaves (beginFinish(),
// src/tasks/sequence_engine.cpp): a ring panel the run left open is closed --
// one at a time, like everything here -- unless the routine is a toggle's open
// half; pies and body Parts stay where they were; lights and sound are over.
// A routine with no end step has no "past the end".
//
// ONE PART AT A TIME, NEVER A GROUP. A group target (:OP00, :CL14, :CL15) is
// expanded into its members and each is sent on its own. Every motion -- a
// dome panel or a body Output -- starts at least the Cadence Floor after the
// one before it; a body Output also holds the next one off for its own full
// throw, and until ServoTask no longer reports it moving, exactly as a bulk
// centre does (include/sequence_bulk_centre.h). Sound and light commands move
// nothing, so they go first and are not spaced.
//
// REFUSED UNDER A HALT. A latched estop or Sleep Mode refuses the pose
// outright, not held for later (sequencePoseRefusal()). The rule lives here
// once: the route asks it to answer the press, and the Coordinator asks it
// again when the request reaches it, because either halt can land in between.
//
// Pure: no NVS, no FreeRTOS, no Arduino, no clock, no heap. The Coordinator
// owns a static plan and run and passes millis(); this header owns the rules.
// =============================================================================

#pragma once

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "sequence_bulk_centre.h"  // SEQ_CADENCE_FLOOR_MS, sequenceCadenceSpacingMs()
#include "sequence_engine.h"       // SequenceEntry, SeqStep, SeqAction, panel targets
#include "sequence_gesture.h"      // a Gesture's members, order and spread

// What a pose command is, which decides how far apart it goes.
enum SeqPoseClass : uint8_t {
    SEQ_POSE_INSTANT = 0,  // sound or a light mode: moves nothing, not spaced
    SEQ_POSE_PANEL,        // one dome panel: the Cadence Floor after the last motion
    SEQ_POSE_BODY,         // one body Part: its Output's throw, floored, and until it stops
};

// One command of a pose. `key` names what it sets -- a panel target ("01",
// "P1"), a light ("L:FLD", "H:F", "DV") or the sound ("SND") -- so a later step
// replaces an earlier one; a body Part is keyed by its id in act.payload.
struct SeqPoseCmd {
    SeqAction act;
    uint32_t  atMs;     // when the routine sent the step this command repeats
    uint32_t  untilMs;  // a light mode's own time runs out here; 0 = until changed
    uint8_t   cls;      // SeqPoseClass
    char      key[8];
};

// Thirteen panels, eight lights, a visual preset and a sound is 23; the rest is
// body Parts, and a droid has at most twenty-four Servo Output rows. A routine
// that names more than fits sets `truncated`, and the pose then leaves out the
// latest body Parts rather than overrunning.
constexpr uint8_t SEQ_POSE_MAX = 40;

struct SeqPosePlan {
    SeqPoseCmd cmds[SEQ_POSE_MAX];
    uint8_t    count;
    bool       truncated;
};

// -----------------------------------------------------------------------------
// sequencePoseRefusal()
// Why a pose may not start, in the words the surface shows, or nullptr when it
// may. The one copy of the rule (see the header comment).
// -----------------------------------------------------------------------------
inline const char* sequencePoseRefusal(bool estopLatched, bool sleepMode) {
    if (estopLatched) return "Estop latched. Clear it to send the droid to this moment.";
    if (sleepMode) return "The droid is asleep. Wake it to send it to this moment.";
    return nullptr;
}

// -----------------------------------------------------------------------------
// Building the plan
// -----------------------------------------------------------------------------
namespace seq_pose_detail {

inline SeqAction blankAction(SeqActionKind kind, const char* payload) {
    SeqAction a;
    memset(&a, 0, sizeof(a));
    a.kind = kind;
    if (payload != nullptr) {
        strncpy(a.payload, payload, sizeof(a.payload) - 1);
    }
    return a;
}

inline bool sameTarget(const SeqPoseCmd& cmd, uint8_t cls, const char* key, const char* part) {
    if (cmd.cls != cls) return false;
    if (cls == SEQ_POSE_BODY) return strcmp(cmd.act.payload, part) == 0;
    return strcmp(cmd.key, key) == 0;
}

// Record that the routine set this target at fireMs. A later step replaces an
// earlier one; equal times keep the later step, which is the engine's order.
inline void upsert(SeqPosePlan& plan, uint8_t cls, const char* key, const SeqAction& act,
                   uint32_t fireMs, uint32_t untilMs) {
    for (uint8_t i = 0; i < plan.count; ++i) {
        SeqPoseCmd& cmd = plan.cmds[i];
        if (sameTarget(cmd, cls, key, act.payload)) {
            if (fireMs >= cmd.atMs) {
                cmd.act = act;
                cmd.atMs = fireMs;
                cmd.untilMs = untilMs;
            }
            return;
        }
    }
    if (plan.count >= SEQ_POSE_MAX) {
        plan.truncated = true;
        return;
    }
    SeqPoseCmd& cmd = plan.cmds[plan.count++];
    cmd.act = act;
    cmd.atMs = fireMs;
    cmd.untilMs = untilMs;
    cmd.cls = cls;
    snprintf(cmd.key, sizeof(cmd.key), "%s", key != nullptr ? key : "");
}

inline void forget(SeqPosePlan& plan, uint8_t cls, const char* key) {
    for (uint8_t i = 0; i < plan.count; ++i) {
        if (sameTarget(plan.cmds[i], cls, key, "")) {
            plan.cmds[i].act.kind = SEQ_ACT_NONE;  // compacted away at the end
        }
    }
}

// The `field`-th ':'-separated field of cmd (0 is the prefix), copied into
// out. Returns false when the command has fewer fields.
inline bool fieldOf(const char* cmd, uint8_t field, char* out, size_t outLen) {
    const char* p = cmd;
    for (uint8_t i = 0; i < field; ++i) {
        p = strchr(p, ':');
        if (p == nullptr) return false;
        ++p;
    }
    const char* end = strchr(p, ':');
    const size_t n = (end != nullptr) ? (size_t)(end - p) : strlen(p);
    if (n + 1 > outLen) return false;
    memcpy(out, p, n);
    out[n] = '\0';
    return true;
}

// A duration field in whole seconds (DL: field 4, DT: field 3), 0 when absent.
inline uint32_t secondsField(const char* cmd, uint8_t field) {
    char buf[8];
    if (!fieldOf(cmd, field, buf, sizeof(buf))) return 0;
    uint32_t v = 0;
    for (const char* c = buf; *c >= '0' && *c <= '9'; ++c) v = v * 10u + (uint32_t)(*c - '0');
    return v;
}

// :OP / :CL / :OF on a panel target, expanded to one command per member
// panel. Returns false when cmd is not a panel intent.
inline bool visitPanel(SeqPosePlan& plan, const char* cmd, uint32_t fireMs) {
    if (cmd[0] != ':' || strlen(cmd) != 5) return false;
    const char* word = cmd + 1;
    if (strncmp(word, "OP", 2) != 0 && strncmp(word, "CL", 2) != 0 && strncmp(word, "OF", 2) != 0) {
        return false;
    }
    const char* target = cmd + 3;
    const uint8_t ring = seqEngineRingPanelCount();
    const uint8_t all = seqEnginePanelTargetCount();
    uint8_t from = 0;
    uint8_t to = 0;  // exclusive
    if (strcmp(target, "00") == 0) {
        to = all;
    } else if (strcmp(target, "15") == 0) {
        to = ring;
    } else if (strcmp(target, "14") == 0) {
        from = ring;
        to = all;
    } else {
        for (uint8_t i = 0; i < all; ++i) {
            if (strcmp(seqEnginePanelTarget(i), target) == 0) {
                from = i;
                to = (uint8_t)(i + 1);
                break;
            }
        }
    }
    for (uint8_t i = from; i < to; ++i) {
        const char* member = seqEnginePanelTarget(i);
        char one[8];
        snprintf(one, sizeof(one), ":%c%c%s", word[0], word[1], member);
        upsert(plan, SEQ_POSE_PANEL, member, blankAction(SEQ_ACT_DOME_CMD, one), fireMs, 0);
    }
    return true;
}

// DL:/DT: set logic and PSI lights, DH: the holoprojectors, DV: the dome's
// visual preset. Each light is keyed on its own, so DL:LOGIC then DL:RLD
// leaves the front logic on the first and the rear on the second.
inline bool visitLight(SeqPosePlan& plan, const char* cmd, uint32_t fireMs) {
    const SeqAction act = blankAction(SEQ_ACT_DOME_CMD, cmd);
    if (strncmp(cmd, "DV:", 3) == 0) {
        upsert(plan, SEQ_POSE_INSTANT, "DV", act, fireMs, 0);
        return true;
    }
    char target[8];
    if (!fieldOf(cmd, 1, target, sizeof(target))) return false;
    if (strncmp(cmd, "DL:", 3) == 0 || strncmp(cmd, "DT:", 3) == 0) {
        // kDlTargets / kDtTargets (src/protocol_check.cpp).
        static const char* const kLights[] = { "FLD", "RLD", "FPSI", "RPSI" };
        const bool logic = strcmp(target, "LOGIC") == 0;
        const bool psi = strcmp(target, "PSI") == 0;
        const bool every = strcmp(target, "ALL") == 0;
        const uint32_t seconds = secondsField(cmd, cmd[1] == 'L' ? 4 : 3);
        const uint32_t untilMs = seconds > 0 ? fireMs + seconds * 1000u : 0;
        for (uint8_t i = 0; i < 4; ++i) {
            const bool isLogic = i < 2;
            if (every || (logic && isLogic) || (psi && !isLogic) || strcmp(target, kLights[i]) == 0) {
                char key[8];
                snprintf(key, sizeof(key), "L:%s", kLights[i]);
                upsert(plan, SEQ_POSE_INSTANT, key, act, fireMs, untilMs);
            }
        }
        return true;
    }
    if (strncmp(cmd, "DH:", 3) == 0) {
        // Front, rear, top; A is all three. Its fourth field is a duration for
        // some effects and a count for others, so a holo effect is held until
        // it is changed or the routine ends.
        static const char kHolos[] = { 'F', 'R', 'T' };
        for (uint8_t i = 0; i < 3; ++i) {
            if (target[1] == '\0' && (target[0] == 'A' || target[0] == kHolos[i])) {
                char key[8];
                snprintf(key, sizeof(key), "H:%c", kHolos[i]);
                upsert(plan, SEQ_POSE_INSTANT, key, act, fireMs, 0);
            }
        }
        return true;
    }
    return false;
}

// The panel command target a dome Part is sent to ("01", "P3"), or false for a
// Part the dome has no individual target for.
inline bool domePartTarget(uint8_t partIndex, char* out, size_t outLen) {
    const char* id = droidPartIdAt(partIndex);
    if (seqGestureDomeBit(partIndex) < 0) return false;
    if (strncmp(id, "panel", 5) == 0) {
        snprintf(out, outLen, "%02d", atoi(id + 5));
        return true;
    }
    if (strncmp(id, "pie", 3) == 0) {
        snprintf(out, outLen, "P%s", id + 3);
        return true;
    }
    return false;
}

// A Gesture, as a run performs it, up to atMs (ADR 0046). Each Part it moves
// is where the Gesture's last move before the instant left it. A body Gesture
// is expanded by the Coordinator on the spread's own timing, pass by pass. A
// dome Gesture is the dome's `$` command: a together open or close leaves its
// panels that way; every other command the dome performs ends them closed, on
// the dome's own timing, and a pair the dome has no command for moves nothing
// (include/sequence_gesture.h) -- so neither does the pose.
inline void visitGesture(SeqPosePlan& plan, const SeqStep& step, uint32_t fireMs, uint32_t atMs) {
    uint8_t members[SEQ_GESTURE_MEMBERS_MAX];
    const uint8_t n = seqGestureMembers(step, members, SEQ_GESTURE_MEMBERS_MAX);
    const SeqBodyShape shape = seqBodyShape(step.params);
    const uint8_t spread = seqGestureSpread(step.params);
    if (seqGestureIsDome(step.payload)) {
        if (seqGestureDomePrefix(shape, spread) == nullptr) return;
        if (spread == GESTURE_SPREAD_TOGETHER && shape == BODY_SHAPE_FLUTTER) return;  // the dome's to know
        const bool opens = spread == GESTURE_SPREAD_TOGETHER && shape == BODY_SHAPE_OPEN;
        for (uint8_t i = 0; i < n; ++i) {
            char target[4];
            if (!domePartTarget(members[i], target, sizeof(target))) continue;
            char one[8];
            snprintf(one, sizeof(one), ":%s%s", opens ? "OP" : "CL", target);
            upsert(plan, SEQ_POSE_PANEL, target, blankAction(SEQ_ACT_DOME_CMD, one), fireMs, 0);
        }
        return;
    }
    const uint16_t stepMs = seqGestureStepMs(step.params);
    const uint16_t repeat = seqGestureRepeatMs(step.params);
    const uint32_t passes = seqGesturePasses(step.params);
    const uint16_t moves = seqGesturePassMoves(spread, n);
    for (uint32_t pass = 0; pass < passes; ++pass) {
        const uint32_t passAt = fireMs + pass * repeat;
        if (passAt > atMs) break;
        for (uint16_t k = 0; k < moves; ++k) {
            const SeqGestureMove move = seqGesturePassMove(spread, n, stepMs, k);
            const uint32_t at = passAt + move.atMs;
            if (at > atMs) continue;
            SeqAction a = blankAction(SEQ_ACT_BODY_MOVE, droidPartIdAt(members[move.member]));
            a.bodyShape = (uint8_t)seqGestureMoveShape(shape, move.undo);
            a.bodyHowFar = seqBodyHowFar(step.params);
            upsert(plan, SEQ_POSE_BODY, "", a, at, 0);
        }
    }
}

// One step, as the engine would fire it at fireMs.
inline void visit(SeqPosePlan& plan, const SeqStep& step, uint32_t fireMs, uint32_t atMs) {
    switch (step.type) {
        case STEP_DOME_CMD:
            if (!visitPanel(plan, step.payload, fireMs)) {
                visitLight(plan, step.payload, fireMs);
            }
            break;
        case STEP_AUDIO:
            // $s stops the sound and mutes chatter until a power cycle: after
            // it nothing is playing, and it is never something to send again.
            if (strcmp(step.payload, "$s") == 0) {
                forget(plan, SEQ_POSE_INSTANT, "SND");
            } else {
                upsert(plan, SEQ_POSE_INSTANT, "SND", blankAction(SEQ_ACT_AUDIO_DOLLAR, step.payload),
                       fireMs, 0);
            }
            break;
        case STEP_AUDIO_CATEGORY: {
            SeqAction a = blankAction(SEQ_ACT_AUDIO_CATEGORY, nullptr);
            a.audioCategory = step.params.audioCategory;
            a.audioFallbackSlot = step.params.audioFallbackSlot;
            upsert(plan, SEQ_POSE_INSTANT, "SND", a, fireMs, 0);
            break;
        }
        case STEP_AUDIO_STOP:
            forget(plan, SEQ_POSE_INSTANT, "SND");
            break;
        case STEP_BODY: {
            SeqAction a = blankAction(SEQ_ACT_BODY_MOVE, step.payload);
            a.bodyShape = (uint8_t)seqBodyShape(step.params);
            a.bodyHowFar = seqBodyHowFar(step.params);
            a.bodyFlutterMs = step.params.flutterMs;
            upsert(plan, SEQ_POSE_BODY, "", a, fireMs, 0);
            break;
        }
        case STEP_GESTURE:
            visitGesture(plan, step, fireMs, atMs);
            break;
        default:
            // A random step's pick, a dome turn, a latch reset: nothing an
            // instant can be sent to (see the header comment).
            break;
    }
}

}  // namespace seq_pose_detail

// -----------------------------------------------------------------------------
// sequencePosePlan()
// Everything `steps` has commanded by atMs, as the commands that reach it, in
// the order they go: sound and lights, then dome panels, then body Parts, each
// class in the order the routine set them.
//
// The steps are walked the way the engine fires them (seqEnginePeek(),
// src/tasks/sequence_engine.cpp): a loop's body once per period while the pass
// starts inside the loop's duration, timed from the pass start; nothing after
// the end step. `toggleOpenHalf` is whether these steps are a toggle's open
// half, which the engine leaves open at the end.
// -----------------------------------------------------------------------------
inline void sequencePosePlan(const SeqStep* steps, uint8_t count, bool toggleOpenHalf, uint32_t atMs,
                             SeqPosePlan* out) {
    if (out == nullptr) return;
    out->count = 0;
    out->truncated = false;
    if (steps == nullptr) return;

    bool ended = false;
    uint8_t i = 0;
    while (i < count) {
        const SeqStep& step = steps[i];
        if (step.type == STEP_END) {
            ended = atMs >= step.tMs;
            break;
        }
        if (step.type == STEP_LOOP && step.params.bodyCount > 0) {
            const uint8_t last = (uint8_t)((i + step.params.bodyCount < count) ? i + step.params.bodyCount
                                                                               : count - 1);
            const uint32_t period = step.params.periodMs;
            uint32_t start = 0;
            do {
                for (uint8_t k = (uint8_t)(i + 1); k <= last; ++k) {
                    const uint32_t fire = step.tMs + start + steps[k].tMs;
                    if (fire <= atMs) seq_pose_detail::visit(*out, steps[k], fire, atMs);
                }
                start += period;
            } while (period > 0 && start < step.params.durationMs && step.tMs + start <= atMs);
            i = (uint8_t)(last + 1);
            continue;
        }
        if (step.tMs <= atMs) seq_pose_detail::visit(*out, step, step.tMs, atMs);
        ++i;
    }

    // Past the end: what the engine's own ending leaves.
    const uint8_t ring = seqEngineRingPanelCount();
    for (uint8_t c = 0; c < out->count; ++c) {
        SeqPoseCmd& cmd = out->cmds[c];
        if (cmd.act.kind == SEQ_ACT_NONE) continue;
        if (cmd.cls == SEQ_POSE_INSTANT) {
            const bool ranOut = cmd.untilMs != 0 && atMs >= cmd.untilMs;
            if (ended || ranOut) cmd.act.kind = SEQ_ACT_NONE;
        } else if (cmd.cls == SEQ_POSE_PANEL) {
            if (strncmp(cmd.act.payload, ":OF", 3) == 0) {
                cmd.act.kind = SEQ_ACT_NONE;  // where a flutter leaves a panel is the dome's
                continue;
            }
            bool isRing = false;
            for (uint8_t r = 0; r < ring; ++r) {
                if (strcmp(seqEnginePanelTarget(r), cmd.key) == 0) isRing = true;
            }
            if (ended && isRing && !toggleOpenHalf && strncmp(cmd.act.payload, ":OP", 3) == 0) {
                cmd.act.payload[1] = 'C';
                cmd.act.payload[2] = 'L';
            }
        }
    }

    // Compact, then order by class and time, stable: an insertion sort over at
    // most SEQ_POSE_MAX entries, in place, so nothing is allocated.
    uint8_t kept = 0;
    for (uint8_t c = 0; c < out->count; ++c) {
        if (out->cmds[c].act.kind != SEQ_ACT_NONE) {
            if (kept != c) out->cmds[kept] = out->cmds[c];
            ++kept;
        }
    }
    out->count = kept;
    for (uint8_t a = 1; a < out->count; ++a) {
        const SeqPoseCmd moving = out->cmds[a];
        uint8_t b = a;
        while (b > 0 && (out->cmds[b - 1].cls > moving.cls ||
                         (out->cmds[b - 1].cls == moving.cls && out->cmds[b - 1].atMs > moving.atMs))) {
            out->cmds[b] = out->cmds[b - 1];
            --b;
        }
        out->cmds[b] = moving;
    }
}

// -----------------------------------------------------------------------------
// The run: the cursor over a plan. `dueMs` is the earliest the next command
// may go; `awaitArm` is the body Output the last command moved, which the next
// waits on until ServoTask no longer reports it moving. A run stays active past
// its last command until that command's spacing has run and its Output has
// stopped: the pose owns the motion it started until then, and the
// Coordinator ends it there (poseOneCommand(), src/tasks/sequence_dispatcher.cpp).
// -----------------------------------------------------------------------------
struct SeqPoseRun {
    bool     active;
    uint8_t  next;
    uint8_t  count;
    uint32_t dueMs;
    uint8_t  awaitArm;  // SEQ_BULK_CENTRE_NO_AWAIT when nothing is awaited
    uint8_t  sent;
    uint8_t  skipped;
    uint8_t  src;       // CommandSource of who pressed
};

// ONE MOTION OWNER ON THE DOME. A dome resync -- when an estop clears, and
// when the dome link comes up -- assumes the ring is open and closes it one
// panel at a time on its own timer, the Coordinator's staged ring close. It
// and a pose never share the dome, the way a run and a resync do not: staging
// a resync ends a pose being reached (sequenceResyncCloseStage()), and a pose
// that starts supersedes a staged close (sequencePoseStart()). Otherwise a
// resync :CLnn and a pose's panel command go out together, for the same panel.

// The staged ring close's cursor when no close is pending.
constexpr uint8_t SEQ_RESYNC_CLOSE_NONE = 0xFF;

// Start a pose, or refuse it. Refused under either halt, never queued: the
// same rule a bulk centre keeps (sequenceBootPassStart()). Returns whether the
// run started. A pose that starts clears `*resyncCloseIdx`, the staged close
// it supersedes; a refused or empty one moves nothing and leaves it.
//
// A pose that replaces one still active keeps that one's spacing: its first
// command waits for the pending `dueMs` and for the Output being awaited, as
// the next command of the same pose would, so a second press never starts a
// motion inside the Cadence Floor of the first press's last one. A replacement
// with nothing to command still runs out that spacing, so a third press after
// it waits too. A refused one ends the run; a halt has let every Output go.
inline bool sequencePoseStart(SeqPoseRun* run, uint32_t nowMs, bool estopLatched, bool sleepMode,
                              uint8_t count, uint8_t src, uint8_t* resyncCloseIdx) {
    if (run == nullptr) return false;
    const bool replacing = run->active;
    const uint32_t dueMs = (replacing && (int32_t)(run->dueMs - nowMs) > 0) ? run->dueMs : nowMs;
    const uint8_t awaitArm = replacing ? run->awaitArm : SEQ_BULK_CENTRE_NO_AWAIT;
    run->active = false;
    run->awaitArm = SEQ_BULK_CENTRE_NO_AWAIT;
    if (sequencePoseRefusal(estopLatched, sleepMode) != nullptr) return false;
    run->active = count > 0 || replacing;
    run->next = 0;
    run->count = count;
    run->dueMs = dueMs;
    run->awaitArm = awaitArm;
    run->sent = 0;
    run->skipped = 0;
    run->src = src;
    if (count > 0 && resyncCloseIdx != nullptr) {
        *resyncCloseIdx = SEQ_RESYNC_CLOSE_NONE;
    }
    return count > 0;
}

// The run is over, whoever ended it. Nothing is commanded on the way out, for
// the reason a bulk centre ends that way (sequenceBulkCentreEnd()).
inline void sequencePoseEnd(SeqPoseRun* run) {
    if (run != nullptr) {
        run->active = false;
        run->awaitArm = SEQ_BULK_CENTRE_NO_AWAIT;
    }
}

// Stage a resync's ring close from the first ring panel, due now, and end a
// pose being reached, where it has got to. Both resyncs stage through here.
// Returns whether a pose was ended, for the log.
inline bool sequenceResyncCloseStage(SeqPoseRun* pose, uint8_t* closeIdx, uint32_t* closeDueMs,
                                     uint32_t nowMs) {
    const bool endedPose = pose != nullptr && pose->active;
    sequencePoseEnd(pose);
    if (closeIdx != nullptr) *closeIdx = 0;
    if (closeDueMs != nullptr) *closeDueMs = nowMs;
    return endedPose;
}

// Whether the next command may be looked at now. Unsigned subtraction handles
// millis() wrapping.
inline bool sequencePoseDue(const SeqPoseRun& run, uint32_t nowMs) {
    return run.active && (int32_t)(nowMs - run.dueMs) >= 0;
}

// Whether the Output the last body command moved has stopped, given what
// ServoTask reports. Clears the wait when it has.
inline bool sequencePoseAwaitDone(SeqPoseRun* run, bool outputMoving) {
    if (run == nullptr) return true;
    return sequencePaceAwaitDone(&run->awaitArm, outputMoving);
}

// The command whose turn it was has been dealt with. `started` is whether it
// was sent; a skipped one spaces nothing, because nothing moved. A dome panel
// holds the next command off by the Cadence Floor, a body Output by its own
// full throw, floored (sequenceCadenceSpacingMs()), and `armId` is then the
// Output the next command waits on.
inline void sequencePoseAdvance(SeqPoseRun* run, uint32_t nowMs, uint8_t cls, bool started,
                                uint16_t throwMs, uint8_t armId) {
    if (run == nullptr || !run->active) return;
    if (started) {
        run->sent++;
    } else {
        run->skipped++;
    }
    sequencePaceMotion(&run->dueMs, &run->awaitArm, nowMs, started, cls != SEQ_POSE_INSTANT,
                       cls == SEQ_POSE_BODY, throwMs, armId);
    run->next++;
}

// Whether every command has been dealt with. The run is then over once it is
// next due -- its last spacing run, its Output stopped -- and the Coordinator
// ends it with sequencePoseEnd().
inline bool sequencePoseFinished(const SeqPoseRun& run) {
    return run.next >= run.count;
}
