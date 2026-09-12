// =============================================================================
// src/tasks/servo_task.cpp
//
// ServoTask  --  LEDC PWM control for utility arm servos and spare servo outputs.
// Handles open, close and position commands, from every source, for:
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
#include "servo_motion_ramp.h"  // a move planned in time from the Output's profile (ADR 0052)
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
// `seqMoved` marks an output a sequence was the last thing to command, which is
// what the park below acts on. Any other source commanding the output clears it.
// -----------------------------------------------------------------------------
static constexpr uint8_t kArmCount = 5;  // ARM1, ARM2, AUX1-3

static struct {
    uint16_t commandedUs;
    bool known;
    bool moving;
    bool seqMoved;
    ServoMotionRamp ramp;
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
// writeArmPulse()
// Put one width on the pin and say so. The width has already been through
// resolveArmPulse(); this is the write and nothing else.
//
// What is written is what robotState then reports, because the target a status
// reader sees has to be the pulse the pin is actually holding -- part way
// through a ramp too.
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
    taskENTER_CRITICAL(&robotStateMux);
    if (armId == 0) {
        robotState.arm1TargetUs = pulseUs;
    } else if (armId == 1) {
        robotState.arm2TargetUs = pulseUs;
    }
    taskEXIT_CRITICAL(&robotStateMux);
}

// -----------------------------------------------------------------------------
// setArmPosition()
// Snap a single arm to a pulse width: the straight-through write, with any move
// in progress on that arm abandoned first.
// armId: 0=ARM1, 1=ARM2, 2=AUX1, 3=AUX2, 4=AUX3
//
// A command that should move at the Output's own pace goes through driveArmTo()
// instead. This one exists for the paths that must not ease: the park below,
// and a move the profile cannot plan.
// -----------------------------------------------------------------------------
static void setArmPosition(uint8_t armId, uint16_t pulseUs) {
    uint8_t channel = LEDC_CH_MAX;
    uint16_t commandedUs = 0;
    if (!resolveArmPulse(armId, pulseUs, &channel, &commandedUs)) {
        return;
    }
    s_arm[armId].moving = false;
    writeArmPulse(armId, channel, commandedUs);
}

// -----------------------------------------------------------------------------
// readMotionProfile()
// The part of the Output row behind this channel that a move needs: how far a
// full throw is, the two profile times, and whether anybody measured the ends.
// False when no live row is addressed there.
//
// This is the one read in ServoTask that holds a whole ServoOutputRow. The cache
// answers the clamp and the Endpoint Pair by address, as values, but has no
// such accessor for the Motion Profile, and that accessor would live in the
// config store this ticket may not touch (#354's fence). So the row is read
// through configCacheReadServoOutput() -- the existing door -- once per command
// rather than once per frame, in a frame of its own so the 70 B is gone again
// before the move is planned.
// -----------------------------------------------------------------------------
static bool readMotionProfile(uint8_t channel, uint16_t* spanUs, uint16_t* throwMs,
                              uint16_t* accelMs, bool* calibrated) __attribute__((noinline));
static bool readMotionProfile(uint8_t channel, uint16_t* spanUs, uint16_t* throwMs,
                              uint16_t* accelMs, bool* calibrated) {
    const uint8_t count = configCacheServoOutputCount();
    ServoOutputRow row = {};
    for (uint8_t i = 0; i < count; ++i) {
        if (!configCacheReadServoOutput(i, &row) || row.driver != SERVO_DRIVER_LEDC ||
            row.channel != channel) {
            continue;
        }
        *spanUs = (uint16_t)(servoOutputHighUs(row) - servoOutputLowUs(row));
        *throwMs = row.throw_ms;
        *accelMs = row.accel_ms;
        *calibrated = row.calibrated;
        return true;
    }
    return false;
}

// -----------------------------------------------------------------------------
// driveArmTo()
// Send an arm to a pulse width at the pace its Output's Motion Profile sets.
//
// The time comes from the row and from nowhere else: a ServoCommand carries no
// duration, so a Body Step, an RC toggle and a browser move cannot disagree
// about how long a door takes (ADR 0049, ADR 0052). Where the profile cannot
// plan a move -- no row, an unmeasured output, no known starting point -- the
// arm snaps, which is exactly what every move did before the profile existed.
// -----------------------------------------------------------------------------
static void driveArmTo(uint8_t armId, uint16_t pulseUs) {
    uint8_t channel = LEDC_CH_MAX;
    uint16_t targetUs = 0;
    if (!resolveArmPulse(armId, pulseUs, &channel, &targetUs)) {
        return;
    }

    uint16_t spanUs = 0;
    uint16_t throwMs = 0;
    uint16_t accelMs = 0;
    bool calibrated = false;
    if (!s_arm[armId].known ||
        !readMotionProfile(channel, &spanUs, &throwMs, &accelMs, &calibrated)) {
        s_arm[armId].moving = false;
        writeArmPulse(armId, channel, targetUs);
        return;
    }

    const ServoMotionRamp ramp = servoMotionPlan(s_arm[armId].commandedUs, targetUs, spanUs,
                                                 throwMs, accelMs, calibrated, millis());
    if (ramp.durationMs == 0) {
        s_arm[armId].moving = false;
        writeArmPulse(armId, channel, targetUs);
        return;
    }
    s_arm[armId].ramp = ramp;
    s_arm[armId].moving = true;
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
        writeArmPulse(armId, channel, servoMotionPositionAt(s_arm[armId].ramp, now));
        if (servoMotionArrived(s_arm[armId].ramp, now)) {
            s_arm[armId].moving = false;
        }
    }
}

// -----------------------------------------------------------------------------
// stopAllMoves()
// End every move in progress where it has got to, commanding nothing further.
//
// Estop and Sleep Mode land here the moment either is entered. A ramp that kept
// running would be the droid still moving after it was told to stop, so the pin
// keeps the last width a frame wrote and nothing eases on. Commanding no new
// position is the half of ADR 0043 this can already honour; releasing the
// output is the other half, and whoever brings the release replaces this rather
// than adding to it.
// -----------------------------------------------------------------------------
static void stopAllMoves(const char* reason) {
    bool stopped = false;
    for (uint8_t armId = 0; armId < kArmCount; ++armId) {
        stopped = stopped || s_arm[armId].moving;
        s_arm[armId].moving = false;
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
// parkSequenceMovedOutputs()
// Put every output a running sequence moved at its close position.
//
// Estop and Sleep Mode land here, and only while a sequence run is in progress:
// the body routines :SE30..:SE36 were parked closed at estop and sleep when
// ServoTask ran them itself, and they are sequences now (ADR 0049), so the same
// promise holds for every run -- not for a door somebody opened by hand, and not
// for one a routine deliberately left open after it finished.
//
// This path SNAPS, and it must keep snapping. A Servo Output's Motion Profile
// (ADR 0052) gives it a ramp, and a ramp opens a gap between the commanded
// position and where the servo actually is -- so easing into a safe state
// leaves the droid somewhere nobody asked for while it eases. setArmPosition()
// writes the endpoint straight through and abandons any move in progress
// (ADR 0041, ADR 0043).
//
// Both ADRs are cited as decisions this snap has to survive, NOT as behaviour
// this function implements -- and ADR 0043 in particular is not implemented at
// this call site. It decides that estop and Sleep Mode RELEASE every servo
// output and command no position; what happens below is the opposite, a drive
// to `close` with PWM held. The ADR says as much itself ("the code at
// 939ed705 implements none of it yet"), and whoever brings the release here
// replaces this park rather than adding to it.
// -----------------------------------------------------------------------------
static void parkSequenceMovedOutputs(const char* reason) {
    bool parked = false;
    for (uint8_t armId = 0; armId < kArmCount; ++armId) {
        if (!s_arm[armId].seqMoved) {
            continue;
        }
        uint16_t openUs = 0;
        uint16_t closeUs = SERVO_PULSE_NEUTRAL_US;
        getOpenClosePositions(armId, openUs, closeUs);
        setArmPosition(armId, closeUs);
        s_arm[armId].seqMoved = false;
        parked = true;
    }
    if (parked) {
        PA_LOG_INFO(TAG, "Outputs a sequence was moving parked closed - %s", reason);
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
                return;  // moved nothing, so it changes nothing below
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

    }

    // Record who moved the output last, for the park. Only a command that got
    // this far counts: a rejected one moved nothing.
    const bool fromSequence = cmd.source == SRC_SEQ;
    if (cmd.armId == 255) {
        s_arm[0].seqMoved = fromSequence;
        s_arm[1].seqMoved = fromSequence;
    } else if (cmd.armId < kArmCount) {
        s_arm[cmd.armId].seqMoved = fromSequence;
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
    bool runActiveBeforeHalt = false;

    while (true) {
        if (!hwmLogged) {
            PA_LOG_DEBUG("ServoTask", "stack HWM: %u bytes free",
                         (unsigned)uxTaskGetStackHighWaterMark(NULL));
            hwmLogged = true;
        }

        // Entering estop or Sleep Mode stops every move where it is, before a
        // command or a frame can carry one further, and parks what a running
        // sequence moved. On the edge only: a direct command is still accepted
        // in Sleep Mode, and it must be able to move.
        //
        // The three flags are read in one critical section, and "a run was in
        // progress" is taken from the last frame BEFORE the halt. The Sequence
        // Coordinator ends the run when it sees estop, so reading the run flag
        // on the halt frame itself could find it already cleared and skip the
        // park; a frame that still saw no halt cannot have seen that either.
        taskENTER_CRITICAL(&robotStateMux);
        const bool haltNow = robotState.estop || robotState.sleepMode;
        const bool runActive = robotState.seqRunActive;
        taskEXIT_CRITICAL(&robotStateMux);
        if (haltNow && !halted) {
            stopAllMoves("estop or sleep mode entered");
            if (runActiveBeforeHalt || runActive) {
                parkSequenceMovedOutputs("estop or sleep mode entered");
            }
        }
        if (!haltNow) {
            runActiveBeforeHalt = runActive;
        }
        halted = haltNow;

        // Once no run is in progress, nothing a finished run moved is the park's
        // any more: a routine that left a door open meant to, and the next run
        // must not close it at estop. Cleared before this frame's commands, so a
        // move from a run that starts in this frame is still recorded.
        if (!runActive) {
            for (uint8_t armId = 0; armId < kArmCount; ++armId) {
                s_arm[armId].seqMoved = false;
            }
        }

        // Process any pending commands (non-blocking)
        while (xQueueReceive(servoCmdQueue, &cmd, 0) == pdTRUE) {
            processCommand(cmd);
        }

        // Advance every move in progress by one frame
        updateMotion();

        // Feed watchdog
        esp_task_wdt_reset();

        // 50Hz update rate
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}
