// =============================================================================
// src/tasks/servo_task.cpp
//
// ServoTask  --  every Servo Output, from every source: open, close, position,
// nudge, travel, hold and release.
//
// A command names its Output by its Output Address (#444). This task keeps one
// state per Output it can drive, in that Output's slot (include/servo_backend.h),
// and puts widths on pins and takes them off through the backend seam. Its two
// members are LEDC on the board's own GPIO Outputs - ARM1/ARM2 (the utility
// arms) and ARM3-5 on the Artoo PCB - and, when a builder has chosen it as the
// body servo controller, a PCA9685 on the I2C header, `pca:0`-`pca:15`, whose
// writes leave this core for a task on Core 0 (include/pca9685.h). The ramp,
// the component clamp, the dial's hold and both kinds of release are decided
// here, above the seam, for both. DOME (GPIO 25) is controlled separately as an
// ESC, not a servo.
// =============================================================================

#include "servo_task.h"

#include <esp_task_wdt.h>

#include "board_output_enabled.h"  // boardOutputIsWired() - the wired ticks, by Output
#include "board_outputs.h"  // boardOutputOnChannel(), boardOutputLabel()
#include "component_registry.h"  // componentPartByValue() - this boot's body servo controller
#include "config.h"
#include "config_cache.h"
#include "ledc_pwm.h"
#include "logging.h"
#include "output_wire.h"  // which wires LEDC must stay off (#416)
#include "servo_backend.h"  // slots, and the three verbs that reach a pin (#444)
#include "robot_state.h"
#include "servo_component_helpers.h"  // servoCompTypeToString, for the clamp note
#include "servo_halt.h"         // when estop and Sleep Mode each let go (ADR 0043)
#include "servo_helpers.h"
#include "servo_hold.h"         // the dial's hold and its two bounds (ADR 0064)
#include "servo_motion_ramp.h"  // a move planned in time from the Output's profile (ADR 0052)
#include "servo_nudge.h"        // the bounded pair a Find by Moving nudge visits (ADR 0050)
#include "servo_output_row.h"  // the addressed rows an endpoint lives on (ADR 0041)
#include "servo_release.h"     // the Output Release a move owes once it arrives (ADR 0043)
#include "servo_run.h"         // a Find by Moving run's hold on a free Output (#411)
#include "servo_travel.h"      // the recorded ends a body view's press visits (ADR 0063)

static const char* TAG = "SERVO";

// Boot snapshot of component toggles, captured once at startup.
// Toggles are staged at reboot (ADR 0027); this snapshot is the stable read for
// the whole session. The wired ticks are one bit per slot (include/
// servo_backend.h), each read from its board Output's stored tick.
static uint32_t s_wired_at_start_mask = 0;
static bool s_dome_enabled = false;
// Which Outputs carry a light instead of a servo, one bit per slot. Captured at
// startup beside the toggles above, because what a wire carries is read once at
// boot like every other Component Toggle (ADR 0027). A droid may have several
// lit wires (ADR 0067, #413), which is why this is a mask and not the single
// slot number it replaced.
static uint32_t s_lit_mask = 0;
// Whether LEDC came up at start. An Output on a timer that never started is
// driven by nothing, whatever its tick says (backendReady()).
static bool s_ledc_ready = false;

// Whether the PCA9685 is this boot's body servo controller: chosen when the
// droid started, which is when a member choice takes effect (ADR 0027). Its
// sixteen slots are wired at start the way a board Output is - by having a
// Part on them (wiredAtStartMask()).
static bool s_expander_chosen = false;

// The expander's slots, one bit each, in the slot order include/
// servo_backend.h lays out.
static constexpr uint32_t kExpanderSlotMask =
    ((1u << PCA9685_CHANNEL_COUNT) - 1u)
    << servoOutputSlotOf(ServoOutputAddress{SERVO_DRIVER_PCA9685, 0});

// The Outputs whose backend cannot be reached, one bit per slot: the expander's
// sixteen when it was chosen and did not answer at start, or stopped answering
// since (noticeAnUnreachableExpander()). Nothing is driven on one, and it
// reports why (SERVO_LIMP_UNREACHABLE). Bits are only ever set, by this task -
// in servoTaskInit() and on its own loop - and the Core 0 readers
// (servoTaskDrivesOutput(), servoTaskExpanderFacts()) read the aligned word
// without a lock: a stale read is one frame late saying an Output it can no
// longer drive is unreachable, and the command it admits is then dropped here.
static uint32_t s_unreachable_mask = 0;

// -----------------------------------------------------------------------------
// Where each output is, and the move it is part way through (ADR 0052).
//
// One entry per slot: s_out[slot] is the Output at servoOutputSlotAddress(slot),
// and robotState.servoCommanded[slot] is its mirror.
//
// `commandedUs` is the pulse this task last put on the pin. A move starts from
// it, so it is only trusted once this task has written something there:
// `known` is false until then, and a move from an unknown position is a jump.
//
// `hold` is the calibration dial's hold on the output (ADR 0064, #364): while
// it stands the pulse stays on until one of its two bounds fires, the builder
// lets go, or the halt edge releases it. When s_runSlot below names the slot, the
// hold is a Find by Moving run's on a FREE Output instead (include/servo_run.h,
// #411): nothing drives that Output this boot, so while the run holds it this
// task drives it anyway, the same two bounds let it go, and s_runSlot is what
// lets a halt, a release and a bound reach an Output isOutputEnabled() says no to.
// `limp` is why there is no pulse, read only while `known` is false, so a
// surface can say "pulses off" and "the estop let go" differently.
//
// `release` is the Output Release the last move to ARRIVE owes (ADR 0043,
// #443): armed by armReleaseOnArrival() at every place a move arrives, and
// cancelled by endMove(), which everything that starts a move or takes the
// pulse off runs first. Never pending under a hold (ADR 0064).
//
// An OUT-AND-BACK is one more kind of move on this state, not a second machine
// beside it. There are two of them and they share every field here: a Find by
// Moving nudge (ADR 0050, #363) over a small pair about the pin, and a Part run
// through its travel and back (ADR 0063, #352) over the Output's recorded ends.
// `legNo` is which of the three legs is in progress, 0 when none is; `ramp` is
// the leg's own ramp, planned the way any move is; and `moving` stays true for
// the whole out-and-back, dwells included, so stopAllMoves() ends it where it is
// exactly as it ends a ramp.
//
// `legIsNudge` is the one thing the two are told apart by after they start, and
// it exists for one reason: `nudgesDone` is what a discovery run steps on, so a
// travel must not bump it. Everything else about the two motions is identical
// and is deliberately not duplicated.
// -----------------------------------------------------------------------------
static constexpr uint8_t kSlotCount = SERVO_OUTPUT_SLOT_COUNT;

// Out, across, back. Both planners lay out the same three legs over their own
// two ends, which is what lets one machine drive both; the static_assert is
// there so a planner that grew a fourth leg breaks the build rather than
// quietly writing past legTargetUs.
static_assert(SERVO_NUDGE_LEG_COUNT == SERVO_TRAVEL_LEG_COUNT,
              "a nudge and a travel are the same three-leg out-and-back");
static constexpr uint8_t kLegCount = SERVO_NUDGE_LEG_COUNT;

// How long an out-and-back rests at each end before moving on. Long enough for
// an eye that is on the droid rather than the page to catch a small twitch and
// see which way it went -- a snap is over in one frame, and it is the pause on
// either side that makes it readable as "out, across, back" rather than a
// flicker. Short enough that stepping through five spare outputs is seconds,
// not a chore: a snap nudge is two dwells, 1.2 s, and the whole run of five is
// well under half a minute with the browser's one-second reads between them.
static constexpr uint32_t kLegDwellMs = 600;

static struct {
    uint16_t commandedUs;
    bool known;
    bool moving;
    ServoHoldState hold;
    ServoLimpReason limp;
    ServoReleaseTimer release;  // the Output Release owed since the last arrival
    ServoMotionRamp ramp;
    uint8_t legNo;             // 1..kLegCount while an out-and-back is in progress, else 0
    bool legIsNudge;           // that out-and-back is a nudge, not a travel
    bool legDwelling;          // the leg has arrived and the output is resting there
    uint32_t legDwellEndMs;    // when that rest ends
    uint16_t legTargetUs[kLegCount];  // where each leg ends, from whichever planner started it
    uint8_t nudgesDone;        // mirrored to robotState; see ServoCommandedPosition
} s_out[kSlotCount] = {};

// The slot of the one free Output a Find by Moving run holds, or SERVO_RUN_NONE
// (#411). One index, not a flag per Output: a run holds at most one Output, and
// taking the next lets go of this one first (takeForRun()), so two free servos
// energized at once is not a state this task can be in. Written only on Core 1, by this
// task; servoTaskRunHolds() reads the byte from Core 0 (include/servo_task.h).
static uint8_t s_runSlot = SERVO_RUN_NONE;

// Forward declaration for functions used in static helpers below.
static bool isOutputEnabled(uint8_t slot);

// -----------------------------------------------------------------------------
// wiredAtStartMask() / litOutputMask()
// The two facts the boot snapshot keeps per slot.
//
// The wired tick is stored per board Output (include/board_output_enabled.h),
// so a slot reads its board Output's tick; an Output that is not one of the
// board's has no tick and is not wired.
//
// The lit mask says which Outputs carry a light rather than a servo, read from
// the Servo Output rows (ADR 0067). It exists to keep LEDC off a pin a WS2812B
// may be driving, and include/output_wire.h decides which pins those are -
// outputWirePinKeptForLight(), deliberately wider than the question AuxLedTask
// asks, so an unticked strip ends with neither side on the pin. It asks by
// board index, because a light only goes on the board's own wires.
// -----------------------------------------------------------------------------
//
// An expander's Output has no stored tick (include/board_output_enabled.h), so
// its slot reads what the tick follows on a board Output: whether a Part is on
// its row. That, and only while the expander is this boot's member - a row
// kept from a session it was chosen in is not wired to anything now.
static uint32_t wiredAtStartMask(const SystemConfig& system) {
    uint32_t mask = 0;
    for (uint8_t slot = 0; slot < kSlotCount; ++slot) {
        const ServoOutputAddress output = servoOutputSlotAddress(slot);
        const size_t boardIndex = boardOutputIndexOf(output);
        const bool wired =
            boardIndex < BOARD_OUTPUT_COUNT
                ? boardOutputIsWired(system, boardIndex)
                : output.driver == SERVO_DRIVER_PCA9685 && s_expander_chosen &&
                      configCacheServoOutputPartCountAt(output.driver, output.channel) > 0;
        if (wired) {
            mask |= 1u << slot;
        }
    }
    return mask;
}

static uint32_t litOutputMask(const SystemConfig& system) {
    uint32_t mask = 0;
    for (uint8_t slot = 0; slot < kSlotCount; ++slot) {
        const ServoOutputAddress output = servoOutputSlotAddress(slot);
        const size_t boardIndex = boardOutputIndexOf(output);
        const OutputWireInputs in = {
            boardIndex < BOARD_OUTPUT_COUNT && boardOutputIsWired(system, boardIndex),
            configCacheReadServoOutputComponent(output.driver, output.channel),
        };
        if (outputWirePinKeptForLight(in, boardIndex)) {
            mask |= 1u << slot;
        }
    }
    return mask;
}

// -----------------------------------------------------------------------------
// isOutputEnabled()
// Check feature toggle for a given slot using the boot-time snapshot.
// Toggles are read once at startup and never re-checked per iteration.
// An output whose wire carries a Light Type is treated as unavailable to avoid
// pin ownership conflicts: a WS2812B's signal line and a servo's PWM cannot
// share a pin. SERVO_OUTPUT_SLOT_NONE - an address no member drives - is never
// enabled.
// Per ADR 0027, this function gates all servo operations on the component
// toggle snapshot captured at startup.
// -----------------------------------------------------------------------------
//
// An Output whose backend cannot be reached (s_unreachable_mask) is not
// enabled either: a write to it goes nowhere, so a command to it is dropped
// here like a command to an Output nothing drives.
static bool isOutputEnabled(uint8_t slot) {
    return servo_output_enabled(slot, s_wired_at_start_mask & ~s_unreachable_mask, s_lit_mask);
}

// -----------------------------------------------------------------------------
// backendReady()
// Whether the backend an Output is on can put a pulse on it at all: LEDC's
// timer came up, or the expander was chosen and is answering.
// -----------------------------------------------------------------------------
static bool backendReady(uint8_t slot) {
    switch (servoOutputSlotAddress(slot).driver) {
        case SERVO_DRIVER_LEDC:
            return s_ledc_ready;
        case SERVO_DRIVER_PCA9685:
            return s_expander_chosen && (s_unreachable_mask & (1u << slot)) == 0;
        default:
            return false;
    }
}

// -----------------------------------------------------------------------------
// isTargetEnabled() / bothArmsSlot() / wiredAtStart()
// A command's target against the boot snapshot: one Output's isOutputEnabled(),
// or for `both` (SERVO_OUTPUT_BOTH_ARMS) both arms wired at start, each then
// driven through its own slot and its own light check (servo_target_enabled(),
// include/servo_helpers.h). wiredAtStart() is the tick alone, light or none.
//
// isTargetEnabled() is noinline, deliberately: processCommand() is forced into
// servoTask()'s frame, and inlined there its two walks of the board's table for
// `both` grew that root frame by 32 B and the walked chain past its recorded
// figure (ADR 0040, #444). Out of line it is a leaf of its own, far shallower
// than the route that sets the chain.
// -----------------------------------------------------------------------------
static bool __attribute__((noinline)) isTargetEnabled(ServoOutputAddress output) {
    return servo_target_enabled(output, s_wired_at_start_mask, s_lit_mask);
}

static uint8_t bothArmsSlot(uint8_t which) {
    return servoOutputSlotOf(boardOutputAddress(which));
}

static bool wiredAtStart(uint8_t slot) {
    return servo_output_enabled(slot, s_wired_at_start_mask, /*lit_mask=*/0);
}

// -----------------------------------------------------------------------------
// isOutputLive()
// Whether this task drives the Output right now: enabled since start, or a free
// Output a Find by Moving run has taken (#411). What writes a pulse and what
// takes one off both ask this, so a run's Output is driven and let go by the
// same paths as any other - the estop's release included.
// -----------------------------------------------------------------------------
static bool isOutputLive(uint8_t slot) {
    return servoRunOutputLive(isOutputEnabled(slot), slot < kSlotCount && s_runSlot == slot);
}

// -----------------------------------------------------------------------------
// runTakeInputs()
// What decides whether a Find by Moving run may take this Output, each read
// where it lives: the boot snapshot for what this task drives and what it kept
// LEDC off for, the live cache for the Parts on the row and what its wire
// carries now. Values only, no row copy: this runs on the command path of the
// Core 1 loop, whose chain is a measured constant (ADR 0040).
// -----------------------------------------------------------------------------
static ServoRunTakeInputs runTakeInputs(uint8_t slot) {
    const ServoOutputAddress output = servoOutputSlotAddress(slot);
    ServoRunTakeInputs in = {};
    in.drivenNow = backendReady(slot) && isOutputEnabled(slot);
    in.wiredAtStart = wiredAtStart(slot);
    in.litAtStart = (s_lit_mask & (1u << slot)) != 0;
    in.lightNow = outputWirePinKeptForLight(
        {false, configCacheReadServoOutputComponent(output.driver, output.channel)},
        boardOutputIndexOf(output));
    in.partCount = configCacheServoOutputPartCountAt(output.driver, output.channel);
    in.backendReady = backendReady(slot);
    return in;
}

static bool mayTakeForRun(uint8_t slot) {
    return slot < kSlotCount && servoRunMayTake(runTakeInputs(slot));
}

// -----------------------------------------------------------------------------
// resolveOutputPulse()
// The pulse width an Output may actually be driven to. Returns false (no log,
// nothing to write) if the Output is not live (isOutputLive()).
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
// a ServoOutputRow is 72 B to answer a question whose answer is one number.
// -----------------------------------------------------------------------------
static bool resolveOutputPulse(uint8_t slot, uint16_t pulseUs, uint16_t* commandedOut) {
    if (!isOutputLive(slot)) {
        return false;
    }

    const ServoOutputAddress output = servoOutputSlotAddress(slot);
    if (slot >= kSlotCount) {
        PA_LOG_WARN(TAG, "resolveOutputPulse: invalid slot %d", slot);
        return false;
    }

    // A returned width equal to the request is nothing to report, which is also
    // what an output no row describes comes back as - so the two cases need no
    // second flag to tell them apart.
    ServoComponentType component = SERVO_COMP_NONE;
    const uint16_t commandedUs =
        configCacheClampServoOutputPulse(output.driver, output.channel, pulseUs, &component);
    if (commandedUs != pulseUs) {
        PA_LOG_WARN(TAG, "%s %d us is outside what a %s takes - sending %d us instead",
                    servoOutputSlotName(slot), pulseUs, servoCompTypeToString(component),
                    commandedUs);
    }

    *commandedOut = commandedUs;
    return true;
}

// -----------------------------------------------------------------------------
// publishCommanded()
// Tell every surface where this output has been told to be (#362).
//
// `nowUs` is the width on the pin. `targetUs` is where the move in progress
// ends, or `nowUs` again when nothing is moving, so the two marks a surface
// draws close up exactly when the move does. An overshoot's aim is never the
// target: the move ends where it settles (ramp.settleUs), and that is the
// number a builder asked for. `pulsing` is `known`: an output
// only becomes known by this task putting a pulse on it -- the neutral pulse at
// init, or a write -- and only releaseOutput() takes one away (pulses off, a hold
// bound, or the halt edge; ADR 0043, ADR 0064), which is where `known` is
// cleared and `limp` says why.
//
// Called at every place one of them changes: a write, a ramp planned, a move
// abandoned, a hold taken, a release, and init -- so `moving` is published at
// every place it changes too, the settle back of an overshoot included, since
// a new ramp is planned by stepMove() without the move ever ending. It is a
// copy into robotState under robotStateMux, like every robotState write, and
// allocates nothing.
// -----------------------------------------------------------------------------
static void publishCommanded(uint8_t slot) {
    const ServoCommandedPosition commanded = {
        s_out[slot].commandedUs,
        s_out[slot].moving ? s_out[slot].ramp.settleUs : s_out[slot].commandedUs,
        s_out[slot].known,
        s_out[slot].nudgesDone,
        // `held` is the dial's (GET /api/servo/outputs): a run's hold on a free
        // Output is not a dial holding it.
        s_out[slot].hold.held && s_runSlot != slot,
        s_out[slot].limp,
        s_out[slot].moving,
    };
    taskENTER_CRITICAL(&robotStateMux);
    robotState.servoCommanded[slot] = commanded;
    taskEXIT_CRITICAL(&robotStateMux);
}

// -----------------------------------------------------------------------------
// endLeg() / endMove()
// The one place a move stops being in progress, whatever stops it: arrival, a
// later command on the same Output, or the halt edge.
//
// It also ends the Output Release the last arrival left pending (#443). Every
// command that moves the output ends the move in progress first (driveOutputTo(),
// beginNudge(), beginTravel(), takeForRun()), and so does every way the pulse
// comes off (releaseOutput()), so a pending release is cancelled by exactly the
// things ADR 0043 says cancel it, and never fires later on a move it did not
// belong to. An arrival re-arms it straight after (armReleaseOnArrival()). A
// command refused before it moves anything - a nudge outside the cautious band,
// a travel on an Output nobody measured - reaches none of these and leaves the
// release as it was: the Output was not moved, so the hold it is on is still
// the one the release was counting.
//
// A NUDGE that was in progress is counted as ended here, however it ended. The
// count is what a discovery run waits on -- it cannot watch the nudge itself,
// because a whole nudge can fall between two of its one-second reads -- so a
// nudge cut short by an estop or overtaken by a new command must count exactly
// as a returned one does, or the run would wait for a return that is never
// coming. A TRAVEL is the same motion and is deliberately NOT counted: nobody
// is waiting on one, and a body view's press bumping the count would step a
// discovery run somewhere else on the droid onto its next output. The callers
// publish; this only changes what they will publish.
// -----------------------------------------------------------------------------
static void endLeg(uint8_t slot) {
    if (s_out[slot].legNo == 0) {
        return;
    }
    const bool wasNudge = s_out[slot].legIsNudge;
    s_out[slot].legNo = 0;
    s_out[slot].legDwelling = false;
    if (wasNudge) {
        s_out[slot].nudgesDone++;
    }
}

static void endMove(uint8_t slot) {
    s_out[slot].moving = false;
    servoReleaseCancel(&s_out[slot].release);
    endLeg(slot);
}

// -----------------------------------------------------------------------------
// armReleaseOnArrival()
// A move on this output has arrived: a snap's one write, a ramp's last frame,
// an overshoot's settle back, or an out-and-back's last leg. Start its Output
// Release counting from now (ADR 0043, #443) - the row's release time, read as
// one number (configCacheReadServoOutputReleaseMs(), never for a light), not as
// the row it sits in: this is ServoTask's measured chain (ADR 0040). A row at 0
// arms nothing and the output holds where it stopped, exactly as before the
// release existed.
//
// Nothing is armed while a hold stands (servoReleaseArm()). For the dial that
// is ADR 0064's suppression: the dial's two bounds end the hold instead. A Find
// by Moving run's hold on a free Output is left to the same two bounds for the
// same reason, and every hold ends by releaseOutput(), so nothing is owed after it.
//
// Called after endMove(), which cancelled whatever the move before owed.
// -----------------------------------------------------------------------------
static void armReleaseOnArrival(uint8_t slot, uint32_t nowMs) {
    const ServoOutputAddress output = servoOutputSlotAddress(slot);
    servoReleaseArm(&s_out[slot].release, nowMs,
                    configCacheReadServoOutputReleaseMs(output.driver, output.channel),
                    s_out[slot].hold.held);
}

// -----------------------------------------------------------------------------
// writeOutputPulse()
// Put one width on the pin, through the Output's backend (include/
// servo_backend.h), and say so. The width has already been through
// resolveOutputPulse(); this is the write and nothing else.
//
// What is written is what robotState then reports, because the position a
// status reader sees has to be the pulse the pin is actually holding -- part
// way through a ramp too.
// -----------------------------------------------------------------------------
static void writeOutputPulse(uint8_t slot, uint16_t pulseUs) {
    servoBackendWrite(servoOutputSlotAddress(slot), pulseUs);
    s_out[slot].commandedUs = pulseUs;
    s_out[slot].known = true;

    // The commanded width, and only that. There was an armOpen[] bit beside it
    // deriving "open" from `commandedUs > SERVO_PULSE_NEUTRAL_US`, which is
    // wrong on any reversed Endpoint Pair -- past neutral does not mean open
    // when open is the lower number (ADR 0041). It has gone; anything wanting
    // to say which end this output is at compares the width against the pair on
    // its row, where the direction is recorded.
    publishCommanded(slot);
}

// -----------------------------------------------------------------------------
// driveOutputTo()
// Send an Output to a pulse width at the pace its Motion Profile sets.
//
// The time comes from the row and from nowhere else: a ServoCommand carries no
// duration, so a Body Step, an RC toggle and a browser move cannot disagree
// about how long a door takes (ADR 0049, ADR 0052). Where the profile cannot
// plan a move -- no row, an unmeasured output, no known starting point -- the
// Output snaps, which is exactly what every move did before the profile existed,
// and is also the first move after a release (#364): a released output is no
// longer `known`, so there is no position to ramp from.
//
// The profile arrives as a ServoMotionProfile, not as the row it sits in: like
// the clamp and the Endpoint Pair, it is answered by address out of the live
// table, so no 72 B ServoOutputRow is put on ServoTask's measured chain
// (ADR 0040). Its ease is already the one that runs (servoMotionProfileOf()),
// so an overshoot on an unmeasured Output never reaches the planner as one.
// -----------------------------------------------------------------------------
//
// A Gesture's move may carry its own throw time and easing (`throwMs`,
// `easingPlusOne`, both 0 for the Output's own), which apply to this one move
// and never to the row (servoMotionOverride(), ADR 0049).
static void driveOutputTo(uint8_t slot, uint16_t pulseUs, uint16_t throwMs = 0,
                          uint8_t easingPlusOne = 0) {
    uint16_t targetUs = 0;
    if (!resolveOutputPulse(slot, pulseUs, &targetUs)) {
        return;
    }
    const ServoOutputAddress output = servoOutputSlotAddress(slot);
    // Whatever the Output was doing is over: a new command replaces a ramp part
    // way through, and a nudge part way through, alike.
    endMove(slot);

    // A snap arrives in the frame it is written, so its release counts from
    // here. The boot pass's move home is always one - nothing is `known` at
    // boot - so a row set to go home and hold holds for its release time and
    // lets go, like any arrival (operator, 2026-09-30 on #443).
    ServoMotionProfile profile = {};
    if (!s_out[slot].known ||
        !configCacheReadServoOutputMotionProfile(output.driver, output.channel, &profile)) {
        writeOutputPulse(slot, targetUs);
        armReleaseOnArrival(slot, millis());
        return;
    }

    servoMotionOverride(&profile, throwMs, easingPlusOne);
    const ServoMotionRamp ramp =
        servoMotionPlan(s_out[slot].commandedUs, targetUs, profile, millis());
    if (ramp.durationMs == 0) {
        writeOutputPulse(slot, targetUs);
        armReleaseOnArrival(slot, millis());
        return;
    }
    s_out[slot].ramp = ramp;
    s_out[slot].moving = true;
    // Nothing is written until the next frame, but the move already has a
    // target, and that is what a surface shows beside where the output stands.
    publishCommanded(slot);
}

// -----------------------------------------------------------------------------
// The out-and-back: a Find by Moving nudge (ADR 0050, #363) and a Part run
// through its travel (ADR 0063, #352)
//
// One command, run to completion by this task: out to one end, a dwell, across
// to the other, a dwell, and back to the width the output started from. The
// whole out-and-back is ServoTask's so that the return cannot depend on a
// browser staying alive between two requests -- a discovery run that died half
// way would otherwise leave a spare output parked 100 us off wherever it was,
// and a body view whose tab was closed mid-press would leave a door standing
// open.
//
// Each leg is an ordinary move: through resolveOutputPulse() like every drive
// (ADR 0041), planned from the Output's Motion Profile like every drive
// (ADR 0052), so a calibrated output eases through it and an unmeasured one
// snaps, exactly as either does for any other command.
//
// The two ends never come from the command. A nudge's pair is computed from the
// width on the pin (include/servo_nudge.h) and a travel's is read off the
// Output's own row (include/servo_travel.h), so no source can ask for a big
// move by either route. Both planners hand this machine the same three leg
// targets, and from here on the motion is one thing.
// -----------------------------------------------------------------------------
static void beginLeg(uint8_t slot, uint8_t leg, uint32_t nowMs);

// The leg has arrived. Rest there where an eye can catch it, or, after the
// last leg, the out-and-back is over: the output is back where it started.
static void legArrived(uint8_t slot, uint32_t nowMs) {
    if (s_out[slot].legNo < kLegCount) {
        s_out[slot].legDwelling = true;
        s_out[slot].legDwellEndMs = nowMs + kLegDwellMs;
        return;
    }
    const bool wasNudge = s_out[slot].legIsNudge;
    endMove(slot);
    // The out-and-back is the move, so its release counts from the return, not
    // from any leg before it (#443).
    armReleaseOnArrival(slot, nowMs);
    publishCommanded(slot);
    PA_LOG_INFO(TAG, "%s %s returned to %u us", servoOutputSlotName(slot),
                wasNudge ? "nudge" : "travel",
                (unsigned)s_out[slot].commandedUs);
}

static void beginLeg(uint8_t slot, uint8_t leg, uint32_t nowMs) {
    uint16_t targetUs = 0;
    // A leg number outside 1..kLegCount is the return, never another way out:
    // the same rule both planners' own LegTarget() keeps, so a caller that has
    // run off the end sends the output home rather than indexing past the array.
    const uint8_t legIndex =
        (leg >= 1 && leg <= kLegCount) ? (uint8_t)(leg - 1) : (uint8_t)(kLegCount - 1);
    // A nudge's pair lies inside the cautious band and a travel's ends were
    // clamped onto the row when they were recorded, so the clamp hands either
    // back unchanged -- but the door is the rule (ADR 0041), not the outcome.
    // This can only fail for a slot resolveOutputPulse() rejects, which both
    // beginNudge() and beginTravel() have already refused.
    if (!resolveOutputPulse(slot, s_out[slot].legTargetUs[legIndex], &targetUs)) {
        endMove(slot);
        publishCommanded(slot);
        return;
    }
    s_out[slot].legNo = leg;
    s_out[slot].legDwelling = false;
    s_out[slot].moving = true;

    // An Output no row describes leaves the profile as it was initialised:
    // `calibrated` unset, which the planner answers with a snap -- the same
    // jump every unmeasured move makes.
    const ServoOutputAddress output = servoOutputSlotAddress(slot);
    ServoMotionProfile profile = {};
    configCacheReadServoOutputMotionProfile(output.driver, output.channel, &profile);
    const ServoMotionRamp ramp = servoMotionPlan(s_out[slot].commandedUs, targetUs, profile, nowMs);
    s_out[slot].ramp = ramp;
    if (ramp.durationMs == 0) {
        // A snap: the leg is over the moment it is written.
        writeOutputPulse(slot, targetUs);
        legArrived(slot, nowMs);
        return;
    }
    // A ramp: nothing is written until the next frame, but the leg already has
    // its target, and that is what a surface shows beside where the output is.
    publishCommanded(slot);
}

static void releaseOutput(uint8_t slot, ServoLimpReason reason);

// -----------------------------------------------------------------------------
// sayTheOutputLetGo()
// The log line for an Output being let go - by a run moving on from it, or by
// its own release time running out (#443) - and nothing else.
//
// noinline, and a leaf, deliberately: its PA_LOG_* line buffer must stay out of
// servoTask()'s frame, which processCommand() and so takeForRun() are inlined
// into (see processCommand()) and which every route on ServoTask's measured
// chain starts from (ADR 0040). A leaf, because a helper that also called
// releaseOutput() put its log frame over the LEDC driver's log route: +208 B
// walked on the ESP32-P4 (#411 slice 4). Its callers release the Output
// themselves, so the buffer is only ever on the stack for the line.
// -----------------------------------------------------------------------------
static void __attribute__((noinline)) sayTheOutputLetGo(uint8_t slot, const char* why) {
    PA_LOG_INFO(TAG, "%s let go - %s", servoOutputSlotName(slot), why);
}

// -----------------------------------------------------------------------------
// takeForRun()
// A nudge has named a free Output (#411). Whether it is still free is asked on
// every nudge, not only the first (servoRunOnNudge()): the run takes it, keeps
// the hold it has (a nudge is the arrival, and not even one moves the ceiling),
// or refuses it, letting go of it if a Part or a Light Type has landed on it
// since. Taking a new Output lets go of the one the run held before FIRST:
// releaseOutput() reaches only an Output isOutputLive() calls driven, and s_runSlot
// still names the old one until this moves it on.
//
// Taking it attaches its channel with no pulse and puts a first width on the
// pin, through the component clamp, so the nudge has a width to be about: the
// Output's recorded centre, kept inside the part of the cautious band a nudge
// can be symmetric about (servoRunFirstWidthUs()). A free servo has never been
// driven, so that first width is a jump, as every first move after boot is
// (#364). False, with nothing driven, when the run may not take it or its
// backend will not attach it.
// -----------------------------------------------------------------------------
static bool takeForRun(uint8_t slot, CommandSource source) {
    const ServoRunNudgeStep step = servoRunOnNudge(s_runSlot, slot, mayTakeForRun(slot));
    if (step.letGo != SERVO_RUN_NONE) {
        releaseOutput(step.letGo, SERVO_LIMP_OFF);
        sayTheOutputLetGo(step.letGo, step.letGo == slot ? "it is not free for a run any more"
                                                         : "the run moved on to the next output");
    }
    if (step.act == SERVO_RUN_REFUSE) {
        return false;
    }
    if (step.act == SERVO_RUN_KEEP) {
        servoHoldCommand(&s_out[slot].hold, millis(), SERVO_HOLD_ASK_TAKE);
        return true;
    }
    const ServoOutputAddress output = servoOutputSlotAddress(slot);
    if (!servoBackendAttach(output)) {
        PA_LOG_WARN(TAG, "[%s] %s not taken for the run - its channel would not attach",
                    commandSourceToString(source), servoOutputSlotName(slot));
        return false;
    }
    uint16_t centreUs = SERVO_PULSE_NEUTRAL_US;
    configCacheReadServoOutputCentre(output.driver, output.channel, &centreUs);
    const uint16_t firstUs = servoRunFirstWidthUs(centreUs, SERVO_BAND_STD, SERVO_NUDGE_AMPLITUDE_US);
    servoHoldCommand(&s_out[slot].hold, millis(), SERVO_HOLD_ASK_TAKE);
    s_runSlot = slot;
    uint16_t commandedUs = 0;
    if (resolveOutputPulse(slot, firstUs, &commandedUs)) {
        endMove(slot);
        writeOutputPulse(slot, commandedUs);
    }
    PA_LOG_INFO(TAG, "[%s] %s taken for a Find by Moving run at %u us (recorded centre %u us)",
                commandSourceToString(source), servoOutputSlotName(slot), (unsigned)commandedUs,
                (unsigned)centreUs);
    return true;
}

// A SERVO_CMD_NUDGE that got past processCommand()'s gates. Refused, with
// nothing moved and the count still bumped so a run waiting on it steps on,
// when there is no width on the pin to nudge about or that width is outside
// the cautious band.
static void beginNudge(uint8_t slot, CommandSource source) {
    // One output per nudge, always: a run nudges the spare outputs one at a
    // time so the builder can say which one moved, and the ARM1+ARM2 broadcast
    // would move two in one press.
    if (slot >= kSlotCount) {
        PA_LOG_WARN(TAG, "[%s] Nudge rejected - takes one output, not slot %d",
                    commandSourceToString(source), slot);
        return;
    }
    // A free Output is taken for the run first (#411); an enabled one is driven
    // already and is nudged about where it is.
    if (!isOutputEnabled(slot) && !takeForRun(slot, source)) {
        PA_LOG_WARN(TAG, "[%s] %s not nudged - it is not free for a run",
                    commandSourceToString(source), servoOutputSlotName(slot));
        s_out[slot].nudgesDone++;
        publishCommanded(slot);
        return;
    }
    ServoNudgePlan plan = {};
    if (!s_out[slot].known ||
        !servoNudgePlan(s_out[slot].commandedUs, SERVO_BAND_STD, SERVO_NUDGE_AMPLITUDE_US, &plan)) {
        PA_LOG_WARN(TAG, "[%s] %s not nudged - %s", commandSourceToString(source),
                    servoOutputSlotName(slot),
                    s_out[slot].known ? "it sits outside the cautious band" : "no pulse on it yet");
        s_out[slot].nudgesDone++;
        publishCommanded(slot);
        return;
    }
    // Whatever the Output was doing is over, an out-and-back in progress included:
    // this one starts from the width on the pin now.
    endMove(slot);
    for (uint8_t leg = 1; leg <= kLegCount; ++leg) {
        s_out[slot].legTargetUs[leg - 1] = servoNudgeLegTarget(plan, leg);
    }
    s_out[slot].legIsNudge = true;
    PA_LOG_INFO(TAG, "[%s] %s nudged %u/%u us about %u us", commandSourceToString(source),
                servoOutputSlotName(slot), (unsigned)plan.hiUs, (unsigned)plan.loUs,
                (unsigned)plan.homeUs);
    beginLeg(slot, 1, millis());
}

// -----------------------------------------------------------------------------
// beginTravel()
// A SERVO_CMD_TRAVEL that got past processCommand()'s gates (ADR 0063, #352):
// the Part on this Output runs out to its recorded open end, across to its
// recorded close end, and back to where it was, as the one command a deliberate
// press on a body view sends.
//
// Refused, with nothing moved, on an Output that has no travel to run:
//
//   no pulse on it -- there is no width to come back to, and a released Output's
//   first move is a jump (#364), which is not a thing to start a three-leg run
//   with;
//   no live row addressed here -- an Output the table does not describe has no
//   recorded ends at all, and getOpenClosePositions()'s band-ends fallback is
//   deliberately NOT used: it is the right answer for "open this door" and the
//   wrong one for "show me this door's travel", which would then be a travel
//   over numbers nobody recorded;
//   `calibrated` unset -- the ends are whatever the row was stored with rather
//   than anything measured against this linkage. Same refusal the calibration
//   dial's test sweep makes for the same reason (data/parts.js), and the same
//   shape ADR 0052's overshoot easing degrades by.
//
// Nothing is counted here. `nudgesDone` is a discovery run's clock and a travel
// is nobody's -- see endLeg().
// -----------------------------------------------------------------------------
static void beginTravel(uint8_t slot, CommandSource source) {
    // One output per travel, always: a press on a body view is about one Part,
    // and the ARM1+ARM2 broadcast would run two parts through their travel on
    // one press. Refused at the API door too, where the caller hears why.
    if (slot >= kSlotCount) {
        PA_LOG_WARN(TAG, "[%s] Travel rejected - takes one output, not slot %d",
                    commandSourceToString(source), slot);
        return;
    }
    if (!s_out[slot].known) {
        PA_LOG_WARN(TAG, "[%s] %s not travelled - no pulse on it yet",
                    commandSourceToString(source), servoOutputSlotName(slot));
        return;
    }
    const ServoOutputAddress output = servoOutputSlotAddress(slot);
    uint16_t openUs = 0;
    uint16_t closeUs = 0;
    ServoMotionProfile profile = {};
    if (!configCacheReadServoOutputEndpoints(output.driver, output.channel, &openUs, &closeUs) ||
        !configCacheReadServoOutputMotionProfile(output.driver, output.channel, &profile)) {
        PA_LOG_WARN(TAG, "[%s] %s not travelled - no output row records its ends",
                    commandSourceToString(source), servoOutputSlotName(slot));
        return;
    }
    ServoTravelPlan plan = {};
    if (!profile.calibrated ||
        !servoTravelPlan(s_out[slot].commandedUs, openUs, closeUs, &plan)) {
        PA_LOG_WARN(TAG, "[%s] %s not travelled - %s", commandSourceToString(source),
                    servoOutputSlotName(slot),
                    profile.calibrated ? "its two ends are the same width"
                                       : "nobody has measured its ends");
        return;
    }
    // Whatever the Output was doing is over, an out-and-back in progress included:
    // this one comes back to the width on the pin now.
    endMove(slot);
    for (uint8_t leg = 1; leg <= kLegCount; ++leg) {
        s_out[slot].legTargetUs[leg - 1] = servoTravelLegTarget(plan, leg);
    }
    s_out[slot].legIsNudge = false;
    PA_LOG_INFO(TAG, "[%s] %s travelling open %u us, close %u us, back to %u us",
                commandSourceToString(source), servoOutputSlotName(slot), (unsigned)plan.openUs,
                (unsigned)plan.closeUs, (unsigned)plan.homeUs);
    beginLeg(slot, 1, millis());
}

// -----------------------------------------------------------------------------
// stepMove()
// One frame of whatever ramp the Output is on: put the width the ramp says on the
// pin, and answer whether the whole move is over.
//
// An overshoot is over when it has SETTLED, not when it reaches its aim. The
// aim was decided when the target was set (servoMotionPlan()); arriving there
// starts the way back to the target, planned from the Output's profile as it
// stands now, and the Output stays `moving` through both halves -- so a new
// command, a hold, stopAllMoves() or a release ends an overshoot exactly where
// it has got to, the same way it ends any ramp.
// -----------------------------------------------------------------------------
static bool stepMove(uint8_t slot, uint32_t nowMs) {
    writeOutputPulse(slot, servoMotionPositionAt(s_out[slot].ramp, nowMs));
    if (!servoMotionArrived(s_out[slot].ramp, nowMs)) {
        return false;
    }
    if (!servoMotionSettles(s_out[slot].ramp)) {
        return true;
    }
    // A row that has gone from under the move leaves `calibrated` unset, and
    // the planner answers that with a snap onto the target.
    const ServoOutputAddress output = servoOutputSlotAddress(slot);
    ServoMotionProfile profile = {};
    configCacheReadServoOutputMotionProfile(output.driver, output.channel, &profile);
    s_out[slot].ramp = servoMotionSettleBack(s_out[slot].ramp, profile, nowMs);
    if (s_out[slot].ramp.durationMs == 0) {
        writeOutputPulse(slot, s_out[slot].ramp.toUs);
        return true;
    }
    return false;
}

// One frame of an out-and-back in progress: wait out a dwell, then start the
// next leg; otherwise advance the leg's ramp like any other move and notice its
// arrival.
static void advanceLegs(uint8_t slot, uint32_t nowMs) {
    if (s_out[slot].legDwelling) {
        if ((int32_t)(nowMs - s_out[slot].legDwellEndMs) < 0) {
            return;
        }
        beginLeg(slot, (uint8_t)(s_out[slot].legNo + 1), nowMs);
        return;
    }
    if (stepMove(slot, nowMs)) {
        legArrived(slot, nowMs);
    }
}

// -----------------------------------------------------------------------------
// updateMotion()
// Advance every move in progress by one frame.
// -----------------------------------------------------------------------------
static void updateMotion() {
    const uint32_t now = millis();
    for (uint8_t slot = 0; slot < kSlotCount; ++slot) {
        if (!s_out[slot].moving) {
            continue;
        }
        if (s_out[slot].legNo != 0) {
            advanceLegs(slot, now);
            continue;
        }
        if (stepMove(slot, now)) {
            // The frame stepMove() wrote was published with the move still in
            // progress; this is where it ends, and a reader waiting on
            // `moving` to fall has to hear it. It is also where it arrives - an
            // overshoot only once it has settled back - so its release counts
            // from here.
            endMove(slot);
            armReleaseOnArrival(slot, now);
            publishCommanded(slot);
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
    for (uint8_t slot = 0; slot < kSlotCount; ++slot) {
        if (!s_out[slot].moving) {
            continue;
        }
        endMove(slot);
        stopped = true;
        // The move is over where it got to, so its target is too: no surface
        // may go on showing a destination the output will never reach. An
        // out-and-back ends the same way, its return never made; a nudge is
        // counted as ended and a travel, which nobody is waiting on, is not.
        publishCommanded(slot);
    }
    if (stopped) {
        PA_LOG_INFO(TAG, "Moves stopped where they were - %s", reason);
    }
}

// -----------------------------------------------------------------------------
// getOpenClosePositions()
// The Endpoint Pair of the Servo Output row at this slot's address (ADR 0041).
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
static void getOpenClosePositions(uint8_t slot, uint16_t& openUs, uint16_t& closeUs) {
    const ServoOutputAddress output = servoOutputSlotAddress(slot);
    if (configCacheReadServoOutputEndpoints(output.driver, output.channel, &openUs, &closeUs)) {
        return;
    }
    openUs = SERVO_BAND_STD.hi;
    closeUs = SERVO_BAND_STD.lo;
}

// -----------------------------------------------------------------------------
// releaseOutput()
// Take the pulse off one output: pulses off (ADR 0043, ADR 0064, #364).
//
// The one place a pulse comes off a pin, through the Output's backend
// (include/servo_backend.h), whoever asks -- the builder's pulses
// off, one of the dial's two bounds, the halt edge, or the Output's own release
// time running out after a move arrived (#443) -- so a released output
// means one thing everywhere on the droid: nothing is driven, the servo goes
// limp where it is, and its resting position is whatever gravity and friction
// decide. Nothing is commanded first: driving to a known position before
// letting go is exactly the option ADR 0064 refused, since it moves a part
// while by definition nobody is watching it.
//
// Any move, out-and-back or hold on the output ends here too. A nudge cut short
// is counted as ended (endLeg), so a discovery run waiting on it steps on. The
// output stops being `known`: the next command to it starts from a position
// nobody can vouch for, so it snaps, exactly as the first move after boot does.
// -----------------------------------------------------------------------------
static void releaseOutput(uint8_t slot, ServoLimpReason reason) {
    if (slot >= kSlotCount || !isOutputLive(slot)) {
        return;
    }
    endMove(slot);
    servoHoldEnd(&s_out[slot].hold);
    // A run's Output is free again: nothing drives it until a run takes it once
    // more. Its channel stays attached with no pulse, which is what the gates
    // above read rather than the channel.
    if (s_runSlot == slot) {
        s_runSlot = SERVO_RUN_NONE;
    }
    servoBackendRelease(servoOutputSlotAddress(slot));
    s_out[slot].known = false;
    s_out[slot].limp = reason;
    publishCommanded(slot);
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
// routine was closing, and the stop has to reach it too - and so does a free
// Output a Find by Moving run is driving (releaseOutput() asks isOutputLive()).
// -----------------------------------------------------------------------------
static void releaseAllOutputs(ServoLimpReason reason) {
    for (uint8_t slot = 0; slot < kSlotCount; ++slot) {
        releaseOutput(slot, reason);
    }
    PA_LOG_INFO(TAG, "Every output released - %s",
                reason == SERVO_LIMP_ESTOP ? "estop" : "sleep mode");
}

// -----------------------------------------------------------------------------
// noticeAnUnreachableExpander()
// The expander stopped answering (ADR 0043: "a bus drop is reported, not
// escalated"). Its sender marks it the moment a write fails, on Core 0
// (src/drivers/pca9685.cpp); this is where the Outputs on it learn, within a
// frame. Each ends whatever it was doing - a move, the dial's hold, a run's
// hold - and goes to no pulse with the reason every surface shows
// (SERVO_LIMP_UNREACHABLE), and from then on nothing drives it: its bit in
// s_unreachable_mask takes it out of isOutputEnabled() and backendReady().
//
// Nothing is written to it from here - the bus is what failed, and the
// sender has already made the one last all-off attempt a release could
// (src/drivers/pca9685.cpp) - and nothing here touches drive, the estop or the
// failsafe gate, or latches anything (ADR 0032). It does not log: the sender
// said which board stopped answering and which Outputs that leaves, and a log
// line here would put a second formatting route on this task's measured chain
// (ADR 0040) to say it again.
//
// Once a session: a board that dropped off the bus is not trusted to be in the
// state this task thinks it is in, so the Outputs stay unreachable until the
// droid restarts with it answering.
// -----------------------------------------------------------------------------
static void noticeAnUnreachableExpander() {
    if (!s_expander_chosen || (s_unreachable_mask & kExpanderSlotMask) != 0 ||
        pca9685Answering()) {
        return;
    }
    s_unreachable_mask |= kExpanderSlotMask;
    for (uint8_t slot = 0; slot < kSlotCount; ++slot) {
        if ((kExpanderSlotMask & (1u << slot)) == 0) {
            continue;
        }
        endMove(slot);
        servoHoldEnd(&s_out[slot].hold);
        if (s_runSlot == slot) {
            s_runSlot = SERVO_RUN_NONE;
        }
        s_out[slot].known = false;
        s_out[slot].limp = SERVO_LIMP_UNREACHABLE;
        publishCommanded(slot);
    }
}

// -----------------------------------------------------------------------------
// holdOutput()
// The calibration dial's hold (ADR 0064, #364): drive one output to a width and
// keep the pulse on it until the builder lets go or a bound fires.
//
// A press takes the output and starts both bounds; the keepalive and the dial's
// own moves only refresh a hold that stands (include/servo_hold.h), so the page
// that sends one a second keeps the hold alive, can never push it past the
// ceiling, and can never take back an output a bound, the estop or pulses off
// let go (#417). A dropped refresh drives nothing. A hold at the width the
// output is already going to -- the page's keepalive -- is a refresh and no
// move at all: replanning a ramp part way through, once a second, would
// restart the move the builder is watching.
//
// The drive itself is driveOutputTo()'s, like every other command: through the
// component clamp (ADR 0041), at the Output's own pace (ADR 0052), and a snap
// where the profile cannot plan one.
//
// ADR 0064 also says Output Release is SUPPRESSED while a dial holds an output
// (#443). A hold that stands cancels a release the last arrival left pending
// here - a press on an output at rest, or the keepalive, which moves nothing
// and so never reaches endMove() - and armReleaseOnArrival() arms nothing while
// `hold.held` is set, so a move the dial makes arrives owing nothing. The
// dial's two bounds (expireHolds()) are what end the hold instead.
// -----------------------------------------------------------------------------
static void holdOutput(uint8_t slot, uint16_t positionUs, CommandSource source, ServoHoldAsk ask) {
    if (slot >= kSlotCount) {
        PA_LOG_WARN(TAG, "[%s] Hold rejected - takes one output, not slot %d",
                    commandSourceToString(source), slot);
        return;
    }
    const ServoHoldOutcome outcome = servoHoldCommand(&s_out[slot].hold, millis(), ask);
    if (outcome == SERVO_HOLD_DROPPED) {
        PA_LOG_DEBUG(TAG, "[%s] %s hold refresh dropped - the dial no longer holds it",
                     commandSourceToString(source), servoOutputSlotName(slot));
        return;
    }
    servoReleaseCancel(&s_out[slot].release);
    const bool taken = outcome == SERVO_HOLD_TAKEN;
    if (taken) {
        PA_LOG_INFO(TAG, "[%s] %s held by the dial at %u us", commandSourceToString(source),
                    servoOutputSlotName(slot), (unsigned)positionUs);
    }
    // Where the move ends, which on an overshoot is where it settles, not its
    // aim: a keepalive for the width the builder asked for is still a refresh.
    const uint16_t goingToUs =
        s_out[slot].moving ? s_out[slot].ramp.settleUs : s_out[slot].commandedUs;
    if (s_out[slot].known && goingToUs == positionUs) {
        // Nothing to drive; the mirror still has to learn the hold was taken.
        if (taken) {
            publishCommanded(slot);
        }
        return;
    }
    driveOutputTo(slot, positionUs);
}

// -----------------------------------------------------------------------------
// expireHolds()
// One frame of the dial's two bounds (ADR 0064): an output whose hold commands
// have stopped arriving, or that has been held for the most a dial may, is
// released here and the reason recorded, so the surface can say which one it
// was and offer to take the output again. A Find by Moving run's hold on a
// free Output is bounded here by the same two numbers, its nudges being its
// arrivals, and goes back to limp-since-start (servoRunLimpReason(), #411).
// -----------------------------------------------------------------------------
static void expireHolds() {
    const uint32_t now = millis();
    for (uint8_t slot = 0; slot < kSlotCount; ++slot) {
        const ServoHoldBound bound = servoHoldBoundHit(s_out[slot].hold, now, SERVO_HOLD_EXPIRY_MS,
                                                       SERVO_HOLD_CEILING_MS);
        if (bound == SERVO_HOLD_BOUND_NONE) {
            continue;
        }
        const bool ceiling = bound == SERVO_HOLD_BOUND_CEILING;
        const bool run = s_runSlot == slot;
        releaseOutput(slot, servoRunLimpReason(run, bound));
        if (run) {
            PA_LOG_INFO(TAG, "%s let go - %s", servoOutputSlotName(slot),
                        ceiling ? "held for the most a run may" : "the run's nudges moved on");
        } else {
            PA_LOG_WARN(TAG, "%s released - %s", servoOutputSlotName(slot),
                        ceiling ? "held for the most a dial may" : "the dial's commands stopped arriving");
        }
    }
}

// -----------------------------------------------------------------------------
// letGoOnceHeldAfterArriving()
// One frame of every Output Release pending (ADR 0043, #443): an output whose
// release time has run out since its move arrived is let go here, through
// releaseOutput() like every other release, and says why (SERVO_LIMP_OUTPUT_RELEASE).
// Cancelled first, so an output releaseOutput() turns away cannot come due again
// on every frame after. Allocates nothing, blocks on nothing: the release time
// was read once, at the arrival.
// -----------------------------------------------------------------------------
static void letGoOnceHeldAfterArriving() {
    const uint32_t now = millis();
    for (uint8_t slot = 0; slot < kSlotCount; ++slot) {
        if (!servoReleaseDue(s_out[slot].release, now)) {
            continue;
        }
        servoReleaseCancel(&s_out[slot].release);
        releaseOutput(slot, SERVO_LIMP_OUTPUT_RELEASE);
        sayTheOutputLetGo(slot, "held for its release time after it arrived");
    }
}

// -----------------------------------------------------------------------------
// letGoIfTheRunsOutputIsNoLongerFree()
// A config commit can put a Part or a Light Type on the Output a run holds
// between two of its nudges - the Part's own picker, "That one", another tab.
// That Output is not free any more: a Part's Output moves through its Part,
// and a light's wire may have a strip on the pin. So every frame a run holds
// an Output, the same question the take asked (servoRunMayTake()) is asked
// again, and the run lets go of it within a frame of the commit rather than at
// the expiry. Two cache reads under configCacheMux, a critical section, and
// only while a run holds something: no heap and no blocking on Core 1.
// -----------------------------------------------------------------------------
static void letGoIfTheRunsOutputIsNoLongerFree() {
    const uint8_t slot = s_runSlot;
    if (slot == SERVO_RUN_NONE || mayTakeForRun(slot)) {
        return;
    }
    releaseOutput(slot, SERVO_LIMP_OFF);
    sayTheOutputLetGo(slot, "a Part or a light is on it now");
}

// What a log line calls a command's target: `both arms` for the broadcast,
// which has no slot, and otherwise the Output's own name (servoOutputSlotName()).
static const char* targetName(ServoOutputAddress output, uint8_t slot) {
    return output == SERVO_OUTPUT_BOTH_ARMS ? "both arms" : servoOutputSlotName(slot);
}

// -----------------------------------------------------------------------------
// processCommand()
// Process incoming servo command.
// The command names its Output by address; this is where the address becomes
// the slot the rest of the task works in (servoOutputSlotOf()), and `both`
// the board's first two Outputs' slots (bothArmsSlot()). Every command is gated
// per isTargetEnabled() (ADR 0027).
//
// always_inline, deliberately: servoTask() is its only caller, and ServoTask's
// measured chain (ADR 0040) is walked through the two as one frame. Left to
// GCC, the ESP32 build folded it in only while it stayed under the inliner's
// size limit, and the ESP32-P4 build never did; out of line its 352 B frame
// stacks under servoTask()'s and the walked chain grows by that much (#411
// slice 4 measured +352 B from a few lines added below). Forced, both chips
// walk the same shape whatever this function grows by.
// -----------------------------------------------------------------------------
static inline __attribute__((always_inline)) void processCommand(const ServoCommand& cmd) {
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

    // SERVO_OUTPUT_SLOT_NONE for `both` and for an address no member drives:
    // `both` is taken apart below, and anything else is refused by the gate.
    const uint8_t slot = servoOutputSlotOf(cmd.output);

    // Feature toggle: reject commands for disabled or AUX-LED-reserved
    // Outputs. Two exceptions, both a Find by Moving run's on a free Output
    // (#411): a nudge may take one, and a release lets go of one a run holds.
    // Nothing else reaches an Output this task does not drive.
    // A nudge on the Output a run holds always reaches beginNudge(), which asks
    // again whether it is still free and lets go of it if not.
    const bool runNudge = cmd.type == SERVO_CMD_NUDGE && slot < kSlotCount &&
                          (s_runSlot == slot || mayTakeForRun(slot));
    const bool runRelease = cmd.type == SERVO_CMD_RELEASE && slot < kSlotCount &&
                            s_runSlot == slot;
    if (!isTargetEnabled(cmd.output) && !runNudge && !runRelease) {
        PA_LOG_DEBUG(TAG, "[%s] Command rejected - %s disabled or reserved",
                     commandSourceToString(cmd.source), targetName(cmd.output, slot));
        return;
    }
    const bool both = cmd.output == SERVO_OUTPUT_BOTH_ARMS;

    uint16_t openUs, closeUs;

    switch (cmd.type) {
        case SERVO_CMD_OPEN:
            if (both) {
                getOpenClosePositions(bothArmsSlot(0), openUs, closeUs);
                driveOutputTo(bothArmsSlot(0), openUs);
                getOpenClosePositions(bothArmsSlot(1), openUs, closeUs);
                driveOutputTo(bothArmsSlot(1), openUs);
                PA_LOG_INFO(TAG, "[%s] Both arms opened", commandSourceToString(cmd.source));
            } else {
                getOpenClosePositions(slot, openUs, closeUs);
                driveOutputTo(slot, openUs);
                PA_LOG_INFO(TAG, "[%s] %s opened", commandSourceToString(cmd.source),
                            servoOutputSlotName(slot));
            }
            break;

        case SERVO_CMD_CLOSE:
            if (both) {
                getOpenClosePositions(bothArmsSlot(0), openUs, closeUs);
                driveOutputTo(bothArmsSlot(0), closeUs);
                getOpenClosePositions(bothArmsSlot(1), openUs, closeUs);
                driveOutputTo(bothArmsSlot(1), closeUs);
                PA_LOG_INFO(TAG, "[%s] Both arms closed", commandSourceToString(cmd.source));
            } else {
                getOpenClosePositions(slot, openUs, closeUs);
                driveOutputTo(slot, closeUs);
                PA_LOG_INFO(TAG, "[%s] %s closed", commandSourceToString(cmd.source),
                            servoOutputSlotName(slot));
            }
            break;

        case SERVO_CMD_POSITION:
            // Validate pulse width before setting
            if (cmd.positionUs < SERVO_PULSE_MIN_US || cmd.positionUs > SERVO_PULSE_MAX_US) {
                PA_LOG_WARN(TAG, "[%s] Invalid position %d us - rejected",
                            commandSourceToString(cmd.source), cmd.positionUs);
                return;
            }
            if (both) {
                driveOutputTo(bothArmsSlot(0), cmd.positionUs, cmd.motionThrowMs, cmd.motionEasing);
                driveOutputTo(bothArmsSlot(1), cmd.positionUs, cmd.motionThrowMs, cmd.motionEasing);
            } else {
                driveOutputTo(slot, cmd.positionUs, cmd.motionThrowMs, cmd.motionEasing);
            }
            PA_LOG_INFO(TAG, "[%s] %s set to %d us", commandSourceToString(cmd.source),
                        targetName(cmd.output, slot), cmd.positionUs);
            break;

        case SERVO_CMD_PUPPET:
            // A hold outranks a puppet string, and this is the one place that
            // says so: the dial's hold and a run's are the same ServoHoldState
            // (include/servo_run.h), so one test covers both. A run only ever
            // holds a free Output, which no Part - so no string - is on; the
            // dial is the case this is for. Dropped quietly, like every target
            // here: a string sends at stick rate, and a line per target would
            // bury the log.
            if (slot >= kSlotCount || s_out[slot].hold.held ||
                cmd.positionUs < SERVO_PULSE_MIN_US || cmd.positionUs > SERVO_PULSE_MAX_US) {
                return;
            }
            driveOutputTo(slot, cmd.positionUs);
            break;

        case SERVO_CMD_NUDGE:
            // No width to validate: the command carries none, and the pair is
            // computed from the pin (include/servo_nudge.h).
            beginNudge(slot, cmd.source);
            break;

        case SERVO_CMD_TRAVEL:
            // No width to validate either: the ends come off the Output's row
            // (include/servo_travel.h), so a body view's press cannot name one.
            beginTravel(slot, cmd.source);
            break;

        case SERVO_CMD_HOLD:
        case SERVO_CMD_HOLD_REFRESH:
            // The same width rule as a position; the hold is what differs.
            if (cmd.positionUs < SERVO_PULSE_MIN_US || cmd.positionUs > SERVO_PULSE_MAX_US) {
                PA_LOG_WARN(TAG, "[%s] Invalid hold position %d us - rejected",
                            commandSourceToString(cmd.source), cmd.positionUs);
                return;
            }
            holdOutput(slot, cmd.positionUs, cmd.source,
                       cmd.type == SERVO_CMD_HOLD ? SERVO_HOLD_ASK_TAKE : SERVO_HOLD_ASK_REFRESH);
            break;

        case SERVO_CMD_RELEASE:
            if (both) {
                releaseOutput(bothArmsSlot(0), SERVO_LIMP_RELEASED);
                releaseOutput(bothArmsSlot(1), SERVO_LIMP_RELEASED);
                PA_LOG_INFO(TAG, "[%s] Both arms released - pulses off", commandSourceToString(cmd.source));
            } else {
                releaseOutput(slot, SERVO_LIMP_RELEASED);
                PA_LOG_INFO(TAG, "[%s] %s released - pulses off", commandSourceToString(cmd.source),
                            servoOutputSlotName(slot));
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
    // Which body servo controller this boot runs, read once like a toggle
    // (ADR 0027): setup() latched it, and brought the expander up first when
    // it is the one (pca9685Begin()), so whether it answered is known here.
    s_expander_chosen = servoBackendMemberIsPca9685(
        componentPartByValue(configCacheReadActiveBodyServoMember()));

    // Capture toggles snapshot once at startup.
    ConfigSnapshot cfg = {};
    configCacheRead(&cfg);
    s_wired_at_start_mask = wiredAtStartMask(cfg.system);
    s_dome_enabled = cfg.system.enable_dome_esc;
    s_lit_mask = litOutputMask(cfg.system);

    const bool anyServo = s_wired_at_start_mask != 0;

    // An expander chosen and not answering: every one of its Outputs says so
    // from the first read, whatever LEDC does below. pca9685Begin() logged the
    // board, its span and the consequence. Every Output the expander answers
    // for starts limp, which is what bring-up left on the chip and what a
    // zero-filled mirror already reads.
    if (s_expander_chosen && !pca9685Answering()) {
        s_unreachable_mask = kExpanderSlotMask;
        for (uint8_t slot = 0; slot < kSlotCount; ++slot) {
            if ((kExpanderSlotMask & (1u << slot)) != 0) {
                s_out[slot].known = false;
                s_out[slot].limp = SERVO_LIMP_UNREACHABLE;
                publishCommanded(slot);
            }
        }
    }

    // LEDC comes up whatever is wired: its timer is what a Find by Moving run
    // attaches a free Output's channel to (ledcPwmAttach(), #411), and a droid
    // with no Part on any Output is exactly the one a run is for. A mask of 0
    // configures the timer and no channel.
    {
        // Build enabled-channels mask using the helper from servo_helpers.h.
        uint8_t ledcMask =
            servo_enabled_ledc_mask(s_wired_at_start_mask, s_lit_mask, s_dome_enabled);

        // Every servo output comes up with no pulse on it (ledcPwmInit()): limp
        // where it was left, which is what an Output's boot behaviour defaults
        // to (ADR 0052). One whose boot behaviour sends it home is driven there
        // by the boot pass the Sequence Coordinator runs, paced by the Cadence
        // Floor (include/sequence_bulk_centre.h) - never by a pulse put on every
        // output at once here. So no output is `known` yet, the first move to
        // each is a jump from wherever it stands, and a surface reads it as
        // limp since boot (SERVO_LIMP_OFF).
        if (!ledcPwmInit(ledcMask)) {
            PA_LOG_ERROR(TAG, "LEDC init failed");
            return;
        }
        s_ledc_ready = true;
        for (uint8_t slot = 0; slot < kSlotCount; ++slot) {
            if (backendReady(slot) && isOutputEnabled(slot)) {
                s_out[slot].known = false;
                s_out[slot].limp = SERVO_LIMP_OFF;
                publishCommanded(slot);
            }
        }

        for (uint8_t slot = 0; slot < kSlotCount; ++slot) {
            if ((s_lit_mask & (1u << slot)) == 0) {
                continue;
            }
            // A light only goes on the board's own wires, which LEDC drives.
            const uint8_t channel = servoOutputSlotAddress(slot).channel;
            const BoardOutput* output = boardOutputOnChannel(channel);
            PA_LOG_INFO(TAG, "%s carries a light (GPIO %u) - LEDC skipped for that header",
                        output != nullptr ? boardOutputLabel(*output) : "an output",
                        (unsigned)getChannelGpio(channel));
        }
    }

    if (anyServo) {
        PA_LOG_INFO(TAG, "Servo outputs ready (%s, limp until moved)",
                    s_expander_chosen ? "the board's and the PCA9685's" : "the board's");
    } else {
        PA_LOG_INFO(TAG, "No servo output has a Part on it");
    }
}

// -----------------------------------------------------------------------------
// servoTaskWiredAtStart() / servoTaskDrivesOutput()
// The boot snapshot above, read by the servo route and the Output rows
// (include/servo_task.h has the contract and why no lock is taken).
// -----------------------------------------------------------------------------
// Each asks by address, and an address with no slot - `both`, or one no member
// of this image drives - answers false.
bool servoTaskWiredAtStart(ServoOutputAddress output) {
    return wiredAtStart(servoOutputSlotOf(output));
}

bool servoTaskDrivesOutput(ServoOutputAddress output) {
    const uint8_t slot = servoOutputSlotOf(output);
    return slot < kSlotCount && backendReady(slot) && isOutputEnabled(slot);
}

ServoExpanderFacts servoTaskExpanderFacts() {
    ServoExpanderFacts facts = {};
    facts.chosen = s_expander_chosen;
    facts.address = pca9685Address();
    facts.answering = s_expander_chosen && (s_unreachable_mask & kExpanderSlotMask) == 0;
    return facts;
}

// Read from Core 0 (the servo route, the Console): the boot snapshot is
// written once before either starts, and the cache reads take its own lock.
bool servoTaskMayTakeForRun(ServoOutputAddress output) {
    return mayTakeForRun(servoOutputSlotOf(output));
}

bool servoTaskRunHolds(ServoOutputAddress output) {
    const uint8_t slot = servoOutputSlotOf(output);
    return slot < kSlotCount && s_runSlot == slot;
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

    // No idle branch for a droid with nothing wired: a Find by Moving run
    // takes free Outputs through this loop (#411), and with nothing wired every
    // Output is free. A frame with no command and no move costs the queue poll
    // and three empty loops.

    ServoCommand cmd;
    bool hwmLogged = false;
    ServoHaltFlags prevHalt = {false, false};

    while (true) {
        if (!hwmLogged) {
            PA_LOG_DEBUG("ServoTask", "stack HWM: %u bytes free",
                         (unsigned)uxTaskGetStackHighWaterMark(NULL));
            hwmLogged = true;
        }

        // Entering estop or Sleep Mode stops every move where it is, before a
        // command or a frame can carry one further, and then releases every
        // enabled output (ADR 0043): nothing is driven, so nothing can grind
        // and nothing can brown out. Each halt on its own edge
        // (include/servo_halt.h): a direct command is still accepted in Sleep
        // Mode and must be able to move, so an estop entered while asleep has
        // to let go of what it moved. Both flags are read in one critical
        // section so the reason recorded is the one that fired.
        ServoHaltFlags haltNow = {};
        taskENTER_CRITICAL(&robotStateMux);
        haltNow.estop = robotState.estop;
        haltNow.sleep = robotState.sleepMode;
        taskEXIT_CRITICAL(&robotStateMux);
        const ServoHaltEdge haltEdge = servoHaltEdge(prevHalt, haltNow);
        if (haltEdge.release) {
            stopAllMoves("estop or sleep mode entered");
            releaseAllOutputs(haltEdge.reason);
            // Straight away, not at the frame's end: an expander's releases
            // leave this core here, as one ALL_LED_OFF_H (include/pca9685.h).
            servoBackendCommit();
        }
        prevHalt = haltNow;

        // An expander that stopped answering since the last frame: its
        // Outputs go unreachable before a command or a frame can be sent to
        // them.
        noticeAnUnreachableExpander();

        // Process any pending commands (non-blocking)
        while (xQueueReceive(servoCmdQueue, &cmd, 0) == pdTRUE) {
            processCommand(cmd);
        }

        // Advance every move in progress by one frame
        updateMotion();

        // Then the dial's two bounds, after this frame's commands have had
        // their say: a hold refreshed this frame is not an expired one.
        expireHolds();

        // Then every Output Release that has come due, after this frame's
        // commands and arrivals: a command this frame has cancelled its
        // output's, and an arrival this frame has only just started counting.
        letGoOnceHeldAfterArriving();

        // And a run's Output that a commit took from it since the last frame.
        letGoIfTheRunsOutputIsNoLongerFree();

        // Hand this frame's writes to a member that sends them off this core
        // (include/servo_backend.h), all of them at once.
        servoBackendCommit();

        // Feed watchdog
        esp_task_wdt_reset();

        // 50Hz update rate
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}
