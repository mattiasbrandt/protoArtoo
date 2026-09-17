// =============================================================================
// src/failsafe_gate.cpp
//
// FailsafeGate implementation.
// Internal state: 5-bit bitmask (_activeMask) with one bit per FailsafeLayer.
// All critical sections use taskENTER_CRITICAL / taskEXIT_CRITICAL with the
// shared robotState spinlock (no-op in native test mode via stubs).
//
// PUBLISHING AN EDGE. The event stream builds a status payload when something
// asks it to and at no other time, so a layer that latches without asking
// leaves every browser reading the state from before. That is how a latching
// estop raised from the radio reached no screen at all: the web handlers each
// asked for themselves (api_estop.cpp, api_drive.cpp), and the paths that
// latch without a web request - an SBUS timeout, a watchdog-reset boot, an RC
// action - asked for nobody (#346).
//
// The ask lives here rather than at each trigger site because this is the one
// place that owns the mask: a layer added later is published by construction
// instead of by remembering. It is an EDGE, not a state - a held layer is
// re-triggered every SBUS frame, and asking per frame would rebuild the whole
// status payload 50 times a second for a state that has not moved.
//
// requestStatusBroadcastNow() sets one flag inside a critical section and
// returns (web_server.cpp); the payload is built by the event stream task on
// Core 0. That is what makes it safe to call from here, because
// failsafeTrigger() is reached from the real-time path - no allocation, no
// blocking, and never with robotStateMux held.
// =============================================================================

#include "failsafe_gate.h"

#ifdef ARDUINO
#include <Arduino.h>
#endif

#include "logging.h"
#include "robot_state.h"
#include "web_server.h"   // requestStatusBroadcastNow()

static const char* TAG = "FailsafeGate";

// Internal state
static portMUX_TYPE* _mux = nullptr;
static uint8_t _activeMask = 0;  // one bit per FailsafeLayer (bits 0-4)

// Map FailsafeLayer enum to FailsafeSource for diagnostics.
static FailsafeSource layerToSource(FailsafeLayer layer) {
    switch (layer) {
        case FailsafeLayer::SBUS_HW:
            return FS_SBUS_HW;
        case FailsafeLayer::SBUS_WATCHDOG:
            return FS_SBUS_TIMEOUT;
        case FailsafeLayer::WEB_TIMEOUT:
            return FS_WEB_TIMEOUT;
        case FailsafeLayer::WATCHDOG_RESET:
            return FS_WATCHDOG_RESET;
        case FailsafeLayer::ESTOP:
            return FS_ESTOP_CMD;
        default:
            return FS_NONE;
    }
}

// Update robotState mirror fields under mutex lock (caller holds it).
// Called after _activeMask changes.
static void updateMirrorsLocked() {
    bool sbusHwActive = (_activeMask & (1 << (uint8_t)FailsafeLayer::SBUS_HW)) != 0;
    bool sbuswdActive = (_activeMask & (1 << (uint8_t)FailsafeLayer::SBUS_WATCHDOG)) != 0;
    bool webActive = (_activeMask & (1 << (uint8_t)FailsafeLayer::WEB_TIMEOUT)) != 0;
    bool watchdogResetActive = (_activeMask & (1 << (uint8_t)FailsafeLayer::WATCHDOG_RESET)) != 0;
    bool estopActive = (_activeMask & (1 << (uint8_t)FailsafeLayer::ESTOP)) != 0;

    robotState.sbusHwFailsafe = sbusHwActive;
    robotState.sbusSignalLost = sbuswdActive;
    robotState.webDriveExpired = webActive;
    // Note: WATCHDOG_RESET and ESTOP both drive robotState.estop.
    robotState.estop = watchdogResetActive || estopActive;
}

void failsafeInit(portMUX_TYPE* mux) {
    if (mux == nullptr) {
        PA_LOG_ERROR(TAG, "failsafeInit: mutex is null");
        return;
    }
    _mux = mux;
    _activeMask = 0;

    // Initialize mirrors with current state (should be zero at boot).
    taskENTER_CRITICAL(_mux);
    updateMirrorsLocked();
    taskEXIT_CRITICAL(_mux);
    PA_LOG_INFO(TAG, "initialized");
}

void failsafeTrigger(FailsafeLayer layer) {
    if (_mux == nullptr) {
        PA_LOG_ERROR(TAG, "failsafeTrigger: not initialized");
        return;
    }

    uint8_t layerBit = 1 << (uint8_t)layer;
    bool wasActive = false;

    taskENTER_CRITICAL(_mux);
    wasActive = (_activeMask & layerBit) != 0;
    _activeMask |= layerBit;
    updateMirrorsLocked();

    // Record failsafe event only on first trigger (state change).
    if (!wasActive) {
        recordFailsafeTriggerLocked(layerToSource(layer), millis());
    } else {
        // Already active, just update the source field without incrementing count.
        robotState.failsafeSource = layerToSource(layer);
    }

    taskEXIT_CRITICAL(_mux);

    if (!wasActive) {
        PA_LOG_WARN(TAG, "triggered layer=%u", (unsigned)layer);
        requestStatusBroadcastNow();
    }
}

void failsafeClear(FailsafeLayer layer) {
    if (_mux == nullptr) {
        PA_LOG_ERROR(TAG, "failsafeClear: not initialized");
        return;
    }

    // No-op for ESTOP (latching); use failsafeClearEstop() for explicit clear.
    if (layer == FailsafeLayer::ESTOP) {
        return;
    }

    uint8_t layerBit = 1 << (uint8_t)layer;

    taskENTER_CRITICAL(_mux);
    bool wasActive = (_activeMask & layerBit) != 0;
    _activeMask &= ~layerBit;
    updateMirrorsLocked();
    taskEXIT_CRITICAL(_mux);

    if (wasActive) {
        PA_LOG_INFO(TAG, "cleared layer=%u", (unsigned)layer);
        // The falling edge is published too. A plate still showing STOPPED
        // after the droid is free again is the same untruth pointing the other
        // way, and it is the one that teaches an operator to distrust the
        // readout.
        requestStatusBroadcastNow();
    }
}

bool failsafeIsActive() {
    if (_mux == nullptr) {
        return false;
    }

    bool active;
    taskENTER_CRITICAL(_mux);
    active = (_activeMask != 0);
    taskEXIT_CRITICAL(_mux);
    return active;
}

FailsafeLayer failsafeActiveReason() {
    if (_mux == nullptr) {
        return FailsafeLayer::SBUS_HW;
    }

    taskENTER_CRITICAL(_mux);
    // Return lowest-index (highest-priority) active layer.
    for (int i = 0; i < 5; ++i) {
        if ((_activeMask & (1 << i)) != 0) {
            taskEXIT_CRITICAL(_mux);
            return (FailsafeLayer)i;
        }
    }
    taskEXIT_CRITICAL(_mux);
    return FailsafeLayer::SBUS_HW;  // default if none active
}

void failsafeClearEstop() {
    if (_mux == nullptr) {
        PA_LOG_ERROR(TAG, "failsafeClearEstop: not initialized");
        return;
    }

    uint8_t estopBit = 1 << (uint8_t)FailsafeLayer::ESTOP;
    uint8_t watchdogBit = 1 << (uint8_t)FailsafeLayer::WATCHDOG_RESET;

    taskENTER_CRITICAL(_mux);
    bool estopWasActive = (_activeMask & estopBit) != 0;
    bool watchdogWasActive = (_activeMask & watchdogBit) != 0;
    // Clear both ESTOP and WATCHDOG_RESET  --  both represent explicit operator recovery intent
    _activeMask &= ~estopBit;
    _activeMask &= ~watchdogBit;
    updateMirrorsLocked();

    // Clear failsafeSource only if this was the active reason.
    if (robotState.failsafeSource == FS_ESTOP_CMD ||
        robotState.failsafeSource == FS_WATCHDOG_RESET) {
        robotState.failsafeSource = FS_NONE;
    }
    taskEXIT_CRITICAL(_mux);

    if (estopWasActive) {
        PA_LOG_INFO(TAG, "estop cleared (explicit)");
    }
    if (watchdogWasActive) {
        PA_LOG_INFO(TAG, "watchdog_reset cleared (explicit)");
    }
    if (estopWasActive || watchdogWasActive) {
        requestStatusBroadcastNow();
    }
}

void failsafeUpdateWebTimeout(bool webTimedOut) {
    if (webTimedOut) {
        failsafeTrigger(FailsafeLayer::WEB_TIMEOUT);
    } else {
        failsafeClear(FailsafeLayer::WEB_TIMEOUT);
    }
}
