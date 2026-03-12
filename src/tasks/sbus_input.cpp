// =============================================================================
// src/tasks/sbus_input.cpp
//
// SBUSInputTask — decodes SBUS receivers using the RMT-based SbusDecoder.
// Receiver #1 (PIN_SBUS1_RX = GPIO 15): Drive — CH1=speed, CH2=steer, CH8=limit
// Receiver #2 (PIN_SBUS2_RX = GPIO 13): Dome spin — Phase 3 (initialized,
//   dome channel processing deferred until DomeLinkTask plumbing is complete).
//
// SbusDecoder API:
//   .begin(pin)   — initialize RMT channel, returns false if no channel free
//   .read()       — returns true when a new 25-byte frame is decoded
//   .data()       — returns SbusData{ch[16], failsafe, lost_frame}
//   ch[]          — 0-indexed, range SBUS_MIN(172)..SBUS_MAX(1811), center ~992
//
// Safety layers implemented here:
//   Layer 1: SBUS receiver hardware failsafe flag (data.failsafe)
//   Layer 2: SBUS software watchdog (SBUS_TIMEOUT_MS = 200 ms)
//
// CH8 speed-limit: linear scale 0.0–1.0 applied to cfg_speedLimitMax.
// ch8_mode_lock: CH8 at zero (<2%) enters Stationary Mode (drive locked).
// =============================================================================

#include <Arduino.h>

#include "config.h"
#include "robot_state.h"
#include "sbus_decoder.h"
#include "sbus_math.h"

static const char* TAG = "SBUSInputTask";

// SBUS receiver objects — RMT peripheral, no UART consumed.
// GPIO 15 (drive) and GPIO 13 (dome) are dedicated SBUS pins on the PCB.
static SbusDecoder sbus_drive;
static SbusDecoder sbus_dome;

// -----------------------------------------------------------------------------
// sbusInputTask()
// Polls both SBUS receivers at ~200 Hz (5 ms delay) to catch every 100 Hz frame.
// Implements Layer 1 (HW failsafe flag) and Layer 2 (SW watchdog) safety.
// Thread safety: all RobotState writes use taskENTER/EXIT_CRITICAL.
// -----------------------------------------------------------------------------
void sbusInputTask(void* pvParameters) {
    if (!sbus_drive.begin(PIN_SBUS1_RX)) {
        Serial.printf("[%s] ERROR: RMT init failed for SBUS1 GPIO%d\n", TAG, PIN_SBUS1_RX);
    }
    if (!sbus_dome.begin(PIN_SBUS2_RX)) {
        Serial.printf("[%s] WARNING: RMT init failed for SBUS2 GPIO%d\n", TAG, PIN_SBUS2_RX);
    }
    Serial.printf("[%s] started — SBUS1 GPIO%d (drive), SBUS2 GPIO%d (dome)\n", TAG, PIN_SBUS1_RX,
                  PIN_SBUS2_RX);

    while (true) {
        // --- Drive receiver (SBUS #1) ---
        if (sbus_drive.read()) {
            SbusData data = sbus_drive.data();

            taskENTER_CRITICAL(&robotStateMux);
            robotState.lastSbus1Ms = millis();

            // Layer 1: Hardware failsafe flag from receiver firmware
            if (data.failsafe) {
                robotState.sbusHwFailsafe = true;
                robotState.failsafeSource = FS_SBUS_HW;
                robotState.driveSpeed = 0;
                robotState.driveSteer = 0;
                robotState.lastDriveSource = SRC_SBUS;
                taskEXIT_CRITICAL(&robotStateMux);
            } else {
                // Signal confirmed — clear watchdog and hardware failsafe flags
                robotState.sbusHwFailsafe = false;
                robotState.sbusSignalLost = false;

                // CH8 speed-limit scaling (index 7 = CH8 in SBUS, 0-indexed)
                float scale = mapSbusToScale(data.ch[7]);
                robotState.speedLimitScale = scale;

                bool ch8ModeLock = robotState.cfg_ch8ModeLock;
                int16_t maxOut = (int16_t)(robotState.cfg_speedLimitMax * scale);
                taskEXIT_CRITICAL(&robotStateMux);

                if (ch8ModeLock && scale < 0.02f) {
                    // Stationary Mode: CH8 at zero with mode-lock — drive locked
                    taskENTER_CRITICAL(&robotStateMux);
                    robotState.stationary = true;
                    robotState.driveSpeed = 0;
                    robotState.driveSteer = 0;
                    robotState.lastDriveSource = SRC_SBUS;
                    robotState.lastDriveCommandMs = millis();
                    taskEXIT_CRITICAL(&robotStateMux);
                } else {
                    // Normal drive: apply CH8 scale cap then update state
                    int16_t speed =
                        constrain(mapSbusToSpeed(data.ch[0]), (int16_t)(-maxOut), maxOut);
                    int16_t steer =
                        constrain(mapSbusToSpeed(data.ch[1]), (int16_t)(-maxOut), maxOut);
                    taskENTER_CRITICAL(&robotStateMux);
                    robotState.stationary = false;
                    robotState.driveSpeed = speed;
                    robotState.driveSteer = steer;
                    robotState.lastDriveSource = SRC_SBUS;
                    robotState.lastDriveCommandMs = millis();
                    taskEXIT_CRITICAL(&robotStateMux);
                }
            }
        }

        // Layer 2: SBUS software watchdog — fires if no valid frame for SBUS_TIMEOUT_MS
        taskENTER_CRITICAL(&robotStateMux);
        uint32_t lastSbus1 = robotState.lastSbus1Ms;
        uint32_t timeoutMs = robotState.cfg_sbusTimeoutMs;
        taskEXIT_CRITICAL(&robotStateMux);

        bool watchdogFired = false;
        if ((millis() - lastSbus1) > timeoutMs) {
            taskENTER_CRITICAL(&robotStateMux);
            if (!robotState.sbusSignalLost) {
                robotState.sbusSignalLost = true;
                robotState.failsafeSource = FS_SBUS_TIMEOUT;
                robotState.failsafeTriggerCount++;
                robotState.driveSpeed = 0;
                robotState.driveSteer = 0;
                watchdogFired = true;
            }
            taskEXIT_CRITICAL(&robotStateMux);
            if (watchdogFired) {
                Serial.printf("[%s] SBUS1 watchdog fired — signal lost\n", TAG);
            }
        }

        // SBUS #2 (dome spin, GPIO PIN_SBUS2_RX) — dome channel processing
        // deferred to Phase 3. sbus_dome is initialized and receiving but
        // domeTargetSpeed is not updated until DomeLinkTask plumbing is in place.
        // sbus_dome.read() is intentionally not called to avoid stale RMT buffers.

        // ~200 Hz poll rate — SBUS frames arrive at 100 Hz; poll twice per frame
        vTaskDelay(pdMS_TO_TICKS(5));
    }
}
