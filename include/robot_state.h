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

#include "audio_rx_status.h"
#include "config.h"
#include "dome_link_transport.h"
#include "drive_speed_preset.h"
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
            return "RC";
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

enum DomeUartOwner : uint8_t {
    DOME_UART_NONE = 0,
    DOME_UART_DOME,
    DOME_UART_AUDIO,
};

enum ServoComponentType : uint8_t {
    SERVO_COMP_NONE = 0,    // Nothing connected / unassigned
    SERVO_COMP_MG996R = 1,  // Standard hobby servo, 1000-2000 µs range
    SERVO_COMP_MG90S = 2,   // Micro servo, 500-2500 µs range
    SERVO_COMP_RGB = 3,     // RGB LED strip (no servo PWM calibration)
};

enum AuxLedEffect : uint8_t {
    AUX_LED_EFFECT_OFF = 0,
    AUX_LED_EFFECT_SOLID,
    AUX_LED_EFFECT_BLINK,
    AUX_LED_EFFECT_PULSE,
};

struct AuxLedState {
    uint8_t pin;  // 0 when disabled; otherwise active GPIO number
    uint8_t r;
    uint8_t g;
    uint8_t b;
    AuxLedEffect effect;
    bool available;  // false when RMT/driver init failed
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
    float speed;         // -1.0 (full reverse) .. +1.0 (full forward), 0 = stop
    uint32_t durationMs; // 0 = indefinite (RC/web), >0 = auto-stop after this many ms
    CommandSource source;
    uint32_t timestampMs;
};

// -----------------------------------------------------------------------------
// RobotState — shared state, all access under robotStateMux
// -----------------------------------------------------------------------------
struct RobotState {
    // --- Drive output status mirror (written after DriveArbiter resolve) ---
    int16_t driveOutputSpeed;
    int16_t driveOutputSteer;
    float domeTargetSpeed;  // -1.0 .. +1.0

    // --- Subsystem state ---
    bool audioActive;

    // Audio module state — populated by AudioTask from DY-SV5W query responses.
    // link_ok: false until the module responds to at least one query.
    // play_state: 0=stop 1=playing 2=paused 0xFF=unknown
    // device:     0=USB  1=SD/TF  2=FLASH  0xFF=unknown/none
    bool audio_module_link_ok;
    uint8_t audio_module_play_state;
    uint8_t audio_module_device;
    uint16_t audio_module_total_tracks;
    uint16_t audio_module_current_track;
    AudioRxStatus audio_module_rx_status;

    bool armOpen[2];
    uint16_t arm1TargetUs;
    uint16_t arm2TargetUs;
    float dome_speed;
    bool sleepMode;
    uint32_t sleepSinceMs;
    AuxLedState auxLed;
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
    uint32_t failsafeLastTriggerMs;        // millis() when last failsafe trigger latched
    uint32_t failsafeLastWatchdogMs;       // millis() when last SBUS watchdog trigger fired
    uint32_t failsafeLastZeroOutputMs;     // millis() when DriveTask first asserted zero output
    uint32_t failsafeLastTriggerToZeroMs;  // latency from trigger to first zero output (ms)
    FailsafeSource failsafeLastTriggerSource;
    // --- Timing state ---
    uint32_t lastPwmMs;
    uint32_t lastSbus1Ms;
    uint32_t lastSbus2Ms;
    uint32_t driveOutputCommandMs;
    uint32_t domeLastSeenMs;
    uint32_t domeLastSeenUartMs;
    uint32_t domeLastSeenWifiMs;
    CommandSource driveOutputSource;

    uint16_t rcPwmPulseUs[6];
    bool rcPwmPulseValid[6];
    uint16_t rcSbus1Raw[16];
    uint16_t rcSbus2Raw[16];
    bool rcSbus1Digital[2];
    bool rcSbus2Digital[2];

    // --- Dome link diagnostics / transport state ---
    uint32_t domeHbRx;
    uint32_t bodyHbTx;
    uint32_t domeRxOverflowCount;
    uint32_t domeRxUnknownCount;
    DomeLinkTransport domeActiveTransport;
    DomeUartOwner domeUartOwner;
    bool     domeSeqActive;    // true while a dome sequence is running
    uint32_t domeSeqUntilMs;   // safety timeout: auto-clear domeSeqActive at this millis()

    // --- Mood ---
    // Active mood SE1x index: 10=Quiet, 11=Full-Awake, 13=Mid-Awake, 14=Awake+.
    // 0 = unset (no mood applied this session). NVS key: "last_mood".
    uint8_t activeMood;

    // --- Web control state ---
    bool webControlEnabled;
    bool rcDebugMode;    // Enable verbose RC/SBUS logging when RC page is active
    bool rcConfigDirty;  // Set by configSaveSystem/configCacheApply; cleared by RcInputTask after rebuild

    // -------------------------------------------------------------------------
    // Hoverboard controller feedback — populated by DriveTask from UART1 RX.
    // batteryRaw = V × 100; boardTempRaw = °C × 10.
    // currentL/R = A × 100 from Gen2.x firmware only; 0 for FOC firmware.
    // feedbackValid is false until the first valid frame is received.
    // -------------------------------------------------------------------------
    int16_t hb_batteryRaw;
    int16_t hb_boardTempRaw;
    int16_t hb_speedR;
    int16_t hb_speedL;
    int16_t hb_currentL;
    int16_t hb_currentR;
    bool hb_feedbackValid;
    uint32_t hb_lastFeedbackMs;
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

// Load NVS config into the config cache
void loadConfigToState();

bool saveConfigToNvs();

// ----------------------------------------------------------------------------
// Failsafe instrumentation helpers (MUST be called under robotStateMux lock)
// ----------------------------------------------------------------------------
inline void recordFailsafeTriggerLocked(FailsafeSource src, uint32_t nowMs) {
    robotState.failsafeSource = src;
    robotState.failsafeTriggerCount++;
    robotState.failsafeLastTriggerMs = nowMs;
    robotState.failsafeLastTriggerSource = src;
    if (src == FS_SBUS_TIMEOUT || src == FS_SBUS2_TIMEOUT) {
        robotState.failsafeLastWatchdogMs = nowMs;
    }
}

inline void recordFailsafeZeroOutputLocked(uint32_t nowMs) {
    robotState.failsafeLastZeroOutputMs = nowMs;
    if (robotState.failsafeLastTriggerMs == 0) {
        robotState.failsafeLastTriggerToZeroMs = 0;
        return;
    }
    robotState.failsafeLastTriggerToZeroMs = (uint32_t)(nowMs - robotState.failsafeLastTriggerMs);
}

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

// Read resolved drive output speed under mutex
inline int16_t getDriveSpeed() {
    int16_t speed;
    taskENTER_CRITICAL(&robotStateMux);
    speed = robotState.driveOutputSpeed;
    taskEXIT_CRITICAL(&robotStateMux);
    return speed;
}

// Read resolved drive output steer under mutex
inline int16_t getDriveSteer() {
    int16_t steer;
    taskENTER_CRITICAL(&robotStateMux);
    steer = robotState.driveOutputSteer;
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
