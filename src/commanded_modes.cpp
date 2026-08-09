// =============================================================================
// src/commanded_modes.cpp
//
// Commanded Mode Setters  --  promotion of the stationary-mode setter pattern
// into a shared module. Handles state write, config cache sync, edge detection,
// and audio cue queueing for mode transitions.
// =============================================================================

#include "commanded_modes.h"

#include "audio_task.h"
#include "config_cache.h"
#include "robot_state.h"

void commandedSetStationary(bool stationary, CommandSource source) {
    (void)source;  // Reserved for future logging/telemetry; currently unused

    bool queueDriveOn = false;
    bool wasStationary = false;

    taskENTER_CRITICAL(&robotStateMux);
    wasStationary = robotState.stationary;
    robotState.stationary = stationary;
    if (wasStationary && !stationary) {
        queueDriveOn = true;
    }
    taskEXIT_CRITICAL(&robotStateMux);

    // Keep config cache in sync so the next /api/config save persists the
    // commanded mode rather than reverting it from the stale cache.
    ConfigSnapshot cfg = {};
    configCacheRead(&cfg);
    cfg.system.stationary = stationary;
    configCacheApply(cfg);

    if (queueDriveOn) {
        audioQueuePlaySlot(AUDIO_SLOT_SYS_DRIVE_ON, SRC_INTERNAL);
    }
}

bool commandedSetSleep(bool sleep, CommandSource source) {
    (void)source;  // Reserved for future logging/telemetry; currently unused

    uint32_t nowMs = millis();
    bool changed = false;

    taskENTER_CRITICAL(&robotStateMux);
    if (robotState.sleepMode != sleep) {
        robotState.sleepMode = sleep;
        robotState.sleepSinceMs = sleep ? nowMs : 0U;
        changed = true;
    }
    taskEXIT_CRITICAL(&robotStateMux);

    return changed;
}

void commandedSetWebControl(bool enabled, CommandSource source) {
    (void)source;  // Reserved for future logging/telemetry; currently unused

    taskENTER_CRITICAL(&robotStateMux);
    robotState.webControlEnabled = enabled;
    taskEXIT_CRITICAL(&robotStateMux);
}

void commandedSetRcDebug(bool enabled, CommandSource source) {
    (void)source;  // Reserved for future logging/telemetry; currently unused

    taskENTER_CRITICAL(&robotStateMux);
    robotState.rcDebugMode = enabled;
    taskEXIT_CRITICAL(&robotStateMux);
}

void commandedSetActiveMood(uint8_t moodId, CommandSource source) {
    (void)source;  // Reserved for future logging/telemetry; currently unused

    taskENTER_CRITICAL(&robotStateMux);
    robotState.activeMood = moodId;
    taskEXIT_CRITICAL(&robotStateMux);
}
