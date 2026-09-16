// =============================================================================
// src/tasks/servo_task.cpp
//
// ServoTask  --  LEDC PWM control for utility arm servos and spare servo outputs.
// Handles open, close, position, nudge, hold and release commands, from every
// source, for:
//   - ARM1 (Top/Left utility arm, GPIO 23)
//   - ARM2 (Bottom/Right utility arm, GPIO 5)
//   - AUX1-3 (Spare servo outputs, GPIO 19/18/32)
// DOME (GPIO 25) is controlled separately as an ESC, not a servo.
// =============================================================================

#include "servo_task.h"

#include <esp_task_wdt.h>

#include "config.h"
#include "config_cache.h"
#include "ledc_pwm.h"
#include "logging.h"
#include "robot_state.h"
#include "servo_component_helpers.h"  // servoCompTypeToString, for the clamp note
#include "servo_helpers.h"
#include "servo_hold.h"         // the dial's hold and its two bounds (ADR 0064)
#include "servo_motion_ramp.h"  // a move planned in time from the Output's profile (ADR 0052)
#include "servo_nudge.h"        // the bounded pair a Find by Moving nudge visits (ADR 0050)
#include "servo_output_row.h"  // the addressed rows an endpoint lives on (ADR 0041)

static const char* TAG = "SERVO";

// Boot snapshot of component toggles, captured once at startup.
// Toggles are staged at reboot (ADR 0027); this snapshot is the stable read for the whole session.
static bool s_arm1_enabled = false;
static bool s_arm2_enabled = false;
static bool s_aux1_enabled = false;
static bool s_aux2_enabled = false;
static bool s_aux3_enabled = false;
static bool s_dome_enabled = false;
static uint8_t s_aux_led_pin = AUX_LED_PIN_DISABLED;

// -----------------------------------------------------------------------------
// Where each output is, and the move it is part way through (ADR 0052).
//
// `commandedUs` is the pulse this task last put on the pin. A move starts from
// it, so it is only trusted once this task has written something there:
// `known` is false until then, and a move from an unknown position is a jump.
//
// `hold` is the calibration dial's hold on the output (ADR 0064, #364): while
// it stands the pulse stays on until one of its two bounds fires, the builder
// lets go, or the halt edge releases it. `limp` is why there is no pulse, read
// only while `known` is false, so a surface can say "pulses off" and "the
// estop let go" differently.
//
// A Find by Moving nudge (ADR 0050, #363) is one more kind of move on this
// state, not a second machine beside it. `nudgeLeg` is which of its legs is in
// progress, 0 when no nudge is; `ramp` is the leg's own ramp, planned the way
// any move is; and `moving` stays true for the whole nudge, dwells included, so
// stopAllMoves() ends it where it is exactly as it ends a ramp.
// -----------------------------------------------------------------------------
static constexpr uint8_t kArmCount = SERVO_ARM_COUNT;  // ARM1, ARM2, AUX1-3

// How long a nudge rests at each side before moving on. Long enough for an eye
// that is on the droid rather than the page to catch a small twitch and see
// which way it went -- a snap is over in one frame, and it is the pause on
// either side that makes it readable as "out, across, back" rather than a
// flicker. Short enough that stepping through five spare outputs is seconds,
// not a chore: a snap nudge is two dwells, 1.2 s, and the whole run of five is
// well under half a minute with the browser's one-second reads between them.
static constexpr uint32_t kNudgeDwellMs = 600;

static struct {
    uint16_t commandedUs;
    bool known;
    bool moving;
    ServoHoldState hold;
    ServoLimpReason limp;
    ServoMotionRamp ramp;
    uint8_t nudgeLeg;          // 1..SERVO_NUDGE_LEG_COUNT while a nudge is in progress, else 0
    bool nudgeDwelling;        // the leg has arrived and the output is resting there
    uint32_t nudgeDwellEndMs;  // when that rest ends
    ServoNudgePlan nudge;
    uint8_t nudgesDone;        // mirrored to robotState; see ServoCommandedPosition
} s_arm[kArmCount] = {};

// Forward declaration for functions used in static helpers below.
static bool isArmEnabled(uint8_t armId);

// -----------------------------------------------------------------------------
// armIdToLedcChannel()
// Map armId to LEDC channel.
//   0 = ARM1  -> LEDC_CH_ARM1  (GPIO 23)
//   1 = ARM2  -> LEDC_CH_ARM2  (GPIO 5)
//   2 = AUX1  -> LEDC_CH_AUX1 (GPIO 19, also labelled ARM3)
//   3 = AUX2  -> LEDC_CH_AUX2 (GPIO 18, also labelled ARM4)
//   4 = AUX3  -> LEDC_CH_AUX3 (GPIO 32, also labelled ARM5)
// Returns LEDC_CH_MAX (invalid) for unknown armId.
// -----------------------------------------------------------------------------
static uint8_t armIdToLedcChannel(uint8_t armId) {
    return servo_arm_id_to_ledc_channel(armId);
}

// -----------------------------------------------------------------------------
// isArmEnabled()
// Check feature toggle for a given armId using the boot-time snapshot.
// Toggles are read once at startup and never re-checked per iteration.
// armId 255 (broadcast) is allowed only if both ARM1 and ARM2 are enabled.
// AUX channel selected for WS2812 is treated as unavailable to avoid
// pin ownership conflicts.
// Per ADR 0027, this function gates all servo operations on the component
// toggle snapshot captured at startup.
// -----------------------------------------------------------------------------
static bool isArmEnabled(uint8_t armId) {
    return servo_arm_enabled(armId, s_arm1_enabled, s_arm2_enabled, s_aux1_enabled, s_aux2_enabled,
                             s_aux3_enabled, s_aux_led_pin);
}

// -----------------------------------------------------------------------------
// resolveArmPulse()
// Which channel an arm is on, and the pulse width it may actually be driven to.
// Returns false (no log, nothing to write) if the arm is disabled.
// Per ADR 0027, disabled channels never PWM-commanded and never update robotState.
//
// The pulse width is bounded by what the fitted component takes before it
// reaches the pin. ADR 0041 puts that clamp at every door onto a row  --  the
// store, an edit and a drive command  --  so an MG996R output cannot reach
// 500 us by any route, including this one. The clamp is applied to the target of
// a move, once; every point of a ramp lies between two widths inside the band,
// so none of them can leave it.
//
// The cache answers with the clamped number and the component that bounded it,
// never with the row: this frame is on ServoTask's measured chain (ADR 0040) and
// a ServoOutputRow is 70 B to answer a question whose answer is one number.
// -----------------------------------------------------------------------------
static bool resolveArmPulse(uint8_t armId, uint16_t pulseUs, uint8_t* channelOut,
                            uint16_t* commandedOut) {
    if (!isArmEnabled(armId)) {
        return false;
    }

    const uint8_t channel = armIdToLedcChannel(armId);
    if (channel >= LEDC_CH_MAX || armId >= kArmCount) {
        PA_LOG_WARN(TAG, "resolveArmPulse: invalid armId %d", armId);
        return false;
    }

    // A returned width equal to the request is nothing to report, which is also
    // what an output no row describes comes back as - so the two cases need no
    // second flag to tell them apart.
    ServoComponentType component = SERVO_COMP_NONE;
    const uint16_t commandedUs =
        configCacheClampServoOutputPulse(SERVO_DRIVER_LEDC, channel, pulseUs, &component);
    if (commandedUs != pulseUs) {
        PA_LOG_WARN(TAG, "arm%d %d us is outside what a %s takes - driving %d us instead",
                    armId + 1, pulseUs, servoCompTypeToString(component), commandedUs);
    }

    *channelOut = channel;
    *commandedOut = commandedUs;
    return true;
}

// -----------------------------------------------------------------------------
// publishCommanded()
// Tell every surface where this output has been told to be (#362).
//
// `nowUs` is the width on the pin. `targetUs` is where the move in progress
// ends, or `nowUs` again when nothing is moving, so the two marks a surface
// draws close up exactly when the move does. `pulsing` is `known`: an output
// only becomes known by this task putting a pulse on it -- the neutral pulse at
// init, or a write -- and only releaseArm() takes one away (pulses off, a hold
// bound, or the halt edge; ADR 0043, ADR 0064), which is where `known` is
// cleared and `limp` says why.
//
// Called at every place one of them changes: a write, a ramp planned, a move
// abandoned, a hold taken, a release, and init. It is a copy into robotState
// under robotStateMux, like every robotState write, and allocates nothing.
// -----------------------------------------------------------------------------
static void publishCommanded(uint8_t armId) {
    const ServoCommandedPosition commanded = {
        s_arm[armId].commandedUs,
        s_arm[armId].moving ? s_arm[armId].ramp.toUs : s_arm[armId].commandedUs,
        s_arm[armId].known,
        s_arm[armId].nudgesDone,
        s_arm[armId].hold.held,
        s_arm[armId].limp,
    };
    taskENTER_CRITICAL(&robotStateMux);
    robotState.servoCommanded[armId] = commanded;
    taskEXIT_CRITICAL(&robotStateMux);
}

// -----------------------------------------------------------------------------
// endNudge() / endMove()
// The one place a move stops being in progress, whatever stops it: arrival, a
// later command on the same arm, or the halt edge.
//
// A nudge that was in progress is counted as ended here, however it ended. The
// count is what a discovery run waits on -- it cannot watch the nudge itself,
// because a whole nudge can fall between two of its one-second reads -- so a
// nudge cut short by an estop or overtaken by a new command must count exactly
// as a returned one does, or the run would wait for a return that is never
// coming. The callers publish; this only changes what they will publish.
// -----------------------------------------------------------------------------
static void endNudge(uint8_t armId) {
    if (s_arm[armId].nudgeLeg == 0) {
        return;
    }
    s_arm[armId].nudgeLeg = 0;
    s_arm[armId].nudgeDwelling = false;
    s_arm[armId].nudgesDone++;
}

static void endMove(uint8_t armId) {
    s_arm[armId].moving = false;
    endNudge(armId);
}

// -----------------------------------------------------------------------------
// writeArmPulse()
// Put one width on the pin and say so. The width has already been through
// resolveArmPulse(); this is the write and nothing else.
//
// What is written is what robotState then reports, because the position a
// status reader sees has to be the pulse the pin is actually holding -- part
// way through a ramp too.
// -----------------------------------------------------------------------------
static void writeArmPulse(uint8_t armId, uint8_t channel, uint16_t pulseUs) {
    ledcPwmSetPulseWidth(channel, pulseUs);
    s_arm[armId].commandedUs = pulseUs;
    s_arm[armId].known = true;

    // The commanded width, and only that. There was an armOpen[] bit beside it
    // deriving "open" from `commandedUs > SERVO_PULSE_NEUTRAL_US`, which is
    // wrong on any reversed Endpoint Pair -- past neutral does not mean open
    // when open is the lower number (ADR 0041). It has gone; anything wanting
    // to say which end this output is at compares the width against the pair on
    // its row, where the direction is recorded.
    publishCommanded(armId);
}

// -----------------------------------------------------------------------------
// driveArmTo()
// Send an arm to a pulse width at the pace its Output's Motion Profile sets.
//
// The time comes from the row and from nowhere else: a ServoCommand carries no
// duration, so a Body Step, an RC toggle and a browser move cannot disagree
// about how long a door takes (ADR 0049, ADR 0052). Where the profile cannot
// plan a move -- no row, an unmeasured output, no known starting point -- the
// arm snaps, which is exactly what every move did before the profile existed,
// and is also the first move after a release (#364): a released output is no
// longer `known`, so there is no position to ramp from.
//
// The profile arrives as four values, not as the row they sit in: like the
// clamp and the Endpoint Pair, it is answered by address out of the live table,
// so no 70 B ServoOutputRow is put on ServoTask's measured chain (ADR 0040).
// -----------------------------------------------------------------------------
static void driveArmTo(uint8_t armId, uint16_t pulseUs) {
    uint8_t channel = LEDC_CH_MAX;
    uint16_t targetUs = 0;
    if (!resolveArmPulse(armId, pulseUs, &channel, &targetUs)) {
        return;
    }
    // Whatever the arm was doing is over: a new command replaces a ramp part
    // way through, and a nudge part way through, alike.
    endMove(armId);

    uint16_t spanUs = 0;
    uint16_t throwMs = 0;
    uint16_t accelMs = 0;
    bool calibrated = false;
    if (!s_arm[armId].known ||
        !configCacheReadServoOutputMotionProfile(SERVO_DRIVER_LEDC, channel, &spanUs, &throwMs,
                                                 &accelMs, &calibrated)) {
        writeArmPulse(armId, channel, targetUs);
        return;
    }

    const ServoMotionRamp ramp = servoMotionPlan(s_arm[armId].commandedUs, targetUs, spanUs,
                                                 throwMs, accelMs, calibrated, millis());
    if (ramp.durationMs == 0) {
        writeArmPulse(armId, channel, targetUs);
        return;
    }
    s_arm[armId].ramp = ramp;
    s_arm[armId].moving = true;
    // Nothing is written until the next frame, but the move already has a
    // target, and that is what a surface shows beside where the output stands.
    publishCommanded(armId);
}

// -----------------------------------------------------------------------------
// The Find by Moving nudge (ADR 0050, #363)
//
// One command, run to completion by this task: out to the upper side of the
// pair, a dwell, across to the lower side, a dwell, and back to the width the
// output started from. The whole out-and-back is ServoTask's so that the
// return cannot depend on a browser staying alive between two requests -- a
// discovery run that died half way would otherwise leave a spare output
// parked 100 us off wherever it was, and there is no expiry on a servo hold
// today (C1d brings ADR 0064's two bounds).
//
// Each leg is an ordinary move: through resolveArmPulse() like every drive
// (ADR 0041), planned from the Output's Motion Profile like every drive
// (ADR 0052), so a calibrated output eases through its nudge and an unmeasured
// one snaps, exactly as either does for any other command. The pair itself
// comes from include/servo_nudge.h, about the width on the pin, inside the
// cautious band, and from nowhere the command could have put a number.
// -----------------------------------------------------------------------------
static void beginNudgeLeg(uint8_t armId, uint8_t leg, uint32_t nowMs);

// The leg has arrived. Rest there where an eye can catch it, or, after the
// last leg, the nudge is over: the output is back where it started, and the
// count says so.
static void nudgeLegArrived(uint8_t armId, uint32_t nowMs) {
    if (s_arm[armId].nudgeLeg < SERVO_NUDGE_LEG_COUNT) {
        s_arm[armId].nudgeDwelling = true;
        s_arm[armId].nudgeDwellEndMs = nowMs + kNudgeDwellMs;
        return;
    }
    endMove(armId);
    publishCommanded(armId);
    PA_LOG_INFO(TAG, "Arm%d nudge returned to %u us", armId + 1, (unsigned)s_arm[armId].commandedUs);
}

static void beginNudgeLeg(uint8_t armId, uint8_t leg, uint32_t nowMs) {
    uint8_t channel = LEDC_CH_MAX;
    uint16_t targetUs = 0;
    // The pair lies inside the cautious band, which every component band
    // contains, so the clamp hands it back unchanged -- but the door is the
    // rule (ADR 0041), not the outcome. This can only fail for an armId
    // resolveArmPulse() rejects, which beginNudge() has already refused.
    if (!resolveArmPulse(armId, servoNudgeLegTarget(s_arm[armId].nudge, leg), &channel, &targetUs)) {
        endMove(armId);
        publishCommanded(armId);
        return;
    }
    s_arm[armId].nudgeLeg = leg;
    s_arm[armId].nudgeDwelling = false;
    s_arm[armId].moving = true;

    uint16_t spanUs = 0;
    uint16_t throwMs = 0;
    uint16_t accelMs = 0;
    bool calibrated = false;
    ServoMotionRamp ramp = {s_arm[armId].commandedUs, targetUs, nowMs, 0, 0};
    if (configCacheReadServoOutputMotionProfile(SERVO_DRIVER_LEDC, channel, &spanUs, &throwMs,
                                                &accelMs, &calibrated)) {
        ramp = servoMotionPlan(s_arm[armId].commandedUs, targetUs, spanUs, throwMs, accelMs,
                               calibrated, nowMs);
    }
    s_arm[armId].ramp = ramp;
    if (ramp.durationMs == 0) {
        // A snap: the leg is over the moment it is written.
        writeArmPulse(armId, channel, targetUs);
        nudgeLegArrived(armId, nowMs);
        return;
    }
    // A ramp: nothing is written until the next frame, but the leg already has
    // its target, and that is what a surface shows beside where the output is.
    publishCommanded(armId);
}

// A SERVO_CMD_NUDGE that got past processCommand()'s gates. Refused, with
// nothing moved and the count still bumped so a run waiting on it steps on,
// when there is no width on the pin to nudge about or that width is outside
// the cautious band.
static void beginNudge(uint8_t armId, CommandSource source) {
    // One output per nudge, always: a run nudges the spare outputs one at a
    // time so the builder can say which one moved, and the ARM1+ARM2 broadcast
    // would move two in one press.
    if (armId >= kArmCount) {
        PA_LOG_WARN(TAG, "[%s] Nudge rejected - takes one arm, not %d", commandSourceToString(source),
                    armId);
        return;
    }
    ServoNudgePlan plan = {};
    if (!s_arm[armId].known ||
        !servoNudgePlan(s_arm[armId].commandedUs, SERVO_BAND_STD, SERVO_NUDGE_AMPLITUDE_US, &plan)) {
        PA_LOG_WARN(TAG, "[%s] Arm%d not nudged - %s", commandSourceToString(source), armId + 1,
                    s_arm[armId].known ? "it sits outside the cautious band" : "no pulse on it yet");
        s_arm[armId].nudgesDone++;
        publishCommanded(armId);
        return;
    }
    // Whatever the arm was doing is over, a nudge in progress included: this
    // one starts from the width on the pin now.
    endMove(armId);
    s_arm[armId].nudge = plan;
    PA_LOG_INFO(TAG, "[%s] Arm%d nudged %u/%u us about %u us", commandSourceToString(source),
                armId + 1, (unsigned)plan.hiUs, (unsigned)plan.loUs, (unsigned)plan.homeUs);
    beginNudgeLeg(armId, 1, millis());
}

// One frame of a nudge in progress: wait out a dwell, then start the next leg;
// otherwise advance the leg's ramp like any other move and notice its arrival.
static void advanceNudge(uint8_t armId, uint8_t channel, uint32_t nowMs) {
    if (s_arm[armId].nudgeDwelling) {
        if ((int32_t)(nowMs - s_arm[armId].nudgeDwellEndMs) < 0) {
            return;
        }
        beginNudgeLeg(armId, (uint8_t)(s_arm[armId].nudgeLeg + 1), nowMs);
        return;
    }
    writeArmPulse(armId, channel, servoMotionPositionAt(s_arm[armId].ramp, nowMs));
    if (servoMotionArrived(s_arm[armId].ramp, nowMs)) {
        nudgeLegArrived(armId, nowMs);
    }
}

// -----------------------------------------------------------------------------
// updateMotion()
// Advance every move in progress by one frame.
// -----------------------------------------------------------------------------
static void updateMotion() {
    const uint32_t now = millis();
    for (uint8_t armId = 0; armId < kArmCount; ++armId) {
        if (!s_arm[armId].moving) {
            continue;
        }
        const uint8_t channel = armIdToLedcChannel(armId);
        if (s_arm[armId].nudgeLeg != 0) {
            advanceNudge(armId, channel, now);
            continue;
        }
        writeArmPulse(armId, channel, servoMotionPositionAt(s_arm[armId].ramp, now));
        if (servoMotionArrived(s_arm[armId].ramp, now)) {
            endMove(armId);
        }
    }
}

// -----------------------------------------------------------------------------
// stopAllMoves()
// End every move in progress where it has got to, commanding nothing further.
//
// Estop and Sleep Mode land here the moment either is entered, and
// releaseAllOutputs() below runs straight after to take the pulses off
// (ADR 0043). A ramp that kept running would be the droid still moving after
// it was told to stop, so the pin keeps the last width a frame wrote and
// nothing eases on. The release ends a move on every output it touches too:
// what this adds is that the moves end first, and the log line naming what was
// in progress when the stop arrived.
// -----------------------------------------------------------------------------
static void stopAllMoves(const char* reason) {
    bool stopped = false;
    for (uint8_t armId = 0; armId < kArmCount; ++armId) {
        if (!s_arm[armId].moving) {
            continue;
        }
        endMove(armId);
        stopped = true;
        // The move is over where it got to, so its target is too: no surface
        // may go on showing a destination the output will never reach. A nudge
        // ends the same way, its return never made, and is counted as ended.
        publishCommanded(armId);
    }
    if (stopped) {
        PA_LOG_INFO(TAG, "Moves stopped where they were - %s", reason);
    }
}

// -----------------------------------------------------------------------------
// getOpenClosePositions()
// The Endpoint Pair of the addressed Servo Output behind this arm (ADR 0041).
//
// The pair is directional and stays that way: `open` is whichever number the
// builder recorded as open, larger or smaller than close. A reversed linkage is
// open < close and nothing else records it, so taking min/max here would be the
// invert flag the model refuses, arriving by the back door.
//
// With no row addressed to this output there is no calibration to read, so the
// pair is the cautious band's two ends  --  the same numbers an unconfigured
// row defaults to, rather than the full 500-2500 us a servo will take.
//
// Two numbers cross this frame, not the thirteen fields they sit in: ServoTask's
// worst-case chain is a measured constant (ADR 0040) and a whole row would spend
// 70 B of it on fields this path never reads.
// -----------------------------------------------------------------------------
static void getOpenClosePositions(uint8_t armId, uint16_t& openUs, uint16_t& closeUs) {
    const uint8_t channel = armIdToLedcChannel(armId);
    if (channel < LEDC_CH_MAX &&
        configCacheReadServoOutputEndpoints(SERVO_DRIVER_LEDC, channel, &openUs, &closeUs)) {
        return;
    }
    openUs = SERVO_BAND_STD.hi;
    closeUs = SERVO_BAND_STD.lo;
}

// -----------------------------------------------------------------------------
// releaseArm()
// Take the pulse off one output: pulses off (ADR 0043, ADR 0064, #364).
//
// The one place a pulse comes off a pin, whoever asks -- the builder's pulses
// off, one of the dial's two bounds, or the halt edge -- so a released output
// means one thing everywhere on the droid: nothing is driven, the servo goes
// limp where it is, and its resting position is whatever gravity and friction
// decide. Nothing is commanded first: driving to a known position before
// letting go is exactly the option ADR 0064 refused, since it moves a part
// while by definition nobody is watching it.
//
// Any move, nudge or hold on the output ends here too. A nudge cut short is
// counted as ended (endNudge), so a discovery run waiting on it steps on. The
// output stops being `known`: the next command to it starts from a position
// nobody can vouch for, so it snaps, exactly as the first move after boot does.
// -----------------------------------------------------------------------------
static void releaseArm(uint8_t armId, ServoLimpReason reason) {
    if (armId >= kArmCount || !isArmEnabled(armId)) {
        return;
    }
    const uint8_t channel = armIdToLedcChannel(armId);
    if (channel >= LEDC_CH_MAX) {
        return;
    }
    endMove(armId);
    servoHoldEnd(&s_arm[armId].hold);
    ledcPwmRelease(channel);
    s_arm[armId].known = false;
    s_arm[armId].limp = reason;
    publishCommanded(armId);
}

// -----------------------------------------------------------------------------
// releaseAllOutputs()
// Estop and Sleep Mode release every enabled output and command no position
// (ADR 0043). This replaced the park that used to drive what a running
// sequence had moved to its close position: a held drive is the 2026-05-21
// grind, and closing many outputs at once is the documented brownout, so a
// stop lets go of everything instead of choosing a position for anything.
//
// Every enabled output, not only the ones a move was in progress on: a door
// somebody opened by hand a minute ago is being driven just as much as one a
// routine was closing, and the stop has to reach it too.
// -----------------------------------------------------------------------------
static void releaseAllOutputs(ServoLimpReason reason) {
    for (uint8_t armId = 0; armId < kArmCount; ++armId) {
        releaseArm(armId, reason);
    }
    PA_LOG_INFO(TAG, "Every output released - %s",
                reason == SERVO_LIMP_ESTOP ? "estop" : "sleep mode");
}

// -----------------------------------------------------------------------------
// holdArm()
// The calibration dial's hold (ADR 0064, #364): drive one output to a width and
// keep the pulse on it until the builder lets go or a bound fires.
//
// The first hold takes the output and starts both bounds; every hold after it
// refreshes the short expiry and nothing else (include/servo_hold.h), so the
// page that sends one a second keeps the hold alive and can never push it past
// the ceiling. A hold at the width the output is already going to -- the page's
// keepalive -- is a refresh and no move at all: replanning a ramp part way
// through, once a second, would restart the move the builder is watching.
//
// The drive itself is driveArmTo()'s, like every other command: through the
// component clamp (ADR 0041), at the Output's own pace (ADR 0052), and a snap
// where the profile cannot plan one.
// -----------------------------------------------------------------------------
static void holdArm(uint8_t armId, uint16_t positionUs, CommandSource source) {
    if (armId >= kArmCount) {
        PA_LOG_WARN(TAG, "[%s] Hold rejected - takes one arm, not %d", commandSourceToString(source),
                    armId);
        return;
    }
    const bool taken = servoHoldCommand(&s_arm[armId].hold, millis());
    if (taken) {
        PA_LOG_INFO(TAG, "[%s] Arm%d held by the dial at %u us", commandSourceToString(source),
                    armId + 1, (unsigned)positionUs);
    }
    const uint16_t goingToUs =
        s_arm[armId].moving ? s_arm[armId].ramp.toUs : s_arm[armId].commandedUs;
    if (s_arm[armId].known && goingToUs == positionUs) {
        // Nothing to drive; the mirror still has to learn the hold was taken.
        if (taken) {
            publishCommanded(armId);
        }
        return;
    }
    driveArmTo(armId, positionUs);
}

// -----------------------------------------------------------------------------
// expireHolds()
// One frame of the dial's two bounds (ADR 0064): an output whose hold commands
// have stopped arriving, or that has been held for the most a dial may, is
// released here and the reason recorded, so the surface can say which one it
// was and offer to take the output again.
// -----------------------------------------------------------------------------
static void expireHolds() {
    const uint32_t now = millis();
    for (uint8_t armId = 0; armId < kArmCount; ++armId) {
        const ServoHoldBound bound = servoHoldBoundHit(s_arm[armId].hold, now, SERVO_HOLD_EXPIRY_MS,
                                                       SERVO_HOLD_CEILING_MS);
        if (bound == SERVO_HOLD_BOUND_NONE) {
            continue;
        }
        const bool ceiling = bound == SERVO_HOLD_BOUND_CEILING;
        releaseArm(armId, ceiling ? SERVO_LIMP_CEILING : SERVO_LIMP_EXPIRED);
        PA_LOG_WARN(TAG, "Arm%d released - %s", armId + 1,
                    ceiling ? "held for the most a dial may" : "the dial's commands stopped arriving");
    }
}

// -----------------------------------------------------------------------------
// processCommand()
// Process incoming servo command.
// Every command is gated per isArmEnabled() (ADR 0027).
// -----------------------------------------------------------------------------
static void processCommand(const ServoCommand& cmd) {
    // Safety: Check estop  --  reject all commands while emergency stopped
    taskENTER_CRITICAL(&robotStateMux);
    bool estop = robotState.estop;
    bool sleepMode = robotState.sleepMode;
    taskEXIT_CRITICAL(&robotStateMux);

    if (estop) {
        PA_LOG_WARN(TAG, "[%s] Command rejected - estop active", commandSourceToString(cmd.source));
        return;
    }
    // A sequence keeps running through Sleep Mode, but it does not move the body
    // while the droid is asleep -- the same rule the body routines kept when
    // ServoTask ran them itself. A move from a person is still accepted.
    if (sleepMode && cmd.source == SRC_SEQ) {
        PA_LOG_INFO(TAG, "[%s] Sequence move ignored - sleep mode active",
                    commandSourceToString(cmd.source));
        return;
    }

    // Feature toggle: reject arm commands for disabled or AUX-LED-reserved subsystems.
    if (!isArmEnabled(cmd.armId)) {
        PA_LOG_DEBUG(TAG, "[%s] Command rejected - arm%d disabled or reserved",
                     commandSourceToString(cmd.source), cmd.armId);
        return;
    }

    uint16_t openUs, closeUs;

    switch (cmd.type) {
        case SERVO_CMD_OPEN:
            if (cmd.armId == 255) {
                getOpenClosePositions(0, openUs, closeUs);
                driveArmTo(0, openUs);
                getOpenClosePositions(1, openUs, closeUs);
                driveArmTo(1, openUs);
                PA_LOG_INFO(TAG, "[%s] Both arms opened", commandSourceToString(cmd.source));
            } else {
                getOpenClosePositions(cmd.armId, openUs, closeUs);
                driveArmTo(cmd.armId, openUs);
                PA_LOG_INFO(TAG, "[%s] Arm%d opened", commandSourceToString(cmd.source), cmd.armId + 1);
            }
            break;

        case SERVO_CMD_CLOSE:
            if (cmd.armId == 255) {
                getOpenClosePositions(0, openUs, closeUs);
                driveArmTo(0, closeUs);
                getOpenClosePositions(1, openUs, closeUs);
                driveArmTo(1, closeUs);
                PA_LOG_INFO(TAG, "[%s] Both arms closed", commandSourceToString(cmd.source));
            } else {
                getOpenClosePositions(cmd.armId, openUs, closeUs);
                driveArmTo(cmd.armId, closeUs);
                PA_LOG_INFO(TAG, "[%s] Arm%d closed", commandSourceToString(cmd.source), cmd.armId + 1);
            }
            break;

        case SERVO_CMD_POSITION:
            // Validate pulse width before setting
            if (cmd.positionUs < SERVO_PULSE_MIN_US || cmd.positionUs > SERVO_PULSE_MAX_US) {
                PA_LOG_WARN(TAG, "[%s] Invalid position %d us - rejected",
                            commandSourceToString(cmd.source), cmd.positionUs);
                return;
            }
            if (cmd.armId == 255) {
                driveArmTo(0, cmd.positionUs);
                driveArmTo(1, cmd.positionUs);
            } else {
                driveArmTo(cmd.armId, cmd.positionUs);
            }
            PA_LOG_INFO(TAG, "[%s] Arm%d set to %d us", commandSourceToString(cmd.source), cmd.armId + 1,
                        cmd.positionUs);
            break;

        case SERVO_CMD_NUDGE:
            // No width to validate: the command carries none, and the pair is
            // computed from the pin (include/servo_nudge.h).
            beginNudge(cmd.armId, cmd.source);
            break;

        case SERVO_CMD_HOLD:
            // The same width rule as a position; the hold is what differs.
            if (cmd.positionUs < SERVO_PULSE_MIN_US || cmd.positionUs > SERVO_PULSE_MAX_US) {
                PA_LOG_WARN(TAG, "[%s] Invalid hold position %d us - rejected",
                            commandSourceToString(cmd.source), cmd.positionUs);
                return;
            }
            holdArm(cmd.armId, cmd.positionUs, cmd.source);
            break;

        case SERVO_CMD_RELEASE:
            if (cmd.armId == 255) {
                releaseArm(0, SERVO_LIMP_RELEASED);
                releaseArm(1, SERVO_LIMP_RELEASED);
                PA_LOG_INFO(TAG, "[%s] Both arms released - pulses off", commandSourceToString(cmd.source));
            } else {
                releaseArm(cmd.armId, SERVO_LIMP_RELEASED);
                PA_LOG_INFO(TAG, "[%s] Arm%d released - pulses off", commandSourceToString(cmd.source),
                            cmd.armId + 1);
            }
            break;
    }
}

// -----------------------------------------------------------------------------
// servoTaskInit()
// Initialize servo hardware once at startup.
// Captures component toggles snapshot and builds LEDC channel mask.
// Per ADR 0027, toggles are read once at boot, never re-checked per iteration.
// Disabled channels are never PWM-initialized or PWM-commanded.
// Channels reserved by AUX LED are excluded from the mask.
// -----------------------------------------------------------------------------
void servoTaskInit() {
    // Capture toggles snapshot once at startup.
    ConfigSnapshot cfg = {};
    configCacheRead(&cfg);
    s_arm1_enabled = cfg.system.enable_arm1;
    s_arm2_enabled = cfg.system.enable_arm2;
    s_aux1_enabled = cfg.system.enable_aux1;
    s_aux2_enabled = cfg.system.enable_aux2;
    s_aux3_enabled = cfg.system.enable_aux3;
    s_dome_enabled = cfg.system.enable_dome_esc;
    s_aux_led_pin = cfg.servo.aux_led_pin;

    bool anyServo = s_arm1_enabled || s_arm2_enabled || s_aux1_enabled || s_aux2_enabled || s_aux3_enabled;
    bool anyLedc = anyServo || s_dome_enabled;

    if (anyLedc) {
        // Build enabled-channels mask using the helper from servo_helpers.h.
        uint8_t ledcMask = servo_enabled_ledc_mask(s_arm1_enabled, s_arm2_enabled, s_aux1_enabled,
                                                   s_aux2_enabled, s_aux3_enabled, s_dome_enabled,
                                                   s_aux_led_pin);

        if (!ledcPwmInit(ledcMask)) {
            PA_LOG_ERROR(TAG, "LEDC init failed");
            return;
        }

        // Call neutral init; it now respects the configured mask.
        ledcPwmInitNeutralPositions();

        // Every enabled output was just driven to neutral, so that is where its
        // first move starts from rather than from a position nobody knows.
        for (uint8_t armId = 0; armId < kArmCount; ++armId) {
            if (isArmEnabled(armId)) {
                s_arm[armId].commandedUs = SERVO_PULSE_NEUTRAL_US;
                s_arm[armId].known = true;
                publishCommanded(armId);
            }
        }

        if (s_aux_led_pin != AUX_LED_PIN_DISABLED) {
            uint8_t reservedChannel = LEDC_CH_MAX;
            if (s_aux_led_pin == AUX_LED_PIN_AUX1) {
                reservedChannel = LEDC_CH_AUX1;
            } else if (s_aux_led_pin == AUX_LED_PIN_AUX2) {
                reservedChannel = LEDC_CH_AUX2;
            } else if (s_aux_led_pin == AUX_LED_PIN_AUX3) {
                reservedChannel = LEDC_CH_AUX3;
            }
            if (reservedChannel != LEDC_CH_MAX) {
                PA_LOG_INFO(TAG, "AUX LED active on selection %u (GPIO %u) - LEDC skipped for that header",
                            (unsigned)s_aux_led_pin, (unsigned)getChannelGpio(reservedChannel));
            }
        }
    } else {
        PA_LOG_INFO(TAG, "all LEDC outputs disabled - skipping LEDC init");
    }

    if (anyServo) {
        PA_LOG_INFO(TAG, "Servo outputs ready (ARM1/2/AUX1-3 channels armed at neutral)");
    } else {
        PA_LOG_INFO(TAG, "arm/aux outputs disabled");
    }
}

// -----------------------------------------------------------------------------
// servoTask()
// Main servo task loop.
// -----------------------------------------------------------------------------
void servoTask(void* pvParameters) {
    (void)pvParameters;

    // Register with task watchdog unconditionally. Feed immediately after add:
    // the add-to-first-feed window must stay empty of anything that can stall
    // (the disabled-path log below runs inside it, #245 defect 1).
    esp_task_wdt_add(NULL);
    esp_task_wdt_reset();

    // Feature toggle: if no arm/aux outputs are enabled, ServoTask has no
    // channels to drive. Idle here feeding TWDT only  --  no queue processing,
    // no sequence updates.
    if (!configCacheServoAnyEnabled()) {
        PA_LOG_DEBUG("ServoTask", "all arm/aux outputs disabled - task idle");
        for (;;) {
            esp_task_wdt_reset();
            vTaskDelay(pdMS_TO_TICKS(20));
        }
    }

    ServoCommand cmd;
    bool hwmLogged = false;
    bool halted = false;

    while (true) {
        if (!hwmLogged) {
            PA_LOG_DEBUG("ServoTask", "stack HWM: %u bytes free",
                         (unsigned)uxTaskGetStackHighWaterMark(NULL));
            hwmLogged = true;
        }

        // Entering estop or Sleep Mode stops every move where it is, before a
        // command or a frame can carry one further, and then releases every
        // enabled output (ADR 0043): nothing is driven, so nothing can grind
        // and nothing can brown out. On the edge only: a direct command is
        // still accepted in Sleep Mode, and it must be able to move. Both
        // flags are read in one critical section so the reason recorded is the
        // one that fired.
        taskENTER_CRITICAL(&robotStateMux);
        const bool estopNow = robotState.estop;
        const bool sleepNow = robotState.sleepMode;
        taskEXIT_CRITICAL(&robotStateMux);
        const bool haltNow = estopNow || sleepNow;
        if (haltNow && !halted) {
            stopAllMoves("estop or sleep mode entered");
            releaseAllOutputs(estopNow ? SERVO_LIMP_ESTOP : SERVO_LIMP_SLEEP);
        }
        halted = haltNow;

        // Process any pending commands (non-blocking)
        while (xQueueReceive(servoCmdQueue, &cmd, 0) == pdTRUE) {
            processCommand(cmd);
        }

        // Advance every move in progress by one frame
        updateMotion();

        // Then the dial's two bounds, after this frame's commands have had
        // their say: a hold refreshed this frame is not an expired one.
        expireHolds();

        // Feed watchdog
        esp_task_wdt_reset();

        // 50Hz update rate
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}
