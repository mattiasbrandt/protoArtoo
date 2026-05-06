// =============================================================================
// src/drive_arbiter.cpp
//
// Drive Output Arbiter — implementation.
// Owns all drive command state (speed, steer, source, timestamp).
// =============================================================================

#include "drive_arbiter.h"

#include <Arduino.h>

#ifdef ARDUINO
#include <freertos/FreeRTOS.h>
#include <freertos/portmacro.h>
#endif

#include "failsafe_gate.h"
#include "logging.h"
#include "robot_state.h"

static const char* TAG = "DriveArbiter";

// =============================================================================
// Private module state
// =============================================================================

// Spinlock for arbiter state (shared with robotState)
static portMUX_TYPE* g_arbiterMux = nullptr;

struct ArbiterState {
    // Most recent command from each source
    int16_t rcSpeed;          // RC: speed
    int16_t rcSteer;          // RC: steer
    uint32_t rcTimestampMs;   // RC: timestamp of last command

    int16_t webSpeed;         // WEB: speed
    int16_t webSteer;         // WEB: steer
    uint32_t webTimestampMs;  // WEB: timestamp of last command

};

static ArbiterState g_arbiter = {
    .rcSpeed = 0,
    .rcSteer = 0,
    .rcTimestampMs = 0,
    .webSpeed = 0,
    .webSteer = 0,
    .webTimestampMs = 0,
};

// =============================================================================
// Public API
// =============================================================================

void driveArbiterInit(void* mux_ptr) {
    g_arbiterMux = (portMUX_TYPE*)mux_ptr;
}

void driveArbiterReset() {
    if (g_arbiterMux == nullptr) {
        return;
    }

    taskENTER_CRITICAL(g_arbiterMux);
    g_arbiter.rcSpeed = 0;
    g_arbiter.rcSteer = 0;
    g_arbiter.rcTimestampMs = 0;
    g_arbiter.webSpeed = 0;
    g_arbiter.webSteer = 0;
    g_arbiter.webTimestampMs = 0;
    taskEXIT_CRITICAL(g_arbiterMux);
}

void driveArbiterSubmit(DriveSource src,
                        int16_t speed,
                        int16_t steer,
                        uint32_t timestampMs) {
    if (g_arbiterMux == nullptr) {
        PA_LOG_ERROR(TAG, "driveArbiterSubmit called before init");
        return;
    }

    bool freshWebCommand = false;

    taskENTER_CRITICAL(g_arbiterMux);

    if (src == DriveSource::RC) {
        g_arbiter.rcSpeed = speed;
        g_arbiter.rcSteer = steer;
        g_arbiter.rcTimestampMs = timestampMs;
    } else if (src == DriveSource::WEB_API) {
        g_arbiter.webSpeed = speed;
        g_arbiter.webSteer = steer;
        g_arbiter.webTimestampMs = timestampMs;
        freshWebCommand = true;
    }

    taskEXIT_CRITICAL(g_arbiterMux);

    if (freshWebCommand) {
        failsafeClear(FailsafeLayer::WEB_TIMEOUT);
    }
}

DriveOutput driveArbiterResolve(const DriveArbiterConfig& cfg,
                                uint32_t nowMs) {
    if (g_arbiterMux == nullptr) {
        PA_LOG_ERROR(TAG, "driveArbiterResolve called before init");
        return DriveOutput{
            .speed = 0,
            .steer = 0,
            .failsafeActive = true,
            .webTimedOut = false,
            .activeSource = DriveSource::RC,
            .activeTimestampMs = 0,
        };
    }

    int16_t outputSpeed = 0;
    int16_t outputSteer = 0;
    DriveSource activeSource = DriveSource::RC;
    uint32_t activeTimestampMs = 0;
    bool webTimedOut = false;

    taskENTER_CRITICAL(g_arbiterMux);

    // Determine which source provides output
    // Most recent timestamp (within timeout) wins
    bool rcValid = (g_arbiter.rcTimestampMs != 0);
    bool webValid = (g_arbiter.webTimestampMs != 0);
    if (webValid) {
        // Check if web command has timed out
        uint32_t webAge = (uint32_t)(nowMs - g_arbiter.webTimestampMs);
        if (webAge > cfg.webDriveTimeoutMs) {
            webTimedOut = true;
        }
    }

    // Arbitration logic: most recent non-timed-out source wins
    if (rcValid && (!webValid || webTimedOut || g_arbiter.rcTimestampMs >= g_arbiter.webTimestampMs)) {
        // RC wins: either it's the only valid source, or web is timed out, or RC is more recent
        activeSource = DriveSource::RC;
        outputSpeed = g_arbiter.rcSpeed;
        outputSteer = g_arbiter.rcSteer;
        activeTimestampMs = g_arbiter.rcTimestampMs;
    } else if (webValid && !webTimedOut) {
        // Web wins: it's valid and more recent than RC
        activeSource = DriveSource::WEB_API;
        outputSpeed = g_arbiter.webSpeed;
        outputSteer = g_arbiter.webSteer;
        activeTimestampMs = g_arbiter.webTimestampMs;
    } else {
        // No valid source: zero output
        activeSource = DriveSource::RC;
        outputSpeed = 0;
        outputSteer = 0;
        activeTimestampMs = 0;
    }

    taskEXIT_CRITICAL(g_arbiterMux);

    // Clamp output to speed limit
    if (outputSpeed > cfg.speedLimitMax) {
        outputSpeed = cfg.speedLimitMax;
    } else if (outputSpeed < -cfg.speedLimitMax) {
        outputSpeed = (int16_t)(-cfg.speedLimitMax);
    }
    if (outputSteer > cfg.speedLimitMax) {
        outputSteer = cfg.speedLimitMax;
    } else if (outputSteer < -cfg.speedLimitMax) {
        outputSteer = (int16_t)(-cfg.speedLimitMax);
    }

    // Check if any failsafe is active and zero output if so.
    bool failsafeActive = failsafeIsActive() || webTimedOut;
    if (failsafeActive) {
        outputSpeed = 0;
        outputSteer = 0;
    }

    return DriveOutput{
        .speed = outputSpeed,
        .steer = outputSteer,
        .failsafeActive = failsafeActive,
        .webTimedOut = webTimedOut,
        .activeSource = activeSource,
        .activeTimestampMs = activeTimestampMs,
    };
}
