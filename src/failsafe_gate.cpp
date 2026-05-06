// =============================================================================
// src/failsafe_gate.cpp
//
// FailsafeGate implementation.
// Internal state: 5-bit bitmask (_activeMask) with one bit per FailsafeLayer.
// All critical sections use taskENTER_CRITICAL / taskEXIT_CRITICAL with the
// shared robotState spinlock (no-op in native test mode via stubs).
// =============================================================================

#include "failsafe_gate.h"

#ifdef ARDUINO
#include <Arduino.h>
#endif

#include "logging.h"
#include "robot_state.h"

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
        case FailsafeLayer::TWDT_RESET:
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
    bool twdtActive = (_activeMask & (1 << (uint8_t)FailsafeLayer::TWDT_RESET)) != 0;
    bool estopActive = (_activeMask & (1 << (uint8_t)FailsafeLayer::ESTOP)) != 0;

    robotState.sbusHwFailsafe = sbusHwActive;
    robotState.sbusSignalLost = sbuswdActive;
    robotState.webDriveExpired = webActive;
    // Note: TWDT_RESET and ESTOP both drive robotState.estop.
    robotState.estop = twdtActive || estopActive;
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
    uint8_t twdtBit = 1 << (uint8_t)FailsafeLayer::TWDT_RESET;

    taskENTER_CRITICAL(_mux);
    bool estopWasActive = (_activeMask & estopBit) != 0;
    bool twdtWasActive = (_activeMask & twdtBit) != 0;
    // Clear both ESTOP and TWDT_RESET — both represent explicit operator recovery intent
    _activeMask &= ~estopBit;
    _activeMask &= ~twdtBit;
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
    if (twdtWasActive) {
        PA_LOG_INFO(TAG, "twdt_reset cleared (explicit)");
    }
}

void failsafeUpdateWebTimeout(bool webTimedOut) {
    if (webTimedOut) {
        failsafeTrigger(FailsafeLayer::WEB_TIMEOUT);
    } else {
        failsafeClear(FailsafeLayer::WEB_TIMEOUT);
    }
}
