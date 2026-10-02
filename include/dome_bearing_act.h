// =============================================================================
// include/dome_bearing_act.h
//
// The three acts on the Dome Bearing (ADR 0051, #445), and the one copy of why
// each may not happen:
//
//   - FRONT IS HERE - the builder turned the dome to front and says so. The
//     recovery act: it is what makes an unknown bearing believed again.
//   - GO HOME - turn the dome the short way to the believed front. The
//     end-of-show act, never the recovery one: "when the belief is wrong it
//     drives confidently away from home" (ADR 0051).
//   - A BEARING STEP - a sequence turning the dome until front, or a dome Part,
//     faces front.
//
// The web routes (POST /api/dome/front, POST /api/dome/home), the Console's two
// actions and the Sequence Coordinator all ask domeBearingActRefusal() here, so
// a refusal has one reason and one clause wherever it is shown. DomeTask asks
// the questions that are its own again when the command reaches it
// (src/tasks/dome_task.cpp), because the answers can change on the way.
//
// Pure: no FreeRTOS, no RobotState. The callers read the state and pass it in.
// =============================================================================
#pragma once

#include <stdint.h>
#include <string.h>

#include "console_module.h"  // ConsoleReason - the Availability Reason set
#include "dome_bearing.h"
#include "droid_parts.h"     // DROID_PART_BEARING_TENTHS, DROID_PART_ON_DOME

// Why an act on the Dome Bearing does not happen, or OK.
enum DomeBearingRefusal : uint8_t {
    DOME_BEARING_OK = 0,
    DOME_BEARING_ESTOP,           // the estop is latched
    DOME_BEARING_ASLEEP,          // Sleep Mode
    DOME_BEARING_DOME_OFF,        // the Dome ESC is switched off: no DomeTask to tell
    DOME_BEARING_NOT_CALIBRATED,  // a full turn not timed, or its direction not set
    DOME_BEARING_UNKNOWN,         // nothing to turn from: estop, Sleep Mode or a boot forgot it
};

// Which act is being asked for. Front is here needs no belief - it is how one
// is made - and the two turns need one.
enum DomeBearingAct : uint8_t {
    DOME_BEARING_ACT_FRONT_IS_HERE = 0,
    DOME_BEARING_ACT_TURN,  // go home, or a bearing step
};

// The whole rule, in the order a builder can act on it: a halt first, then
// the switch, then the calibration, then the belief. A sequence's step never
// sees the estop or Sleep Mode here - a run is ended by either - so the
// Coordinator passes false for both.
inline DomeBearingRefusal domeBearingActRefusal(DomeBearingAct act, bool estopLatched,
                                                bool sleepMode, bool domeOn, bool calibrated,
                                                bool believed) {
    if (estopLatched) return DOME_BEARING_ESTOP;
    if (sleepMode) return DOME_BEARING_ASLEEP;
    if (!domeOn) return DOME_BEARING_DOME_OFF;
    if (!calibrated) return DOME_BEARING_NOT_CALIBRATED;
    if (act == DOME_BEARING_ACT_TURN && !believed) return DOME_BEARING_UNKNOWN;
    return DOME_BEARING_OK;
}

// The one clause a surface shows for it (docs/ui-copy-voice.md).
inline const char* domeBearingRefusalWords(DomeBearingRefusal refusal) {
    switch (refusal) {
        case DOME_BEARING_ESTOP:
            return "Estop latched. Clear it first.";
        case DOME_BEARING_ASLEEP:
            return "The droid is asleep. Wake it first.";
        case DOME_BEARING_DOME_OFF:
            return "Dome ESC is switched off in Configuration.";
        case DOME_BEARING_NOT_CALIBRATED:
            return "Time the dome's full turn first.";
        case DOME_BEARING_UNKNOWN:
            return "Bearing unknown: turn the dome to front and press Front is here.";
        case DOME_BEARING_OK:
        default:
            return nullptr;
    }
}

// The Availability Reason the Console and a run's report carry for it.
inline ConsoleReason domeBearingRefusalReason(DomeBearingRefusal refusal) {
    switch (refusal) {
        case DOME_BEARING_ESTOP:
        case DOME_BEARING_ASLEEP:
            return CONSOLE_REASON_BLOCKED_BY_STATE;
        case DOME_BEARING_DOME_OFF:
            return CONSOLE_REASON_COMPONENT_DISABLED;
        case DOME_BEARING_NOT_CALIBRATED:
            return CONSOLE_REASON_DOME_NOT_CALIBRATED;
        case DOME_BEARING_UNKNOWN:
            return CONSOLE_REASON_BEARING_UNKNOWN;
        case DOME_BEARING_OK:
        default:
            return CONSOLE_REASON_NONE;
    }
}

// -----------------------------------------------------------------------------
// A bearing step's target: `front`, or a dome Part by its catalog id.
//
// A Part is named, never its bearing, so a step survives a corrected
// `bearing_deg`: the bearing is resolved when the step RUNS, here, from the
// table the catalog generates (include/droid_parts.h). A target that is
// neither front nor a dome Part the catalog gives a bearing is refused at the
// door by Protocol Check, like an unknown Body Step id.
// -----------------------------------------------------------------------------
inline constexpr const char DOME_BEARING_TARGET_FRONT[] = "front";

// True for `front` and for a dome Part with a bearing. Protocol Check's form
// rule, and the run's own guard.
inline bool domeBearingTargetValid(const char* target) {
    if (target == nullptr) return false;
    if (strcmp(target, DOME_BEARING_TARGET_FRONT) == 0) return true;
    const size_t i = droidPartIndexOf(target);
    return i < DROID_PART_COUNT && DROID_PART_ON_DOME[i] &&
           DROID_PART_BEARING_TENTHS[i] != DROID_BEARING_NONE;
}

// The Dome Bearing a valid target turns to, in tenths, 0..3599: front is 0,
// and a Part faces front at domeBearingFacingFrontDeg() of its bearing. -1 for
// a target domeBearingTargetValid() refuses.
inline int16_t domeBearingTargetTenths(const char* target) {
    if (!domeBearingTargetValid(target)) return -1;
    if (strcmp(target, DOME_BEARING_TARGET_FRONT) == 0) return 0;
    const float deg = domeBearingFacingFrontDeg(DROID_PART_BEARING_TENTHS[droidPartIndexOf(target)]);
    const int16_t tenths = (int16_t)(deg * 10.0f + 0.5f);
    return (tenths >= 3600) ? 0 : tenths;
}

// -----------------------------------------------------------------------------
// What the Sequence Coordinator does with one bearing step, the shape a Body
// Step's plan has (include/sequence_body_step.h): `reason` is what to report,
// and only CONSOLE_REASON_NONE comes with `turn`. A reported reason is never a
// refusal of the sequence - the dome is not moved and the run carries on, as
// it does for part-not-assigned.
// -----------------------------------------------------------------------------
struct DomeBearingStepPlan {
    ConsoleReason reason;
    bool          turn;          // true => send DOME_CMD_TURN_TO to targetTenths
    int16_t       targetTenths;
};

inline DomeBearingStepPlan domeBearingStepPlan(const char* target, bool domeOn, bool calibrated,
                                               bool believed) {
    DomeBearingStepPlan plan = {CONSOLE_REASON_NONE, false, 0};
    const int16_t tenths = domeBearingTargetTenths(target);
    if (tenths < 0) {
        // Protocol Check refuses such a step on save; a stored one whose Part
        // the catalog has since dropped reports like an unknown Body Step id.
        plan.reason = CONSOLE_REASON_UNKNOWN_ARGUMENT;
        return plan;
    }
    const DomeBearingRefusal refusal = domeBearingActRefusal(
        DOME_BEARING_ACT_TURN, false, false, domeOn, calibrated, believed);
    if (refusal != DOME_BEARING_OK) {
        plan.reason = domeBearingRefusalReason(refusal);
        return plan;
    }
    plan.turn = true;
    plan.targetTenths = tenths;
    return plan;
}
