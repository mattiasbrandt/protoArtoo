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
    SRC_SBUS,       // RC transmitter via SBUS receiver
    SRC_WEB_API,    // Browser / REST API
    SRC_INTERNAL,   // Internal (safety zeroing, boot defaults)
};

// -----------------------------------------------------------------------------
// Drive command message (sent via driveQueue)
// -----------------------------------------------------------------------------
struct DriveCommand {
    int16_t speed;          // -SPEED_LIMIT_MAX .. +SPEED_LIMIT_MAX
    int16_t steer;          // -SPEED_LIMIT_MAX .. +SPEED_LIMIT_MAX
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
    float domeTargetSpeed;      // -1.0 .. +1.0

    // --- Subsystem state ---
    bool audioActive;
    bool armOpen[2];            // [0]=arm1, [1]=arm2
    bool sleepMode;

    // --- Failsafe state ---
    bool estop;                 // Latching — requires explicit /api/estop/clear
    bool sbusSignalLost;        // Drive receiver watchdog
    bool sbusHwFailsafe;        // Drive receiver hardware failsafe flag
    bool sbus2SignalLost;       // Dome-spin receiver watchdog
    bool webDriveExpired;       // Web API drive timeout
    FailsafeSource failsafeSource;

    // --- Command source tracking ---
    CommandSource lastDriveSource;
    uint32_t lastDriveCommandMs;    // millis() of last drive command

    // --- Timestamps ---
    uint32_t lastSbus1Ms;       // millis() of last valid SBUS1 frame
    uint32_t lastSbus2Ms;       // millis() of last valid SBUS2 frame
    uint32_t lastDomeRxMs;      // millis() of last dome serial RX

    // --- CH8 speed limit ---
    float speedLimitScale;      // 0.0 .. 1.0 (from CH8)
    bool stationary;            // CH8 at zero + ch8_mode_lock enabled

    // --- Health counters ---
    uint32_t bodyHbTx;          // Body heartbeat TX count
    uint32_t domeHbRx;          // Dome heartbeat RX count
    uint32_t domeLastSeenMs;    // millis() of last dome heartbeat
    uint32_t failsafeTriggerCount;

    // --- NVS-backed config (loaded at boot, written via web API) ---
    int16_t cfg_speedLimitMax;          // Default: SPEED_LIMIT_MAX
    uint32_t cfg_sbusTimeoutMs;         // Default: SBUS_TIMEOUT_MS
    uint32_t cfg_webDriveTimeoutMs;     // Default: WEB_DRIVE_TIMEOUT_MS
    bool cfg_ch8ModeLock;               // Default: false
    uint8_t cfg_audioVolume;            // Default: 20 (0-30)
};

// -----------------------------------------------------------------------------
// Global instances (defined in main.cpp)
// -----------------------------------------------------------------------------
extern RobotState robotState;
extern portMUX_TYPE robotStateMux;
extern QueueHandle_t driveQueue;

// -----------------------------------------------------------------------------
// Helper function declarations (defined in main.cpp or a dedicated helpers.cpp)
// -----------------------------------------------------------------------------

// Set drive command under mutex and update lastDriveCommandMs
void setDriveCommand(int16_t speed, int16_t steer, CommandSource src);

// Returns true if dome link is active (last heartbeat < 3000 ms ago)
bool domeConnected();

// Load NVS config into robotState.cfg_* fields
void loadConfigToState();
