// =============================================================================
// include/robot_state.h
//
// Shared robot state structure for protoArtoo.
// All inter-task communication goes through this struct + FreeRTOS primitives.
//
// Thread safety: All fields accessed under robotStateMux (portMUX_TYPE).
// Queues: driveQueue for drive commands from any source.
// =============================================================================
#pragma once

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>

#include "config.h"
#include "rc_mapping.h"

// -----------------------------------------------------------------------------
// Enums
// -----------------------------------------------------------------------------

enum FailsafeSource : uint8_t {
    FS_NONE = 0,
    FS_SBUS_TIMEOUT,    // Layer 2: SBUS watchdog expired
    FS_SBUS_HW,         // Layer 1: SBUS receiver hardware failsafe flag
    FS_SBUS2_TIMEOUT,   // Dome-spin receiver lost (dome stops, drive continues)
    FS_WEB_TIMEOUT,     // Layer 3: Web API drive command expired
    FS_ESTOP_CMD,       // Explicit POST /api/estop
    FS_WATCHDOG_RESET,  // Layer 4: TWDT fired, detected on reboot
};

enum CommandSource : uint8_t {
    SRC_NONE = 0,
    SRC_SBUS,      // RC transmitter via SBUS receiver
    SRC_WEB_API,   // Browser / REST API
    SRC_INTERNAL,  // Internal (safety zeroing, boot defaults)
};

inline const char* commandSourceToString(CommandSource src) {
    switch (src) {
        case SRC_SBUS:
            return "SBUS";
        case SRC_WEB_API:
            return "WEB";
        case SRC_INTERNAL:
            return "INT";
        default:
            return "?";
    }
}

enum RcInputMode : uint8_t {
    RC_INPUT_STANDARD_PWM = 0,
    RC_INPUT_SINGLE_SBUS,
    RC_INPUT_DUAL_SBUS,
};

enum ServoComponentType : uint8_t {
    SERVO_COMP_NONE = 0,    // Nothing connected / unassigned
    SERVO_COMP_MG996R = 1,  // Standard hobby servo, 1000-2000 µs range
    SERVO_COMP_MG90S = 2,   // Micro servo, 500-2500 µs range
    SERVO_COMP_RGB = 3,     // RGB LED strip (no servo PWM calibration)
};

// -----------------------------------------------------------------------------
// Drive command message (sent via driveQueue)
// -----------------------------------------------------------------------------
struct DriveCommand {
    int16_t speed;  // -SPEED_LIMIT_MAX .. +SPEED_LIMIT_MAX
    int16_t steer;  // -SPEED_LIMIT_MAX .. +SPEED_LIMIT_MAX
    CommandSource source;
    uint32_t timestampMs;
};

// -----------------------------------------------------------------------------
// Servo command message (sent via servoCmdQueue)
// -----------------------------------------------------------------------------
enum ServoCommandType : uint8_t {
    SERVO_CMD_POSITION,
    SERVO_CMD_OPEN,
    SERVO_CMD_CLOSE,
    SERVO_CMD_SEQUENCE,
};

struct ServoCommand {
    uint8_t armId;          // 0=ARM1, 1=ARM2, 2=AUX1, 3=AUX2, 4=AUX3, 255=broadcast (ARM1+ARM2)
    ServoCommandType type;  // Command type
    uint16_t positionUs;    // Target pulse width (µs) for POSITION type
    uint8_t sequenceId;     // Sequence ID 30-36 for SEQUENCE type
    CommandSource source;
    uint32_t timestampMs;
};

// -----------------------------------------------------------------------------
// Dome command message (sent via domeCmdQueue)
// -----------------------------------------------------------------------------
struct DomeCommand {
    float speed;  // -1.0 (full reverse) .. +1.0 (full forward), 0 = stop
    CommandSource source;
    uint32_t timestampMs;
};

// -----------------------------------------------------------------------------
// RobotState — shared state, all access under robotStateMux
// -----------------------------------------------------------------------------
struct RobotState {
    // --- Drive output (written by SBUSInputTask/WebAPI, read by DriveTask) ---
    int16_t driveSpeed;
    int16_t driveSteer;
    float speedLimitScale;
    float domeTargetSpeed;  // -1.0 .. +1.0

    // --- Subsystem state ---
    bool audioActive;
    bool armOpen[2];
    uint16_t arm1TargetUs;
    uint16_t arm2TargetUs;
    float dome_speed;
    bool sleepMode;

    // --- Failsafe state ---
    bool estop;
    bool sbusSignalLost;
    bool sbus2SignalLost;
    bool sbusHwFailsafe;
    bool stationary;
    bool webDriveExpired;
    FailsafeSource failsafeSource;
    uint32_t failsafeTriggerCount;
    uint32_t queueOverflowCount;
    uint32_t sbus1LostFrameCount;  // cumulative lost_frame events (not failsafe)
    uint32_t sbus2LostFrameCount;  // cumulative lost_frame events (not failsafe)
    bool sbus2HwFailsafe;

    // --- Timing state ---
    uint32_t lastPwmMs;
    uint32_t lastSbus1Ms;
    uint32_t lastSbus2Ms;
    uint32_t lastDriveCommandMs;
    uint32_t domeLastSeenMs;
    CommandSource lastDriveSource;

    uint16_t rcPwmPulseUs[6];
    bool rcPwmPulseValid[6];
    uint16_t rcSbus1Raw[16];
    uint16_t rcSbus2Raw[16];
    bool rcSbus1Digital[2];
    bool rcSbus2Digital[2];

    // --- Dome heartbeat ---
    uint32_t domeHbRx;
    uint32_t bodyHbTx;

    // --- Mood ---
    // Active mood SE1x index: 10=Quiet, 11=Full-Awake, 13=Mid-Awake, 14=Awake+.
    // 0 = unset (no mood applied this session). NVS key: "last_mood".
    uint8_t activeMood;

    // --- Web control state ---
    bool webControlEnabled;
    bool rcDebugMode;  // Enable verbose RC/SBUS logging when RC page is active

    // --- NVS-backed config (loaded at boot, written via web API) ---
    int16_t cfg_speedLimitMax;       // Default: SPEED_LIMIT_MAX
    uint32_t cfg_sbusTimeoutMs;      // Default: SBUS_TIMEOUT_MS
    uint32_t cfg_webDriveTimeoutMs;  // Default: WEB_DRIVE_TIMEOUT_MS
    bool cfg_ch8ModeLock;            // Default: false
    uint8_t cfg_audioVolume;         // Default: 20 (0-30)

    // Servo config
    uint16_t cfg_arm1_open_us;
    uint16_t cfg_arm1_close_us;
    uint16_t cfg_arm2_open_us;
    uint16_t cfg_arm2_close_us;

    // Servo component types (what is physically connected to each output)
    ServoComponentType cfg_arm1_type;  // arm1_type — default: SERVO_COMP_MG996R
    ServoComponentType cfg_arm2_type;  // arm2_type — default: SERVO_COMP_MG996R
    ServoComponentType cfg_aux1_type;  // aux1_type — default: SERVO_COMP_NONE
    ServoComponentType cfg_aux2_type;  // aux2_type — default: SERVO_COMP_NONE
    ServoComponentType cfg_aux3_type;  // aux3_type — default: SERVO_COMP_NONE

    // AUX servo calibration (NVS-backed, only meaningful when type is a servo type)
    uint16_t cfg_aux1_open_us;
    uint16_t cfg_aux1_close_us;
    uint16_t cfg_aux2_open_us;
    uint16_t cfg_aux2_close_us;
    uint16_t cfg_aux3_open_us;
    uint16_t cfg_aux3_close_us;

    // Dome config
    float cfg_dome_min_speed;
    float cfg_dome_max_speed;
    uint16_t cfg_dome_neutral_us;
    uint16_t cfg_dome_min_pulse_us;
    uint16_t cfg_dome_max_pulse_us;
    uint8_t cfg_dome_speed_limit_pct;

    // Sequence timing (ms)
    uint16_t cfg_seq_open_ms;
    uint16_t cfg_seq_close_ms;

    // Feature toggles (enable/disable entire subsystems)
    // NVS key names are the config source of truth; GPIO assignments from docs/pin_map.md.
    //
    // Servo / ESC outputs (LEDC PWM):
    bool cfg_enable_arm1;  // en_arm1  — ARM1 (GPIO 23) — Utility arm servo #1 — Top / Left arm
    bool cfg_enable_arm2;  // en_arm2  — ARM2 (GPIO 5)  — Utility arm servo #2 — Bottom / Right arm
    bool cfg_enable_aux1;  // en_aux1  — AUX1 / ARM3 (GPIO 19) — Spare servo / AUX output #1
    bool cfg_enable_aux2;  // en_aux2  — AUX2 / ARM4 (GPIO 18) — Spare servo / AUX output #2
    bool cfg_enable_aux3;  // en_aux3  — AUX3 / ARM5 (GPIO 32) — Spare servo / AUX output #3
    bool cfg_enable_dome;  // en_dome  — DOME (GPIO 25) — Dome rotation ESC
    //
    // RC receiver inputs:
    RcInputMode cfg_rc_input_mode;
    bool cfg_enable_rc_ch1;  // en_rc_ch1 — CH1 (GPIO 15) — SBUS #1 (drive) OR Standard PWM CH1
    bool cfg_enable_rc_ch2;  // en_rc_ch2 — CH2 (GPIO 13) — SBUS #2 (dome) OR Standard PWM CH2
    bool
        cfg_enable_rc_ch3;  // en_rc_ch3 — CH3 (GPIO 2)  — Standard PWM CH3 (strapping pin; dormant)
    bool cfg_enable_rc_ch4;  // en_rc_ch4 — CH4 (GPIO 4)  — Standard PWM CH4 (dormant)
    bool
        cfg_enable_rc_ch5;  // en_rc_ch5 — CH5 (GPIO 12) — Standard PWM CH5 (strapping pin; dormant)
    bool cfg_enable_rc_ch6;  // en_rc_ch6 — CH6 (GPIO 27) — Standard PWM CH6 (dormant)
    //
    // Serial links (UART):
    bool cfg_enable_s1_hoverboard;  // en_s1_hoverboard — S1 (GPIO 16/17) — Hoverboard drive (UART1)
    bool cfg_enable_s2_sound;       // en_s2_sound       — S2 (GPIO 26/35) — DY-SV5W audio (UART2)
    bool cfg_enable_s3_dome_ctrl;   // en_s3_dome_ctrl   — S3 (GPIO 33/34) — Marcduino dome link
                                    // (UART2)
    //
    // Operation mode:
    bool cfg_stationary;  // op_mode — true=stationary (performance), false=driving (movement)

    RcBindingConfig cfg_rc_pwm_drive_speed;
    RcBindingConfig cfg_rc_pwm_drive_steer;
    RcBindingConfig cfg_rc_pwm_drive_limit;
    RcBindingConfig cfg_rc_pwm_dome_speed;
    RcBindingConfig cfg_rc_pwm_arm1;
    RcBindingConfig cfg_rc_pwm_arm2;
    RcBindingConfig cfg_rc_pwm_sound;

    RcBindingConfig cfg_rc_sbus_drive_speed;
    RcBindingConfig cfg_rc_sbus_drive_steer;
    RcBindingConfig cfg_rc_sbus_drive_limit;
    RcBindingConfig cfg_rc_sbus_dome_speed;
    RcBindingConfig cfg_rc_sbus_arm1;
    RcBindingConfig cfg_rc_sbus_arm2;
    RcBindingConfig cfg_rc_sbus_sound;

    // Tier 2 Trigger/Button bindings (operator-configurable action targets)
    // Format: source:channel:target:payload:min:center:max:deadband:reverse
    RcTriggerBinding cfg_rc_arm1;
    RcTriggerBinding cfg_rc_arm2;
    RcTriggerBinding cfg_rc_aux1;
    RcTriggerBinding cfg_rc_aux2;
    RcTriggerBinding cfg_rc_aux3;
    RcTriggerBinding cfg_rc_sound;
    RcTriggerBinding cfg_rc_opmode;
    RcTriggerBinding cfg_rc_free0;
    RcTriggerBinding cfg_rc_free1;
    RcTriggerBinding cfg_rc_free2;
    RcTriggerBinding cfg_rc_free3;
};

// -----------------------------------------------------------------------------
// Global instances (defined in main.cpp)
// -----------------------------------------------------------------------------
extern RobotState robotState;
extern portMUX_TYPE robotStateMux;
extern QueueHandle_t servoCmdQueue;
extern QueueHandle_t domeCmdQueue;
extern QueueHandle_t audioCmdQueue;
extern QueueHandle_t domeTxQueue;
// -----------------------------------------------------------------------------
// Helper function declarations (defined in main.cpp or a dedicated helpers.cpp)
// -----------------------------------------------------------------------------

// Set drive command under mutex and update lastDriveCommandMs
void setDriveCommand(int16_t speed, int16_t steer, CommandSource src);

bool domeConnected();

// Load NVS config into robotState.cfg_* fields
void loadConfigToState();

bool saveConfigToNvs();

// -----------------------------------------------------------------------------
// Safe read-only accessors for safety-critical state
// Use these instead of direct field access for safety-critical reads
// -----------------------------------------------------------------------------

// Read estop state under mutex
inline bool isEstopActive() {
    bool estop;
    taskENTER_CRITICAL(&robotStateMux);
    estop = robotState.estop;
    taskEXIT_CRITICAL(&robotStateMux);
    return estop;
}

// Read drive speed under mutex
inline int16_t getDriveSpeed() {
    int16_t speed;
    taskENTER_CRITICAL(&robotStateMux);
    speed = robotState.driveSpeed;
    taskEXIT_CRITICAL(&robotStateMux);
    return speed;
}

// Read drive steer under mutex
inline int16_t getDriveSteer() {
    int16_t steer;
    taskENTER_CRITICAL(&robotStateMux);
    steer = robotState.driveSteer;
    taskEXIT_CRITICAL(&robotStateMux);
    return steer;
}

// Read failsafe source under mutex
inline FailsafeSource getFailsafeSource() {
    FailsafeSource source;
    taskENTER_CRITICAL(&robotStateMux);
    source = robotState.failsafeSource;
    taskEXIT_CRITICAL(&robotStateMux);
    return source;
}

// Read SBUS signal lost state under mutex
inline bool isSbusSignalLost() {
    bool lost;
    taskENTER_CRITICAL(&robotStateMux);
    lost = robotState.sbusSignalLost;
    taskEXIT_CRITICAL(&robotStateMux);
    return lost;
}
