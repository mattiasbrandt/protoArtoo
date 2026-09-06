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
#include "logging.h"
#include "robot_state.h"

static const char* TAG = "CommandedModes";

void commandedSetStationary(bool stationary, CommandSource source) {
    bool queueDriveOn = false;
    bool wasStationary = false;

    taskENTER_CRITICAL(&robotStateMux);
    wasStationary = robotState.stationary;
    robotState.stationary = stationary;
    if (wasStationary && !stationary) {
        queueDriveOn = true;
    }
    taskEXIT_CRITICAL(&robotStateMux);

    // Keep the config cache in sync so the next /api/config save persists the
    // commanded mode rather than reverting it from the stale cache. By field:
    // this runs on the SBUS path (Core 1, once per drive frame), so a
    // whole-snapshot round trip here cost 944 B of stack on this frame and
    // marked the RC mapping dirty for a field the RC processor config does not
    // contain.
    configCacheSetStationary(stationary);

    // Was "(void)source; Reserved for future logging/telemetry" until #220:
    // the Controller Console adds two new sources (SRC_SERIAL_CONSOLE,
    // SRC_WEB_CONSOLE) whose commands must be distinguishable from RC/Web
    // API/sequence-triggered ones downstream (ADR 0036 criterion). Logged
    // only on a real transition, matching the existing edge-triggered
    // queueDriveOn/persistence behavior above.
    if (wasStationary != stationary) {
        PA_LOG_INFO(TAG, "[%s] stationary -> %s", commandSourceToString(source),
                    stationary ? "true" : "false");
    }

    if (queueDriveOn) {
        audioQueuePlaySlot(AUDIO_SLOT_SYS_DRIVE_ON, SRC_INTERNAL);
    }
}

bool commandedSetSleep(bool sleep, CommandSource source) {
    uint32_t nowMs = millis();
    bool changed = false;

    taskENTER_CRITICAL(&robotStateMux);
    if (robotState.sleepMode != sleep) {
        robotState.sleepMode = sleep;
        robotState.sleepSinceMs = sleep ? nowMs : 0U;
        changed = true;
    }
    taskEXIT_CRITICAL(&robotStateMux);

    // See commandedSetStationary() above: source was accepted-but-unused
    // until #220 gave it a real downstream consumer.
    if (changed) {
        PA_LOG_INFO(TAG, "[%s] sleep -> %s", commandSourceToString(source), sleep ? "true" : "false");
    }

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
