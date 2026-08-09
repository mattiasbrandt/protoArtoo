// =============================================================================
// src/tasks/servo_task.cpp
//
// ServoTask — LEDC PWM control for utility arm servos and spare servo outputs.
// Handles open/close commands and Marcduino sequences for:
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
#include "servo_helpers.h"

static const char* TAG = "SERVO";

// -----------------------------------------------------------------------------
// Sequence state machine
// -----------------------------------------------------------------------------
enum SequenceState : uint8_t {
    SEQ_IDLE = 0,
    SEQ_OPENING,
    SEQ_OPEN_PAUSE,
    SEQ_CLOSING,
};

static struct {
    SequenceState state;
    uint32_t stateStartMs;
    uint8_t activeArm;  // 0=ARM1, 1=ARM2, 255=both
    uint8_t sequenceId;
} seqState = {};

// -----------------------------------------------------------------------------
// armIdToLedcChannel()
// Map armId to LEDC channel.
//   0 = ARM1  → LEDC_CH_ARM1  (GPIO 23)
//   1 = ARM2  → LEDC_CH_ARM2  (GPIO 5)
//   2 = AUX1  → LEDC_CH_AUX1 (GPIO 19, also labelled ARM3)
//   3 = AUX2  → LEDC_CH_AUX2 (GPIO 18, also labelled ARM4)
//   4 = AUX3  → LEDC_CH_AUX3 (GPIO 32, also labelled ARM5)
// Returns LEDC_CH_MAX (invalid) for unknown armId.
// -----------------------------------------------------------------------------
static uint8_t armIdToLedcChannel(uint8_t armId) {
    return servo_arm_id_to_ledc_channel(armId);
}

// -----------------------------------------------------------------------------
// setArmPosition()
// Set single arm to specific pulse width.
// armId: 0=ARM1, 1=ARM2, 2=AUX1, 3=AUX2, 4=AUX3
// -----------------------------------------------------------------------------
static void setArmPosition(uint8_t armId, uint16_t pulseUs) {
    uint8_t channel = armIdToLedcChannel(armId);
    if (channel >= LEDC_CH_MAX) {
        PA_LOG_WARN(TAG, "setArmPosition: invalid armId %d", armId);
        return;
    }
    ledcPwmSetPulseWidth(channel, pulseUs);

    taskENTER_CRITICAL(&robotStateMux);
    if (armId == 0) {
        robotState.armOpen[0] = (pulseUs > SERVO_PULSE_NEUTRAL_US);
        robotState.arm1TargetUs = pulseUs;
    } else if (armId == 1) {
        robotState.armOpen[1] = (pulseUs > SERVO_PULSE_NEUTRAL_US);
        robotState.arm2TargetUs = pulseUs;
    }
    taskEXIT_CRITICAL(&robotStateMux);
}

// -----------------------------------------------------------------------------
// getOpenClosePositions()
// Get configured open/close pulse widths for arm.
// ARM1/ARM2 use NVS-backed cal; AUX1-3 also use NVS-backed per-channel cal.
// -----------------------------------------------------------------------------
static void getOpenClosePositions(uint8_t armId, uint16_t& openUs, uint16_t& closeUs) {
    ServoConfig cfg = {};
    configCacheReadServo(&cfg);
    switch (armId) {
        case 0:
            openUs = cfg.arm1_open_us;
            closeUs = cfg.arm1_close_us;
            break;
        case 1:
            openUs = cfg.arm2_open_us;
            closeUs = cfg.arm2_close_us;
            break;
        case 2:
            openUs = cfg.aux1_open_us;
            closeUs = cfg.aux1_close_us;
            break;
        case 3:
            openUs = cfg.aux2_open_us;
            closeUs = cfg.aux2_close_us;
            break;
        case 4:
            openUs = cfg.aux3_open_us;
            closeUs = cfg.aux3_close_us;
            break;
        default:
            openUs = SERVO_PULSE_MAX_US;
            closeUs = SERVO_PULSE_MIN_US;
            break;
    }
}

// -----------------------------------------------------------------------------
// executeSequence()
// Execute Marcduino sequence :SE30-:SE36.
// -----------------------------------------------------------------------------
static void executeSequence(uint8_t seqId) {
    seqState.sequenceId = seqId;
    seqState.state = SEQ_OPENING;
    seqState.stateStartMs = millis();

    uint16_t openUs, closeUs;

    switch (seqId) {
        case 30:                       // Utility arm open-and-close
            seqState.activeArm = 255;  // Both arms
            getOpenClosePositions(0, openUs, closeUs);
            setArmPosition(0, openUs);
            setArmPosition(1, openUs);
            break;

        case 31:  // All body panels open and close
        case 32:  // All body doors open and wiggle-close
            seqState.activeArm = 255;
            getOpenClosePositions(0, openUs, closeUs);
            setArmPosition(0, openUs);
            setArmPosition(1, openUs);
            break;

        case 33:  // Body — use gripper arm (ARM1)
            seqState.activeArm = 0;
            getOpenClosePositions(0, openUs, closeUs);
            setArmPosition(0, openUs);
            break;

        case 34:  // Body — use interface tool (ARM2)
            seqState.activeArm = 1;
            getOpenClosePositions(1, openUs, closeUs);
            setArmPosition(1, openUs);
            break;

        case 35:  // Ping-pong body doors
        case 36:  // BT-1 two-gripper sequence
            seqState.activeArm = 255;
            getOpenClosePositions(0, openUs, closeUs);
            setArmPosition(0, openUs);
            setArmPosition(1, openUs);
            break;

        default:
            seqState.state = SEQ_IDLE;
            break;
    }

    PA_LOG_INFO(TAG, "Sequence :SE%02d started", seqId);
}

static void abortSequenceAndPark(const char* reason) {
    if (seqState.state == SEQ_IDLE) {
        return;
    }

    const uint8_t activeArm = seqState.activeArm;
    const uint8_t sequenceId = seqState.sequenceId;
    seqState.state = SEQ_IDLE;

    uint16_t openUs = 0;
    uint16_t closeUs = SERVO_PULSE_NEUTRAL_US;
    if (activeArm == 255) {
        getOpenClosePositions(0, openUs, closeUs);
        setArmPosition(0, closeUs);
        getOpenClosePositions(1, openUs, closeUs);
        setArmPosition(1, closeUs);
    } else if (activeArm <= 4) {
        getOpenClosePositions(activeArm, openUs, closeUs);
        setArmPosition(activeArm, closeUs);
    }

    PA_LOG_INFO(TAG, "Sequence :SE%02d aborted — %s", sequenceId, reason);
}

// -----------------------------------------------------------------------------
// updateSequence()
// Update sequence state machine.
// -----------------------------------------------------------------------------
static void updateSequence() {
    if (seqState.state == SEQ_IDLE)
        return;

    taskENTER_CRITICAL(&robotStateMux);
    bool estop = robotState.estop;
    bool sleepMode = robotState.sleepMode;
    taskEXIT_CRITICAL(&robotStateMux);

    if (sleepMode) {
        abortSequenceAndPark("sleep mode active");
        return;
    }
    if (estop) {
        abortSequenceAndPark("estop active");
        return;
    }

    uint32_t elapsed = millis() - seqState.stateStartMs;
    uint16_t openUs, closeUs;

    ServoConfig cfg = {};
    configCacheReadServo(&cfg);
    uint16_t seqOpenMs = cfg.seq_open_ms;
    uint16_t seqCloseMs = cfg.seq_close_ms;

    switch (seqState.state) {
        case SEQ_OPENING:
            if (elapsed > seqOpenMs) {
                seqState.state = SEQ_CLOSING;
                seqState.stateStartMs = millis();

                if (seqState.activeArm == 255) {
                    getOpenClosePositions(0, openUs, closeUs);
                    setArmPosition(0, closeUs);
                    setArmPosition(1, closeUs);
                } else {
                    getOpenClosePositions(seqState.activeArm, openUs, closeUs);
                    setArmPosition(seqState.activeArm, closeUs);
                }
                PA_LOG_DEBUG(TAG, "Sequence closing");
            }
            break;

        case SEQ_CLOSING:
            if (elapsed > seqCloseMs) {
                seqState.state = SEQ_IDLE;
                PA_LOG_INFO(TAG, "Sequence :SE%02d complete", seqState.sequenceId);
            }
            break;

        default:
            break;
    }
}

// -----------------------------------------------------------------------------
// isArmEnabled()
// Check feature toggle for a given armId.
// armId 255 (broadcast) is allowed only if at least ARM1 and ARM2 are enabled.
// AUX channel selected for WS2812 (cfg_aux_led_pin) is treated as unavailable
// for servo commands to avoid pin ownership conflicts.
// -----------------------------------------------------------------------------
static bool isArmEnabled(uint8_t armId) {
    ConfigSnapshot cfg = {};
    configCacheRead(&cfg);
    bool arm1 = cfg.system.enable_arm1;
    bool arm2 = cfg.system.enable_arm2;
    bool aux1 = cfg.system.enable_aux1;
    bool aux2 = cfg.system.enable_aux2;
    bool aux3 = cfg.system.enable_aux3;
    uint8_t auxLedPin = cfg.servo.aux_led_pin;

    return servo_arm_enabled(armId, arm1, arm2, aux1, aux2, aux3, auxLedPin);
}

// -----------------------------------------------------------------------------
// processCommand()
// Process incoming servo command.
// -----------------------------------------------------------------------------
static void processCommand(const ServoCommand& cmd) {
    // Safety: Check estop — reject all commands while emergency stopped
    taskENTER_CRITICAL(&robotStateMux);
    bool estop = robotState.estop;
    bool sleepMode = robotState.sleepMode;
    taskEXIT_CRITICAL(&robotStateMux);

    if (estop) {
        PA_LOG_WARN(TAG, "[%s] Command rejected — estop active", commandSourceToString(cmd.source));
        return;
    }
    if (sleepMode && cmd.type == SERVO_CMD_SEQUENCE) {
        PA_LOG_INFO(TAG, "[%s] Sequence command ignored — sleep mode active",
                    commandSourceToString(cmd.source));
        return;
    }

    // Feature toggle: reject commands for disabled or AUX-LED-reserved subsystems
    if (cmd.type != SERVO_CMD_SEQUENCE && !isArmEnabled(cmd.armId)) {
        PA_LOG_WARN(TAG, "[%s] Command rejected — arm%d disabled or reserved",
                    commandSourceToString(cmd.source), cmd.armId);
        return;
    }

    uint16_t openUs, closeUs;

    switch (cmd.type) {
        case SERVO_CMD_OPEN:
            if (cmd.armId == 255) {
                getOpenClosePositions(0, openUs, closeUs);
                setArmPosition(0, openUs);
                getOpenClosePositions(1, openUs, closeUs);
                setArmPosition(1, openUs);
                PA_LOG_INFO(TAG, "[%s] Both arms opened", commandSourceToString(cmd.source));
            } else {
                getOpenClosePositions(cmd.armId, openUs, closeUs);
                setArmPosition(cmd.armId, openUs);
                PA_LOG_INFO(TAG, "[%s] Arm%d opened", commandSourceToString(cmd.source), cmd.armId + 1);
            }
            break;

        case SERVO_CMD_CLOSE:
            if (cmd.armId == 255) {
                getOpenClosePositions(0, openUs, closeUs);
                setArmPosition(0, closeUs);
                getOpenClosePositions(1, openUs, closeUs);
                setArmPosition(1, closeUs);
                PA_LOG_INFO(TAG, "[%s] Both arms closed", commandSourceToString(cmd.source));
            } else {
                getOpenClosePositions(cmd.armId, openUs, closeUs);
                setArmPosition(cmd.armId, closeUs);
                PA_LOG_INFO(TAG, "[%s] Arm%d closed", commandSourceToString(cmd.source), cmd.armId + 1);
            }
            break;

        case SERVO_CMD_POSITION:
            // Validate pulse width before setting
            if (cmd.positionUs < SERVO_PULSE_MIN_US || cmd.positionUs > SERVO_PULSE_MAX_US) {
                PA_LOG_WARN(TAG, "[%s] Invalid position %d us — rejected",
                            commandSourceToString(cmd.source), cmd.positionUs);
                break;
            }
            if (cmd.armId == 255) {
                setArmPosition(0, cmd.positionUs);
                setArmPosition(1, cmd.positionUs);
            } else {
                setArmPosition(cmd.armId, cmd.positionUs);
            }
            PA_LOG_INFO(TAG, "[%s] Arm%d set to %d us", commandSourceToString(cmd.source), cmd.armId + 1,
                        cmd.positionUs);
            break;

        case SERVO_CMD_SEQUENCE:
            PA_LOG_INFO(TAG, "[%s] Sequence :SE%02d started", commandSourceToString(cmd.source),
                        cmd.sequenceId);
            executeSequence(cmd.sequenceId);
            break;
    }
}

// -----------------------------------------------------------------------------
// servoTaskInit()
// Initialize servo hardware.
// -----------------------------------------------------------------------------
void servoTaskInit() {
    ConfigSnapshot cfg = {};
    configCacheRead(&cfg);
    bool enableArm1 = cfg.system.enable_arm1;
    bool enableArm2 = cfg.system.enable_arm2;
    bool enableAux1 = cfg.system.enable_aux1;
    bool enableAux2 = cfg.system.enable_aux2;
    bool enableAux3 = cfg.system.enable_aux3;
    bool enableDome = cfg.system.enable_dome;
    uint8_t auxLedPin = cfg.servo.aux_led_pin;

    bool anyServo = enableArm1 || enableArm2 || enableAux1 || enableAux2 || enableAux3;
    bool anyLedc = anyServo || enableDome;

    uint8_t skipChannel = LEDC_CH_MAX;
    if (auxLedPin == AUX_LED_PIN_AUX1) {
        skipChannel = LEDC_CH_AUX1;
    } else if (auxLedPin == AUX_LED_PIN_AUX2) {
        skipChannel = LEDC_CH_AUX2;
    } else if (auxLedPin == AUX_LED_PIN_AUX3) {
        skipChannel = LEDC_CH_AUX3;
    }

    if (anyLedc) {
        if (!ledcPwmInit(skipChannel)) {
            PA_LOG_ERROR(TAG, "LEDC init failed");
            return;
        }

        for (uint8_t channel = 0; channel < LEDC_CH_MAX; ++channel) {
            if (channel == skipChannel) {
                continue;
            }
            ledcPwmSetNeutral(channel);
        }

        if (skipChannel != LEDC_CH_MAX) {
            PA_LOG_INFO(TAG, "AUX LED active on selection %u (GPIO %u) — LEDC skipped for that header",
                        (unsigned)auxLedPin, (unsigned)getChannelGpio(skipChannel));
        }
    } else {
        PA_LOG_INFO(TAG, "all LEDC outputs disabled — skipping LEDC init");
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

    // Register with task watchdog unconditionally.
    esp_task_wdt_add(NULL);

    // Feature toggle: if no arm/aux outputs are enabled, ServoTask has no
    // channels to drive. Idle here feeding TWDT only — no queue processing,
    // no sequence updates.
    if (!configCacheServoAnyEnabled()) {
        PA_LOG_DEBUG("ServoTask", "all arm/aux outputs disabled — task idle");
        for (;;) {
            esp_task_wdt_reset();
            vTaskDelay(pdMS_TO_TICKS(20));
        }
    }

    ServoCommand cmd;
    bool hwmLogged = false;

    while (true) {
        if (!hwmLogged) {
            PA_LOG_DEBUG("ServoTask", "stack HWM: %u words free",
                         (unsigned)uxTaskGetStackHighWaterMark(NULL));
            hwmLogged = true;
        }

        // Process any pending commands (non-blocking)
        while (xQueueReceive(servoCmdQueue, &cmd, 0) == pdTRUE) {
            processCommand(cmd);
        }

        // Update sequence state machine
        updateSequence();

        // Feed watchdog
        esp_task_wdt_reset();

        // 50Hz update rate
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}
